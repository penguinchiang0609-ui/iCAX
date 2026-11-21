"""Map the existing neutral DXF reader's records into original material.

Straight rings retain their represented coordinates. Circles retain their
analytic definition in metadata; native disks stay analytic, while circular
holes use certified inscribed polygons and never acquire straight common-edge
eligibility. Circular, elliptic and NURBS contours use bounded sampling and a
controlled conservative offset; INSERT blocks retain their nested affine
transforms and every array instance. The floating-point scope is stated in
metadata. This module does not place or optimize parts.
"""
from __future__ import annotations

from collections import Counter, defaultdict
from fractions import Fraction
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


_NON_MATERIAL_OBJECTS = {"OLE2FRAME", "XLINE", "RAY"}
_ANNOTATIONS = {"TEXT", "MTEXT", "DIMENSION", "LEADER", "MULTILEADER", "POINT", "VIEWPORT", "ATTRIB", "ATTDEF", "ICAX_ATTRIBUTE_SEQEND", *_NON_MATERIAL_OBJECTS}
_MAX_CIRCLE_VERTICES = 16384
_CONNECTION_TOLERANCE_MM = 1e-7
_PLANAR_RESIDUAL_MM = 1e-9
_SOURCE_ROLE_MODULE = None
_MATERIAL_SEMANTICS_MODULE = None
_NODE_READER = """
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
try {
  const {parseDxfDocument} = await import(pathToFileURL(process.argv[1]).href);
  const bytes = fs.readFileSync(process.argv[2]);
  const doc = parseDxfDocument(bytes);
  process.stdout.write(JSON.stringify({header:doc.header,sourceFormat:doc.sourceFormat,sourceDxfSha256:createHash('sha256').update(bytes).digest('hex'),entities:doc.sections.ENTITIES,
    blocks:doc.sections.ENTITIES.some(record=>['INSERT','SOLID'].includes(record.type))?(doc.sections.BLOCKS??[]):[],
    tables:doc.sections.TABLES??[]}));
} catch (error) {
  process.stderr.write(String(error?.message ?? error)); process.exitCode=1;
}
"""


def _document(path):
    reader = Path(__file__).resolve().parents[2] / "iCAX-UI/SDK/Cad/dxfDocument.mjs"
    node = os.environ.get("ICAX_NODE_EXECUTABLE") or shutil.which("node")
    if not node:
        raise ValueError("DXF neutral reader requires the existing Node.js runtime")
    try:
        result = subprocess.run([node, "--input-type=module", "-e", _NODE_READER,
                                 str(reader), str(Path(path).resolve())],
                                capture_output=True, encoding="utf-8", timeout=60,
                                check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"DXF neutral reader failed: {error}") from error
    if result.returncode:
        raise ValueError(f"DXF neutral reader: {result.stderr.strip()}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ValueError("DXF neutral reader returned incomplete records") from error


def _number(value, field):
    try:
        value = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"Invalid DXF numeric field {field}") from error
    if not math.isfinite(value):
        raise ValueError(f"Nonfinite DXF numeric field {field}")
    return value


def _values(record):
    result = defaultdict(list)
    for code, value in record["values"]:
        result[int(code)].append(value)
    return result


def _one(values, code, default=None):
    entries = values.get(code)
    return entries[-1] if entries else default


def _zero_length_line(values):
    # This is an exact zero-area entity, not a tolerance-based short-edge
    # simplification. A spatial end point or any nonzero XY extent stays a
    # real LINE and must pass the ordinary material/topology checks.
    return (all(values.get(code) for code in (10, 20, 11, 21)) and
            _number(_one(values, 39, 0), "LINE thickness") == 0 and
            all(_number(_one(values, code, 0), str(code)) ==
                _number(_one(values, code + 1, 0), str(code + 1)) for code in (10, 20, 30)))


def _annotation(record, values):
    paper_space = _number(_one(values, 67, 0), "paper-space flag")
    if paper_space not in (0, 1):
        raise ValueError("Invalid DXF paper-space flag")
    if paper_space:
        return f"PAPERSPACE_{record['type']}"
    if record.get("_materialSemantics", {}).get("role") == "non-through-machining":
        return "SOURCE_DECLARED_NON_THROUGH_MACHINING"
    if record.get("_dimensionCopy"):
        return "PROVED_EXPLODED_DIMENSION"
    if record.get("_sourceRoleProbe") or record.get("_acceptedSourceRole"):
        return "PROVED_SOURCE_PROCESS_STROKE" if record.get("_acceptedSourceRole") else "SOURCE_ROLE_PROBE"
    if record["type"] == "LINE" and _zero_length_line(values):
        return "ZERO_LENGTH_LINE"
    if record["type"] == "HATCH" and record.get("_redundantHatch"):
        return "PROVED_REDUNDANT_HATCH"
    return record["type"] if record["type"] in _ANNOTATIONS else None


def _inherit_paperspace(records):
    result, paper_polyline = [], False
    for record in records:
        kind = record["type"]
        if paper_polyline:
            if kind not in {"VERTEX", "SEQEND"}:
                raise ValueError("Unterminated paper-space POLYLINE")
            record = {**record, "values": [*record["values"], [67, 1]]}
            if kind == "SEQEND":
                paper_polyline = False
        elif kind == "POLYLINE":
            paper_polyline = _number(_one(_values(record), 67, 0), "paper-space flag") == 1
        result.append(record)
    if paper_polyline:
        raise ValueError("Unterminated paper-space POLYLINE")
    return result


def _non_material_provenance(document, path):
    result = []
    source_hash = document.get("sourceDxfSha256")
    for index, record in enumerate(document["entities"]):
        kind = record["type"]
        values = _values(record)
        if (kind not in _NON_MATERIAL_OBJECTS and not (kind == "LINE" and _zero_length_line(values))
                and not (kind == "HATCH" and record.get("_redundantHatch")) and not record.get("_dimensionCopy")):
            if (not record.get("_acceptedSourceRole") and
                    record.get("_materialSemantics", {}).get("role") != "non-through-machining"):
                continue
        if source_hash is None:
            source_hash = hashlib.sha256(Path(path).read_bytes()).hexdigest()
        entry = {"sourceEntity": index, "sourceType": kind,
                 "sourceHandle": _one(values, 5), "sourceLayer": _one(values, 8, "0"),
                 "sourceLayout": _one(values, 410), "sourcePaperSpace": _one(values, 67, 0),
                 "sourceDxfSha256": source_hash,
                 "neutralRecordSha256": hashlib.sha256(json.dumps(record["values"], ensure_ascii=False,
                                                                  separators=(",", ":")).encode("utf-8")).hexdigest(),
                 "boundsUsedAsMaterial": False,
                 "coordinateSpace": "original DXF entity coordinates; never a material boundary"}
        if record.get("_affine") is not None:
            entry["blockTransform"] = list(record["_affine"])
        if record.get("_materialSemantics", {}).get("role") == "non-through-machining":
            entry.update({"classification": "source-bound non-through machining; original entity instance retained as provenance",
                          "sourceTags": record.get("_sourceValues", record["values"]),
                          "materialSemantics": record["_materialSemantics"],
                          "expandedInstancePath": record.get("_instancePath", [])})
        elif record.get("_acceptedSourceRole"):
            entry.update({"classification": "source-declared process stroke with independently proved complete material boundaries",
                          "sourceTags": record.get("_sourceValues", record["values"]),
                          "sourceRole": record.get("_sourceRole"), "materialProof": record["_acceptedSourceRole"]})
        elif record.get("_dimensionCopy"):
            entry.update({"classification": "proved complete source anonymous DIMENSION-block copy",
                          "sourceTags": record.get("_sourceValues", record["values"]),
                          "dimensionCopyProof": record["_dimensionCopy"]})
        elif kind == "OLE2FRAME":
            raw_tags = [[code, value] for code, value in record["values"] if int(code) != 310]
            entry.update({"classification": "embedded OLE object; no vector cutting boundary",
                          "sourceTagsExceptPayload": raw_tags,
                          "sourceBounds": {"firstCorner": [_one(values, code) for code in (10, 20, 30)],
                                           "secondCorner": [_one(values, code) for code in (11, 21, 31)]},
                          "objectFlags": {str(code): _one(values, code) for code in (70, 71, 72, 73) if values.get(code)},
                          "payloadChunkCount": len(values.get(310, [])),
                          "declaredPayloadBytes": _one(values, 90)})
            raw_chunks = [str(value) for value in values.get(310, [])]
            entry["payloadSourceTextSha256"] = hashlib.sha256(json.dumps(raw_chunks, separators=(",", ":")).encode("utf-8")).hexdigest()
            try:
                payload = b"".join(bytes.fromhex(value) for value in raw_chunks)
                entry.update({"payloadDecoded": True, "payloadBytes": len(payload),
                              "payloadSha256": hashlib.sha256(payload).hexdigest()})
                if values.get(90):
                    try:
                        entry["payloadLengthMatchesDeclared"] = int(_one(values, 90)) == len(payload)
                    except (TypeError, ValueError):
                        entry["payloadLengthMatchesDeclared"] = False
            except ValueError:
                # A malformed embedded object cannot create cutting material.
                # Its source text and complete original file remain hash-linked.
                entry.update({"payloadDecoded": False, "payloadIssue": "invalid group-310 hexadecimal data"})
        elif kind == "HATCH":
            entry.update({"classification": "proved duplicate contour-fill presentation; original cutting contours retained",
                          "sourceTags": record.get("_sourceValues", record["values"]),
                          "fillSemantics": {"patternName": _one(values, 2), "solidFill": _one(values, 70),
                                            "associative": _one(values, 71), "hatchStyle": _one(values, 75),
                                            "patternType": _one(values, 76)},
                          "boundaryProof": record["_redundantHatch"]})
        elif kind == "LINE":
            entry.update({"classification": "exact zero-length LINE; zero-area source point",
                          "sourceTags": record["values"],
                          "sourcePoint": [_one(values, code, 0) for code in (10, 20, 30)],
                          "endpointEquality": "exact source numeric equality in all three coordinates; no short-edge tolerance"})
        else:
            entry.update({"classification": "infinite construction line" if kind == "XLINE" else "infinite construction ray",
                          "sourceTags": record["values"],
                          "sourceOrigin": [_one(values, code) for code in (10, 20, 30)],
                          "sourceDirection": [_one(values, code) for code in (11, 21, 31)]})
        result.append(entry)
    return result


def _compose_affine(parent, child):
    a, b, c, d, x, y = parent
    e, f, g, h, u, v = child
    return (a * e + b * g, a * f + b * h, c * e + d * g, c * f + d * h,
            a * u + b * v + x, c * u + d * v + y)


def _prepare_planar_document(document, scale, tolerance):
    """Project only certified numerical XY-plane residuals, using DXF OCS.

    LINE/SPLINE coordinates are WCS. ARC/CIRCLE and planar polylines are
    OCS, including the legal -Z basis. The full projected OCS matrix is
    retained and composed before an INSERT's block-to-world transform.
    Genuine tilted/spatial geometry remains unsupported.
    """
    provenance = []
    elevation = {"LINE": (30, 31), "SPLINE": (30, 31), "ELLIPSE": (30, 31),
                 "ARC": (30,), "CIRCLE": (30,), "LWPOLYLINE": (38,),
                 "POLYLINE": (30,), "VERTEX": (30,), "INSERT": (30,), "BLOCK": (30,), "HATCH": (30,)}
    ocs_kinds = {"ARC", "CIRCLE", "LWPOLYLINE", "POLYLINE", "INSERT", "HATCH"}
    maximum_residual = min(_PLANAR_RESIDUAL_MM, tolerance / 64)
    all_records = [record for section in ("entities", "blocks") for record in document.get(section, [])]
    source_extent = max([1.] + [abs(_number(value, str(code)))
                               for record in all_records for code, value in record["values"]
                               if int(code) in (10, 11, 12, 13, 20, 21, 22, 23)])
    for section in ("entities", "blocks"):
        polyline_extents, pending = {}, None
        for index, record in enumerate(document.get(section, [])):
            values = _values(record)
            if record["type"] == "POLYLINE":
                pending = {"index": index, "closed": bool(int(_one(values, 70, 0)) & 1), "vertices": []}
            elif record["type"] == "VERTEX" and pending is not None:
                pending["vertices"].append((_point(values, 10, 1), _number(_one(values, 42, 0), "VERTEX bulge")))
            elif record["type"] == "SEQEND" and pending is not None:
                vertices = pending["vertices"]
                extent = max([1.] + [abs(value) for point, _ in vertices for value in point])
                pairs = zip(vertices, vertices[1:] + vertices[:1]) if pending["closed"] else zip(vertices, vertices[1:])
                for (a, bulge), (b, _) in pairs:
                    if bulge:
                        # Every point of the source circular arc lies within
                        # two radii of an endpoint, including >180-degree arcs.
                        diameter = math.dist(a, b) * (abs(bulge) + 1 / abs(bulge)) / 2
                        extent = max(extent, max(map(abs, a)) + diameter, max(map(abs, b)) + diameter)
                polyline_extents[pending["index"]] = extent
                pending = None
        prepared = []
        for source_index, record in enumerate(document.get(section, [])):
            kind, values = record["type"], _values(record)
            if kind not in elevation or _annotation(record, values):
                prepared.append(record)
                continue
            normal = tuple(_number(_one(values, code, default), str(code))
                           for code, default in ((210, 0), (220, 0), (230, 1)))
            length = math.hypot(*normal)
            if not length or not math.isfinite(length):
                raise ValueError(f"Invalid zero or nonfinite extrusion direction in {kind}")
            nx, ny, nz = (value / length for value in normal)
            tilt = math.hypot(nx, ny)
            # Merely having small individual Z values is insufficient: the
            # normal's tilt across the complete source extent is bounded too.
            extent = max([1.] + [abs(_number(value, str(code)))
                                  for code, entries in values.items() if code in (10, 11, 12, 13, 20, 21, 22, 23)
                                  for value in entries])
            if kind in {"ARC", "CIRCLE"}:
                extent += abs(_number(_one(values, 40), "radius"))
            if kind == "ELLIPSE":
                extent *= 3
            if kind in {"LWPOLYLINE", "VERTEX"}:
                # A bulged edge can extend beyond its vertex bounds. The
                # chord's bounding-box diagonal bounds its circular radius.
                bulges = [abs(_number(value, "bulge")) for value in values.get(42, [])]
                extent = max(extent, source_extent) * max([1.] + [2 * (1 + b * b) / b for b in bulges if b])
            if kind == "POLYLINE":
                extent = max(extent, source_extent, polyline_extents.get(source_index, 1.))
            z_values = [(code, _number(value, str(code))) for code in elevation[kind]
                        for value in values.get(code, [])]
            z_residual = max([0.] + [abs(value) * scale for _, value in z_values])
            normal_residual = 4 * extent * scale * tilt if tilt else 0.
            if not math.isfinite(normal_residual + z_residual):
                raise ValueError(f"Nonfinite planar residual bound in {kind}")
            if tilt > 1e-12 or normal_residual + z_residual > maximum_residual:
                # Preserve the established strict diagnostics for actual
                # nonplanar input; do not silently flatten it.
                _flat(record, values, *elevation[kind])
            if kind == "INSERT" and (tilt or z_residual):
                # The 2-D block composition is exact for axial INSERTs. A
                # tilted INSERT also couples its descendants' Z coordinates
                # into XY; that needs a full 3-D block transform, not a local
                # residual check using the insertion point alone.
                raise ValueError("Unsupported nonaxial/elevated INSERT: full spatial block transform is required")
            changed = (normal != (0., 0., 1.) or any(value for _, value in z_values))
            if not changed:
                prepared.append(record)
                continue
            replacement = {**record, "values": [[code, value] for code, value in record["values"]
                                                if int(code) not in (210, 220, 230, *elevation[kind])]
                           + [[210, 0], [220, 0], [230, 1]]}
            replacement["_sourceValues"] = record.get("_sourceValues", record["values"])
            replacement["_planarResidualMm"] = normal_residual + z_residual
            ocs_matrix = None
            if kind in ocs_kinds:
                # DXF arbitrary-axis algorithm for a near +/-Z normal:
                # X = normalize(world-Y cross N), Y = N cross X.
                divisor = math.hypot(nz, nx)
                ex = (nz / divisor, 0., -nx / divisor)
                ey = (ny * ex[2], nz * ex[0] - nx * ex[2], -ny * ex[0])
                z = _number(_one(values, elevation[kind][0], 0), "OCS elevation")
                ocs_matrix = (ex[0], ey[0], ex[1], ey[1], z * nx, z * ny)
                replacement["_ocsAffine"] = ocs_matrix
            elif kind == "ELLIPSE":
                # ELLIPSE major axis is WCS; reflecting its WCS coordinates
                # would be incorrect. Its minor direction is N cross major.
                ux, uy, uz = (_number(_one(values, code, 0), str(code)) for code in (11, 21, 31))
                vx, vy, vz = ny * uz - nz * uy, nz * ux - nx * uz, nx * uy - ny * ux
                vlength = math.hypot(vx, vy, vz)
                if not vlength:
                    raise ValueError("ELLIPSE major axis is parallel to its extrusion direction")
                factor = math.hypot(ux, uy, uz) / vlength
                replacement["_ellipseMinorXY"] = (vx * factor, vy * factor)
            prepared.append(replacement)
            provenance.append({"section": section, "sourceEntity": source_index, "sourceType": kind,
                               "sourceHandle": _one(values, 5), "sourceLayer": _one(values, 8, "0"),
                               "sourceNormal": list(normal), "sourceElevationValues": list(map(list, z_values)),
                               "normalizedNormal": [nx, ny, nz], "projectedOcsMatrix": ocs_matrix,
                               "maximumOutOfPlaneResidualMm": normal_residual + z_residual,
                               "acceptedResidualLimitMm": maximum_residual,
                               "coordinatePolicy": "WCS XY projection with explicit DXF arbitrary-axis OCS mapping; source unchanged"})
        document[section] = prepared
    document["planarMappings"] = provenance


def _expand_blocks(document):
    global _SOURCE_ROLE_MODULE
    if _SOURCE_ROLE_MODULE is None:
        spec = importlib.util.spec_from_file_location("icax_dxf_source_roles", Path(__file__).with_name("dxf_source_roles.py"))
        _SOURCE_ROLE_MODULE = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_SOURCE_ROLE_MODULE)
    role_context = _SOURCE_ROLE_MODULE.SourceRoleContext(document.get("tables", ()), document["entities"], document.get("blocks", ()))
    document["_sourceRoleContext"] = role_context
    if not any(record["type"] == "INSERT" and not _annotation(record, _values(record))
               for record in document["entities"]):
        document["entities"] = [{**role_context.annotate(record), "_instancePath": []} for record in document["entities"]]
        return
    blocks, current = {}, None
    for record in document.get("blocks", []):
        if record["type"] == "BLOCK":
            if current is not None:
                raise ValueError("Nested BLOCK definitions are invalid")
            values = _values(record)
            name = str(_one(values, 2, _one(values, 3, ""))).strip()
            if not name or name.casefold() in blocks:
                raise ValueError("Missing or duplicate BLOCK name")
            current = {"name": name, "base": _point(values, 10, 1),
                       "flags": int(_one(values, 70, 0)), "records": [], "sourceRecord": record}
            blocks[name.casefold()] = current
        elif record["type"] == "ENDBLK":
            if current is None:
                raise ValueError("ENDBLK without BLOCK")
            current = None
        elif current is not None:
            current["records"].append(record)
    if current is not None:
        raise ValueError("Unterminated BLOCK definition")
    expanded, references = [], []

    def visit(records, parent=(1., 0., 0., 1., 0., 0.), stack=(), z_amplification=1., inherited_z_error=0., inherited_role=None, reference_path=(), instance_path=()):
        attributes = False
        for record in records:
            record = role_context.annotate(record, inherited_role, reference_path)
            record["_instancePath"] = list(instance_path)
            values = _values(record)
            if attributes:
                if record["type"] == "SEQEND":
                    expanded.append({**record, "type": "ICAX_ATTRIBUTE_SEQEND"})
                    attributes = False
                    continue
                if record["type"] != "ATTRIB":
                    raise ValueError("INSERT attribute sequence is missing SEQEND")
            if record["type"] != "INSERT" or _annotation(record, values):
                instance = {**record, "_affine": parent} if stack else {**record}
                instance["_worldPlanarResidualMm"] = inherited_z_error + z_amplification * record.get("_planarResidualMm", 0.)
                instance["_zAmplification"] = z_amplification
                expanded.append(instance)
            else:
                _flat(record, values, 30)
                name = str(_one(values, 2, "")).strip()
                key = name.casefold()
                if key not in blocks:
                    raise ValueError(f"Unresolved INSERT block {name!r}")
                if key in stack or len(stack) >= 16:
                    raise ValueError(f"Circular or excessive nested INSERT reference: {stack + (key,)}")
                block = blocks[key]
                _flat(block["sourceRecord"], _values(block["sourceRecord"]), 30)
                if block["flags"] & 12:
                    raise ValueError(f"External-reference INSERT {name!r} requires its source geometry")
                insertion = _point(values, 10, 1)
                sx, sy = _number(_one(values, 41, 1), "INSERT X scale"), _number(_one(values, 42, 1), "INSERT Y scale")
                sz = _number(_one(values, 43, 1), "INSERT Z scale")
                if not sx or not sy:
                    raise ValueError("INSERT has singular zero scale")
                child_z_amplification = z_amplification * abs(sz)
                child_z_error = inherited_z_error + child_z_amplification * block["sourceRecord"].get("_planarResidualMm", 0.)
                if not math.isfinite(child_z_amplification + child_z_error):
                    raise ValueError("INSERT has nonfinite world Z-scale/residual propagation")
                angle = math.radians(_number(_one(values, 50, 0), "INSERT rotation"))
                cosine, sine = math.cos(angle), math.sin(angle)
                if math.remainder(math.degrees(angle), 90.) == 0:
                    cosine, sine = round(cosine), round(sine)
                columns, rows = int(_one(values, 70, 1)), int(_one(values, 71, 1))
                if not (1 <= columns <= 128 and 1 <= rows <= 128 and columns * rows <= 4096):
                    raise ValueError("INSERT array count exceeds the bounded import resource limit")
                dx, dy = _number(_one(values, 44, 0), "INSERT column spacing"), _number(_one(values, 45, 0), "INSERT row spacing")
                a, b, c, d = cosine * sx, -sine * sy, sine * sx, cosine * sy
                base_x, base_y = block["base"]
                for row in range(rows):
                    for column in range(columns):
                        # MINSERT spacing rotates with the insertion axes but
                        # is not multiplied by the block's geometry scale.
                        x = insertion[0] + cosine * column * dx - sine * row * dy - a * base_x - b * base_y
                        y = insertion[1] + sine * column * dx + cosine * row * dy - c * base_x - d * base_y
                        local = (a, b, c, d, x, y)
                        if record.get("_ocsAffine") is not None:
                            local = _compose_affine(record["_ocsAffine"], local)
                        transform = _compose_affine(parent, local)
                        references.append({"blockName": name, "sourceHandle": _one(values, 5),
                                           "blockBasePoint": list(block["base"]), "matrix": list(transform),
                                           "arrayIndex": [row, column], "arraySize": [rows, columns],
                                           "referencePath": list(stack + (name,)), "zScale": sz,
                                           "worldZAmplification": child_z_amplification, "inheritedWorldZResidualMm": child_z_error})
                        visit(block["records"], transform, stack + (key,), child_z_amplification, child_z_error,
                              record["_sourceRole"], reference_path + (name,), instance_path +
                              ({"insertHandle": record["_sourceRole"]["sourceHandle"], "blockName": name, "row": row, "column": column},))
                attributes = int(_one(values, 66, 0)) == 1
            if len(expanded) > 1000000 or len(references) > 100000:
                raise ValueError("Expanded INSERT geometry exceeds the bounded import resource limit")
        if attributes:
            raise ValueError("INSERT attribute sequence is missing SEQEND")

    visit(document["entities"])
    document["entities"] = _inherit_paperspace(expanded)
    document["blockReferences"] = references


def _flat(record, values, *elevation_codes):
    for code in (39, *elevation_codes):
        if any(_number(value, str(code)) != 0 for value in values.get(code, [])):
            raise ValueError(f"Unsupported nonplanar {record['type']} (group {code})")
    for code, expected in ((210, 0), (220, 0), (230, 1)):
        if _number(_one(values, code, expected), str(code)) != expected:
            raise ValueError(f"Unsupported extrusion direction in {record['type']}")


def _hatch_boundary_paths(record):
    """Read ordered HATCH boundary tags without guessing unsupported edges.

    The duplicate proof currently supports straight polyline/LINE boundaries
    and complete positive-rational SPLINE parameter records. Pattern lines,
    seed points and hatch style are presentation data retained with the source.
    """
    tags = record["values"]
    starts = [i for i, (code, _) in enumerate(tags) if int(code) == 91]
    if len(starts) != 1:
        raise ValueError("HATCH must declare exactly one boundary-path count")
    cursor = starts[0]

    def take(code):
        nonlocal cursor
        if cursor >= len(tags) or int(tags[cursor][0]) != code:
            raise ValueError(f"HATCH boundary expected group {code} at ordered tag {cursor}")
        value = tags[cursor][1]
        cursor += 1
        return value

    def count(code):
        value = _number(take(code), f"HATCH count {code}")
        if value != int(value) or not 0 <= value <= 100000:
            raise ValueError(f"HATCH count {code} exceeds the bounded import resource limit")
        return int(value)

    def point(x=10):
        return _number(take(x), str(x)), _number(take(x + 10), str(x + 10))

    paths = []
    for path_index in range(count(91)):
        flags = count(92)
        edges = []
        if flags & 2:
            bulged, closed, vertices = count(72), count(73), count(93)
            if bulged not in (0, 1) or closed != 1:
                raise ValueError("HATCH polyline duplicate proof requires a closed boundary")
            points = []
            for _ in range(vertices):
                points.append(point())
                if cursor < len(tags) and int(tags[cursor][0]) == 42:
                    if _number(take(42), "HATCH bulge"):
                        raise ValueError("HATCH bulged boundary requires a dedicated analytic duplicate proof")
            if len(points) < 3:
                raise ValueError("HATCH polyline boundary has too few vertices")
            edges = [{"type": "LINE", "a": a, "b": b} for a, b in zip(points, points[1:] + points[:1])]
        else:
            for _ in range(count(93)):
                edge_type = count(72)
                if edge_type == 1:
                    edges.append({"type": "LINE", "a": point(), "b": point(11)})
                elif edge_type == 4:
                    degree, rational, periodic = count(94), count(73), count(74)
                    knot_count, control_count = count(95), count(96)
                    if not 1 <= degree <= 15 or rational not in (0, 1) or periodic not in (0, 1):
                        raise ValueError("HATCH SPLINE has unsupported degree/rational/periodic flags")
                    knots = tuple(_number(take(40), "HATCH spline knot") for _ in range(knot_count))
                    controls = tuple(point() for _ in range(control_count))
                    weights = tuple(_number(take(42), "HATCH spline weight") for _ in range(control_count)) if rational else (1.,) * control_count
                    if (len(knots) != control_count + degree + 1 or control_count <= degree or
                            any(weight <= 0 for weight in weights) or any(a > b for a, b in zip(knots, knots[1:]))):
                        raise ValueError("Invalid HATCH SPLINE parameter counts/order/weights")
                    if knots[:degree + 1] != (knots[degree],) * (degree + 1) or knots[-degree - 1:] != (knots[-degree - 1],) * (degree + 1):
                        raise ValueError("Unclamped HATCH SPLINE requires a dedicated source-domain endpoint proof")
                    edges.append({"type": "SPLINE", "degree": degree, "knots": knots,
                                  "controls": controls, "weights": weights, "periodic": periodic})
                else:
                    raise ValueError(f"HATCH edge type {edge_type} has no complete analytic duplicate proof yet")
        references = [str(take(330)) for _ in range(count(97))]
        if not edges:
            raise ValueError("HATCH boundary path has no edges")
        paths.append({"pathIndex": path_index, "flags": flags, "sourceBoundaryHandles": references, "edges": edges})
    if not paths:
        raise ValueError("HATCH contains no boundary paths")
    if cursor >= len(tags) or int(tags[cursor][0]) != 75:
        raise ValueError("HATCH boundary has unconsumed fit/source tags; no duplicate proof is assumed")
    return paths


def _prove_redundant_hatches(document, scale):
    """Deduplicate only complete boundaries already carried by vector records.

    A LINE's endpoint displacement bounds its whole parameterized segment by
    convexity. Equal positive NURBS controls/knots/weights prove the complete
    curve, independently of sampling. The final ordinary importer must still
    establish closed, valid material from all retained original vectors.
    """
    records = document["entities"]
    if not any(record["type"] == "HATCH" and not _annotation(record, _values(record)) for record in records):
        return
    lines, splines, polyline = defaultdict(list), defaultdict(list), None

    def transform(record, point):
        matrix = record.get("_affine", (1., 0., 0., 1., 0., 0.))
        if record.get("_ocsAffine") is not None:
            matrix = _compose_affine(matrix, record["_ocsAffine"])
        a, b, c, d, x, y = matrix
        return ((a * point[0] + b * point[1] + x) * scale,
                (c * point[0] + d * point[1] + y) * scale)

    def add_line(record, source_index, a, b):
        a, b = transform(record, a), transform(record, b)
        if a != b:
            lines[tuple(sorted((a, b)))].append({"sourceEntity": source_index, "sourceType": record["type"],
                                               "sourceHandle": _one(_values(record), 5), "a": a, "b": b})

    for source_index, record in enumerate(records):
        values, kind = _values(record), record["type"]
        if _annotation(record, values):
            continue
        if kind == "LINE":
            add_line(record, source_index, _point(values, 10, 1), _point(values, 11, 1))
        elif kind == "SPLINE":
            controls = tuple(transform(record, p) for p in zip(map(float, values.get(10, [])), map(float, values.get(20, []))))
            knots, weights = tuple(map(float, values.get(40, []))), tuple(map(float, values.get(41, []))) or (1.,) * len(controls)
            key = (int(_one(values, 71, -1)), knots, controls, weights)
            splines[key].append({"sourceEntity": source_index, "sourceType": kind, "sourceHandle": _one(values, 5)})
        elif kind == "LWPOLYLINE":
            points, current = [], None
            for code, value in record["values"]:
                if int(code) == 10:
                    current = [_number(value, "LWPOLYLINE X"), None, 0.]
                    points.append(current)
                elif int(code) in (20, 42) and current is not None:
                    current[1 if int(code) == 20 else 2] = _number(value, str(code))
            if all(p[1] is not None for p in points):
                pairs = zip(points, points[1:] + points[:1]) if int(_one(values, 70, 0)) & 1 else zip(points, points[1:])
                for a, b in pairs:
                    if not a[2]:
                        add_line(record, source_index, tuple(a[:2]), tuple(b[:2]))
        elif kind == "POLYLINE":
            polyline = {"record": record, "points": [], "closed": bool(int(_one(values, 70, 0)) & 1)}
        elif kind == "VERTEX" and polyline is not None:
            polyline["points"].append((_point(values, 10, 1), _number(_one(values, 42, 0), "VERTEX bulge"), source_index))
        elif kind == "SEQEND" and polyline is not None:
            points = polyline["points"]
            pairs = zip(points, points[1:] + points[:1]) if polyline["closed"] else zip(points, points[1:])
            for a, b in pairs:
                if not a[1]:
                    add_line(polyline["record"], a[2], a[0], b[0])
            polyline = None
    mappings, maximum_error, affected = [], 0., set()
    limit = _CONNECTION_TOLERANCE_MM
    for source_index, record in enumerate(records):
        if record["type"] != "HATCH" or _annotation(record, _values(record)):
            continue
        values = _values(record)
        proof = {"sourceEntity": source_index, "sourceHandle": _one(values, 5), "status": "unresolved", "paths": [],
                 "sourceTags": record.get("_sourceValues", record["values"]),
                 "fillSemantics": {"patternName": _one(values, 2), "solidFill": _one(values, 70),
                                   "associative": _one(values, 71), "hatchStyle": _one(values, 75), "patternType": _one(values, 76)}}
        try:
            paths = _hatch_boundary_paths(record)
            hatch_error, hatch_affected = 0., set()
            for path in paths:
                matches = []
                endpoints = [(transform(record, edge["a"]), transform(record, edge["b"])) if edge["type"] == "LINE"
                             else (transform(record, edge["controls"][0]), transform(record, edge["controls"][-1]))
                             for edge in path["edges"]]
                # Boundary edge records need not follow traversal direction.
                # Prove one complete degree-two component without inserting
                # missing edges or discarding open branches.
                closure_segments = [{"points": [a, b], "sourceEntity": source_index, "sourceType": "HATCH_BOUNDARY"} for a, b in endpoints]
                try:
                    closed_rings, closure_error, _, open_strokes, closure_connections = _join_curved_segments(closure_segments, limit)
                except ValueError as error:
                    raise ValueError(f"path {path['pathIndex']}: boundary closure is unproved: {error}") from error
                if len(closed_rings) != 1 or open_strokes:
                    raise ValueError(f"path {path['pathIndex']}: boundary is not one complete closed component")
                hatch_error = max(hatch_error, closure_error)
                for edge_index, edge in enumerate(path["edges"]):
                    if edge["type"] == "SPLINE":
                        controls = tuple(transform(record, p) for p in edge["controls"])
                        key = (edge["degree"], edge["knots"], controls, edge["weights"])
                        matched = splines.get(key)
                        if not matched:
                            raise ValueError(f"path {path['pathIndex']} edge {edge_index}: complete SPLINE controls/knots/weights do not match an existing vector")
                        matches.append({"edgeIndex": edge_index, "type": "SPLINE", "proof": "identical complete positive NURBS parameter record in world millimetres",
                                        "matchedEntities": matched, "maximumDeviationMm": 0.})
                    else:
                        a, b = transform(record, edge["a"]), transform(record, edge["b"])
                        matched, error = lines.get(tuple(sorted((a, b)))), 0.
                        if not matched:
                            candidates = []
                            for (u, v), references in lines.items():
                                error = min(max(math.dist(a, u), math.dist(b, v)), max(math.dist(a, v), math.dist(b, u)))
                                if error <= limit:
                                    candidates.append((error, references))
                            if len(candidates) != 1:
                                raise ValueError(f"path {path['pathIndex']} edge {edge_index}: LINE has {len(candidates)} matching vector geometries within {limit:g} mm")
                            error, matched = candidates[0]
                        hatch_error = max(hatch_error, error)
                        if error:
                            hatch_affected.update(r["sourceEntity"] for r in matched)
                        matches.append({"edgeIndex": edge_index, "type": "LINE", "proof": "complete linear interpolation; maximum endpoint displacement bounds every source point",
                                        "hatchEndpointsWorldMm": [list(a), list(b)], "matchedEntities": matched, "maximumDeviationMm": error})
                if closure_error:
                    hatch_affected.update(r["sourceEntity"] for match in matches for r in match["matchedEntities"])
                proof["paths"].append({key: value for key, value in path.items() if key != "edges"} |
                                      {"matches": matches, "maximumClosureDisplacementMm": closure_error,
                                       "closureEndpointMappings": closure_connections})
            proof.update(status="proved-redundant-boundary", maximumDeviationMm=hatch_error,
                         boundScope="source analytic boundary proof; retained vectors must independently pass the ordinary closed-material importer")
            record["_redundantHatch"] = proof
            maximum_error = max(maximum_error, hatch_error)
            affected.update(hatch_affected)
        except ValueError as error:
            proof["reason"] = str(error)
            record["_hatchMappingFailure"] = str(error)
        mappings.append(proof)
    document["hatchBoundaryMappings"] = mappings
    document["hatchMappingErrorMm"] = maximum_error
    document["hatchAffectedEntities"] = affected


def _prove_exploded_dimensions(document, scale, tolerance):
    """Recognize complete, uniquely matched source-generated dimension copies.

    The reserved anonymous *D block, anonymous flag, dimension control POINTs,
    MTEXT, LINE and SOLID records must all support the same complete copy.
    Triangle size, layer name, or an isolated matching arrow is insufficient.
    Matching is in the original document coordinate frame; arbitrary real
    parts are never searched under fitted rotations/translations.
    """
    records = document["entities"]
    if not any(record["type"] == "SOLID" and not _annotation(record, _values(record)) for record in records):
        return
    blocks, current = [], None
    for record in document.get("blocks", []):
        if record["type"] == "BLOCK":
            current = {"header": record, "records": []}
        elif record["type"] == "ENDBLK":
            if current is not None:
                blocks.append(current)
            current = None
        elif current is not None:
            current["records"].append(record)

    def point(values, code):
        return tuple(_number(_one(values, code + delta, 0), str(code + delta)) * scale for delta in (0, 10, 20))

    def geometry(record):
        values, kind = _values(record), record["type"]
        if kind == "LINE":
            return [point(values, 10), point(values, 11)]
        if kind == "SOLID":
            return [point(values, code) for code in (10, 11, 12, 13)]
        return [point(values, 10)]

    def text_content(values):
        return "".join(str(value) for code, value in values if int(code) in (1, 3))

    def descriptor(record):
        values, kind = _values(record), record["type"]
        # Dimension explosion resolves BYBLOCK colour/layer/line weight.
        # Compare all geometric/text layout fields, rather than those style
        # inheritance tags or object handles.
        if kind == "MTEXT":
            return (text_content(record["values"]),
                    tuple((code, _number(_one(values, code, default), str(code))) for code, default in
                          ((40, 1), (41, 0), (42, 0), (43, 0), (44, 1), (50, 0), (71, 1), (72, 1), (73, 1))),
                    tuple(_number(_one(values, code, default), str(code)) for code, default in ((11, 1), (21, 0), (31, 0))))
        return tuple(_number(_one(values, code, default), str(code)) for code, default in
                     ((39, 0), (210, 0), (220, 0), (230, 1)))

    candidates, diagnostics = [], []
    for block in blocks:
        header = _values(block["header"])
        name, flags = str(_one(header, 2, "")), int(_one(header, 70, 0))
        source = block["records"]
        counts = Counter(record["type"] for record in source)
        if (not re.fullmatch(r"\*D\d+", name, flags=re.IGNORECASE) or not flags & 1 or
                not 2 <= counts["POINT"] <= 4 or counts["MTEXT"] != 1 or not counts["LINE"] or not counts["SOLID"] or
                any(kind not in {"LINE", "SOLID", "MTEXT", "POINT"} for kind in counts)):
            continue
        proof = {"sourceBlockName": name, "sourceBlockHandle": _one(header, 5), "sourceBlockOwner": _one(header, 330),
                 "sourceBlockFlags": flags, "sourceBlockTags": block["header"]["values"], "status": "unresolved", "matches": []}
        source_text = next(record for record in source if record["type"] == "MTEXT")
        extent = max([1.] + [abs(value) for record in source if record["type"] != "POINT" for p in geometry(record) for value in p])
        copy_tolerance = min(_CONNECTION_TOLERANCE_MM, 256 * sys.float_info.epsilon * extent)
        proof["numericCopyBoundMm"] = copy_tolerance

        def compatible(source_record, model_record):
            if source_record["type"] != model_record["type"] or descriptor(source_record) != descriptor(model_record):
                return None
            a, b = geometry(source_record), geometry(model_record)
            distance = max(math.dist(p, q) for p, q in zip(a, b))
            if source_record["type"] == "LINE":
                distance = min(distance, max(math.dist(p, q) for p, q in zip(a, reversed(b))))
            return distance if distance <= copy_tolerance else None

        text_matches = [(i, record) for i, record in enumerate(records)
                        if record["type"] == "MTEXT" and record.get("_affine") is None and
                        not _number(_one(_values(record), 67, 0), "paper-space flag") and compatible(source_text, record) is not None]
        all_text_matches = list(text_matches)
        exact_text_matches = [(i, record) for i, record in text_matches if compatible(source_text, record) == 0]
        if exact_text_matches:
            text_matches = exact_text_matches
        if len(text_matches) != 1:
            proof["reason"] = f"source dimension MTEXT has {len(text_matches)} complete model-space copies"
            diagnostics.append(proof)
            continue
        _, model_text = text_matches[0]
        proof["unselectedModelTextCandidates"] = [
            {"modelEntity": index, "sourceTags": record.get("_sourceValues", record["values"]),
             "maximumDeviationMm": compatible(source_text, record)}
            for index, record in all_text_matches if index != text_matches[0][0]]
        model_layer = _one(_values(model_text), 8, "0")
        used, complete = set(), True
        for source_record in source:
            if source_record["type"] == "POINT":
                continue  # Dimension definition/control points are annotation by DXF type.
            found = []
            for model_index, model_record in enumerate(records):
                values = _values(model_record)
                if (model_record.get("_affine") is not None or _one(values, 8, "0") != model_layer or
                        _number(_one(values, 67, 0), "paper-space flag")):
                    continue
                distance = compatible(source_record, model_record)
                if distance is not None:
                    found.append((model_index, model_record, distance))
            exact = [match for match in found if match[2] == 0]
            all_found = list(found)
            if exact:
                found = exact
            if len(found) != 1 or found[0][0] in used:
                proof["reason"] = f"source {source_record['type']} {_one(_values(source_record), 5)} has {len(found)} unique model-space copies"
                proof["unresolvedCandidates"] = [{"modelEntity": index, "sourceTags": record.get("_sourceValues", record["values"]),
                                                   "maximumDeviationMm": distance} for index, record, distance in all_found]
                complete = False
                break
            model_index, model_record, distance = found[0]
            used.add(model_index)
            proof["matches"].append({"sourceType": source_record["type"], "sourceHandle": _one(_values(source_record), 5),
                                     "sourceTags": source_record.get("_sourceValues", source_record["values"]),
                                     "modelEntity": model_index, "modelHandle": _one(_values(model_record), 5),
                                     "sourceGeometryWorldMm": list(map(list, geometry(source_record))),
                                     "modelGeometryWorldMm": list(map(list, geometry(model_record))), "maximumDeviationMm": distance,
                                     "unselectedBoundedCandidates": [{"modelEntity": index, "sourceHandle": _one(_values(other), 5),
                                                                      "sourceTags": other.get("_sourceValues", other["values"]),
                                                                      "maximumDeviationMm": delta, "geometryRetained": True}
                                                                     for index, other, delta in all_found if index != model_index]})
        if complete:
            proof.update(status="complete-copy-candidate", modelLayer=model_layer,
                         maximumDeviationMm=max(match["maximumDeviationMm"] for match in proof["matches"]),
                         matchingPolicy="unique exact represented copy first; bounded numerical match only when no exact copy exists")
            candidates.append((proof, used))
        diagnostics.append(proof)
    ownership = Counter(index for _, indices in candidates for index in indices)
    line_bridges, graph_failure, protected_reasons = set(), None, {}
    if candidates:
        try:
            # Extract every original vector, including all matched dimension
            # LINEs. A shared cutting/hole edge must participate in this graph
            # before any annotation candidate is filtered.
            segments = _import_curved_document(document, scale, tolerance, _endpoint_graph_only=True)
            centers, buckets, adjacency, ends = [], defaultdict(list), defaultdict(list), []
            connection = document.get("connectionToleranceMm", _CONNECTION_TOLERANCE_MM)
            for edge_index, segment in enumerate(segments):
                ids = []
                for p in (segment["points"][0], segment["points"][-1]):
                    cell = tuple(math.floor(value / connection) for value in p)
                    near = [index for x in range(cell[0] - 1, cell[0] + 2) for y in range(cell[1] - 1, cell[1] + 2)
                            for index in buckets.get((x, y), []) if math.dist(centers[index], p) <= connection]
                    if len(near) > 1:
                        raise ValueError("ambiguous source endpoints prevent the dimension LINE bridge certificate")
                    if near:
                        index = near[0]
                    else:
                        index = len(centers)
                        centers.append(p)
                        buckets[cell].append(index)
                    ids.append(index)
                ends.append(ids)
                a, b = ids
                adjacency[a].append((b, edge_index))
                adjacency[b].append((a, edge_index))
            discovery, low, parent, bridges, tick = {}, {}, {}, set(), 0
            for root in range(len(centers)):
                if root in discovery:
                    continue
                discovery[root] = low[root] = tick
                tick += 1
                stack = [(root, iter(adjacency[root]))]
                while stack:
                    node, iterator = stack[-1]
                    try:
                        other, edge = next(iterator)
                    except StopIteration:
                        stack.pop()
                        if node in parent:
                            previous, edge = parent[node]
                            if low[node] > discovery[previous]:
                                bridges.add(edge)
                            low[previous] = min(low[previous], low[node])
                        continue
                    if node in parent and edge == parent[node][1]:
                        continue
                    if other not in discovery:
                        parent[other] = node, edge
                        discovery[other] = low[other] = tick
                        tick += 1
                        stack.append((other, iter(adjacency[other])))
                    else:
                        low[node] = min(low[node], discovery[other])
            by_entity = defaultdict(list)
            for index, segment in enumerate(segments):
                by_entity[segment.get("sourceEntity")].append(index)
            line_bridges = {entity for entity, edges in by_entity.items() if edges and all(edge in bridges for edge in edges)}
            # The endpoint graph does not split T contacts, crossings or
            # partial overlaps. Certify their absence separately; uncertain
            # curve contacts retain the original LINE's material role.
            shapely, _, _ = _geos()
            geometries = [shapely.LineString(s["points"]) for s in segments]
            bounds = [g.bounds for g in geometries]
            coordinate = max([1.] + [abs(value) for bound in bounds for value in bound])
            guard = connection + 256 * sys.float_info.epsilon * coordinate
            curve_guard = guard + tolerance / 8 + document.get("hatchMappingErrorMm", 0.)
            requested_lines = {match["modelEntity"] for proof, _ in candidates for match in proof["matches"] if match["sourceType"] == "LINE"}
            for entity in line_bridges & requested_lines:
                index = by_entity[entity][0]
                a, b = segments[index]["points"][0], segments[index]["points"][-1]
                for other, segment in enumerate(segments):
                    if other == index:
                        continue
                    uncertainty = curve_guard if segment.get("curved") else guard
                    first, second = bounds[index], bounds[other]
                    if (first[2] + uncertainty < second[0] or second[2] + uncertainty < first[0] or
                            first[3] + uncertainty < second[1] or second[3] + uncertainty < first[1]):
                        continue
                    if geometries[index].distance(geometries[other]) > uncertainty:
                        continue
                    points = segment["points"]
                    common = set((a, b)) & set((points[0], points[-1]))
                    safe_endpoint_only = False
                    if len(points) == 2 and not segment.get("curved") and len(common) == 1:
                        u, v = points
                        shared = next(iter(common))
                        # Two represented straight segments can meet only at
                        # their common endpoint, unless they overlap there.
                        safe_endpoint_only = not (
                            _cross(a, b, u) == _cross(a, b, v) == 0 and
                            (any(p != shared and _on_segment(a, b, p) for p in (u, v)) or
                             any(p != shared and _on_segment(u, v, p) for p in (a, b))))
                    if not safe_endpoint_only:
                        protected_reasons[entity] = f"possible internal contact/overlap with source entity {segment.get('sourceEntity')}; endpoint graph alone is insufficient"
                        break
            line_bridges.difference_update(protected_reasons)
        except (ValueError, OverflowError) as error:
            graph_failure = str(error)
    for proof, indices in candidates:
        if any(ownership[index] != 1 for index in indices):
            proof.update(status="unresolved", reason="overlapping source dimension groups have ambiguous model-space ownership")
            continue
        proof["status"] = "proved-complete-dimension-copy"
        for match in proof["matches"]:
            if records[match["modelEntity"]].get("_materialSemantics", {}).get("role") in {"through-hole", "outer-profile"}:
                match.update(materialRoleRetained=True, materialProtection="explicit source-bound material/through-hole role takes precedence over dimension-copy inference")
                continue
            if match["sourceType"] == "LINE" and match["modelEntity"] not in line_bridges:
                match["materialRoleRetained"] = True
                match["materialProtection"] = graph_failure or protected_reasons.get(match["modelEntity"]) or "source LINE participates in a closed/parallel endpoint graph or has no proved bridge certificate"
                continue
            match["materialRoleRetained"] = False
            if match["sourceType"] == "LINE":
                match["materialProtection"] = "proved bridge in the complete original vector endpoint graph; cannot be the sole boundary of a material ring or hole"
            records[match["modelEntity"]]["_dimensionCopy"] = {
                "sourceBlockName": proof["sourceBlockName"], "sourceBlockHandle": proof["sourceBlockHandle"],
                "sourceBlockOwner": proof["sourceBlockOwner"], "sourceBlockFlags": proof["sourceBlockFlags"],
                "numericCopyBoundMm": proof["numericCopyBoundMm"], "completeGroupMatches": proof["matches"],
                "matchedSourceHandle": match["sourceHandle"], "matchedMaximumDeviationMm": match["maximumDeviationMm"]}
    document["dimensionCopyMappings"] = diagnostics


def _try_source_process_roles(document, scale, tolerance):
    """Admit explicit open process strokes only after independent material.

    This path is attempted only when ordinary source import failed. Successful
    existing inputs therefore retain their original geometry/vertex ordering.
    """
    records = document["entities"]
    candidates = {index: record for index, record in enumerate(records)
                  if record["type"] == "LINE" and not _annotation(record, _values(record)) and
                  record.get("_materialSemantics", {}).get("role") not in {"through-hole", "outer-profile"} and
                  record.get("_sourceRole", {}).get("roleCandidate") in {"bend", "bend-extent", "centerline"}}
    if not candidates:
        return None
    return _recover_source_process_roles(document, scale, tolerance, records, candidates)


def _semantics_module():
    global _MATERIAL_SEMANTICS_MODULE
    if _MATERIAL_SEMANTICS_MODULE is None:
        spec = importlib.util.spec_from_file_location("icax_dxf_material_semantics", Path(__file__).with_name("dxf_material_semantics.py"))
        _MATERIAL_SEMANTICS_MODULE = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_MATERIAL_SEMANTICS_MODULE)
    return _MATERIAL_SEMANTICS_MODULE


def _apply_material_semantics(document, plan, scale, tolerance):
    if plan is None:
        return
    document["materialSemantics"] = {**plan, "status": "validating-targets", "geometryReplaced": False}
    bound = _semantics_module().bind(plan, document["entities"])
    document["materialSemantics"] = bound
    shapely, Polygon, explain_validity = _geos()
    connection = document.get("connectionToleranceMm", _CONNECTION_TOLERANCE_MM)
    # Prove every declared whole feature before applying any exclusion. A
    # partial or ambiguous boundary never becomes a silently filled hole.
    for feature in bound["bindings"]:
        members = set(feature["familyEntities"])
        view = {**document, "entities": [record for index, record in enumerate(document["entities"]) if index in members]}
        segments = _import_curved_document(view, scale, tolerance, _endpoint_graph_only=True)
        if not segments:
            raise ValueError(f"Material semantics feature {feature['id']} has no complete original curve representation")
        if feature["topology"] == "closed-contours":
            rings, error, _, open_strokes, connections = _join_curved_segments(segments, connection)
            if not rings or open_strokes:
                raise ValueError(f"Material semantics feature {feature['id']} contains incomplete closed contours")
            for points in rings:
                if len(points) < 3:
                    raise ValueError("Material semantics closed feature has too few original points")
                polygon = Polygon(points)
                if polygon.is_empty or polygon.area <= 0 or not polygon.is_valid:
                    raise ValueError(f"Material semantics closed feature has invalid original topology: {explain_validity(polygon)}")
            feature["topologyProof"] = {"closedContours": len(rings), "originalSegments": len(segments),
                                         "maximumConnectionDisplacementMm": error, "endpointConnections": connections,
                                         "connectionToleranceMm": connection, "openStrokes": 0}
        else:
            closed, _, _, _, _ = _join_curved_segments(segments, connection, allow_no_closed=True)
            if closed:
                raise ValueError(f"Material semantics feature {feature['id']} declares open strokes but closes within the import connection bound")
            parts = shapely.get_parts(shapely.union_all([shapely.LineString(segment["points"]) for segment in segments]))
            if any(polygon.area > 0 for polygon in shapely.polygonize(parts).geoms):
                raise ValueError(f"Material semantics feature {feature['id']} declares open strokes but establishes a closed region")
            feature["topologyProof"] = {"closedContours": 0, "originalSegments": len(segments), "hasClosedRegion": False}
            if feature["role"] == "non-through-machining":
                document.setdefault("_semanticOpenProcessSegments", []).extend(segments)
    markers = {}
    for feature in bound["bindings"]:
        for target in feature["targets"]:
            marker = {"documentSha256": bound["documentSha256"], "featureId": feature["id"], "role": feature["role"],
                      "operation": feature.get("operation"), "sourceDepthMm": feature.get("sourceDepthMm"),
                      "evidenceIds": feature["evidence"], "selector": target["selector"],
                      "expandedInstancePath": target["expandedInstancePath"], "sourceRecordSha256": target["sourceRecordSha256"],
                      "logicalEntity": target["sourceEntity"]}
            for index in target["familyEntities"]:
                markers[index] = marker
    for index, marker in markers.items():
        document["entities"][index]["_materialSemantics"] = marker
    bound.update(status="roles-bound-material-not-yet-validated", geometryReplaced=False,
                 sourceEntityInstancesBound=len(markers), appliedToPublishedMaterial=False)


def _validate_material_semantics(document, imported):
    plan = document.get("materialSemantics")
    if plan is None:
        return imported
    metadata = imported["metadata"]
    if metadata.get("nonBoundaryEntities"):
        raise ValueError("Material semantics leaves unclassified open cutting strokes; complete material and holes are not proved")
    ownership = metadata.get("sourceContourOwnership", [])
    for feature in plan["bindings"]:
        members = set(feature["familyEntities"])
        represented = [ring for ring in ownership if members.intersection(ring["sourceEntities"])]
        if feature["role"] == "non-through-machining":
            if represented:
                raise ValueError("Explicit non-through source entity still participates in a material contour")
        else:
            for target in feature["targets"]:
                target_members = set(target["familyEntities"])
                target_rings = [ring for ring in represented if target_members.intersection(ring["sourceEntities"])]
                if not target_rings or any(ring["role"] != feature["role"] for ring in target_rings):
                    raise ValueError(f"Material semantics {feature['role']} target {target['sourceHandle']} did not retain its declared source contour role")
            if len(represented) != feature["topologyProof"]["closedContours"]:
                raise ValueError("Material semantics feature does not map one-to-one onto its complete original material contours")
        feature["resultContourOwnership"] = represented
    actual = {"materialComponents": len(imported["geometries"]),
              "throughHoles": sum(len(geometry["holes"]) for geometry in imported["geometries"])}
    if actual != plan["expectations"]:
        raise ValueError(f"Material semantics physical expectations failed: expected {plan['expectations']}, actual {actual}")
    plan.update(status="applied-and-validated", appliedToPublishedMaterial=True, actualMaterial=actual,
                rawSourceCoordinatesChanged=False, rawSourceEntitiesRemovedFromFile=False)
    metadata["materialSemantics"] = plan
    return imported


def _recover_source_process_roles(document, scale, tolerance, records, candidates):
    context = document.get("_sourceRoleContext")
    document["sourceRoleProvenance"] = context.provenance() if context is not None else None
    proof = {"status": "unresolved", "candidateEntities": sorted(candidates), "sourceGeometryChanged": False}
    document["sourceRoleValidation"] = proof
    probe = {**document, "entities": [{**record, "_sourceRoleProbe": True} if index in candidates else record
                                       for index, record in enumerate(records)]}
    try:
        # The probe retains every non-candidate entity. Unknown/open material,
        # holes, disconnected components and unrelated strokes still fail.
        material = _material_from_document(probe, scale, tolerance)
        if material["metadata"].get("nonBoundaryEntities"):
            raise ValueError("retained source cutting vectors contain open strokes; complete material/hole ownership is not proved")
        cut_segments = _import_curved_document(probe, scale, tolerance, _endpoint_graph_only=True)
        original_segments = _import_curved_document(document, scale, tolerance, _endpoint_graph_only=True)
        source_segments = {s.get("sourceEntity"): s for s in original_segments if s.get("sourceEntity") in candidates}
        if set(source_segments) != set(candidates):
            raise ValueError("not every process candidate has a complete original LINE representation")
        shapely, _, _ = _geos()
        process_proof_segments = [source_segments[index] for index in candidates] + document.get("_semanticOpenProcessSegments", [])
        joined_cycles, _, _, _, _ = _join_curved_segments(process_proof_segments,
                                                         document.get("connectionToleranceMm", _CONNECTION_TOLERANCE_MM),
                                                         allow_no_closed=True)
        if joined_cycles:
            raise ValueError("process candidates and declared open machining strokes establish a closed region within the import connection bound")
        process_lines = [shapely.LineString(segment["points"]) for segment in process_proof_segments]
        # Open process strokes may cross, but a closed candidate-only region
        # is not silently removed as an annotation.
        noded = shapely.union_all(process_lines)
        if any(p.area > 0 for p in shapely.polygonize(shapely.get_parts(noded)).geoms):
            raise ValueError("process candidates themselves establish a closed region; material ownership remains unresolved")
        boundary_lines = [shapely.LineString(segment["points"]) for segment in cut_segments]
        boundary = shapely.union_all(boundary_lines)
        uncertainty = max(document.get("connectionToleranceMm", _CONNECTION_TOLERANCE_MM),
                          material["metadata"].get("maxDeviationMm", 0.),
                          material["metadata"].get("numericalGuardMm", 0.))
        cut_endpoints = [point for segment in cut_segments for point in (segment["points"][0], segment["points"][-1])]
        accepted = {}
        for index, record in candidates.items():
            role = record["_sourceRole"]
            points = source_segments[index]["points"]
            a, b = points[0], points[-1]
            endpoint_distances = [shapely.Point(point).distance(boundary) for point in (a, b)]
            endpoint_node_distances = [min(math.dist(point, node) for node in cut_endpoints) for point in (a, b)]
            if role["roleCandidate"] == "centerline" and min(endpoint_node_distances) <= uncertainty:
                raise ValueError(f"centerline entity {index} shares or may share a source cutting endpoint")
            for segment in cut_segments:
                # A source LINE cannot be an additional positive-length copy
                # of a cutting edge under its process label.
                for u, v in zip(segment["points"], segment["points"][1:]):
                    if _cross(a, b, u) == _cross(a, b, v) == 0:
                        axis = 0 if abs(b[0] - a[0]) >= abs(b[1] - a[1]) else 1
                        overlap = min(max(a[axis], b[axis]), max(u[axis], v[axis])) - max(min(a[axis], b[axis]), min(u[axis], v[axis]))
                        if overlap > 0:
                            raise ValueError(f"process entity {index} shares a positive-length source cutting boundary")
            accepted[index] = {"role": role["roleCandidate"], "materialComponents": len(material["geometries"]),
                               "materialHoles": sum(len(g["holes"]) for g in material["geometries"]),
                               "originalBoundarySegments": len(cut_segments), "candidateGraphHasClosedRegion": False,
                               "sharesPositiveLengthCutBoundary": False,
                               "endpointBoundaryDistancesMm": endpoint_distances,
                               "endpointCutNodeDistancesMm": endpoint_node_distances,
                               "nonCandidateCutVectorsAllClosed": True,
                               "boundaryUncertaintyMm": uncertainty,
                               "sourceRoleEvidence": role["roleEvidence"],
                               "sourceRoleDoesNotAuthorizeGeometryAlone": True}
        for index, evidence in accepted.items():
            records[index]["_acceptedSourceRole"] = evidence
        proof.update(status="proved-process-strokes", acceptedEntities=accepted,
                     materialComponents=len(material["geometries"]), materialHoles=sum(len(g["holes"]) for g in material["geometries"]))
        document["nonMaterialEntities"] = _non_material_provenance(document, document["_sourcePath"])
        return _material_from_document(document, scale, tolerance)
    except ValueError as error:
        proof.update(status="unresolved", reason=str(error))
        for index in candidates:
            records[index].pop("_acceptedSourceRole", None)
        document["nonMaterialEntities"] = _non_material_provenance(document, document["_sourcePath"])
        return None


def _point(values, x_code, scale):
    return (_number(_one(values, x_code), str(x_code)) * scale,
            _number(_one(values, x_code + 10), str(x_code + 10)) * scale)


def _fraction_point(point):
    return Fraction(point[0]), Fraction(point[1])


def _cross(a, b, c):
    a, b, c = _fraction_point(a), _fraction_point(b), _fraction_point(c)
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _on_segment(a, b, point):
    return (_cross(a, b, point) == 0 and
            min(a[0], b[0]) <= point[0] <= max(a[0], b[0]) and
            min(a[1], b[1]) <= point[1] <= max(a[1], b[1]))


def _intersects(a, b, c, d):
    if (max(a[0], b[0]) < min(c[0], d[0]) or max(c[0], d[0]) < min(a[0], b[0]) or
            max(a[1], b[1]) < min(c[1], d[1]) or max(c[1], d[1]) < min(a[1], b[1])):
        return False
    ac, ad, ca, cb = _cross(a, b, c), _cross(a, b, d), _cross(c, d, a), _cross(c, d, b)
    if ((ac > 0 > ad or ad > 0 > ac) and (ca > 0 > cb or cb > 0 > ca)):
        return True
    return ((ac == 0 and _on_segment(a, b, c)) or (ad == 0 and _on_segment(a, b, d)) or
            (ca == 0 and _on_segment(c, d, a)) or (cb == 0 and _on_segment(c, d, b)))


def _distance_sq(point, a, b):
    point, a, b = _fraction_point(point), _fraction_point(a), _fraction_point(b)
    dx, dy = b[0] - a[0], b[1] - a[1]
    px, py = point[0] - a[0], point[1] - a[1]
    length = dx * dx + dy * dy
    projection = px * dx + py * dy
    if not length or projection <= 0:
        return px * px + py * py
    if projection >= length:
        return (point[0] - b[0]) ** 2 + (point[1] - b[1]) ** 2
    return (px * dy - py * dx) ** 2 / length


def _circle_point(loop, point):
    point, center = _fraction_point(point), _fraction_point(loop["center"])
    return (point[0] - center[0]) ** 2 + (point[1] - center[1]) ** 2 - Fraction(loop["radius"]) ** 2


def _inside_polygon(point, points):
    inside = False
    for a, b in zip(points, points[1:] + points[:1]):
        if _on_segment(a, b, point):
            return 0
        if (a[1] > point[1]) != (b[1] > point[1]):
            cross = _cross(a, b, point)
            if (b[1] > a[1] and cross > 0) or (b[1] < a[1] and cross < 0):
                inside = not inside
    return 1 if inside else -1


def _inside(loop, point):
    if loop["kind"] == "circle":
        value = _circle_point(loop, point)
        return 1 if value < 0 else (-1 if value > 0 else 0)
    return _inside_polygon(point, loop["points"])


def _representative(loop):
    if loop["kind"] == "polygon":
        return loop["points"][0]
    center = _fraction_point(loop["center"])
    return center[0] + Fraction(loop["radius"]), center[1]


def _boundaries_intersect(a, b):
    if a["kind"] == b["kind"] == "circle":
        ca, cb = _fraction_point(a["center"]), _fraction_point(b["center"])
        distance = (ca[0] - cb[0]) ** 2 + (ca[1] - cb[1]) ** 2
        ra, rb = Fraction(a["radius"]), Fraction(b["radius"])
        return (ra - rb) ** 2 <= distance <= (ra + rb) ** 2
    if a["kind"] == "circle" or b["kind"] == "circle":
        circle, polygon = (a, b) if a["kind"] == "circle" else (b, a)
        points = polygon["points"]
        for p, q in zip(points, points[1:] + points[:1]):
            first, second = _circle_point(circle, p), _circle_point(circle, q)
            if first == 0 or second == 0 or first * second < 0:
                return True
            if first > 0 and second > 0 and _distance_sq(circle["center"], p, q) <= Fraction(circle["radius"]) ** 2:
                return True
        return False
    pa, pb = a["points"], b["points"]
    return any(_intersects(p, q, u, v)
               for p, q in zip(pa, pa[1:] + pa[:1])
               for u, v in zip(pb, pb[1:] + pb[:1]))


def _polygon(points):
    points = list(points)
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    if len(points) < 3 or len(set(points)) != len(points):
        raise ValueError("Invalid closed ring: too few or repeated vertices")
    if any(not math.isfinite(value) for point in points for value in point):
        raise ValueError("Nonfinite transformed DXF coordinates")
    area2 = sum(Fraction(a[0]) * Fraction(b[1]) - Fraction(a[1]) * Fraction(b[0])
                for a, b in zip(points, points[1:] + points[:1]))
    if not area2:
        raise ValueError("Closed DXF ring has zero area")
    size = len(points)
    for i in range(size):
        a, b = points[i], points[(i + 1) % size]
        for j in range(i + 1, size):
            if j == i + 1 or (i == 0 and j == size - 1):
                continue
            if _intersects(a, b, points[j], points[(j + 1) % size]):
                raise ValueError("Self-intersecting DXF ring")
    return {"kind": "polygon", "points": points}


def _line_rings(segments):
    adjacent = defaultdict(list)
    for index, (a, b) in enumerate(segments):
        if a == b:
            raise ValueError("Zero-length DXF LINE segment")
        adjacent[a].append(index)
        adjacent[b].append(index)
    if any(len(edges) != 2 for edges in adjacent.values()):
        raise ValueError("Open or branching LINE chain: original endpoints are not exactly coincident")
    remaining = set(range(len(segments)))
    result = []
    while remaining:
        edge = min(remaining)
        start, current = segments[edge]
        points = [start]
        remaining.remove(edge)
        while current != start:
            points.append(current)
            next_edges = [value for value in adjacent[current] if value in remaining]
            if len(next_edges) != 1:
                raise ValueError("Invalid LINE-chain closure")
            edge = next_edges[0]
            remaining.remove(edge)
            a, b = segments[edge]
            current = b if a == current else a
        result.append(_polygon(points))
    return result


def _circle_ring(loop, tolerance, *, hole):
    radius, center = loop["radius"], loop["center"]
    # Half the requested bound leaves room for represented coordinate rounding.
    argument = 1 - min(.5, tolerance / (2 * radius))
    angle = math.acos(argument)
    if not angle:
        raise ValueError("Circle tolerance is below the represented coordinate resolution")
    count = max(8, math.ceil(math.pi / angle))
    for _ in range(4):
        if count > _MAX_CIRCLE_VERTICES:
            raise ValueError("Circle import tolerance exceeds the curve vertex resource limit")
        sample_radius = radius if hole else radius / math.cos(math.pi / count)
        sample_radius *= 1 + (-128 if hole else 128) * sys.float_info.epsilon
        points = [(center[0] + sample_radius * math.cos(2 * math.pi * i / count),
                   center[1] + sample_radius * math.sin(2 * math.pi * i / count))
                  for i in range(count)]
        if len(set(points)) != count:
            raise ValueError("Circle import cannot represent distinct vertices at these coordinates")
        r, epsilon = Fraction(radius), Fraction(tolerance)
        distances = [_distance_sq(center, a, b) for a, b in zip(points, points[1:] + points[:1])]
        vertex_distances = [_circle_point(loop, point) + r * r for point in points]
        if hole:
            safe = max(vertex_distances) <= r * r and (epsilon >= r or min(distances) >= (r - epsilon) ** 2)
        else:
            safe = min(distances) >= r * r and max(vertex_distances) <= (r + epsilon) ** 2
        if safe:
            # The exact rational comparisons above certify every represented
            # chord, not just the intended trigonometric sample positions.
            return points
        count *= 2
    raise ValueError("Circle approximation could not certify the requested material/error bound")


def import_material(path, scale_to_mm=1, tolerance_mm=.01, connection_tolerance_mm=None, material_semantics=None):
    scale = _number(scale_to_mm, "scale_to_mm")
    tolerance = _number(tolerance_mm, "tolerance_mm")
    if scale <= 0 or tolerance <= 0:
        raise ValueError("DXF import scale and curve tolerance must be positive")
    connection_tolerance = (_CONNECTION_TOLERANCE_MM if connection_tolerance_mm is None
                            else _number(connection_tolerance_mm, "connection_tolerance_mm"))
    if connection_tolerance <= 0 or connection_tolerance > tolerance / 8:
        raise ValueError("DXF endpoint connection tolerance must be positive and fit within one eighth of the total import error budget")
    document = _document(path)
    document["_sourcePath"] = path
    document["connectionToleranceMm"] = connection_tolerance
    document["explicitConnectionTolerance"] = connection_tolerance_mm is not None
    document["entities"] = _inherit_paperspace(document["entities"])
    document["nonMaterialEntities"] = _non_material_provenance(document, path)
    try:
        semantic_plan = _semantics_module().load(material_semantics, document.get("sourceDxfSha256")) if material_semantics is not None else None
        _prepare_planar_document(document, scale, tolerance)
        _expand_blocks(document)
        _apply_material_semantics(document, semantic_plan, scale, tolerance)
        _prove_redundant_hatches(document, scale)
        _prove_exploded_dimensions(document, scale, tolerance)
        document["nonMaterialEntities"] = _non_material_provenance(document, path)
        maximum_world_residual = 0.
        for record in document["entities"]:
            residual = record.get("_planarResidualMm", 0.)
            transform = record.get("_affine")
            if transform is not None:
                residual *= max(1., math.hypot(*transform[:4]))
            residual = max(residual, record.get("_worldPlanarResidualMm", 0.))
            if residual > min(_PLANAR_RESIDUAL_MM, tolerance / 64):
                raise ValueError("Expanded INSERT amplifies the certified planar residual beyond its import bound")
            maximum_world_residual = max(maximum_world_residual, residual)
        document["maximumPlanarResidualMm"] = maximum_world_residual
        try:
            imported = _material_from_document(document, scale, tolerance)
        except ValueError:
            imported = _try_source_process_roles(document, scale, tolerance)
            if imported is None:
                raise
        if semantic_plan is not None and imported["metadata"].get("nonBoundaryEntities"):
            recovered = _try_source_process_roles(document, scale, tolerance)
            if recovered is not None:
                imported = recovered
        return _validate_material_semantics(document, imported)
    except ValueError as error:
        # Preserve provenance even when the remaining vectors cannot establish
        # closed material. Catalogue callers may store it with the actual failure.
        error.non_material_entities = document["nonMaterialEntities"]
        error.hatch_boundary_mappings = document.get("hatchBoundaryMappings", [])
        error.dimension_copy_mappings = document.get("dimensionCopyMappings", [])
        error.source_role_validation = document.get("sourceRoleValidation")
        error.source_role_provenance = document.get("sourceRoleProvenance")
        if document.get("materialSemantics") is not None:
            document["materialSemantics"].update(status="rejected", appliedToPublishedMaterial=False, reason=str(error))
            error.material_semantics_diagnostics = document["materialSemantics"]
        raise


def _material_from_document(document, scale, tolerance):
    if document.get("materialSemantics") or document.get("blockReferences") or document.get("planarMappings") or document.get("hatchMappingErrorMm") or any(not _annotation(record, _values(record)) and (record["type"] in {"ARC", "SPLINE", "ELLIPSE"} or
           (record["type"] in {"LWPOLYLINE", "VERTEX"} and
            any(int(code) == 42 and _number(value, "bulge") != 0
                for code, value in record["values"])))
           for record in document["entities"]):
        return _import_curved_document(document, scale, tolerance)
    loops, segments, ignored = [], [], Counter()
    polyline = None
    for record in document["entities"]:
        kind, values = record["type"], _values(record)
        annotation = _annotation(record, values)
        if annotation:
            ignored[annotation] += 1
            continue
        if kind == "LINE":
            _flat(record, values, 30, 31)
            segments.append((_point(values, 10, scale), _point(values, 11, scale)))
        elif kind == "CIRCLE":
            _flat(record, values, 30)
            radius = _number(_one(values, 40), "circle radius") * scale
            if radius <= 0 or not math.isfinite(radius):
                raise ValueError("Invalid CIRCLE radius")
            loops.append({"kind": "circle", "center": _point(values, 10, scale), "radius": radius})
        elif kind == "LWPOLYLINE":
            _flat(record, values, 38)
            if any(_number(value, str(code)) != 0 for code in (40, 41, 42, 43) for value in values.get(code, [])):
                raise ValueError("Unsupported curved/width LWPOLYLINE: bulge and width require explicit material mapping")
            points, point = [], None
            for code, value in record["values"]:
                if code == 10:
                    if point is not None and len(point) != 2:
                        raise ValueError("Malformed LWPOLYLINE vertex")
                    point = [_number(value, "vertex X") * scale]
                    points.append(point)
                elif code == 20:
                    if point is None or len(point) != 1:
                        raise ValueError("Malformed LWPOLYLINE vertex")
                    point.append(_number(value, "vertex Y") * scale)
            flags = int(_one(values, 70, 0))
            if flags & ~129 or any(len(point) != 2 for point in points) or int(_one(values, 90, len(points))) != len(points):
                raise ValueError("Invalid planar LWPOLYLINE flags/count")
            points = list(map(tuple, points))
            if flags & 1:
                loops.append(_polygon(points))
            else:
                segments.extend(zip(points, points[1:]))
        elif kind == "POLYLINE":
            _flat(record, values, 30)
            flags = int(_one(values, 70, 0))
            if polyline is not None or flags & ~129:
                raise ValueError("Unsupported spatial/nested POLYLINE")
            if any(_number(value, str(code)) != 0 for code in (40, 41) for value in values.get(code, [])):
                raise ValueError("Unsupported POLYLINE width")
            polyline = {"closed": bool(flags & 1), "points": []}
        elif kind == "VERTEX":
            _flat(record, values, 30)
            if polyline is None or int(_one(values, 70, 0)) != 0:
                raise ValueError("Unsupported VERTEX without planar POLYLINE")
            if any(_number(value, str(code)) != 0 for code in (40, 41, 42) for value in values.get(code, [])):
                raise ValueError("Unsupported curved/width POLYLINE vertex")
            polyline["points"].append(_point(values, 10, scale))
        elif kind == "SEQEND":
            if polyline is None:
                raise ValueError("Unmatched POLYLINE SEQEND")
            points = polyline["points"]
            if polyline["closed"]:
                loops.append(_polygon(points))
            else:
                segments.extend(zip(points, points[1:]))
            polyline = None
        else:
            if kind == "HATCH" and record.get("_hatchMappingFailure"):
                raise ValueError(f"Unresolved DXF HATCH boundary: {record['_hatchMappingFailure']}")
            raise ValueError(f"Unsupported DXF material entity {kind}: no conservative material mapping yet")
    if polyline is not None:
        raise ValueError("Unterminated POLYLINE")
    try:
        loops.extend(_line_rings(segments))
    except ValueError as original_error:
        # Failed closure is not repaired. A second topology path may establish
        # a complete closed material boundary plus separately retained internal
        # zero-area strokes. Open/outside strokes still fail explicitly.
        try:
            return _import_curved_document(document, scale, tolerance)
        except ValueError as detailed_error:
            raise ValueError(f"{original_error}; detailed topology: {detailed_error}") from detailed_error
    if not loops:
        raise ValueError("DXF contains no closed material contours")
    containers = [[] for _ in loops]
    for i, first in enumerate(loops):
        for j in range(i + 1, len(loops)):
            second = loops[j]
            if _boundaries_intersect(first, second):
                raise ValueError("Closed DXF contours intersect/touch: material/hole ownership is ambiguous")
            if _inside(first, _representative(second)) > 0:
                containers[j].append(i)
            if _inside(second, _representative(first)) > 0:
                containers[i].append(j)
    depths = list(map(len, containers))
    geometries, curves, exact_outers = [], [], []
    for i, outer in enumerate(loops):
        if depths[i] % 2:
            continue
        children = [j for j in range(len(loops)) if depths[j] == depths[i] + 1 and i in containers[j]]
        if outer["kind"] == "circle" and not children:
            geometries.append({"radius": outer["radius"], "center": list(outer["center"]), "outer": [], "holes": [], "commonEdgeRings": []})
            curves.append({"type": "CIRCLE", "role": "outer", "center": list(outer["center"]), "radius": outer["radius"], "representation": "native-circle", "maxDeviationMm": 0})
            exact_outers.append(True)
            continue
        exact_outer = outer["kind"] == "polygon"
        points = outer["points"] if exact_outer else _circle_ring(outer, tolerance, hole=False)
        common_edges, holes = [exact_outer], []
        if not exact_outer:
            curves.append({"type": "CIRCLE", "role": "outer", "center": list(outer["center"]), "radius": outer["radius"], "representation": "circumscribed-material", "maxDeviationMm": tolerance})
        for child in children:
            hole = loops[child]
            exact_hole = hole["kind"] == "polygon"
            holes.append(hole["points"] if exact_hole else _circle_ring(hole, tolerance, hole=True))
            common_edges.append(exact_hole)
            if not exact_hole:
                curves.append({"type": "CIRCLE", "role": "hole", "center": list(hole["center"]), "radius": hole["radius"], "representation": "inscribed-hole", "maxDeviationMm": tolerance})
        geometries.append({"radius": 0, "center": [0, 0], "outer": list(map(list, points)), "holes": [list(map(list, hole)) for hole in holes], "commonEdgeRings": common_edges})
        exact_outers.append(exact_outer)
    approximated = any(curve["maxDeviationMm"] > 0 for curve in curves)
    metadata = {
        "reader": "SDK/Cad/dxfDocument.mjs",
        "sourceFormat": document["sourceFormat"],
        "sourceDxfSha256": document.get("sourceDxfSha256"),
        "sourceUnmodified": True,
        "scaleToMm": scale,
        "curveToleranceMm": tolerance,
        "exact": not approximated,
        "exactOuter": all(exact_outers),
        "curveApproximation": approximated,
        "maxDeviationMm": tolerance if approximated else 0,
        "materialSuperset": True,
        "requiresCommonEdgeExclusion": approximated,
        "commonEdgeEligibility": "per-ring; approximated circle chords are excluded",
        "materialComponents": len(geometries),
        "closedContours": len(loops),
        "ignoredAnnotationEntities": dict(ignored),
        "circles": curves,
        "blockReferences": document.get("blockReferences", []),
        "nonMaterialEntities": document.get("nonMaterialEntities", []),
        "planarMappings": document.get("planarMappings", []),
        "maximumPlanarResidualMm": document.get("maximumPlanarResidualMm", 0.),
        "hatchBoundaryMappings": document.get("hatchBoundaryMappings", []),
        "hatchBoundaryMaximumDeviationMm": document.get("hatchMappingErrorMm", 0.),
        "dimensionCopyMappings": document.get("dimensionCopyMappings", []),
        "sourceRoleProvenance": document.get("sourceRoleProvenance"),
        "sourceRoleValidation": document.get("sourceRoleValidation"),
        "materialSemantics": document.get("materialSemantics"),
    }
    return {"geometry": geometries[0] if len(geometries) == 1 else None,
            "geometries": geometries, "metadata": metadata}


def _geos():
    """Load the repository's existing pinned offline geometry dependency."""
    dependency = Path(__file__).resolve().parents[3] / ".deps/plane-dxf-python"
    if dependency.is_dir() and str(dependency) not in sys.path:
        sys.path.insert(0, str(dependency))
    try:
        import shapely
        from shapely.geometry import Polygon
        from shapely.validation import explain_validity
    except ImportError as error:
        raise ValueError("Curved DXF import requires the existing Shapely/NumPy offline runtime") from error
    return shapely, Polygon, explain_validity


def _sample_arc(center, radius, start, sweep, error, endpoints=None):
    if not (radius > 0 and math.isfinite(radius)) or not math.isfinite(sweep) or not sweep:
        raise ValueError("Invalid zero-sweep or nonpositive-radius circular arc")
    angle = 2 * math.acos(max(-1., 1 - min(1., error / radius)))
    if not angle:
        raise ValueError("Arc sampling error is below coordinate resolution")
    count = max(1, math.ceil(abs(sweep) / min(angle, math.pi / 2)))
    if count > _MAX_CIRCLE_VERTICES:
        raise ValueError("Arc import tolerance exceeds the curve vertex resource limit")
    points = [(center[0] + radius * math.cos(start + sweep * i / count),
               center[1] + radius * math.sin(start + sweep * i / count))
              for i in range(count + 1)]
    if endpoints is not None:
        points[0], points[-1] = endpoints
    if any(not math.isfinite(value) for point in points for value in point):
        raise ValueError("Nonfinite transformed ARC coordinates")
    return points


def _ellipse_edge(record, scale, error, source_index, parameters):
    values = _values(record)
    _flat(record, values, 30, 31)
    center, major = _point(values, 10, scale), _point(values, 11, scale)
    ratio = _number(_one(values, 40), "ELLIPSE minor/major ratio")
    if not 0 < ratio <= 1 or not math.hypot(*major):
        raise ValueError("Invalid ELLIPSE major axis or minor/major ratio")
    direction = record.get("_ellipseMinorXY")
    minor = ((direction[0] * scale * ratio, direction[1] * scale * ratio) if direction is not None
             else (-major[1] * ratio, major[0] * ratio))
    start = _number(_one(values, 41, 0), "ELLIPSE start parameter")
    end = _number(_one(values, 42, math.tau), "ELLIPSE end parameter")
    raw_sweep = end - start
    full = abs(abs(raw_sweep) - math.tau) <= 32 * sys.float_info.epsilon * max(1., abs(start), abs(end))
    sweep = math.tau if full else raw_sweep % math.tau
    if not sweep or abs(raw_sweep) > math.tau + 1e-12:
        raise ValueError("Invalid zero-sweep or multiply-wound ELLIPSE")
    # For c+u*cos(t)+v*sin(t), ||r''|| <= ||u||+||v||.
    # Linear interpolation on a parameter interval has vector error <= M*h²/8.
    second_derivative = math.hypot(*major) + math.hypot(*minor)
    step = min(math.pi / 2, math.sqrt(8 * error / second_derivative))
    count = max(1, math.ceil(sweep / step))
    if count > _MAX_CIRCLE_VERTICES:
        raise ValueError("ELLIPSE import tolerance exceeds the bounded curve resource limit")
    points = [(center[0] + major[0] * math.cos(start + sweep * i / count) + minor[0] * math.sin(start + sweep * i / count),
               center[1] + major[1] * math.cos(start + sweep * i / count) + minor[1] * math.sin(start + sweep * i / count))
              for i in range(count + 1)]
    if full:
        points[-1] = points[0]
    parameters.append({"type": "ELLIPSE", "sourceEntity": source_index, "center": list(center),
                       "majorAxis": list(major), "minorMajorRatio": ratio,
                       "startParameter": start, "endParameter": end, "sweepRadians": sweep,
                       "secondDerivativeNormBound": second_derivative,
                       "samplingDistanceBoundMm": error, "sampledSegments": count})
    return {"points": points, "curved": True, "sourceEntity": source_index, "sourceType": "ELLIPSE"}


def _bulge_edge(a, b, bulge, error, source_index, arc_parameters):
    if not bulge:
        return {"points": [a, b], "sourceEntity": source_index, "sourceType": "POLYLINE_EDGE"}
    dx, dy = b[0] - a[0], b[1] - a[1]
    chord = math.hypot(dx, dy)
    if not chord:
        raise ValueError("Nonzero bulge on a zero-length polyline edge")
    offset = (1 - bulge * bulge) / (4 * bulge)
    center = ((a[0] + b[0]) / 2 - dy * offset, (a[1] + b[1]) / 2 + dx * offset)
    radius = chord * (1 + bulge * bulge) / (4 * abs(bulge))
    start, sweep = math.atan2(a[1] - center[1], a[0] - center[0]), 4 * math.atan(bulge)
    points = _sample_arc(center, radius, start, sweep, error, (a, b))
    arc_parameters.append({"type": "POLYLINE_BULGE", "sourceEntity": source_index,
                           "start": list(a), "end": list(b), "bulge": bulge,
                           "center": list(center), "radius": radius,
                           "startRadians": start, "sweepRadians": sweep,
                           "samplingSagittaBoundMm": error})
    return {"points": points, "curved": True, "sourceEntity": source_index, "sourceType": "POLYLINE_BULGE"}


def _join_curved_segments(segments, connection_tolerance, ring_provenance=None, allow_no_closed=False):
    """Join only unambiguous degree-two endpoints; record every displacement."""
    centers, center_sources, buckets, adjacent, endpoints, connections = [], [], defaultdict(list), defaultdict(list), [], []
    maximum_error = 0.
    for segment_index, segment in enumerate(segments):
        ids = []
        for endpoint_index, point in enumerate((segment["points"][0], segment["points"][-1])):
            cell = (math.floor(point[0] / connection_tolerance), math.floor(point[1] / connection_tolerance))
            candidates = []
            for x in range(cell[0] - 1, cell[0] + 2):
                for y in range(cell[1] - 1, cell[1] + 2):
                    candidates.extend(index for index in buckets.get((x, y), [])
                                      if math.dist(centers[index], point) <= connection_tolerance)
            if len(candidates) > 1:
                raise ValueError("Ambiguous curved-chain endpoints within the explicit connection tolerance")
            if candidates:
                index = candidates[0]
                distance = math.dist(centers[index], point)
                maximum_error = max(maximum_error, distance)
                if distance:
                    reference_segment, reference_endpoint = center_sources[index]
                    connections.append({"sourceSegment": segment_index, "sourceEntity": segment.get("sourceEntity"),
                                        "sourceType": segment.get("sourceType"),
                                        "sourceEndpoint": "start" if endpoint_index == 0 else "end",
                                        "sourcePointMm": list(point), "mappedPointMm": list(centers[index]),
                                        "displacementMm": distance,
                                        "referenceSegment": reference_segment,
                                        "referenceEntity": segments[reference_segment].get("sourceEntity"),
                                        "referenceEndpoint": "start" if reference_endpoint == 0 else "end"})
            else:
                index = len(centers)
                centers.append(point)
                center_sources.append((segment_index, endpoint_index))
                buckets[cell].append(index)
            ids.append(index)
            adjacent[index].append(segment_index)
        endpoints.append(ids)
    # Peel open graph branches only as candidates for a later material-domain
    # proof. They are retained in metadata, never assumed to be a boundary or
    # silently discarded to force closure.
    active = set(range(len(segments)))
    degree = {index: len(edges) for index, edges in adjacent.items()}
    leaves = [index for index, value in degree.items() if value < 2]
    while leaves:
        node = leaves.pop()
        for edge in adjacent[node]:
            if edge not in active:
                continue
            active.remove(edge)
            for end in endpoints[edge]:
                degree[end] -= 1
                if degree[end] == 1:
                    leaves.append(end)
    non_boundary = [segments[index] for index in range(len(segments)) if index not in active]
    invalid = [(index, value) for index, value in degree.items() if value not in (0, 2)]
    if invalid:
        sample = [{"point": list(centers[index]), "degree": degree} for index, degree in invalid[:3]]
        raise ValueError(f"Open or branching curved LINE/ARC chain: endpoint degree is not two within {connection_tolerance:g} mm; "
                         f"invalid endpoints={len(invalid)}; sample={sample}")
    if not active:
        if allow_no_closed:
            return [], maximum_error, [], non_boundary, connections
        raise ValueError("Open or branching curved LINE/ARC chain: no closed material boundary is established")
    remaining, rings, exact_rings = set(active), [], []
    while remaining:
        edge = min(remaining)
        ring_edges = [edge]
        start, current = endpoints[edge]
        points = list(segments[edge]["points"])
        exact_ring = (not segments[edge].get("curved") and
                      points[0] == centers[start] and points[-1] == centers[current])
        points[0], points[-1] = centers[start], centers[current]
        remaining.remove(edge)
        while current != start:
            choices = [index for index in adjacent[current] if index in remaining]
            if len(choices) != 1:
                raise ValueError("Invalid or ambiguous curved-chain closure")
            edge = choices[0]
            ring_edges.append(edge)
            a, b = endpoints[edge]
            addition = list(segments[edge]["points"])
            if a != current:
                addition.reverse()
                a, b = b, a
            exact_ring = (exact_ring and not segments[edge].get("curved") and
                          addition[0] == centers[a] and addition[-1] == centers[b])
            addition[0], addition[-1] = centers[a], centers[b]
            points.extend(addition[1:])
            current = b
            remaining.remove(edge)
        if len(points) > 1 and points[-1] == points[0]:
            points.pop()
        rings.append(points)
        exact_rings.append(exact_ring)
        if ring_provenance is not None:
            ring_provenance.append({"sourceEntities": sorted({segments[index]["sourceEntity"] for index in ring_edges
                                                               if segments[index].get("sourceEntity") is not None}),
                                    "segments": ring_edges})
    return rings, maximum_error, exact_rings, non_boundary, connections


def _spline_edge(record, scale, error, source_index, parameters):
    values = _values(record)
    _flat(record, values, 30, 31)
    degree = int(_one(values, 71, -1))
    flags = int(_one(values, 70, 0))
    if not 1 <= degree <= 15:
        raise ValueError("Unsupported SPLINE degree: expected 1..15")
    xs, ys = values.get(10, []), values.get(20, [])
    if len(xs) != len(ys) or len(xs) != int(_one(values, 73, len(xs))) or len(xs) <= degree:
        raise ValueError("Invalid SPLINE control-point count")
    if len(xs) > _MAX_CIRCLE_VERTICES:
        raise ValueError("SPLINE control-point count exceeds the bounded import resource limit")
    controls = [(_number(x, "SPLINE control X") * scale, _number(y, "SPLINE control Y") * scale)
                for x, y in zip(xs, ys)]
    knots = [_number(value, "SPLINE knot") for value in values.get(40, [])]
    if len(knots) != len(controls) + degree + 1 or len(knots) != int(_one(values, 72, len(knots))) or any(a > b for a, b in zip(knots, knots[1:])):
        raise ValueError("Invalid SPLINE knot count/order")
    weights = [_number(value, "SPLINE weight") for value in values.get(41, [])] or [1.] * len(controls)
    if len(weights) != len(controls) or any(value <= 0 for value in weights):
        raise ValueError("SPLINE conservative convex-hull import requires one positive weight per control point")
    if knots[:degree + 1] != [knots[degree]] * (degree + 1) or knots[-degree - 1:] != [knots[-degree - 1]] * (degree + 1) or knots[degree] >= knots[-degree - 1]:
        raise ValueError("Unsupported unclamped/periodic SPLINE: source end-domain closure requires a dedicated mapping")
    if any(value > degree + 1 for value in Counter(knots).values()):
        raise ValueError("Invalid SPLINE knot multiplicity")
    source_knots = list(knots)
    homogeneous = [(point[0] * weight, point[1] * weight, weight) for point, weight in zip(controls, weights)]

    def mix(a, b, alpha):
        return tuple((1 - alpha) * x + alpha * y for x, y in zip(a, b))

    for knot in sorted(set(knots[degree + 1:-degree - 1])):
        while knots.count(knot) < degree:
            n = len(homogeneous) - 1
            span = max(index for index in range(degree, n + 1) if knots[index] <= knot < knots[index + 1])
            multiplicity = knots.count(knot)
            inserted = [None] * (n + 2)
            for index in range(span - degree + 1):
                inserted[index] = homogeneous[index]
            for index in range(span - multiplicity, n + 1):
                inserted[index + 1] = homogeneous[index]
            for index in range(span - degree + 1, span - multiplicity + 1):
                denominator = knots[index + degree] - knots[index]
                if denominator <= 0:
                    raise ValueError("Degenerate SPLINE knot insertion span")
                inserted[index] = mix(homogeneous[index - 1], homogeneous[index], (knot - knots[index]) / denominator)
            homogeneous = inserted
            knots = knots[:span + 1] + [knot] + knots[span + 1:]

    def project(point):
        result = point[0] / point[2], point[1] / point[2]
        if not all(math.isfinite(value) for value in result):
            raise ValueError("SPLINE evaluation exceeds represented coordinate resolution")
        return result

    points, maximum_leaf_bound = [], 0.
    for span in range(degree, len(homogeneous)):
        if knots[span] >= knots[span + 1]:
            continue
        piece = homogeneous[span - degree:span + 1]
        stack = [(piece, 0)]
        while stack:
            piece, depth = stack.pop()
            projected = list(map(project, piece))
            a, b = projected[0], projected[-1]
            bound = max(math.sqrt(float(_distance_sq(point, a, b))) for point in projected)
            if bound <= error:
                maximum_leaf_bound = max(maximum_leaf_bound, bound)
                if not points:
                    points.append(a)
                elif points[-1] != a:
                    raise ValueError("Discontinuous SPLINE internal knot: cannot invent a connecting edge")
                points.append(b)
            else:
                if depth >= 32 or len(points) + len(stack) > _MAX_CIRCLE_VERTICES:
                    raise ValueError("SPLINE subdivision exceeds the bounded import resource limit")
                levels, left, right = list(piece), [piece[0]], [piece[-1]]
                while len(levels) > 1:
                    levels = [mix(x, y, .5) for x, y in zip(levels, levels[1:])]
                    left.append(levels[0])
                    right.append(levels[-1])
                stack.append((list(reversed(right)), depth + 1))
                stack.append((left, depth + 1))
    if len(points) < 2:
        raise ValueError("SPLINE has no evaluable nonzero knot span")
    if degree == 1:
        # Positive rational degree-one spans trace the original control edges
        # exactly. Retain those source coordinates instead of divide/multiply
        # roundoff from homogeneous evaluation.
        points = list(controls)
    if flags & 1 and math.dist(points[0], points[-1]) > _CONNECTION_TOLERANCE_MM:
        raise ValueError("SPLINE closed flag disagrees with its evaluated clamped end points")
    fit_x, fit_y = values.get(11, []), values.get(21, [])
    if len(fit_x) != len(fit_y) or len(fit_x) != int(_one(values, 74, len(fit_x))):
        raise ValueError("Invalid SPLINE fit-point count")
    parameters.append({"type": "SPLINE", "sourceEntity": source_index, "degree": degree, "flags": flags,
                       "controlPoints": list(map(list, controls)), "knots": source_knots, "weights": weights,
                       "fitPoints": [[_number(x, "SPLINE fit X") * scale, _number(y, "SPLINE fit Y") * scale]
                                     for x, y in zip(fit_x, fit_y)],
                       "representation": "exact-degree-one-edges" if degree == 1 else "positive-rational-Bezier-convex-hull-subdivision",
                       "maximumLeafHullDistanceMm": maximum_leaf_bound,
                       "samplingDistanceBoundMm": 0 if degree == 1 else error,
                       "sampledSegments": len(points) - 1})
    return {"points": points, "curved": degree != 1, "sourceEntity": source_index, "sourceType": "SPLINE"}


def _import_curved_document(document, scale, tolerance, _endpoint_graph_only=False):
    shapely, Polygon, explain_validity = _geos()
    sampling_error = tolerance / 8
    segments, arc_parameters, circle_parameters, spline_parameters, ellipse_parameters, ignored = [], [], [], [], [], Counter()
    polyline = None
    operation_scale = 1.

    def transform_context(record):
        transform = record.get("_affine")
        if record.get("_ocsAffine") is not None:
            transform = _compose_affine(transform or (1., 0., 0., 1., 0., 0.), record["_ocsAffine"])
        if transform is None:
            return None, sampling_error
        a, b, c, d, x, y = transform
        norm_bound = math.nextafter(math.hypot(a, b, c, d), math.inf)
        if not norm_bound or not math.isfinite(norm_bound):
            raise ValueError("INSERT transform has invalid/nonfinite scale")
        return transform, sampling_error / norm_bound

    def append_segment(segment, transform):
        nonlocal operation_scale
        if transform is not None:
            a, b, c, d, x, y = transform
            operation_scale = max(operation_scale, *(abs(a * p[0]) + abs(b * p[1]) + abs(x * scale)
                                                     for p in segment["points"]),
                                  *(abs(c * p[0]) + abs(d * p[1]) + abs(y * scale)
                                    for p in segment["points"]))
            segment["points"] = [(a * p[0] + b * p[1] + x * scale,
                                   c * p[0] + d * p[1] + y * scale) for p in segment["points"]]
            if any(not math.isfinite(value) for point in segment["points"] for value in point):
                raise ValueError("Expanded INSERT has nonfinite geometry coordinates")
        if segment.get("sourceEntity") in document.get("hatchAffectedEntities", set()):
            segment["curved"] = True  # Requires the conservative contour offset; no exact common edge.
        segments.append(segment)

    def curve_transform(parameters, start, transform):
        if transform is not None:
            for parameter in parameters[start:]:
                parameter["blockTransformToWorldMm"] = [*transform[:4], transform[4] * scale, transform[5] * scale]
                parameter["transformToWorldMm"] = parameter["blockTransformToWorldMm"]
                parameter["coordinateSpace"] = "source-local-mm; apply transformToWorldMm (OCS then INSERT)"

    def append_polyline(vertices, closed, transform, local_error):
        if len(vertices) < 2:
            raise ValueError("Curved polyline has fewer than two vertices")
        edges = zip(vertices, vertices[1:] + vertices[:1]) if closed else zip(vertices, vertices[1:])
        for (a, bulge, source_index), (b, _, _) in edges:
            start = len(arc_parameters)
            append_segment(_bulge_edge(a, b, bulge, local_error, source_index, arc_parameters), transform)
            curve_transform(arc_parameters, start, transform)

    for source_index, record in enumerate(document["entities"]):
        kind, values = record["type"], _values(record)
        annotation = _annotation(record, values)
        if annotation:
            ignored[annotation] += 1
            continue
        if _endpoint_graph_only and kind == "SOLID":
            # This internal mode never returns material: it only inspects the
            # original vector graph before deciding whether a matched LINE is
            # shared with a cutting boundary. SOLID handling remains explicit
            # in the ordinary importer.
            continue
        transform, local_error = transform_context(record)
        if polyline is not None and kind not in {"VERTEX", "SEQEND"}:
            raise ValueError("Unterminated POLYLINE before another material entity")
        if kind == "LINE":
            _flat(record, values, 30, 31)
            a, b = _point(values, 10, scale), _point(values, 11, scale)
            if a == b:
                raise ValueError("Zero-length LINE in a curved contour")
            append_segment({"points": [a, b], "sourceEntity": source_index, "sourceType": "LINE",
                             "sourceLayer": _one(values, 8, "0"), "sourceHandle": _one(values, 5),
                             "sourceLineType": _one(values, 6)}, transform)
        elif kind == "SPLINE":
            start = len(spline_parameters)
            append_segment(_spline_edge(record, scale, local_error, source_index, spline_parameters), transform)
            curve_transform(spline_parameters, start, transform)
        elif kind == "ELLIPSE":
            start = len(ellipse_parameters)
            append_segment(_ellipse_edge(record, scale, local_error, source_index, ellipse_parameters), transform)
            curve_transform(ellipse_parameters, start, transform)
        elif kind in {"ARC", "CIRCLE"}:
            _flat(record, values, 30)
            center = _point(values, 10, scale)
            radius = _number(_one(values, 40), f"{kind} radius") * scale
            start_degrees = _number(_one(values, 50, 0), "ARC start angle")
            end_degrees = _number(_one(values, 51, 360), "ARC end angle")
            raw_sweep = end_degrees - start_degrees
            if not math.isfinite(raw_sweep):
                raise ValueError("ARC angle difference is not finite")
            full_turn = kind == "ARC" and raw_sweep != 0 and math.remainder(raw_sweep, 360.) == 0
            sweep_degrees = 360 if kind == "CIRCLE" or full_turn else raw_sweep % 360
            start, sweep = math.radians(start_degrees), math.radians(sweep_degrees)
            points = _sample_arc(center, radius, start, sweep, local_error)
            if kind == "CIRCLE":
                points[-1] = points[0]
                circle_parameters.append({"type": "CIRCLE", "sourceEntity": source_index,
                                          "center": list(center), "radius": radius})
                curve_transform(circle_parameters, len(circle_parameters) - 1, transform)
            else:
                if full_turn:
                    points[-1] = points[0]
                arc_parameters.append({"type": "ARC", "sourceEntity": source_index,
                                       "center": list(center), "radius": radius,
                                       "startDegrees": start_degrees, "endDegrees": end_degrees,
                                       "sweepRadians": sweep, "samplingSagittaBoundMm": local_error,
                                       "explicitFullTurn": full_turn, "sourceAngleDifferenceDegrees": raw_sweep})
                curve_transform(arc_parameters, len(arc_parameters) - 1, transform)
            append_segment({"points": points, "curved": True, "sourceEntity": source_index, "sourceType": kind}, transform)
        elif kind == "LWPOLYLINE":
            _flat(record, values, 38)
            if any(_number(value, str(code)) != 0 for code in (40, 41, 43) for value in values.get(code, [])):
                raise ValueError("Unsupported LWPOLYLINE width in curved material")
            vertices, point = [], None
            for code, value in record["values"]:
                if code == 10:
                    point = {"x": _number(value, "vertex X") * scale, "bulge": 0.}
                    vertices.append(point)
                elif code in (20, 42):
                    if point is None:
                        raise ValueError("Malformed curved LWPOLYLINE vertex")
                    point["y" if code == 20 else "bulge"] = _number(value, str(code)) * (scale if code == 20 else 1)
            flags = int(_one(values, 70, 0))
            if flags & ~129 or any("y" not in point for point in vertices) or int(_one(values, 90, len(vertices))) != len(vertices):
                raise ValueError("Invalid curved planar LWPOLYLINE flags/count")
            append_polyline([((point["x"], point["y"]), point["bulge"], source_index) for point in vertices], bool(flags & 1), transform, local_error)
        elif kind == "POLYLINE":
            _flat(record, values, 30)
            flags = int(_one(values, 70, 0))
            if flags & ~129 or any(_number(value, str(code)) != 0 for code in (40, 41) for value in values.get(code, [])):
                raise ValueError("Unsupported spatial/width POLYLINE in curved material")
            polyline = {"closed": bool(flags & 1), "vertices": [], "transform": transform, "local_error": local_error}
        elif kind == "VERTEX":
            _flat(record, values, 30)
            if polyline is None or int(_one(values, 70, 0)) != 0:
                raise ValueError("Unsupported curved VERTEX without planar POLYLINE")
            if any(_number(value, str(code)) != 0 for code in (40, 41) for value in values.get(code, [])):
                raise ValueError("Unsupported POLYLINE vertex width")
            polyline["vertices"].append((_point(values, 10, scale), _number(_one(values, 42, 0), "bulge"), source_index))
        elif kind == "SEQEND":
            if polyline is None:
                raise ValueError("Unmatched POLYLINE SEQEND")
            append_polyline(polyline["vertices"], polyline["closed"], polyline["transform"], polyline["local_error"])
            polyline = None
        else:
            if kind == "HATCH" and record.get("_hatchMappingFailure"):
                raise ValueError(f"Unresolved DXF HATCH boundary: {record['_hatchMappingFailure']}")
            raise ValueError(f"Unsupported DXF material entity {kind}: no conservative material mapping yet")
    if polyline is not None:
        raise ValueError("Unterminated curved POLYLINE")
    if _endpoint_graph_only:
        return segments
    connection_tolerance = document.get("connectionToleranceMm", _CONNECTION_TOLERANCE_MM)
    ring_sources = []
    rings, connection_error, exact_rings, non_boundary, endpoint_connections = _join_curved_segments(segments, connection_tolerance, ring_sources)
    if not rings:
        raise ValueError("Curved DXF contains no closed material contours")
    coordinate_scale = max(operation_scale, *(abs(value) for ring in rings for point in ring for value in point))
    numerical_guard = 256 * sys.float_info.epsilon * coordinate_scale
    uncertainty = sampling_error + connection_error + numerical_guard + document.get("hatchMappingErrorMm", 0.)
    if numerical_guard > tolerance / 16:
        raise ValueError("Curved DXF coordinate resolution is too coarse for the requested import tolerance")
    polygons = []
    for index, points in enumerate(rings):
        polygon = Polygon(points)
        if polygon.is_empty or polygon.area <= 0 or not polygon.is_valid:
            raise ValueError(f"Invalid sampled curved ring {index}: {explain_validity(polygon)}")
        if polygon.minimum_clearance <= 2 * uncertainty:
            raise ValueError(f"Curved ring {index} has clearance below its curve/topology uncertainty; use a tighter curve tolerance")
        polygons.append(polygon)
    containers = [[] for _ in polygons]
    for i, first in enumerate(polygons):
        for j in range(i + 1, len(polygons)):
            second = polygons[j]
            if first.boundary.intersects(second.boundary):
                raise ValueError(f"Sampled curved contours {i}/{j} intersect/touch; source material topology is unconfirmed")
            if first.boundary.distance(second.boundary) <= 2 * uncertainty:
                raise ValueError(f"Curved contours {i}/{j} are closer than their sampling/connection uncertainty")
            if first.contains(second):
                containers[j].append(i)
            if second.contains(first):
                containers[i].append(j)
    depths = list(map(len, containers))
    quad_segs = 16
    desired_offset = 2 * uncertainty
    # GEOS round buffers inscribe their circular joins. Inflating the radius
    # by sec(pi/(4*q)) makes each join chord reach the desired safety offset.
    offset = desired_offset / math.cos(math.pi / (4 * quad_segs)) + numerical_guard
    deviation_bound = uncertainty + offset + numerical_guard
    if deviation_bound > tolerance:
        raise ValueError("Curve/connection/round-buffer error budget exceeds the requested import tolerance")
    geometries, exact_outers, source_materials = [], [], []

    def conservative_ring(index, hole):
        if exact_rings[index]:
            return polygons[index]
        source = polygons[index]
        candidate = source.buffer(-offset if hole else offset, quad_segs=quad_segs, join_style="round")
        if candidate.is_empty or candidate.geom_type != "Polygon" or not candidate.is_valid or candidate.interiors:
            raise ValueError("Conservative curve offset would remove/split a contour or change its topology; use a tighter curve tolerance")
        contains = source.covers(candidate) if hole else candidate.covers(source)
        if not contains:
            raise ValueError("Conservative curve offset failed the sampled-material containment check")
        return candidate

    for i, outer in enumerate(polygons):
        if depths[i] % 2:
            continue
        children = [j for j in range(len(polygons)) if depths[j] == depths[i] + 1 and i in containers[j]]
        material = Polygon(rings[i], [rings[j] for j in children])
        if not material.is_valid:
            raise ValueError(f"Invalid curved material with assigned holes: {explain_validity(material)}")
        source_materials.append(material)
        expanded_outer = conservative_ring(i, False)
        shrunk_holes = [conservative_ring(j, True) for j in children]
        expanded = Polygon(expanded_outer.exterior.coords, [hole.exterior.coords for hole in shrunk_holes])
        if expanded.is_empty or not expanded.is_valid or len(expanded.interiors) != len(children):
            raise ValueError("Conservative curve offset changed/invalidated source material/hole topology")
        if not expanded.covers(material):
            raise ValueError("Conservative curve offset failed the sampled-material containment check")
        geometries.append({"radius": 0, "center": [0, 0],
                           "outer": [list(point) for point in list(expanded.exterior.coords)[:-1]],
                           "holes": [[list(point) for point in list(ring.coords)[:-1]] for ring in expanded.interiors],
                           "commonEdgeRings": [exact_rings[i]] + [exact_rings[j] for j in children]})
        exact_outers.append(exact_rings[i])
    retained_strokes = []
    if non_boundary:
        material_domain = shapely.union_all(source_materials)
        for segment in non_boundary:
            if segment.get("curved"):
                raise ValueError("Open or branching curved LINE/ARC chain: an open curved stroke has no proved material-boundary role")
            stroke = shapely.LineString(segment["points"])
            if not material_domain.covers(stroke):
                raise ValueError("Open non-boundary stroke extends outside the proved closed material or crosses a source hole")
            retained_strokes.append({"sourceEntity": segment.get("sourceEntity"),
                                     "sourceType": segment.get("sourceType", "POLYLINE_EDGE"),
                                     "sourceLayer": segment.get("sourceLayer"),
                                     "sourceHandle": segment.get("sourceHandle"),
                                     "sourceLineType": segment.get("sourceLineType"),
                                     "points": list(map(list, segment["points"])),
                                     "classification": "internal-zero-area-stroke; process type not inferred",
                                     "topologyEvidence": "entire source stroke covered by closed material; no passage through holes"})
    exact_geometry = all(exact_rings)
    metadata = {
        "reader": "SDK/Cad/dxfDocument.mjs", "sourceFormat": document["sourceFormat"],
        "sourceDxfSha256": document.get("sourceDxfSha256"),
        "sourceUnmodified": True, "scaleToMm": scale, "curveToleranceMm": tolerance,
        "exact": exact_geometry, "exactOuter": all(exact_outers), "curveApproximation": not exact_geometry,
        "maxDeviationMm": 0 if exact_geometry else tolerance, "computedDeviationBudgetMm": 0 if exact_geometry else deviation_bound,
        "samplingSagittaBoundMm": sampling_error,
        "connectionToleranceMm": connection_tolerance, "maxConnectionErrorMm": connection_error,
        "explicitConnectionTolerance": document.get("explicitConnectionTolerance", False),
        "endpointConnections": endpoint_connections,
        "closureModel": "source endpoint displacements are explicit; their maximum enters the conservative sampling/connection/offset budget",
        "roundBufferDesiredOffsetMm": desired_offset, "roundBufferRadiusMm": offset,
        "roundBufferQuadSegments": quad_segs, "numericalGuardMm": numerical_guard,
        "materialSuperset": True, "materialSupersetExactCertificate": False,
        "approximationMethod": ("unchanged exact straight rings; internal zero-area strokes retained separately"
                                if exact_geometry else
                                "bounded circular/elliptic/NURBS chords plus outward compensated round offset"),
        "boundScope": "analytic sagitta/offset budget with floating-point GEOS topology; not an exact arithmetic proof",
        "geometryBackend": f"Shapely {shapely.__version__} / GEOS {shapely.geos_version_string}",
        "requiresCommonEdgeExclusion": not exact_geometry,
        "commonEdgeEligibility": "changed rings excluded; unchanged exact straight rings retain eligibility",
        "materialComponents": len(geometries), "closedContours": len(rings),
        "ignoredAnnotationEntities": dict(ignored), "circles": circle_parameters, "arcs": arc_parameters,
        "splines": spline_parameters, "nonBoundaryEntities": retained_strokes,
        "ellipses": ellipse_parameters, "blockReferences": document.get("blockReferences", []),
        "nonMaterialEntities": document.get("nonMaterialEntities", []),
        "planarMappings": document.get("planarMappings", []),
        "maximumPlanarResidualMm": document.get("maximumPlanarResidualMm", 0.),
        "hatchBoundaryMappings": document.get("hatchBoundaryMappings", []),
        "hatchBoundaryMaximumDeviationMm": document.get("hatchMappingErrorMm", 0.),
        "dimensionCopyMappings": document.get("dimensionCopyMappings", []),
        "sourceRoleProvenance": document.get("sourceRoleProvenance"),
        "sourceRoleValidation": document.get("sourceRoleValidation"),
        "materialSemantics": document.get("materialSemantics"),
        "sourceContourOwnership": [{"ringIndex": i, "role": "outer-profile" if depths[i] % 2 == 0 else "through-hole",
                                     "depth": depths[i], "sourceEntities": ring_sources[i]["sourceEntities"]}
                                    for i in range(len(rings))],
    }
    return {"geometry": geometries[0] if len(geometries) == 1 else None,
            "geometries": geometries, "metadata": metadata}
