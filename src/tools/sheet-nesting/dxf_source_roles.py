"""Resolve DXF source roles without changing or excluding any geometry.

The neutral reader's original tags remain authoritative. This module resolves
layer/linetype inheritance and identifies narrowly evidenced *candidates*.
The material importer must still prove complete material boundaries before it
admits a candidate as an auxiliary process stroke. No layer name, bounding
rectangle, line pattern or candidate alone authorizes removing material.
"""
from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
import math
import re


_PROFILE_ROLES = {
    "outer_profiles": "outer-profile",
    "interior_profiles": "inner-profile",
    "bend": "bend",
    "bend_extent": "bend-extent",
}
_GEOMETRIC_RECORDS = {"LINE", "ARC", "CIRCLE", "ELLIPSE", "SPLINE", "LWPOLYLINE", "POLYLINE"}
_CENTER_DESCRIPTION = re.compile(r"\bcent(?:er|re)\b|中心", re.IGNORECASE)


def _values(record):
    result = defaultdict(list)
    for code, value in record.get("values", ()):
        result[int(code)].append(value)
    return result


def _one(values, code, default=None):
    values = values.get(code, ())
    return values[-1] if values else default


def _name(value):
    return str(value).strip()


def _key(value):
    return _name(value).casefold()


def _identity(record):
    # Group 330 within a 102 reactor/application group is not the entity's
    # structural owner. Preserve all tags while identifying only the latter.
    source = record.get("_sourceValues", record.get("values", ()))
    owners, handles, groups = [], [], []
    for code, value in source:
        code = int(code)
        if code == 102:
            text = _name(value)
            if text.startswith("{"):
                groups.append(text)
            elif text == "}" and groups:
                groups.pop()
        elif code == 330 and not groups:
            owners.append(_name(value))
        elif code == 5:
            handles.append(_name(value))
    return {
        "sourceType": record.get("type"),
        "sourceHandle": handles[0] if len(handles) == 1 else None,
        "sourceOwner": owners[0] if len(owners) == 1 else None,
        "sourceOwnerCandidates": owners,
        "sourceTags": deepcopy(list(source)),
    }


def _definition(record):
    values = _values(record)
    return {**_identity(record), "name": _name(_one(values, 2, ""))}


def _finite(value):
    try:
        value = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return value if math.isfinite(value) else None


def _center_definition(record):
    """Require an actual simple long-gap-short-gap center-line definition."""
    values = _values(record)
    description = _name(_one(values, 3, ""))
    if not _CENTER_DESCRIPTION.search(description):
        return None
    segments = [_finite(value) for value in values.get(49, ())]
    count, alignment = _finite(_one(values, 73)), _finite(_one(values, 72))
    period = _finite(_one(values, 40))
    complex_flags = [_finite(value) for value in values.get(74, ())]
    if (count != 4 or alignment != 65 or len(segments) != 4 or any(value is None for value in segments)
            or period is None or period <= 0 or any(value != 0 for value in complex_flags)
            or values.get(75) or values.get(340)):
        return None
    long, first_gap, short, second_gap = segments
    if not (long > short > 0 and first_gap < 0 and second_gap < 0):
        return None
    if not math.isclose(period, math.fsum(map(abs, segments)), rel_tol=1e-12, abs_tol=1e-12):
        return None
    return {"description": description, "alignment": int(alignment), "declaredSegments": int(count),
            "patternLength": period, "segments": segments,
            "proof": "declared Center description and simple long-gap-short-gap LTYPE pattern"}


class SourceRoleContext:
    """Immutable-source role context shared by one neutral DXF document.

    ``resolve`` accepts the parent INSERT's resolved dictionary as ``inherited``.
    The importer supplies its existing reference path and affine transform; this
    helper neither expands nor transforms a geometric instance. ``annotate`` is
    a convenience copy operation and never changes the input record.
    """

    def __init__(self, tables=(), entities=(), blocks=()):
        self.diagnostics = []
        self._layers = defaultdict(list)
        self._linetypes = defaultdict(list)
        for record in tables:
            target = self._layers if record.get("type") == "LAYER" else self._linetypes if record.get("type") == "LTYPE" else None
            if target is not None:
                name = _key(_one(_values(record), 2, ""))
                if name:
                    target[name].append(deepcopy(record))
        for kind, definitions in (("LAYER", self._layers), ("LTYPE", self._linetypes)):
            for name, records in definitions.items():
                if len(records) != 1:
                    self.diagnostics.append(f"Ambiguous {kind} definition: {name}")
        labels, complete = self._active_profile_labels(entities, blocks)
        self.profile_roles_present = sorted(labels)
        self.profile_convention = complete and set(_PROFILE_ROLES).issubset(labels)

    def _active_profile_labels(self, entities, records):
        # Only referenced BLOCK content can establish an active export-role
        # convention. An unused historical block must not enable filtering.
        blocks, current, complete = {}, None, True
        for record in records:
            kind = record.get("type")
            if kind == "BLOCK":
                name = _key(_one(_values(record), 2, ""))
                if current is not None or not name or name in blocks:
                    complete = False
                current = []
                if name and name not in blocks:
                    blocks[name] = current
            elif kind == "ENDBLK":
                if current is None:
                    complete = False
                current = None
            elif current is not None:
                current.append(record)
        if current is not None:
            complete = False
        labels, visited = set(), 0

        def visit(source, parent_layer="0", path=()):
            nonlocal complete, visited
            if len(path) > 16:
                complete = False
                return
            for record in source:
                visited += 1
                if visited > 1000000:
                    complete = False
                    return
                values = _values(record)
                declared = _name(_one(values, 8, "0")) or "0"
                effective = parent_layer if path and declared == "0" else declared
                if record.get("type") in _GEOMETRIC_RECORDS:
                    labels.add(_key(effective))
                elif record.get("type") == "INSERT":
                    name = _key(_one(values, 2, ""))
                    if name in path or name not in blocks:
                        complete = False
                    else:
                        visit(blocks[name], effective, path + (name,))

        visit(entities)
        if not complete:
            self.diagnostics.append("Active source-role convention is incomplete or ambiguous in BLOCK references")
        return labels, complete

    @staticmethod
    def _unique(definitions, name, kind, reasons):
        matches = definitions.get(_key(name), ())
        if len(matches) > 1:
            reasons.append(f"Ambiguous {kind} definition: {name}")
            return None
        return matches[0] if matches else None

    def resolve(self, record, inherited=None, reference_path=()):
        if inherited and "_sourceRole" in inherited:
            inherited = inherited["_sourceRole"]
        values, reasons = _values(record), []
        declared_layer = _name(_one(values, 8, "0")) or "0"
        layer_inherited = declared_layer == "0" and inherited is not None
        layer = inherited.get("effectiveLayer") if layer_inherited else declared_layer
        if layer is None:
            reasons.append("Parent INSERT layer is unresolved")
        layer_definition = self._unique(self._layers, layer or "", "LAYER", reasons)
        declared_type = _name(_one(values, 6, "BYLAYER")) or "BYLAYER"
        linetype, origin = declared_type, "explicit"
        if _key(declared_type) == "bylayer":
            origin = "layer"
            linetype = _name(_one(_values(layer_definition), 6, "")) if layer_definition else None
            if not linetype or _key(linetype) in ("bylayer", "byblock"):
                linetype = None
                reasons.append("BYLAYER linetype has no unique concrete LAYER definition")
        elif _key(declared_type) == "byblock":
            origin = "block"
            linetype = inherited.get("effectiveLinetype") if inherited else None
            if not linetype or _key(linetype) in ("bylayer", "byblock"):
                linetype = None
                reasons.append("BYBLOCK linetype has no resolved parent INSERT")
        linetype_definition = self._unique(self._linetypes, linetype or "", "LTYPE", reasons)
        if linetype and linetype_definition is None and _key(linetype) not in ("continuous",):
            reasons.append(f"No unique LTYPE definition for effective linetype: {linetype}")
        role, evidence = None, None
        if self.profile_convention and record.get("type") in _GEOMETRIC_RECORDS:
            role = _PROFILE_ROLES.get(_key(layer or ""))
            if role:
                evidence = {"kind": "joint-sheet-metal-profile-export-convention",
                            "activeRoles": [name for name in self.profile_roles_present if name in _PROFILE_ROLES],
                            "declaredByEntityLayerTags": True}
        if role is None and record.get("type") == "LINE" and linetype_definition is not None:
            center = _center_definition(linetype_definition)
            if center:
                role, evidence = "centerline", {"kind": "declared-center-linetype", **center}
        ancestry = None
        if inherited:
            ancestry = {name: deepcopy(inherited.get(name)) for name in
                        ("sourceType", "sourceHandle", "effectiveLayer", "effectiveLinetype", "referencePath")}
        return {
            **_identity(record), "declaredLayer": declared_layer, "effectiveLayer": layer,
            "layerInherited": layer_inherited,
            "layerDefinitionPresent": layer_definition is not None,
            "layerDefinition": _definition(layer_definition) if layer_definition is not None else None,
            "declaredLinetype": declared_type, "effectiveLinetype": linetype, "linetypeOrigin": origin,
            "linetypeDefinitionPresent": linetype_definition is not None,
            "linetypeDefinition": _definition(linetype_definition) if linetype_definition is not None else None,
            "referencePath": list(reference_path), "inheritedFrom": ancestry,
            "roleCandidate": role, "roleEvidence": evidence, "requiresMaterialProof": True,
            "geometryExcluded": False, "sourceCoordinatesChanged": False,
            "unresolvedReasons": reasons,
        }

    def annotate(self, record, inherited=None, reference_path=()):
        return {**record, "_sourceRole": self.resolve(record, inherited, reference_path)}

    def provenance(self):
        return {"schemaVersion": 1, "candidateOnly": True, "geometryExcluded": False,
                "profileConvention": self.profile_convention,
                "profileRolesPresent": list(self.profile_roles_present),
                "diagnostics": list(self.diagnostics),
                "layerDefinitions": [_definition(record) for records in self._layers.values() for record in records],
                "linetypeDefinitions": [_definition(record) for records in self._linetypes.values() for record in records]}
