"""Import a millimetre DXF task into the native nesting interchange.

This adapter imports one closed, straight POLYLINE/LWPOLYLINE per part file.
Unsupported entities, bulges, widths and non-planar geometry are rejected;
coordinates are never scaled, simplified or replaced by envelopes.
The solver receives only the resulting original Problem, never old layouts.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path


def straight_contour(path: Path) -> list[list[float]]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    if len(lines) % 2:
        raise ValueError(f"Incomplete DXF group pair: {path}")
    tags = [(int(lines[i].strip()), lines[i + 1].strip()) for i in range(0, len(lines), 2)]
    records: list[tuple[str, list[tuple[int, str]]]] = []
    current = None
    for code, value in tags:
        if code == 0:
            current = (value, [])
            records.append(current)
        elif current is not None:
            current[1].append((code, value))

    def flat(data, kind):
        for code, value in data:
            material_width_codes = (40, 41, 42, 43) if kind in ("POLYLINE", "VERTEX", "LWPOLYLINE") else ()
            if code in (30, 38, 39, *material_width_codes) and float(value) != 0:
                raise ValueError(f"Unsupported thickness, elevation, width or bulge: {path}")
        data = dict(data)
        if any(float(data.get(code, default)) != float(default)
               for code, default in ((210, "0"), (220, "0"), (230, "1"))):
            raise ValueError(f"Unsupported extrusion direction: {path}")

    active = False
    contours = []
    poly = None
    for kind, data in records:
        f = dict(data)
        if kind == "SECTION":
            active = f.get(2) == "ENTITIES"
            continue
        if kind == "ENDSEC":
            if active and poly is not None:
                raise ValueError(f"Unterminated POLYLINE: {path}")
            active = False
            continue
        if not active:
            continue
        flat(data, kind)
        if kind == "POLYLINE":
            flags = int(f.get(70, "0"))
            if poly is not None or not flags & 1 or flags & ~129:
                raise ValueError(f"Expected closed planar POLYLINE: {path}")
            poly = []
        elif kind == "VERTEX":
            if poly is None or int(f.get(70, "0")) != 0:
                raise ValueError(f"Unsupported DXF vertex: {path}")
            poly.append([float(f[10]), float(f[20])])
        elif kind == "SEQEND":
            if poly is None:
                raise ValueError(f"Unmatched SEQEND: {path}")
            contours.append(poly)
            poly = None
        elif kind == "LWPOLYLINE":
            flags = int(f.get(70, "0"))
            if poly is not None or not flags & 1 or flags & ~129:
                raise ValueError(f"Expected closed LWPOLYLINE: {path}")
            points = []
            for code, value in data:
                if code == 10:
                    points.append([float(value)])
                elif code == 20:
                    if not points or len(points[-1]) != 1:
                        raise ValueError(f"Malformed LWPOLYLINE vertex: {path}")
                    points[-1].append(float(value))
            if any(len(p) != 2 for p in points) or int(f.get(90, str(len(points)))) != len(points):
                raise ValueError(f"Malformed LWPOLYLINE count: {path}")
            contours.append(points)
        else:
            raise ValueError(f"Unsupported DXF entity {kind}: {path}")
    if poly is not None or len(contours) != 1:
        raise ValueError(f"Expected exactly one closed contour per part file: {path}")
    contour = contours[0]
    if len(contour) > 1 and contour[-1] == contour[0]:
        contour = contour[:-1]
    if len(contour) < 3 or any(not math.isfinite(v) for p in contour for v in p):
        raise ValueError(f"Invalid contour: {path}")
    return contour


_material_module = None


def import_shape(path: Path, scale_to_mm=1, tolerance_mm=.01, connection_tolerance_mm=None, material_semantics=None):
    """Import actual closed material, preserving holes and all components.

    Curve tolerance is independent of the solver's exact-clearance tolerance.
    Multiple disconnected material components appear in ``geometries`` and
    ``geometry`` is then None; the caller must retain every component.
    """
    global _material_module
    if _material_module is None:
        spec = importlib.util.spec_from_file_location(
            "icax_dxf_material", Path(__file__).with_name("dxf_material.py"))
        _material_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_material_module)
    return _material_module.import_material(path, scale_to_mm, tolerance_mm, connection_tolerance_mm, material_semantics)


def prepare(manifest_path: Path):
    task = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if task.get("units") != "mm":
        raise ValueError("Task must explicitly declare units=mm")
    parts, sources = [], []
    for row in task["parts"]:
        path = (manifest_path.parent / row["file"]).resolve()
        quantity = row["quantity"]
        if type(quantity) is not int or quantity < 0:
            raise ValueError("Part quantity must be a nonnegative integer")
        allow_mirror=row.get("allowMirror",task.get("rules",{}).get("allowMirror",False))
        if type(allow_mirror) is not bool:raise ValueError("allowMirror must be boolean")
        parts.append(dict(id=row["id"], quantity=quantity, material=row["material"],
                          freeRotation=row.get("freeRotation", False), allowedAngles=row["allowedAngles"],
                          allowMirror=allow_mirror,
                          geometry=dict(radius=0, center=[0, 0], outer=straight_contour(path), holes=[])))
        sources.append(dict(id=row["id"], file=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    if len({p["id"] for p in parts}) != len(parts):
        raise ValueError("Duplicate original part IDs")
    rules = task["rules"]
    common = rules["allowCommonEdges"]
    if type(common) is not bool:
        raise ValueError("allowCommonEdges must be boolean")
    problem = dict(parts=parts, stocks=task["stocks"], partGap=rules["partGap"],
                   edgeMargin=rules["edgeMargin"], tolerance=rules.get("tolerance", 1e-7),
                   allowCommonEdges=common)
    return problem, sources


def export(problem, path: Path):
    number = lambda v: format(float(v), ".17g")
    quote = lambda s: json.dumps(s, ensure_ascii=False)
    ring_eligibility = any(p["geometry"].get("commonEdgeRings") for p in problem["parts"])
    if any(type(p.get("allowMirror",False)) is not bool for p in problem["parts"]):
        raise ValueError("allowMirror must be boolean")
    mirror_permissions=any(p.get("allowMirror",False) for p in problem["parts"])
    version="ICAX_NESTING_PROBLEM_4" if mirror_permissions else "ICAX_NESTING_PROBLEM_3" if ring_eligibility else "ICAX_NESTING_PROBLEM_2"
    rows = [version, f'{len(problem["parts"])} {len(problem["stocks"])}',
            " ".join(number(problem[k]) for k in ("partGap", "edgeMargin", "tolerance"))
            + f' {int(problem["allowCommonEdges"])}']
    for p in problem["parts"]:
        if any(c in p["id"] + p["material"] for c in "\n\r\\"):
            raise ValueError("Newline/backslash not supported by text interchange identities")
        angles, g = p["allowedAngles"], p["geometry"]
        permission=f' {int(p.get("allowMirror",False))}' if mirror_permissions else ''
        rows.append(f'{quote(p["id"])} {p["quantity"]} {quote(p["material"])} {int(p["freeRotation"])}{permission} {len(angles)} '
                    + " ".join(map(number, angles)))
        rows.append(f'{number(g["radius"])} {number(g["center"][0])} {number(g["center"][1])} {len(g["outer"])} {len(g["holes"])}')
        rows.extend(" ".join(map(number, point)) for point in g["outer"])
        for ring in g["holes"]:
            rows.append(str(len(ring)))
            rows.extend(" ".join(map(number, point)) for point in ring)
        if ring_eligibility or mirror_permissions:
            flags = g.get("commonEdgeRings", [])
            if any(type(value) is not bool for value in flags) or (flags and (g["radius"] > 0 or len(flags) != 1 + len(g["holes"]))):
                raise ValueError("Common-edge eligibility must match polygon outer/hole rings")
            rows.append(str(len(flags)) + (" " + " ".join(str(int(value)) for value in flags) if flags else ""))
    for s in problem["stocks"]:
        rows.append(f'{quote(s["id"])} {number(s["width"])} {number(s["height"])} {s["quantity"]} {number(s["cost"])} {quote(s["material"])}')
    with path.open("x", encoding="utf-8") as handle:
        handle.write("\n".join(rows) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--snapshot", type=Path, required=True)
    args = parser.parse_args()
    problem, sources = prepare(args.manifest.resolve())
    export(problem, args.output)
    with args.snapshot.open("x", encoding="utf-8") as handle:
        json.dump(dict(problem=problem, sources=sources, manifest=str(args.manifest.resolve())), handle,
                  ensure_ascii=False, indent=2, allow_nan=False)
    print(json.dumps(dict(types=len(problem["parts"]), required=sum(p["quantity"] for p in problem["parts"]),
                          stocks=problem["stocks"], rules={k: v for k, v in problem.items() if k not in ("parts", "stocks")}),
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
