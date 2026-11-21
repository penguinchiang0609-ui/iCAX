"""Independently verify and report a native convex quarter-turn nesting task.

Only the native result, its input snapshot and original DXF files are inputs.
No prior placements, benchmark identities or stock dimensions are embedded.
Lattice output receives a strict integer/rational certificate. Continuous
postprocessing output is evaluated at its stored binary64 positions with exact
rational arithmetic and the declared geometric tolerance. GEOS reads original
DXF contours as a second check. The tool never changes output positions.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
from fractions import Fraction
import hashlib
import html
from itertools import combinations
import json
import math
import os
from pathlib import Path
import re
import sys
import time

from dxf_task import straight_contour


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write(path, data):
    def exact_number(value):
        if isinstance(value, Fraction):
            return str(value)
        raise TypeError(f"Unsupported audit value type: {type(value).__name__}")
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False,
                                    default=exact_number), encoding="utf-8")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def exact_dxf_contour(path):
    """Retain original decimal coordinates, after the strict entity validator."""
    imported = straight_contour(path)
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    tags = [(int(lines[i].strip()), lines[i + 1].strip()) for i in range(0, len(lines), 2)]
    records = []
    for code, value in tags:
        if code == 0:
            records.append((value, []))
        elif records:
            records[-1][1].append((code, value))
    active = False
    points = []
    for kind, data in records:
        fields = dict(data)
        if kind == "SECTION":
            active = fields.get(2) == "ENTITIES"
        elif kind == "ENDSEC":
            active = False
        elif active and kind == "VERTEX":
            points.append((Fraction(fields[10]), Fraction(fields[20])))
        elif active and kind == "LWPOLYLINE":
            for code, value in data:
                if code == 10:
                    points.append([Fraction(value)])
                elif code == 20:
                    points[-1].append(Fraction(value))
    if len(points) > 1 and points[-1] == points[0]:
        points.pop()
    require([[float(v) for v in p] for p in points] == imported,
            f"Independent DXF decimal parser disagrees with strict importer: {path}")
    return [tuple(p) for p in points]


def lattice(value, scale, label):
    """Recover only a value which round-trips from the solver's n/scale double."""
    require(type(value) in (int, float) and math.isfinite(value), f"Non-finite numeric {label}")
    integer = round(value * scale)
    require(float(Fraction(integer, scale)) == value, f"Value is not lossless on declared lattice: {label}")
    return integer


def on_lattice(value, scale):
    return (type(value) in (int, float) and math.isfinite(value)
            and float(Fraction(round(value * scale), scale)) == value)


def stored_fraction(value):
    require(type(value) in (int, float) and math.isfinite(value), "Non-finite stored coordinate")
    return Fraction.from_float(float(value))


def edges(poly):
    return list(zip(poly, poly[1:] + poly[:1]))


def sub(a, b):
    return a[0] - b[0], a[1] - b[1]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def signed_area2(poly):
    return sum(cross(a, b) for a, b in edges(poly))


def convex(poly):
    if len(poly) < 3 or len(set(poly)) != len(poly) or not signed_area2(poly):
        return False
    direction = 1 if signed_area2(poly) > 0 else -1
    return all(direction * cross(sub(b, a), sub(p, a)) >= 0
               for a, b in edges(poly) for p in poly)


def rotate(poly, quarter):
    return [[(x, y), (-y, x), (-x, -y), (y, -x)][quarter] for x, y in poly]


def separated(a, b):
    for p, q in edges(a) + edges(b):
        dx, dy = sub(q, p)
        axis = -dy, dx
        aa, bb = [dot(axis, v) for v in a], [dot(axis, v) for v in b]
        if max(aa) <= min(bb) or max(bb) <= min(aa):
            return True
    return False


def separated_with_tolerance(a, b, tolerance):
    for p, q in edges(a) + edges(b):
        dx, dy = sub(q, p)
        axis = -dy, dx
        aa, bb = [dot(axis, v) for v in a], [dot(axis, v) for v in b]
        signed_distance = max(min(bb) - max(aa), min(aa) - max(bb))
        if signed_distance >= 0 or signed_distance * signed_distance <= tolerance * tolerance * dot(axis, axis):
            return True
    return False


def collinear_roundoff(a, b, tolerance):
    coordinates = [abs(value) for point in a + b for value in point]
    edge_lengths = [math.hypot(float(q[0] - p[0]), float(q[1] - p[1])) for p, q in edges(a)]
    magnitude = max([1.] + [float(v) for v in coordinates] + edge_lengths)
    return min(tolerance, Fraction.from_float(64 * sys.float_info.epsilon * magnitude))


def shared_segments(a, b, scale):
    segments = []
    for p, q in edges(a):
        direction = sub(q, p)
        length2 = dot(direction, direction)
        for u, v in edges(b):
            if cross(direction, sub(u, p)) or cross(direction, sub(v, p)):
                continue
            first, second = dot(sub(u, p), direction), dot(sub(v, p), direction)
            low, high = max(0, min(first, second)), min(length2, max(first, second))
            if high <= low:
                continue
            at = lambda t: [str(Fraction(p[k], scale) + Fraction(direction[k] * t, length2 * scale))
                            for k in range(2)]
            segments.append(dict(start=at(low), end=at(high),
                                 lengthMm=(high - low) / math.sqrt(length2) / scale))
    return segments


def shared_segments_with_roundoff(a, b, tolerance):
    segments = []
    for p, q in edges(a):
        direction = sub(q, p)
        length2 = dot(direction, direction)
        for u, v in edges(b):
            allowance = collinear_roundoff([p, q], [u, v], tolerance)
            offsets = [cross(direction, sub(point, p)) for point in (u, v)]
            if any(offset * offset > allowance * allowance * length2 for offset in offsets):
                continue
            first, second = dot(sub(u, p), direction), dot(sub(v, p), direction)
            low, high = max(0, min(first, second)), min(length2, max(first, second))
            if high <= low or (high - low) ** 2 <= tolerance * tolerance * length2:
                continue
            at = lambda t: [str(p[k] + direction[k] * t / length2) for k in range(2)]
            segments.append(dict(start=at(low), end=at(high),
                                 lengthMm=float(high - low) / math.sqrt(float(length2)),
                                 collinearityRoundoffBoundMm=float(allowance),
                                 exactlyCollinear=all(offset == 0 for offset in offsets)))
    return segments


def point_segment_squared(p, a, b):
    direction, delta = sub(b, a), sub(p, a)
    length2 = dot(direction, direction)
    projection = dot(delta, direction)
    if projection <= 0:
        return Fraction(dot(delta, delta))
    if projection >= length2:
        delta = sub(p, b)
        return Fraction(dot(delta, delta))
    return Fraction(cross(direction, delta) ** 2, length2)


def distance_squared(a, b):
    return min(min(point_segment_squared(p, u, v), point_segment_squared(q, u, v),
                   point_segment_squared(u, p, q), point_segment_squared(v, p, q))
               for p, q in edges(a) for u, v in edges(b))


def recover_inputs(native, snapshot, snapshot_path):
    problem = snapshot["problem"]
    require(native["problem"] == problem, "Native input differs from immutable DXF task snapshot")
    scale = native["settings"].get("coordinateScale", native["settings"].get("scale"))
    require(type(scale) is int and scale > 0, "Missing positive native coordinate scale")
    parts = {p["id"]: p for p in problem["parts"]}
    require(len(parts) == len(problem["parts"]), "Duplicate original part identities")
    sources = {p["id"]: p for p in snapshot["sources"]}
    require(len(sources) == len(snapshot["sources"]) and sources.keys() == parts.keys(), "DXF sources do not match original identities")
    polygons, decimals, source_records = {}, {}, []
    for pid, part in parts.items():
        path = Path(sources[pid]["file"])
        if not path.is_absolute():
            path = snapshot_path.parent / path
        path = path.resolve()
        require(sha(path) == sources[pid]["sha256"], f"Original DXF hash changed: {pid}")
        exact = exact_dxf_contour(path)
        geometry = part["geometry"]
        require(geometry["radius"] == 0 and not geometry["holes"] and not part["freeRotation"],
                f"Independent report scope requires fixed-angle hole-free polygons: {pid}")
        require([[float(v) for v in p] for p in exact] == geometry["outer"], f"DXF contour differs from original input: {pid}")
        scaled = []
        for p in exact:
            require(all((v * scale).denominator == 1 for v in p), f"DXF decimals are not exact lattice coordinates: {pid}")
            scaled.append(tuple(int(v * scale) for v in p))
        require(convex(scaled), f"Independent exact checker requires convex original material: {pid}")
        angles = part["allowedAngles"] or [0]
        require(all(math.isfinite(a) and a % 90 == 0 for a in angles), f"Non-quarter original angle: {pid}")
        require(type(part["quantity"]) is int and part["quantity"] >= 0, f"Invalid original quantity: {pid}")
        polygons[pid], decimals[pid] = scaled, exact
        source_records.append(dict(id=pid, file=str(path), sha256=sources[pid]["sha256"],
                                   vertices=len(scaled), sourceContourUnchanged=True))
    return problem, scale, parts, polygons, decimals, source_records


def exact_audit(native, problem, scale, parts, polygons):
    stocks = {s["id"]: s for s in problem["stocks"]}
    require(len(stocks) == len(problem["stocks"]), "Duplicate stock identities")
    continuous = any(not on_lattice(pose[axis], scale)
                     for layout in native["result"]["layouts"] for pose in layout["placements"] for axis in ("x", "y"))
    source_scale = scale
    if continuous:
        polygons = {pid: [(Fraction(x, scale), Fraction(y, scale)) for x, y in poly]
                    for pid, poly in polygons.items()}
        scale = 1
    coordinate = stored_fraction if continuous else lambda value: lattice(value, scale, "coordinate")
    gap = coordinate(problem["partGap"])
    margin = coordinate(problem["edgeMargin"])
    tolerance = stored_fraction(problem["tolerance"]) if continuous else Fraction(0)
    common = problem["allowCommonEdges"]
    require(type(common) is bool, "Common-edge setting must be boolean")
    demanded = Counter({p["id"]: p["quantity"] for p in problem["parts"]})
    seen, used_boards, remaining, pairs, boards = set(), set(), Counter(), [], []
    counts = Counter()
    material2, sheet_area_scaled, minimum_edge = 0, 0, None
    minimum_gap_squared, common_pairs, common_length = None, 0, 0.
    for bi, layout in enumerate(native["result"]["layouts"]):
        sid, instance = layout["stock"], layout["instance"]
        require(sid in stocks and type(instance) is int and 0 <= instance < stocks[sid]["quantity"], "Unknown/out-of-inventory stock")
        require((sid, instance) not in used_boards, "Repeated consumed stock identity")
        used_boards.add((sid, instance))
        stock = stocks[sid]
        require(layout["width"] == stock["width"] and layout["height"] == stock["height"], "Native board dimensions changed")
        width = coordinate(stock["width"])
        height = coordinate(stock["height"])
        require(layout["placements"], "Empty board reported as consumed stock")
        values, board_area2 = [], 0
        for pose in layout["placements"]:
            pid, copy = pose["part"], pose["instance"]
            require(pid in parts and type(copy) is int and 0 <= copy < parts[pid]["quantity"], "Unknown/out-of-demand original part")
            require((pid, copy) not in seen, "Duplicated original part instance")
            require(parts[pid]["material"] == stock["material"], "Material compatibility changed")
            seen.add((pid, copy)); counts[pid] += 1
            angle = pose["angle"]
            require(type(angle) in (int, float) and math.isfinite(angle) and angle % 90 == 0, f"Invalid original rotation: {pid}")
            require(angle % 360 in {a % 360 for a in (parts[pid]["allowedAngles"] or [0])}, f"Forbidden original rotation: {pid}")
            x = coordinate(pose["x"])
            y = coordinate(pose["y"])
            world = [(a + x, b + y) for a, b in rotate(polygons[pid], int(angle / 90) % 4)]
            edge = min(min(a for a, b in world), min(b for a, b in world),
                       width - max(a for a, b in world), height - max(b for a, b in world))
            require(edge >= margin - tolerance, f"Original material violates declared board margin: {pid}")
            minimum_edge = edge if minimum_edge is None else min(minimum_edge, edge)
            area = abs(signed_area2(world)); board_area2 += area
            values.append(dict(part=pid, instance=copy, xScaled=x, yScaled=y, x=pose["x"], y=pose["y"],
                               rotationQuarter=int(angle / 90) % 4, world=world))
        for a, b in combinations(values, 2):
            strictly_disjoint = separated(a["world"], b["world"])
            require(strictly_disjoint or (continuous and separated_with_tolerance(a["world"], b["world"], tolerance)),
                    f"Original material overlap exceeds declared tolerance on board {bi + 1}: {a['part']} / {b['part']}")
            segments = (shared_segments_with_roundoff(a["world"], b["world"], tolerance)
                        if continuous else shared_segments(a["world"], b["world"], scale))
            record = dict(board=bi + 1, first=a["part"], firstInstance=a["instance"],
                          second=b["part"], secondInstance=b["instance"], strictlyDisjointMaterial=strictly_disjoint)
            if common and segments:
                length = sum(s["lengthMm"] for s in segments)
                common_pairs += 1; common_length += length
                record.update(mode="common-edge", segments=segments, sharedLengthMm=length)
            else:
                distance = distance_squared(a["world"], b["world"]) if strictly_disjoint else Fraction(0)
                allowed_gap = max(Fraction(0), gap - tolerance)
                require(distance >= allowed_gap * allowed_gap, f"Ordinary part gap exceeds declared tolerance: {record}")
                minimum_gap_squared = distance if minimum_gap_squared is None else min(minimum_gap_squared, distance)
                record.update(mode="clearance", distanceSquaredScaledExact=str(distance),
                              minimumDistanceMm=math.sqrt(float(distance)) / scale)
            pairs.append(record)
        material2 += board_area2; sheet_area_scaled += width * height
        boards.append(dict(board=bi + 1, stock=sid, stockInstance=instance, widthScaled=width,
                           heightScaled=height, placements=values, materialArea=float(board_area2 / (2 * scale * scale)),
                           utilization=float(board_area2 / (2 * width * height))))
    for row in native["result"]["remaining"]:
        require(row["part"] in parts and row["part"] not in remaining and type(row["quantity"]) is int
                and row["quantity"] > 0, "Invalid remaining demand record")
        remaining[row["part"]] = row["quantity"]
    require(all(counts[pid] + remaining[pid] == qty for pid, qty in demanded.items()), "Original demand is not conserved")
    required, placed = sum(demanded.values()), sum(counts.values())
    complete = required == placed
    result, validation = native["result"], native["validation"]
    require(result["valid"] and validation["valid"], "Native result did not pass native validation")
    require(result["complete"] == complete == validation["complete"], "Native completeness claim differs from original inventory")
    require(result["placed"] == placed == validation["placed"], "Native placed count differs from original inventory")
    require(result["boards"] == len(boards) and result["remainingCount"] == required - placed, "Native board/remaining counts differ")
    required_material = defaultdict(int)
    for pid, part in parts.items():
        required_material[part["material"]] += abs(signed_area2(polygons[pid])) * part["quantity"]
    lower = 0
    for material, area in required_material.items():
        if not area:
            continue
        effective_margin = margin - tolerance
        capacities = [(coordinate(s["width"]) - 2 * effective_margin)
                      * (coordinate(s["height"]) - 2 * effective_margin)
                      for s in stocks.values() if s["quantity"] > 0 and s["material"] == material]
        require(capacities and max(capacities) > 0, "No compatible usable stock for original material")
        lower += math.ceil(Fraction(area, 2 * max(capacities)))
    summary = dict(passed=True, exactIntegerAndRationalAudit=not continuous, exactRationalEvaluation=True,
                   coordinateMode="stored-binary64-with-declared-tolerance" if continuous else "strict-integer-lattice",
                   auditCoordinateScale=scale, searchCoordinateScale=source_scale,
                   geometryToleranceMm=problem["tolerance"] if continuous else 0,
                   collinearityPolicy="coordinate-scale floating-point roundoff capped by declared tolerance" if continuous else "exact collinearity",
                   required=required, placed=placed,
                   remaining=required - placed, complete=complete, boards=len(boards), areaLowerBound=lower,
                   reachesBoardCountLowerBound=complete and len(boards) == lower,
                   minimumEdgeMarginMm=float(minimum_edge / scale) if minimum_edge is not None else None,
                   minimumNonCommonGapMm=math.sqrt(float(minimum_gap_squared)) / scale if minimum_gap_squared is not None else None,
                   minimumNonCommonDistanceSquaredScaledExact=str(minimum_gap_squared) if minimum_gap_squared is not None else None,
                   commonEdgePairs=common_pairs, commonEdgeLengthMm=common_length,
                   requiredMaterialArea=float(sum(required_material.values()) / (2 * scale * scale)),
                   materialArea=float(material2 / (2 * scale * scale)),
                   stockArea=float(sheet_area_scaled / (scale * scale)),
                   utilization=float(material2 / (2 * sheet_area_scaled)) if sheet_area_scaled else 0,
                   allOriginalIdentitiesAccountedFor=True, sourceGeometryChanged=False,
                   pairProofs=pairs, perBoard=[{k: v for k, v in b.items() if k != "placements"}
                                               | {"parts": [{k: v for k, v in q.items() if k != "world"} for q in b["placements"]]}
                                               for b in boards])
    return summary, boards


def geos_audit(boards, decimals, scale, problem, exact):
    from shapely.geometry import Polygon
    gap, margin = problem["partGap"], problem["edgeMargin"]
    continuous = exact["coordinateMode"] == "stored-binary64-with-declared-tolerance"
    tolerance = problem["tolerance"] if continuous else 0.
    checks, total_area, seen = [], 0., []
    for board in boards:
        polygons = []
        width, height = board["widthScaled"], board["heightScaled"]
        for pose in board["placements"]:
            raw = (decimals[pose["part"]] if continuous else
                   [(int(x * scale), int(y * scale)) for x, y in decimals[pose["part"]]])
            world = [(x + pose["xScaled"], y + pose["yScaled"])
                     for x, y in rotate(raw, pose["rotationQuarter"])]
            poly = Polygon([(float(x), float(y)) for x, y in world])
            require(poly.is_valid and poly.area > 0, "GEOS found invalid actual DXF material")
            edge = min(poly.bounds[0], poly.bounds[1], width - poly.bounds[2], height - poly.bounds[3]) / scale
            require(edge >= margin - tolerance - 1e-10, "GEOS board margin failed")
            polygons.append((pose, poly)); total_area += poly.area / (scale * scale)
            seen.append((pose["part"], pose["instance"]))
        for (a, p), (b, q) in combinations(polygons, 2):
            overlap = p.intersection(q).area / (scale * scale)
            distance = p.distance(q) / scale
            shared = p.boundary.intersection(q.boundary).length / scale
            near_shared = 0.
            if continuous:
                contracted_overlap = p.buffer(-tolerance * .5, join_style=2).intersection(
                    q.buffer(-tolerance * .5, join_style=2)).area
                require(contracted_overlap <= tolerance * tolerance, "GEOS material overlap exceeds declared tolerance")
                allowance = float(collinear_roundoff(a["world"], b["world"], stored_fraction(tolerance)))
                if allowance > 0 and distance <= allowance:
                    near_shared = min(p.boundary.intersection(q.boundary.buffer(allowance, cap_style=2, join_style=2)).length,
                                      q.boundary.intersection(p.boundary.buffer(allowance, cap_style=2, join_style=2)).length)
            else:
                require(overlap <= 1e-10, "GEOS material overlap failed")
            require((problem["allowCommonEdges"] and max(shared, near_shared) > tolerance)
                    or distance >= gap - tolerance - 1e-10,
                    "GEOS independent mixed gap check failed")
            checks.append(dict(board=board["board"], first=a["part"], firstInstance=a["instance"],
                               second=b["part"], secondInstance=b["instance"],
                               intersectionArea=overlap, distanceMm=distance, sharedLengthMm=shared,
                               sharedLengthWithinRoundoffMm=near_shared))
    require(len(seen) == exact["placed"] and abs(total_area - exact["materialArea"]) <= 1e-8,
            "GEOS original identity/area totals differ")
    return dict(passed=True, engine="Independent Shapely/GEOS from original DXF decimal vertices",
                integerLatticeUsedForGeos=not continuous, coordinateScale=scale, placed=len(seen),
                materialArea=total_area, floatCrossCheckToleranceMm=1e-10,
                geometryToleranceMm=tolerance, exactArithmeticAuditIsAuthoritative=True,
                auditMode=exact["coordinateMode"], pairChecks=checks)


def construction_comparison(native, decimals):
    before = native.get("constructionResult")
    if before is None:
        return None
    problem = native["problem"]
    margin = stored_fraction(problem["edgeMargin"])
    material = {}
    for pid, poly in decimals.items():
        twice_area = signed_area2(poly)
        center = tuple(sum((a[k] + b[k]) * cross(a, b) for a, b in edges(poly)) / (3 * twice_area)
                       for k in range(2))
        material[pid] = abs(twice_area) / 2, center

    def measure(result):
        measurements, identities = {}, {}
        for board in result["layouts"]:
            key = board["stock"], board["instance"]
            require(key not in measurements, "Repeated construction stock identity")
            world, area, moment_x, moment_y = [], Fraction(0), Fraction(0), Fraction(0)
            for pose in board["placements"]:
                identity = pose["part"], pose["instance"]
                require(identity not in identities and pose["part"] in material, "Invalid construction original identity")
                require(pose["angle"] % 90 == 0, "Construction comparison requires original quarter turns")
                x, y = stored_fraction(pose["x"]), stored_fraction(pose["y"])
                quarter = int(pose["angle"] / 90) % 4
                world.extend((a + x, b + y) for a, b in rotate(decimals[pose["part"]], quarter))
                part_area, center = material[pose["part"]]
                cx, cy = rotate([center], quarter)[0]
                area += part_area
                moment_x += part_area * (cx + x)
                moment_y += part_area * (cy + y)
                identities[identity] = dict(stock=key, x=x, y=y, angle=pose["angle"])
            require(world, "Empty construction board cannot be compared")
            left, bottom = min(x for x, y in world), min(y for x, y in world)
            right, top = max(x for x, y in world), max(y for x, y in world)
            width, height = stored_fraction(board["width"]), stored_fraction(board["height"])
            envelope = (right - left) * (top - bottom)
            origin_extent = max(Fraction(0), right - margin) * max(Fraction(0), top - margin)
            left_free, bottom_free = max(Fraction(0), left - margin), max(Fraction(0), bottom - margin)
            right_free, top_free = max(Fraction(0), width - margin - right), max(Fraction(0), height - margin - top)
            usable_width, usable_height = width - 2 * margin, height - 2 * margin
            measurements[key] = dict(pieces=len(board["placements"]),
                materialBoundsMm=[float(v) for v in (left, bottom, right, top)],
                materialEnvelopeAreaMm2=float(envelope),
                materialEnvelopeFill=float(area / envelope) if envelope else 0.,
                materialAreaMm2=float(area), stockFreeAreaMm2=float(width * height - area),
                areaWeightedCentroidXmm=float(moment_x / area), areaWeightedCentroidYmm=float(moment_y / area),
                originLeftEmptyMm=float(left_free), originBottomEmptyMm=float(bottom_free),
                originVoidWithinOccupiedExtentMm2=float(max(Fraction(0), origin_extent - envelope)),
                certifiedBoundaryStripAreaLowerBoundMm2=float(max(left_free * usable_height,
                    right_free * usable_height, bottom_free * usable_width, top_free * usable_width)))
        return measurements, identities

    initial, initial_ids = measure(before)
    final, final_ids = measure(native["result"])
    require(initial_ids.keys() == final_ids.keys(), "Construction/final original identity sets differ")
    rows = []
    for key in sorted(initial.keys() | final.keys()):
        first, last = initial.get(key), final.get(key)
        row = dict(stock=key[0], stockInstance=key[1], before=first, after=last)
        if first is not None and last is not None:
            row["deltaAfterMinusBefore"] = {field: last[field] - first[field] for field in (
                "materialEnvelopeAreaMm2", "areaWeightedCentroidXmm", "areaWeightedCentroidYmm",
                "originVoidWithinOccupiedExtentMm2", "certifiedBoundaryStripAreaLowerBoundMm2")}
        rows.append(row)
    movements = []
    for identity, pose in initial_ids.items():
        last = final_ids[identity]
        if pose != last:
            movements.append(dict(part=identity[0], instance=identity[1],
                beforeStock=list(pose["stock"]), afterStock=list(last["stock"]),
                deltaXmm=float(last["x"] - pose["x"]), deltaYmm=float(last["y"] - pose["y"]),
                angleBefore=pose["angle"], angleAfter=last["angle"]))
    return dict(originalIdentitiesPreserved=True,
                originalAnglesPreserved=all(initial_ids[key]["angle"] == final_ids[key]["angle"] for key in initial_ids),
                originalStockAssignmentPreserved=all(initial_ids[key]["stock"] == final_ids[key]["stock"] for key in initial_ids),
                changedStockAssignmentOriginalInstances=sum(initial_ids[key]["stock"] != final_ids[key]["stock"]
                                                            for key in initial_ids),
                movedOriginalInstances=len(movements), movements=movements, perStock=rows,
                envelopeAreaBeforeMm2=sum(row["materialEnvelopeAreaMm2"] for row in initial.values()),
                envelopeAreaAfterMm2=sum(row["materialEnvelopeAreaMm2"] for row in final.values()),
                axisDiagnostics=[s for s in native["result"].get("diagnostics", [])
                                 if s.startswith("compact.axis") or s.startswith("pipeline.basic")],
                metricsScope="Actual stored poses and original DXF material. Boundary strips are certified lower bounds, not maximum reusable remnants.")


def search_mechanism(native):
    records = []
    stage_summaries = []
    pipeline_outcomes = []
    working_exchanges = []
    full_cohort_attempts = []
    for index, raw in enumerate(native["result"].get("diagnostics", [])):
        if raw.startswith("pipeline.post=sheet-count.refill:"):
            pipeline_outcomes.append(dict(diagnosticIndex=index, raw=raw,
                                          accepted=raw.split(":", 1)[1].strip().startswith("accepted")))
        body = raw
        rejected_stages = []
        while body.startswith("pipeline.rejected=") and " | " in body:
            rejection, body = body.split(" | ", 1)
            rejected_stages.append(rejection[len("pipeline.rejected="):])
        if "sheet-count.refill" not in body and "sheet-count.perturb" not in body:
            continue
        subproblem = "subproblem | " in body
        context = ("rejected-stage-candidate" if rejected_stages else
                   "internal-subproblem" if subproblem else "stage-output")
        payload = body.rsplit(" | ", 1)[-1] if subproblem else body
        description = payload.split(":", 1)[-1].strip()
        fields = {}
        for key, value in re.findall(r"(?:^|;\s*)([A-Za-z][A-Za-z0-9_-]*)=([^;]*)", description):
            value = value.strip()
            if re.fullmatch(r"[+-]?\d+", value):
                fields[key] = int(value)
            elif re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", value):
                number = float(value)
                fields[key] = number if math.isfinite(number) else value
            else:
                fields[key] = value
        record = dict(diagnosticIndex=index, publicationContext=context, rejectedStages=rejected_stages,
                      internalSubproblem=subproblem, fields=fields, raw=raw)
        records.append(record)
        if (not subproblem and fields.get("scope") == "full-cohort" and "skipped" not in fields
                and ((payload.startswith("sheet-count.refill:") and "reconstructed" in fields)
                     or (payload.startswith("sheet-count.refill exchange-attempt:") and fields.get("executed") == 1))):
            full_cohort_attempts.append(record)
        if (not subproblem and payload.startswith("sheet-count.refill exchange:")
                and fields.get("verified") == 1 and fields.get("working-accepted") == 1
                and isinstance(fields.get("changed-members"), int) and fields["changed-members"] > 0):
            working_exchanges.append(record)
        if (not subproblem and payload.startswith("sheet-count.refill:")
                and (("working-boards" in fields and "best-boards" in fields)
                     or ("perturbations" in fields and "exchanges" in fields))):
            stage_summaries.append(record)
    published_summaries = [row for row in stage_summaries if row["publicationContext"] == "stage-output"]
    last_published = published_summaries[-1] if published_summaries else None
    last_internal = stage_summaries[-1] if stage_summaries else None
    stage_counts = last_published["fields"] if last_published else None
    retention = None
    if stage_counts is not None:
        for key in ("published", "returned", "retained", "output"):
            if key in stage_counts:
                retention = stage_counts[key]
                break
    return dict(nativeLogRecords=records, stageSummaries=stage_summaries,
                verifiedWorkingExchangeEvents=working_exchanges,
                fullCohortSearchAttempts=full_cohort_attempts,
                maximumGlobalWindowBoards=max((row["fields"]["window"] for row in full_cohort_attempts), default=0),
                maximumGlobalReleasedInstances=max((row["fields"]["members"] for row in full_cohort_attempts), default=0),
                publishedStageSummary=last_published, lastInternalStageSummary=last_internal,
                publishedStageCounts=stage_counts, publishedStageRetentionMarker=retention,
                pipelineStageOutcomes=pipeline_outcomes,
                publishedFinalBoards=native["result"]["boards"],
                publishedFinalPlaced=native["result"]["placed"],
                publishedFinalRemaining=native["result"]["remainingCount"],
                countsSource="Native stage diagnostics. Rejected candidate and internal subproblem records retain their context.")


def render(path, title, boards, exact, problem, scale, parts):
    from PIL import Image, ImageDraw, ImageFont
    font_path = "C:/Windows/Fonts/msyh.ttc"
    font = lambda size: ImageFont.truetype(font_path, size)
    cols = min(2, max(1, len(boards)))
    long = max((max(b["widthScaled"], b["heightScaled"]) for b in boards), default=1) / scale
    short = max((min(b["widthScaled"], b["heightScaled"]) for b in boards), default=1) / scale
    factor = min(790 / long, 440 / short)
    cell_width, cell_height = 850, math.ceil(short * factor) + 90
    top, rows = 185, max(1, math.ceil(len(boards) / cols))
    image = Image.new("RGB", (cols * cell_width + 45, top + rows * cell_height + 100), "#f2f5f7")
    draw = ImageDraw.Draw(image)
    draw.text((25, 18), title, font=font(32), fill="#173448")
    draw.text((25, 67), f"{exact['boards']} 张板 · {exact['placed']} / {exact['required']} 件 · 剩余 {exact['remaining']} 件 · 整板利用率 {exact['utilization'] * 100:.2f}%",
              font=font(25), fill="#173448")
    gap_rule = f"真实共边允许贴合，其他间距 ≥ {problem['partGap']:g} mm" if problem["allowCommonEdges"] else f"零件间距 ≥ {problem['partGap']:g} mm"
    draw.text((25, 110), f"四边留边 {problem['edgeMargin']:g} mm；{gap_rule}；按原始许可角度，不镜像。", font=font(20), fill="#496170")
    draw.text((25, 145), "各板按统一物理比例横向展示；橙线为真实共边，完整原件 ID 标在轮廓内。", font=font(20), fill="#496170")
    colors = ["#a9d4e6", "#d5bddf", "#c8dfb2", "#edc499", "#a6d9cd", "#ecc0cb", "#c2cbed"]
    color_ids = {pid: i for i, pid in enumerate(parts)}
    for i, board in enumerate(boards):
        ox, oy = 25 + (i % cols) * cell_width, top + (i // cols) * cell_height + 34
        width, height = float(board["widthScaled"] / scale), float(board["heightScaled"] / scale)
        landscape = width >= height
        def point(x, y):
            x, y = float(x), float(y)
            return (ox + x * factor, oy + (height - y) * factor) if landscape else (ox + y * factor, oy + x * factor)
        draw.text((ox, oy - 32), f"板 {i + 1:02}  |  {len(board['placements'])} 件  |  {width:g} × {height:g} mm  |  {board['utilization'] * 100:.2f}%",
                  font=font(21), fill="#173448")
        draw.rectangle((ox, oy, ox + max(width, height) * factor, oy + min(width, height) * factor),
                       fill="white", outline="#354f61", width=2)
        for pose in board["placements"]:
            draw.polygon([point(x / scale, y / scale) for x, y in pose["world"]],
                         fill=colors[color_ids[pose["part"]] % len(colors)], outline="#46616d", width=2)
        for pair in exact["pairProofs"]:
            if pair["board"] == i + 1 and pair["mode"] == "common-edge":
                for segment in pair["segments"]:
                    draw.line([point(*(float(Fraction(v)) for v in segment["start"])),
                               point(*(float(Fraction(v)) for v in segment["end"]))], fill="#e77118", width=3)
        for pose in board["placements"]:
            poly = pose["world"]
            cx, cy = sum(x for x, y in poly) / len(poly) / scale, sum(y for x, y in poly) / len(poly) / scale
            x, y = point(cx, cy)
            label = pose["part"] + (f" [{pose['instance']}]" if parts[pose["part"]]["quantity"] > 1 else "")
            label_font = font(18)
            box = draw.textbbox((0, 0), label, font=label_font)
            while box[2] - box[0] > 145 and label_font.size > 9:
                label_font = font(label_font.size - 1)
                box = draw.textbbox((0, 0), label, font=label_font)
            w, h = box[2] - box[0], box[3] - box[1]
            draw.rounded_rectangle((x - w / 2 - 3, y - h / 2 - 3, x + w / 2 + 3, y + h / 2 + 4), radius=3, fill="white")
            draw.text((x - w / 2, y - h / 2 - box[1]), label, font=label_font, fill="#173448")
    if not boards:
        draw.text((25, top + 40), "本次未获得可发布的完整排样布局，未绘制虚构板材。", font=font(25), fill="#8e3928")
    footer = top + rows * cell_height + 8
    state = "达到材料面积板数下界" if exact["reachesBoardCountLowerBound"] else "未证明板数最优"
    draw.text((25, footer), f"当前任务冷启动；板数优先，共边仅为允许的接触方式。{state}。", font=font(20), fill="#496170")
    verification_label = (f"实际浮点位姿按 {problem['tolerance']:g} mm 声明容差复核"
                          if exact["coordinateMode"] == "stored-binary64-with-declared-tolerance"
                          else "严格整数格点与有理数证书")
    draw.text((25, footer + 33), verification_label + "；通用 C++ 入口，尚未部署到产品 UI。", font=font(20), fill="#496170")
    image.save(path)


def report(out, native_path, snapshot_path, native, title, summary, exact, sources, comparison):
    esc = lambda value: html.escape(str(value))
    relative = lambda path: Path(os.path.relpath(path, out)).as_posix()
    problem, result = native["problem"], native["result"]
    lower_statement = ("全部需求已完成，实际板数等于材料面积下界，因此本任务达到最少板数。"
                       if exact["reachesBoardCountLowerBound"] else
                       "本次结果尚未证明板数最优；较小板数的超时不表示几何上不可行。")
    board_rows = "".join(f"<tr><td>{b['board']}</td><td>{esc(b['stock'])} / {b['stockInstance']}</td>"
                         f"<td>{float(b['widthScaled'] / summary['auditCoordinateScale']):g} × {float(b['heightScaled'] / summary['auditCoordinateScale']):g}</td>"
                         f"<td>{len(b['parts'])}</td><td>{b['utilization'] * 100:.2f}%</td>"
                         f"<td>{esc('、'.join(p['part'] + '[' + str(p['instance']) + ']' for p in b['parts']))}</td></tr>"
                         for b in exact["perBoard"])
    parameter_rows = [
        ("全部需求", f"{exact['required']} 件 / {len(problem['parts'])} 种，保留每种原始数量"),
        ("板材输入", "; ".join(f"{s['id']}: {s['width']:g} × {s['height']:g} mm，库存 {s['quantity']}" for s in problem["stocks"])),
        ("四边留边", f"各 {problem['edgeMargin']:g} mm"),
        ("间距与共边", f"普通间距 {problem['partGap']:g} mm；共边允许={problem['allowCommonEdges']}；点接触不算共边"),
        ("全部需求材料面积 / 板数下界", f"{exact['requiredMaterialArea']:g} mm² / {exact['areaLowerBound']} 张（考虑板边距后的面积必要条件）"),
        ("最小实际板边距", summary["minimumEdgeMarginMm"]),
        ("最小非共边间距", summary["minimumNonCommonGapMm"]),
        ("共边", f"{exact['commonEdgePairs']} 对，总长 {exact['commonEdgeLengthMm']:.3f} mm；未生成单次共边切割刀路"),
        ("时间", f"原生完整任务 {native['wholeJobWallSeconds']:.3f} 秒；交付独立复核 {summary['independentVerificationSeconds']:.3f} 秒"),
        ("搜索输入与目标", "仅当前任务的原始零件、数量、板材、规则；从面积下界开始依次尝试板数；不接收旧布局；共边没有优化奖励"),
        ("搜索范围", f"凸无孔多边形，采用原许可四分角。构造和跨板重排采用 {1 / summary['coordinateScale']:g} mm 平移格点，基础收紧采用连续接触位移。"),
        ("交付复核", "实际存储浮点位姿的有理数计算，按声明几何容差复核" if summary["coordinateMode"] == "stored-binary64-with-declared-tolerance" else "严格整数格点与有理数证书"),
        ("程序状态", "通用 C++ 冷启动排样入口；尚未部署到产品 UI"),
    ]
    provenance_path = out / "run-provenance.json"
    if provenance_path.is_file():
        build_description = read(provenance_path).get("buildDescription")
        if build_description:
            parameter_rows.append(("实际构建方式", build_description))
    params = "".join(f"<tr><th>{esc(k)}</th><td>{esc(v) if v is not None else '无适用件对'}</td></tr>" for k, v in parameter_rows)
    diagnostics = "\n".join(result.get("diagnostics", []))
    mechanism = summary.get("searchMechanism")
    mechanism_html = ""
    if mechanism is not None and mechanism["nativeLogRecords"]:
        count_labels = (("plans", "累计窗口求解次数"), ("accepted", "减板方案接收次数"),
                        ("removed", "窗口累计释放板数"), ("transferred", "跨板转移成员记录"),
                        ("transferPlans", "累计逐件转移求解次数"), ("transfer-moved", "逐件转移成功记录"),
                        ("global-reductions", "整批减板模型次数"), ("global-exchanges", "整批同板数换组模型次数"),
                        ("perturbations", "同板数扰动尝试"),
                        ("exchanges", "已核验跨板换组次数"), ("exchanged-members", "累计跨组成员记录"),
                        ("tabu-rejected", "禁忌分组拒绝次数"), ("restarts", "重启次数"),
                        ("best-updates", "历史最佳更新次数"), ("working-boards", "退出时内部工作解板数"),
                        ("best-boards", "阶段历史最佳板数"), ("returned", "阶段返回对象"),
                        ("published", "阶段输出对象"), ("retained", "阶段保留对象"),
                        ("quota-scope", "减板配额作用范围"))
        context_names = {"stage-output": "阶段返回记录", "rejected-stage-candidate": "流水线拒绝的候选阶段记录"}
        count_rows = ""
        for record in mechanism["stageSummaries"]:
            for key, label in count_labels:
                if key in record["fields"]:
                    count_rows += (f"<tr><td>{esc(context_names[record['publicationContext']])}</td>"
                                   f"<td>{esc(label)}</td><td>{esc(record['fields'][key])}</td></tr>")
        mechanism_html = ("<h2>跨板搜索的实际运行记录</h2>"
            f"<p>最终发布 {mechanism['publishedFinalBoards']} 张板、{mechanism['publishedFinalPlaced']} 件，"
            f"剩余 {mechanism['publishedFinalRemaining']} 件。内部工作解、阶段历史最佳及流水线发布结果分别记录。</p>"
            f"<table><tr><th>记录所属阶段</th><th>计数含义</th><th>原生计数</th></tr>{count_rows}</table>")
        if mechanism["publishedStageRetentionMarker"] is not None:
            mechanism_html += f"<p>原生阶段返回标记为 {esc(mechanism['publishedStageRetentionMarker'])}。</p>"
        exchange_rows = ""
        for event in mechanism["verifiedWorkingExchangeEvents"]:
            fields = event["fields"]
            exchange_rows += (f"<tr><td>{esc(context_names[event['publicationContext']])}</td>"
                f"<td>{esc(fields.get('window', '—'))}</td><td>{esc(fields['changed-members'])}</td>"
                f"<td>{esc(fields.get('best-updated', fields.get('best-improved', '—')))}</td>"
                f"<td>{esc(fields.get('working-boards', '—'))}</td><td>{esc(fields.get('best-boards', '—'))}</td></tr>")
        if exchange_rows:
            mechanism_html += ("<h3>经过原生全件核验并接收为工作解的真实换组</h3>"
                "<table><tr><th>记录所属阶段</th><th>窗口板数</th><th>分组变更成员数</th>"
                f"<th>是否更新最佳</th><th>工作解板数</th><th>最佳板数</th></tr>{exchange_rows}</table>")
        published_exchange = summary.get("constructionComparison", {}).get("changedStockAssignmentOriginalInstances")
        if published_exchange is not None:
            mechanism_html += f"<p>独立比较同次构造与最终结果，实际更换库存实例的原件为 {published_exchange} 件。</p>"
        if mechanism["fullCohortSearchAttempts"]:
            mechanism_html += (f"<p>内部实际执行整批重排模型 {len(mechanism['fullCohortSearchAttempts'])} 次，"
                f"单次最大释放 {mechanism['maximumGlobalWindowBoards']} 张板、"
                f"{mechanism['maximumGlobalReleasedInstances']} 件原件。模型执行记录与最终接收结果分别核对。</p>")
        raw_search = "\n".join(row["raw"] for row in mechanism["nativeLogRecords"])
        mechanism_html += f"<details><summary>全部原生跨板搜索日志</summary><pre>{esc(raw_search)}</pre></details>"
    provenance_links = " · ".join(
        f'<a href="{filename}">{label}</a>'
        for filename, label in (("task.json", "本次任务清单"),
                                ("run-provenance.json", "实际程序与源码版本"),
                                ("implementation-checks.json", "通用性与回归检查"))
        if (out / filename).is_file())
    comparison_html = ""
    if comparison is not None:
        if isinstance(comparison, dict) and isinstance(comparison.get("records"), list):
            rows = "".join(
                f"<tr><td>{esc(row['name'])}</td><td>{esc(row['boards'])}</td>"
                f"<td>{esc(row['placed'])}</td><td>{esc(row.get('utilization', '—'))}</td>"
                f"<td>{esc(row.get('note', ''))}</td></tr>"
                for row in comparison["records"])
            comparison_html = ("<h2>对标观察记录</h2><table><tr><th>程序</th><th>板数</th>"
                               "<th>已排件数</th><th>利用率</th><th>证据范围</th></tr>" + rows + "</table>"
                               + f"<p>{esc(comparison.get('scope', ''))}</p>")
        else:
            comparison_html = f"<h2>外部观察记录</h2><pre>{esc(json.dumps(comparison, ensure_ascii=False, indent=2))}</pre>"
    if summary["coordinateMode"] == "stored-binary64-with-declared-tolerance":
        audit_description = (f"后处理的实际位姿含非格点坐标。对 JSON 回读得到的每个实际 double 使用 Fraction.from_float，"
            f"结合原始 DXF 十进制顶点做精确有理数运算。按输入 Tolerance={problem['tolerance']:g} mm 检查板界、"
            "凸材料分离和普通间距。共边须有长于该容差的共享线段，其共线偏差仅允许 64ε×坐标尺度的浮点舍入界，"
            "同时受声明容差上限约束。原生及报告位姿均保留原值。GEOS 交叉核验使用相同声明容差。"
            "该结果属于实际浮点位姿的容差复核。")
    else:
        audit_description = ("输出位姿能从 n/scale 原样浮点往返，整数分离轴验证材料不重叠，整数与有理数验证板界、"
            "正长度共边、非共边最短距离。此路径不使用几何容差放宽距离和板界，提供严格格点证书。")
    construction = summary.get("constructionComparison")
    construction_html = ""
    before_image_html = ""
    if construction is not None:
        audited_before = summary.get("constructionAudit")
        if audited_before is not None:
            before_image_html = ("<h2>同次构造阶段的完整布局</h2>"
                f"<p>{audited_before['boards']} 张板，{audited_before['placed']} 件。该阶段独立复核采用 "
                f"{esc(audited_before['coordinateMode'])}。</p>"
                '<a href="layout-before.png"><img src="layout-before.png" alt="同次原生构造阶段的全部板材与原件"></a>'
                '<p><a href="construction-exact-audit.json">构造阶段算术核验</a> · '
                '<a href="construction-dxf-geos-audit.json">构造阶段 DXF / GEOS 核验</a></p>')
        metric_rows = ""
        for row in construction["perStock"]:
            first, last = row["before"], row["after"]
            if first is None or last is None:
                continue
            metric_rows += (f"<tr><td>{esc(row['stock'])} / {row['stockInstance']}</td>"
                f"<td>{first['materialEnvelopeAreaMm2']:.3f} → {last['materialEnvelopeAreaMm2']:.3f}</td>"
                f"<td>({first['areaWeightedCentroidXmm']:.3f}, {first['areaWeightedCentroidYmm']:.3f}) → "
                f"({last['areaWeightedCentroidXmm']:.3f}, {last['areaWeightedCentroidYmm']:.3f})</td>"
                f"<td>{first['certifiedBoundaryStripAreaLowerBoundMm2']:.3f} → {last['certifiedBoundaryStripAreaLowerBoundMm2']:.3f}</td></tr>")
        construction_html = (f"<h2>构造与后处理的定量比较</h2><p>按相同库存 ID 与实例对照，"
            f"实际移动 {construction['movedOriginalInstances']} 件。材料包络总面积 "
            f"{construction['envelopeAreaBeforeMm2']:.3f} → {construction['envelopeAreaAfterMm2']:.3f} mm²。"
            "边界空白条是根据实际材料边界可证明的空矩形面积下界。固定板数和零件时整板空白面积保持相同。</p>"
            "<table><tr><th>库存身份</th><th>材料包络面积 mm²</th><th>面积加权材料质心 mm</th>"
            f"<th>可证边界空白条 mm²</th></tr>{metric_rows}</table>"
            f"<pre>{esc(chr(10).join(construction['axisDiagnostics']))}</pre>")
    document = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)}</title><style>body{{max-width:1400px;margin:30px auto;padding:0 26px;font:17px/1.7 "Microsoft YaHei",sans-serif;color:#243d4b}}h1,h2{{color:#153447}}h1{{font-size:30px}}table{{border-collapse:collapse;width:100%;margin:20px 0}}th,td{{padding:9px 12px;border:1px solid #cbd5db;text-align:left}}th{{background:#edf3f6}}img{{width:100%;height:auto}}a{{color:#166789}}pre{{white-space:pre-wrap;overflow-wrap:anywhere;background:#f3f6f7;padding:16px}}.lead{{font-size:24px;font-weight:bold}}@media print{{body{{margin:0;font-size:11pt}}tr,img{{break-inside:avoid}}}}</style>
<h1>{esc(title)}</h1><p class="lead">{exact['boards']} 张板，{exact['placed']} / {exact['required']} 件，剩余 {exact['remaining']} 件，整板利用率 {exact['utilization'] * 100:.2f}%。</p>
<p>{lower_statement}</p><h2>参数与结果</h2><table>{params}</table>
{before_image_html}<h2>最终完整排样图</h2><p>各板以同一物理比例横向展示，橙线标出核验通过的共边。</p>
<a href="layout-all.png"><img src="layout-all.png" alt="当前任务全部板材与原始零件 ID"></a>
<table><tr><th>板号</th><th>原库存身份</th><th>尺寸 mm</th><th>件数</th><th>整板利用率</th><th>原件身份</th></tr>{board_rows}</table>
{construction_html}{mechanism_html}<h2>独立复核范围</h2><p>逐一验证原始 DXF 的 SHA-256 和顶点与输入快照、原生回显一致，再验证原件身份、数量和许可角度。{audit_description}</p>
<p>共边是一种可用接触方式，没有作为求解奖励。结果仅说明几何排样能力，不包含生产切割刀路。</p>
<p><a href="{esc(relative(native_path))}">原生输入与结果</a> · <a href="{esc(relative(snapshot_path))}">DXF 输入快照</a> · <a href="delivery-summary.json">交付指标</a> · <a href="exact-audit.json">整数与有理数核验</a> · <a href="dxf-geos-audit.json">独立 DXF / GEOS 核验</a></p>
<p>{provenance_links}</p>
<h2>搜索状态</h2><pre>{esc(diagnostics)}</pre>{comparison_html}</html>'''
    (out / "index.html").write_text(document, encoding="utf-8")


def self_test():
    problem = dict(parts=[dict(id="arbitrary-part", quantity=2, material="metal", freeRotation=False,
                               allowedAngles=[0])],
                   stocks=[dict(id="arbitrary-stock", width=8, height=6, quantity=1, material="metal")],
                   partGap=.2, edgeMargin=.2, tolerance=1e-7, allowCommonEdges=False)
    parts = {row["id"]: row for row in problem["parts"]}
    polygons = {"arbitrary-part": [(0, 0), (20, 0), (20, 20), (0, 20)]}
    decimals = {"arbitrary-part": [(Fraction(x, 10), Fraction(y, 10)) for x, y in polygons["arbitrary-part"]]}
    native = dict(problem=problem, result=dict(
        layouts=[dict(stock="arbitrary-stock", instance=0, width=8, height=6, placements=[
            dict(part="arbitrary-part", instance=0, x=.2, y=.2, angle=0),
            dict(part="arbitrary-part", instance=1, x=2.4, y=.2, angle=0)])],
        remaining=[], valid=True, complete=True, placed=2, boards=1, remainingCount=0),
        validation=dict(valid=True, complete=True, placed=2))
    controls = []
    strict, boards = exact_audit(native, problem, 10, parts, polygons)
    require(strict["exactIntegerAndRationalAudit"], "Strict lattice certificate was weakened")
    geos_audit(boards, decimals, strict["auditCoordinateScale"], problem, strict)
    controls.append("strict lattice path retained")
    continuous = deepcopy(native)
    poses = continuous["result"]["layouts"][0]["placements"]
    poses[0].update(x=.234567, y=.345678)
    poses[1].update(x=2.5, y=.345678)
    audited, boards = exact_audit(continuous, problem, 10, parts, polygons)
    require(not audited["exactIntegerAndRationalAudit"] and audited["geometryToleranceMm"] == problem["tolerance"],
            "Actual stored non-grid output was mislabeled as an integer certificate")
    geos_audit(boards, decimals, audited["auditCoordinateScale"], problem, audited)
    controls.append("actual non-grid double positions accepted with declared tolerance")
    seam = deepcopy(continuous)
    common_problem = deepcopy(problem)
    common_problem["allowCommonEdges"] = True
    seam["result"]["layouts"][0]["placements"][1]["x"] = 2.234567
    audited, boards = exact_audit(seam, common_problem, 10, parts, polygons)
    require(audited["commonEdgePairs"] == 1, "Roundoff-only coincident line was rejected")
    geos_audit(boards, decimals, audited["auditCoordinateScale"], common_problem, audited)
    controls.append("real shared line with only double roundoff accepted")
    for name, x, allow_common in (("ordinary gap shortage beyond tolerance rejected", 2.434467, False),
                                  ("material overlap rejected", 2.1, True),
                                  ("near common edge rejected", 2.234577, True),
                                  ("board margin violation rejected", -.1, False)):
        candidate, rules = deepcopy(continuous), deepcopy(problem)
        candidate["result"]["layouts"][0]["placements"][1]["x"] = x
        rules["allowCommonEdges"] = allow_common
        try:
            exact_audit(candidate, rules, 10, parts, polygons)
        except ValueError:
            controls.append(name)
        else:
            raise ValueError(f"Negative independent control incorrectly passed: {name}")
    continuous["constructionResult"] = deepcopy(continuous["result"])
    continuous["constructionResult"]["layouts"][0]["placements"][1]["x"] = 3.5
    comparison = construction_comparison(continuous, decimals)
    require(comparison["movedOriginalInstances"] == 1
            and comparison["envelopeAreaAfterMm2"] < comparison["envelopeAreaBeforeMm2"],
            "Same-stock original-material comparison failed")
    controls.append("construction/final same-stock material metrics checked")
    trace_native = deepcopy(native)
    trace_native["result"]["diagnostics"] = [
        "sheet-count.refill exchange: verified=1; working-accepted=1; changed-members=3; window=2; best-updated=0; working-boards=6; best-boards=5",
        "sheet-count.refill: completed; plans=2; perturbations=2; exchanges=1; exchanged-members=3; working-boards=6; best-boards=5; returned=best",
        "pipeline.rejected=sheet-count.refill | sheet-count.refill exchange: verified=1; changed-members=2; window=3; working-accepted=1; best-improved=0; working-boards=7; best-boards=6",
        "pipeline.rejected=sheet-count.refill | sheet-count.refill exchange subproblem | sheet-count.perturb: verified=1; changed-members=99; working-accepted=1",
        "pipeline.rejected=sheet-count.refill | sheet-count.refill: completed; perturbations=4; exchanges=1; working-boards=7; best-boards=6; returned=best",
        "pipeline.post=sheet-count.refill: rejected",
        "sheet-count.refill: scope=full-cohort; window=9; members=40; target-max=8; reconstructed=0",
        "pipeline.rejected=sheet-count.refill | sheet-count.refill exchange-attempt: scope=full-cohort; window=9; members=40; target-max=9; executed=1",
        "sheet-count.refill exchange-attempt: scope=full-cohort; window=10; members=129; target-max=10; skipped=solver-instance-scope",
    ]
    trace = search_mechanism(trace_native)
    require(trace["publishedFinalBoards"] == 1 and trace["publishedStageCounts"]["working-boards"] == 6
            and trace["lastInternalStageSummary"]["publicationContext"] == "rejected-stage-candidate"
            and len(trace["verifiedWorkingExchangeEvents"]) == 2
            and trace["publishedStageRetentionMarker"] == "best",
            "Internal, rejected or subproblem search logs were confused with the published layout")
    controls.append("working/best, rejected-stage and subproblem search records remain distinct")
    require(len(trace["fullCohortSearchAttempts"]) == 2 and trace["maximumGlobalWindowBoards"] == 9
            and trace["maximumGlobalReleasedInstances"] == 40,
            "Executed full-cohort attempts were confused with skipped models or accepted layouts")
    controls.append("executed full-cohort models and skipped resource scopes remain distinct")
    return dict(passed=True, controls=controls)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", nargs="?", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dependency-dir", action="append", type=Path, default=[])
    parser.add_argument("--title", default="板材排样任务报告")
    parser.add_argument("--comparison", type=Path)
    args = parser.parse_args()
    for path in args.dependency_dir:
        sys.path.insert(0, str(path.resolve()))
    if args.self_test:
        print(json.dumps(self_test(), ensure_ascii=False, indent=2))
        return
    if args.native is None or args.snapshot is None:
        parser.error("native and --snapshot are required for task reporting")
    native_path, snapshot_path = args.native.resolve(), args.snapshot.resolve()
    native, snapshot = read(native_path), read(snapshot_path)
    require(native.get("format") == "ICAX_NESTING_TASK_RESULT_1" and native.get("strategy") == "sheet-count",
            "Expected the current native cold-start sheet-count CLI result")
    diagnostic_text = "\n".join(native["result"].get("diagnostics", []))
    require("prior-layout-input=0" in diagnostic_text and "common-edge-objective=0" in diagnostic_text,
            "Native result does not declare the current-input-only, no-common-edge-reward contract")
    out = native_path.parent
    started = time.perf_counter()
    problem, scale, parts, polygons, decimals, sources = recover_inputs(native, snapshot, snapshot_path)
    exact, boards = exact_audit(native, problem, scale, parts, polygons)
    exact.update(originalDxfHashesMatch=True, originalDxfContoursMatch=True, sources=sources,
                 snapshotSha256=sha(snapshot_path), nativeResultSha256=sha(native_path), coordinateScale=scale)
    audit_scale = exact["auditCoordinateScale"]
    geos = geos_audit(boards, decimals, audit_scale, problem, exact)
    before_exact, before_boards, before_geos = None, None, None
    captured = native.get("constructionResult")
    if captured is not None and captured.get("valid"):
        before_native = dict(native, result=captured, validation=native.get("constructionValidation"))
        require(before_native["validation"] is not None, "Valid construction result is missing its native validation")
        before_exact, before_boards = exact_audit(before_native, problem, scale, parts, polygons)
        before_exact.update(stage="constructionResult", originalDxfHashesMatch=True, originalDxfContoursMatch=True,
                            nativeResultSha256=sha(native_path), snapshotSha256=sha(snapshot_path))
        before_geos = geos_audit(before_boards, decimals, before_exact["auditCoordinateScale"], problem, before_exact)
    verification_seconds = time.perf_counter() - started
    write(out / "exact-audit.json", exact)
    write(out / "dxf-geos-audit.json", geos)
    summary = {k: v for k, v in exact.items() if k not in ("pairProofs", "perBoard", "sources")}
    summary.update(independentDxfGeosPassed=True, nativeValidationPassed=True, productionDeployed=False,
                   oldLayoutsUsed=False, commonEdgeOptimizationReward=False,
                   wholeJobWallSeconds=native["wholeJobWallSeconds"], independentVerificationSeconds=verification_seconds,
                   nativeSettings=native["settings"], report="index.html", image="layout-all.png")
    construction = construction_comparison(native, decimals)
    if construction is not None:
        summary["constructionComparison"] = construction
    summary["searchMechanism"] = search_mechanism(native)
    if before_exact is not None:
        summary["constructionAudit"] = {field: before_exact[field] for field in (
            "passed", "coordinateMode", "exactIntegerAndRationalAudit", "geometryToleranceMm", "boards", "placed", "complete")}
        summary["constructionAudit"]["independentDxfGeosPassed"] = True
        summary["constructionAudit"]["image"] = "layout-before.png"
        write(out / "construction-exact-audit.json", before_exact)
        write(out / "construction-dxf-geos-audit.json", before_geos)
        render(out / "layout-before.png", args.title + " · 同次构造阶段", before_boards, before_exact,
               problem, before_exact["auditCoordinateScale"], parts)
    render(out / "layout-all.png", args.title, boards, exact, problem, audit_scale, parts)
    report(out, native_path, snapshot_path, native, args.title, summary, exact, sources,
           read(args.comparison) if args.comparison else None)
    write(out / "delivery-summary.json", summary)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print("REPORT", out / "index.html")


if __name__ == "__main__":
    main()
