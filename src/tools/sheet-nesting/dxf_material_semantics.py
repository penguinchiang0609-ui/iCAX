"""Validate source-bound machining declarations; never construct geometry.

Only explicitly selected original entity instances receive a role. A role
document cannot provide coordinates, scale, quantities, or nesting results.
The DXF importer owns contour completeness and final material validation.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import re


_GEOMETRY_TYPES = {"LINE", "ARC", "CIRCLE", "ELLIPSE", "SPLINE", "LWPOLYLINE", "POLYLINE"}
_ROLES = {"non-through-machining", "through-hole", "outer-profile"}
_SHA = re.compile(r"[0-9a-fA-F]{64}")


def _fields(value, allowed, required, label):
    if not isinstance(value, dict) or set(value) - set(allowed) or not set(required) <= set(value):
        raise ValueError(f"Invalid material semantics {label}: unknown or missing fields")


def _text(value, label):
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Material semantics {label} must be a nonempty string")
    return value


def _integer(value, label):
    if type(value) is not int or value < 0:
        raise ValueError(f"Material semantics {label} must be a nonnegative integer")
    return value


def _sha(value, label):
    if not isinstance(value, str) or _SHA.fullmatch(value) is None:
        raise ValueError(f"Material semantics {label} must be SHA256")
    return value.lower()


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for data in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def _unique_object(pairs):
    value = {}
    for key, entry in pairs:
        if key in value:
            raise ValueError(f"Duplicate JSON key in material semantics: {key}")
        value[key] = entry
    return value


def _nonfinite(value):
    raise ValueError(f"Nonfinite JSON number in material semantics: {value}")


def _load(value, actual_source_sha256, audit):
    """Load a mapping or sidecar. Relative evidence is sidecar-relative only."""
    if value is None:
        return None
    origin, base = None, None
    if isinstance(value, (str, Path)):
        origin = Path(value).resolve()
        data = origin.read_bytes()
        if len(data) > 16 * 1024 * 1024:
            raise ValueError("Material semantics document exceeds the bounded import resource limit")
        digest, encoding, base = hashlib.sha256(data).hexdigest(), "original-file-bytes", origin.parent
        audit.update(documentSha256=digest, documentHashEncoding=encoding, documentPath=str(origin))
        document = json.loads(data.decode("utf-8-sig"), object_pairs_hook=_unique_object, parse_constant=_nonfinite)
    elif isinstance(value, dict):
        document = deepcopy(value)
        try:
            data = json.dumps(document, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
        except (TypeError, ValueError) as error:
            raise ValueError("Material semantics mapping must contain finite JSON values") from error
        digest, encoding = hashlib.sha256(data).hexdigest(), "canonical-json-sort-keys-compact-utf8"
        audit.update(documentSha256=digest, documentHashEncoding=encoding, documentPath=None)
    else:
        raise ValueError("Material semantics must be a JSON sidecar path or mapping")
    _fields(document, ("schemaVersion", "sourceDxfSha256", "evidence", "features", "expectations"),
            ("schemaVersion", "sourceDxfSha256", "evidence", "features", "expectations"), "document")
    if type(document["schemaVersion"]) is not int or document["schemaVersion"] != 1:
        raise ValueError("Unsupported material semantics schema version")
    source_sha = _sha(document["sourceDxfSha256"], "sourceDxfSha256")
    if source_sha != _sha(actual_source_sha256, "actual parsed DXF SHA"):
        raise ValueError("Material semantics source SHA does not match the actual parsed DXF bytes")
    evidence, evidence_ids = [], set()
    audit["evidenceChecks"] = []
    if not isinstance(document["evidence"], list) or not document["evidence"]:
        raise ValueError("Material semantics needs source evidence")
    for entry in document["evidence"]:
        _fields(entry, ("id", "kind", "path", "sha256", "locator", "quote", "sourceURL", "sourceRevision"),
                ("id", "kind", "path", "sha256", "locator"), "evidence")
        identifier = _text(entry["id"], "evidence id")
        if identifier in evidence_ids:
            raise ValueError("Duplicate material semantics evidence id")
        evidence_ids.add(identifier)
        for key in ("kind", "path", "locator"):
            _text(entry[key], f"evidence {key}")
        for key in ("quote", "sourceURL", "sourceRevision"):
            if key in entry:
                _text(entry[key], f"evidence {key}")
        path = Path(entry["path"])
        if not path.is_absolute():
            if base is None:
                raise ValueError("Relative evidence path requires a sidecar file; mapping evidence paths must be absolute")
            path = base / path
        path = path.resolve()
        expected = _sha(entry["sha256"], "evidence SHA")
        actual = file_sha256(path)
        audit["evidenceChecks"].append({"id": identifier, "resolvedPath": str(path), "expectedSha256": expected,
                                        "actualSha256": actual, "matched": expected == actual})
        if expected != actual:
            raise ValueError(f"Material semantics evidence SHA changed: {identifier}")
        evidence.append({**entry, "resolvedPath": str(path), "actualSha256": actual})
    expectations = document["expectations"]
    _fields(expectations, ("materialComponents", "throughHoles"), ("materialComponents", "throughHoles"), "expectations")
    for key in expectations:
        _integer(expectations[key], key)
    if expectations["materialComponents"] == 0:
        raise ValueError("Material semantics must retain at least one physical material component")
    features, identifiers = [], set()
    if not isinstance(document["features"], list) or not document["features"]:
        raise ValueError("Material semantics needs explicit feature selections")
    for feature in document["features"]:
        _fields(feature, ("id", "role", "operation", "topology", "entities", "evidence", "description", "sourceDepthMm"),
                ("id", "role", "topology", "entities", "evidence"), "feature")
        identifier = _text(feature["id"], "feature id")
        if identifier in identifiers:
            raise ValueError("Duplicate material semantics feature id")
        identifiers.add(identifier)
        if (not isinstance(feature["role"], str) or not isinstance(feature["topology"], str) or
                feature["role"] not in _ROLES or feature["topology"] not in {"closed-contours", "open-strokes"}):
            raise ValueError("Unknown material semantics role/topology")
        if feature["role"] != "non-through-machining" and feature["topology"] != "closed-contours":
            raise ValueError("Material contours and through holes must declare closed contours")
        if feature["role"] == "non-through-machining":
            _text(feature.get("operation"), "non-through operation")
        elif "operation" in feature:
            raise ValueError("Machining operation is only valid on non-through features")
        if "description" in feature:
            _text(feature["description"], "feature description")
        if "sourceDepthMm" in feature:
            depth = feature["sourceDepthMm"]
            if type(depth) not in (int, float) or not math.isfinite(depth) or depth < 0:
                raise ValueError("Source depth must be finite, nonnegative metadata and never changes geometry")
        if (not isinstance(feature["evidence"], list) or not feature["evidence"] or
                any(not isinstance(item, str) or item not in evidence_ids for item in feature["evidence"])):
            raise ValueError("Feature references missing material semantics evidence")
        if not isinstance(feature["entities"], list) or not feature["entities"]:
            raise ValueError("Feature must select complete original entities")
        for selector in feature["entities"]:
            _fields(selector, ("handle", "type", "instancePath"), ("handle", "type", "instancePath"), "entity selector")
            _text(selector["handle"], "entity handle")
            if not isinstance(selector["type"], str) or selector["type"].upper() not in _GEOMETRY_TYPES:
                raise ValueError("Material semantics selector must name an original supported curve, not a vertex/unknown entity")
            if not isinstance(selector["instancePath"], list):
                raise ValueError("Entity instancePath must be an explicit ordered array")
            for step in selector["instancePath"]:
                _fields(step, ("insertHandle", "row", "column"), ("insertHandle", "row", "column"), "INSERT instance step")
                _text(step["insertHandle"], "INSERT handle")
                _integer(step["row"], "array row")
                _integer(step["column"], "array column")
        features.append(deepcopy(feature))
    return {"schemaVersion": 1, "sourceDxfSha256": source_sha, "documentSha256": digest,
            "documentHashEncoding": encoding, "documentPath": str(origin) if origin else None,
            "evidence": evidence, "features": features, "expectations": deepcopy(expectations)}


def load(value, actual_source_sha256):
    audit = {"status": "not-applied", "actualSourceDxfSha256": actual_source_sha256, "geometryReplaced": False}
    try:
        return _load(value, actual_source_sha256, audit)
    except (ValueError, OSError) as original:
        error = original if isinstance(original, ValueError) else ValueError(f"Material semantics source/evidence is unreadable: {original}")
        error.material_semantics_diagnostics = {**audit, "reason": str(error)}
        if error is original:
            raise
        raise error from original


def _handle(record):
    values = [str(value).strip().upper() for code, value in record.get("_sourceValues", record["values"]) if int(code) == 5]
    return values[0] if len(values) == 1 else None


def _path(entries):
    if any(not isinstance(step.get("insertHandle"), str) or not step["insertHandle"].strip() for step in entries):
        return None
    return tuple((str(step.get("insertHandle", "")).strip().upper(), step.get("row"), step.get("column")) for step in entries)


def bind(plan, records):
    """Validate all targets first. Caller applies the complete plan atomically."""
    if plan is None:
        return None
    ownership, bindings = {}, []
    for feature in plan["features"]:
        targets, members = [], set()
        for selector in feature["entities"]:
            handle, path = selector["handle"].strip().upper(), _path(selector["instancePath"])
            matches = [(i, record) for i, record in enumerate(records)
                       if _handle(record) == handle and _path(record.get("_instancePath", [])) == path]
            if len(matches) != 1:
                raise ValueError(f"Material semantics target {handle} has {len(matches)} exact entity-instance matches; no partial application")
            index, record = matches[0]
            if record["type"] != selector["type"].upper():
                raise ValueError(f"Material semantics target {handle} type does not match the original entity")
            if any(int(code) == 67 and float(value) != 0 for code, value in record["values"]):
                raise ValueError("Material semantics cannot reinterpret paper-space entities as physical machining")
            family = [index]
            if record["type"] == "POLYLINE":
                cursor = index + 1
                while cursor < len(records) and records[cursor]["type"] == "VERTEX" and _path(records[cursor].get("_instancePath", [])) == path:
                    family.append(cursor)
                    cursor += 1
                if cursor >= len(records) or records[cursor]["type"] != "SEQEND" or _path(records[cursor].get("_instancePath", [])) != path:
                    raise ValueError("Material semantics POLYLINE must bind its complete VERTEX/SEQEND family")
                family.append(cursor)
            for member in family:
                if member in ownership:
                    raise ValueError("Duplicate or conflicting material semantics entity roles")
                ownership[member] = feature["id"]
                members.add(member)
            tags = record.get("_sourceValues", record["values"])
            record_digest = hashlib.sha256(json.dumps({"type": record["type"], "values": tags}, ensure_ascii=False,
                                                        separators=(",", ":"), sort_keys=True).encode("utf-8")).hexdigest()
            targets.append({"selector": deepcopy(selector), "sourceEntity": index, "sourceHandle": _handle(record),
                            "sourceType": record["type"], "expandedInstancePath": deepcopy(record.get("_instancePath", [])),
                            "sourceRecordSha256": record_digest, "familyEntities": family})
        bindings.append({**feature, "targets": targets, "familyEntities": sorted(members)})
    return {**plan, "bindings": bindings, "status": "targets-bound-not-yet-applied", "geometryReplaced": False}
