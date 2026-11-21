"""Export original demand geometry as DXF, without invoking a nesting solver.

Per-specification files retain source-local coordinates.  The combined file
only translates complete original copies into separated display cells.  The
manifest records every translation and source hash.  There is no stock outline,
text geometry, rotation, mirroring, simplification, or coordinate scaling.
"""
from __future__ import annotations

import argparse
import csv
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import re
import shlex


def number(value):
    """Decimal text that round-trips the original binary64 value."""
    return format(value, ".17g") if isinstance(value, float) else str(value)


def decimal(value):
    return Decimal(number(value))


def translated(value, offset):
    with localcontext() as context:
        context.prec = 80
        return format(decimal(value)+offset, "f")


def read_original(path):
    path = Path(path)
    content = path.read_text(encoding="utf-8-sig")
    if content.lstrip().startswith("{"):
        document = json.loads(content)
        problem = document.get("problem", document)
        if "parts" not in problem:
            raise ValueError("JSON does not contain original parts")
        return problem
    tokens = iter(shlex.split(content, posix=True))
    first = next(tokens)
    real = lambda: float(next(tokens))
    integer = lambda: int(next(tokens))
    point = lambda: [real(), real()]
    if first in tuple(f"ICAX_NESTING_PROBLEM_{version}" for version in range(1, 5)):
        version = int(first.rsplit("_", 1)[1])
        count, stock_count = integer(), integer()
        problem = dict(parts=[], stocks=[], partGap=real(), edgeMargin=real(), tolerance=real())
        problem["allowCommonEdges"] = bool(integer()) if version >= 2 else False
        for _ in range(count):
            part = dict(id=next(tokens), quantity=integer(), material=next(tokens), freeRotation=bool(integer()))
            part["allowMirror"] = bool(integer()) if version >= 4 else False
            part["allowedAngles"] = [real() for _ in range(integer())]
            radius, center, outer_count, hole_count = real(), point(), integer(), integer()
            part["geometry"] = dict(radius=radius, center=center, outer=[point() for _ in range(outer_count)],
                                    holes=[[point() for _ in range(integer())] for _ in range(hole_count)])
            if version >= 3:
                part["geometry"]["commonEdgeRings"] = [bool(integer()) for _ in range(integer())]
            problem["parts"].append(part)
        for _ in range(stock_count):
            problem["stocks"].append(dict(id=next(tokens), width=real(), height=real(),
                                           quantity=integer(), cost=real(), material=next(tokens)))
    elif first == "h=":
        height = real()
        if next(tokens) != "n=":
            raise ValueError("Invalid DAT part count marker")
        count = integer()
        problem = dict(parts=[], stocks=[], sourceStockHeight=height)
        for index in range(count):
            if next(tokens) != "d=":
                raise ValueError("Invalid DAT demand marker")
            quantity = integer()
            if next(tokens) != "m=":
                raise ValueError("Invalid DAT angle marker")
            angles = [real() for _ in range(integer())]
            if next(tokens) != "v=":
                raise ValueError("Invalid DAT vertex marker")
            problem["parts"].append(dict(id=f"P{index:03d}", quantity=quantity, material="m",
                freeRotation=False, allowedAngles=angles,
                geometry=dict(radius=0, center=[0, 0], outer=[point() for _ in range(integer())], holes=[])))
    else:
        raise ValueError("Supported originals are PublicBench DAT, Problem v1-v4, or original Problem JSON")
    if next(tokens, None) is not None:
        raise ValueError("Unexpected trailing original input tokens")
    return problem


def safe_name(value):
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(value)).strip("._")
    return result[:96] or "part"


def bounds(geometry):
    radius = geometry.get("radius", 0)
    if radius > 0:
        x, y = map(decimal, geometry.get("center", [0, 0]))
        r = decimal(radius)
        return x-r, y-r, x+r, y+r
    outer = geometry["outer"]
    xs, ys = [decimal(p[0]) for p in outer], [decimal(p[1]) for p in outer]
    return min(xs), min(ys), max(xs), max(ys)


def material_area(geometry):
    if geometry.get("radius", 0) > 0:
        return math.pi * geometry["radius"] ** 2
    def ring_area(ring):
        return abs(sum(decimal(a[0])*decimal(b[1])-decimal(b[0])*decimal(a[1])
                       for a, b in zip(ring, ring[1:]+ring[:1]))) / 2
    return float(ring_area(geometry["outer"])-sum(map(ring_area, geometry.get("holes", [])), Decimal(0)))


def check_original(problem):
    identifiers = set()
    for part in problem["parts"]:
        if part["id"] in identifiers:
            raise ValueError("Duplicate original part ID")
        identifiers.add(part["id"])
        if type(part["quantity"]) is not int or part["quantity"] < 0:
            raise ValueError("Original quantity must be a nonnegative integer")
        geometry = part["geometry"]
        radius = geometry.get("radius", 0)
        if not math.isfinite(radius) or radius < 0:
            raise ValueError("Invalid original radius")
        if radius > 0:
            if geometry.get("outer") or geometry.get("holes"):
                raise ValueError("Circle must not also contain polygon rings")
            points = [geometry.get("center", [0, 0])]
        else:
            if len(geometry.get("outer", [])) < 3 or any(len(hole) < 3 for hole in geometry.get("holes", [])):
                raise ValueError("Original polygon ring has fewer than three vertices")
            points = geometry["outer"] + [p for hole in geometry.get("holes", []) for p in hole]
        if any(len(p) != 2 or not all(math.isfinite(value) for value in p) for p in points):
            raise ValueError("Invalid original point")
        if material_area(geometry) <= 0:
            raise ValueError("Original material area is not positive")


def dxf_text(copies, insertion_units=4):
    """copies contains (original geometry, exact decimal X/Y translation)."""
    tags = []
    def tag(code, value):
        tags.extend((str(code), str(value)))
    tag(0, "SECTION"); tag(2, "HEADER")
    tag(9, "$ACADVER"); tag(1, "AC1015")
    tag(9, "$INSUNITS"); tag(70, insertion_units)
    tag(9, "$MEASUREMENT"); tag(70, 1)
    tag(0, "ENDSEC")
    tag(0, "SECTION"); tag(2, "TABLES")
    tag(0, "TABLE"); tag(2, "LAYER"); tag(70, 1)
    tag(0, "LAYER"); tag(100, "AcDbSymbolTableRecord"); tag(100, "AcDbLayerTableRecord")
    tag(2, "0"); tag(70, 0); tag(62, 7); tag(6, "CONTINUOUS")
    tag(0, "ENDTAB"); tag(0, "ENDSEC")
    tag(0, "SECTION"); tag(2, "BLOCKS"); tag(0, "ENDSEC")
    tag(0, "SECTION"); tag(2, "ENTITIES")
    for geometry, dx, dy in copies:
        if geometry.get("radius", 0) > 0:
            tag(0, "CIRCLE"); tag(100, "AcDbEntity"); tag(8, "0"); tag(100, "AcDbCircle")
            center = geometry.get("center", [0, 0])
            tag(10, translated(center[0], dx)); tag(20, translated(center[1], dy)); tag(30, 0)
            tag(40, number(geometry["radius"]))
        else:
            for ring in [geometry["outer"], *geometry.get("holes", [])]:
                tag(0, "LWPOLYLINE"); tag(100, "AcDbEntity"); tag(8, "0"); tag(100, "AcDbPolyline")
                tag(90, len(ring)); tag(70, 1); tag(38, 0)
                for x, y in ring:
                    tag(10, translated(x, dx)); tag(20, translated(y, dy))
    tag(0, "ENDSEC"); tag(0, "EOF")
    return "\n".join(tags) + "\n"


def export_original(source, destination, case_id=None, separation=10, reference=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    problem = read_original(source)
    check_original(problem)
    case_id = case_id or source.stem
    parts = problem["parts"]
    count = sum(part["quantity"] for part in parts)
    columns = max(1, math.ceil(math.sqrt(count)))
    boxes = [bounds(part["geometry"]) for part in parts]
    cell_w = max(box[2]-box[0] for box in boxes) + decimal(separation)
    cell_h = max(box[3]-box[1] for box in boxes) + decimal(separation)
    # Integer translations preserve source decimal coordinate differences.
    cell_w, cell_h = Decimal(math.ceil(cell_w)), Decimal(math.ceil(cell_h))
    files, copies, instances, records = {}, [], [], []
    entity_index = 0
    for index, (part, box) in enumerate(zip(parts, boxes)):
        filename = f"parts/{index:03d}_{safe_name(part['id'])}_qty{part['quantity']}.dxf"
        files[filename] = dxf_text([(part["geometry"], Decimal(0), Decimal(0))])
        geometry = part["geometry"]
        entities_per_copy = 1 if geometry.get("radius", 0) > 0 else 1+len(geometry.get("holes", []))
        entity_roles = ["outer"]+[f"hole[{index}]" for index in range(entities_per_copy-1)]
        area = material_area(geometry)
        record = dict(id=part["id"], quantity=part["quantity"], file=filename,
            sourceGeometry=geometry, material=part.get("material", ""),
            sourceFreeRotation=part.get("freeRotation"), sourceAllowedAngles=part.get("allowedAngles", []),
            sourceAllowMirror=part.get("allowMirror", False),
            bounds=[str(value) for value in box], materialArea=area, entitiesPerCopy=entities_per_copy, entityRoles=entity_roles)
        if reference:
            usable_area = (reference["width"]-2*reference["edgeMargin"])*(reference["height"]-2*reference["edgeMargin"])
            record["largerThanReferenceUsableArea"] = area > usable_area
            usable_diagonal = math.hypot(reference["width"]-2*reference["edgeMargin"], reference["height"]-2*reference["edgeMargin"])
            outer = geometry.get("outer", [])
            longest = max((math.hypot(a[0]-b[0], a[1]-b[1]) for a, b in zip(outer, outer[1:]+outer[:1])), default=0)
            record["longestOuterSegment"] = longest
            record["largerThanReferenceUsableDiagonal"] = longest > usable_diagonal
        records.append(record)
        for instance in range(part["quantity"]):
            ordinal = len(instances)
            dx = Decimal((ordinal % columns))*cell_w - Decimal(math.floor(box[0]))
            dy = Decimal((ordinal // columns))*cell_h - Decimal(math.floor(box[1]))
            copies.append((geometry, dx, dy))
            instances.append(dict(partID=part["id"], instance=instance, translation=[str(dx), str(dy)],
                angleDegrees=0, mirror=False, scale=1, entityStart=entity_index, entityCount=entities_per_copy, entityRoles=entity_roles))
            entity_index += entities_per_copy
    files["source_parts.dxf"] = dxf_text(copies)
    manifest = dict(schemaVersion=1, caseID=case_id,
        source=dict(path=str(source), sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    unitDeclaration="not present in source unless explicitly recorded there"),
        export=dict(combinedFile="source_parts.dxf", insertionUnits=4, insertionUnitName="mm",
                    interpretation="one unchanged source coordinate unit is interpreted as one millimetre",
                    coordinateScale=1, rotationsApplied=False, mirrorsApplied=False,
                    geometryModified=False, demandCopies=count, specifications=len(parts), entities=entity_index,
                    placementMeaning="separated original copies for import; not a nesting result",
                    cellWidth=str(cell_w), cellHeight=str(cell_h), columns=columns),
        referenceConditions=reference, parts=records, instances=instances)
    files["manifest.json"] = json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False)+"\n"
    import io
    csv_file = io.StringIO(newline="")
    writer = csv.writer(csv_file)
    writer.writerow(["part_id", "quantity", "original_dxf", "material_area", "width", "height", "holes", "radius", "larger_than_reference_area", "outer_segment_longer_than_reference_diagonal"])
    for part, box in zip(records, boxes):
        geometry = part["sourceGeometry"]
        writer.writerow([part["id"], part["quantity"], part["file"], part["materialArea"],
            box[2]-box[0], box[3]-box[1], len(geometry.get("holes", [])), geometry.get("radius", 0), part.get("largerThanReferenceUsableArea", ""), part.get("largerThanReferenceUsableDiagonal", "")])
    files["parts.csv"] = "\ufeff"+csv_file.getvalue()
    files["README.txt"] = (f"{case_id} 原始零件 DXF\n\n"
        f"直接导入 source_parts.dxf：已展开全部需求，共 {count} 件、{len(parts)} 种。\n"
        "联合图中的零件只作整数平移分隔，不是排样方案；不含板框或文字图元。\n"
        "parts 目录每种原件一份，保持原始局部坐标。单种数量见文件名和 parts.csv。\n"
        "原坐标逐值 1:1 写入，没有缩放、旋转、镜像、简化或删除孔洞。\n"
        "DXF 插入单位设为毫米（INSUNITS=4）；原始公开数据没有明确单位时，按 1 来源单位=1mm 解释，不能视为作者的单位声明。\n"
        "manifest.json 保留源文件 SHA-256、原始几何和联合图每个实例的平移，便于独立复核。\n"
        + ("\n对照条件："+json.dumps(reference, ensure_ascii=False)+"\n"
           "单件材料面积超过可用板面积的规格已在 parts.csv/manifest.json 标记；它们在任何角度都放不进该板，不是排样成绩。\n" if reference else ""))
    impossible = [(part["id"], "面积超板" if part.get("largerThanReferenceUsableArea") else "单条直边超可用板对角线")
                  for part in records if part.get("largerThanReferenceUsableArea") or part.get("largerThanReferenceUsableDiagonal")]
    if impossible:
        files["README.txt"] += "本参考板下已有单件不可装入证据："+"、".join(f"{part}（{reason}）" for part, reason in impossible)+"。\n"
    for filename in files:
        if (destination/filename).exists():
            raise FileExistsError(f"Refusing to overwrite existing export: {destination/filename}")
    for filename, content in files.items():
        path = destination/filename
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--case-id")
    parser.add_argument("--separation", type=float, default=10)
    parser.add_argument("--reference-stock", type=float, nargs=2)
    parser.add_argument("--reference-margin", type=float, default=0)
    parser.add_argument("--reference-gap", type=float, default=0)
    parser.add_argument("--reference-common-edges", action="store_true")
    parser.add_argument("--reference-free-rotation", action="store_true")
    args = parser.parse_args()
    if not math.isfinite(args.separation) or args.separation <= 0:
        parser.error("separation must be positive and finite")
    reference = dict(width=args.reference_stock[0], height=args.reference_stock[1],
        edgeMargin=args.reference_margin, partGap=args.reference_gap,
        allowCommonEdges=args.reference_common_edges, freeRotation=args.reference_free_rotation) if args.reference_stock else None
    result = export_original(args.source, args.destination, args.case_id, args.separation, reference)
    print(json.dumps(dict(case=result["caseID"], specifications=result["export"]["specifications"],
        demand=result["export"]["demandCopies"], output=str(args.destination.resolve())), ensure_ascii=False))


if __name__ == "__main__":
    main()
