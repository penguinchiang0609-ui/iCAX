"""Read the extracted textile collection without changing source geometry.

The 200-task archive is a subset of the 1000-task collection. Its DXFs may
fill gaps in the numbered packages; its tasks are not a second workload.
Only source mapping metadata establishes geometric aliases. No file name is
used to infer quantity, scale, rotation or geometric equivalence.
"""
from __future__ import annotations

from collections import Counter
import copy
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
from typing import Any


_METADATA_DIRECTORY = "00_metadata_and_mapping"
_SUBSET_DIRECTORY = "textile_benchmark_1953_converted_dxf_200_tasks"


def _read(path: Path) -> Any:
    with path.open(encoding="utf-8-sig") as source:
        return json.load(source)


def _relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return value if math.isfinite(value) else None


def _asset_path(directory: Path, requested: Any, root: Path) -> Path | None:
    if not isinstance(requested, str):
        return None
    relative = PurePosixPath(requested.replace("\\", "/"))
    if relative.is_absolute() or ".." in relative.parts or ":" in requested:
        return None
    candidate = directory.joinpath(*relative.parts).resolve()
    if candidate.is_relative_to(root) and candidate.is_file():
        return candidate
    return None


def build_tasks(root: Path) -> list[dict]:
    """Return source tasks and quantities from same-name extracted folders.

    ``issues`` contains missing/invalid demand, not a default plate policy.
    The caller decides how a roll or absent finite stock maps to its own PK
    settings. Source dimensions and unitless coordinates remain unchanged.
    """
    root = Path(root).resolve()
    sources = []
    for name in (_METADATA_DIRECTORY, _SUBSET_DIRECTORY):
        directory = root / name
        if (directory / "jobs_with_quantities.json").is_file():
            sources.append(directory)
    if not sources:
        return []

    # Prefer the numbered packages, then the standalone subset. Their DXF
    # headers differ even when their represented source contours are equal.
    packages = sorted(
        (directory for directory in root.iterdir()
         if directory.is_dir() and "textile_dxf_" in directory.name),
        key=lambda directory: directory.name,
    )
    packages += [directory for directory in sources if directory not in packages]
    documents = []
    assets: dict[str, str] = {}
    for directory in sources:
        mapping_path = directory / "shape_mapping_validation.json"
        mapping = _read(mapping_path) if mapping_path.is_file() else []
        by_shape = {str(item["shape_hash"]): item for item in mapping}
        documents.append((directory, _read(directory / "jobs_with_quantities.json"), by_shape))
        for item in mapping:
            geometry = item.get("geometry_hash_translation_invariant")
            if not item.get("valid") or not geometry or geometry in assets:
                continue
            for package in packages:
                candidate = _asset_path(package, item.get("dxf"), root)
                if candidate is not None:
                    assets[str(geometry)] = _relative(candidate, root)
                    break

    tasks = {}
    for directory, jobs, mapping in documents:
        jobs_path = _relative(directory / "jobs_with_quantities.json", root)
        constraints_path = directory / "constraints.json"
        constraint_types: dict[str, Counter] = {}
        if constraints_path.is_file():
            for constraint in _read(constraints_path):
                task_key = str(constraint["tasks_index"])
                constraint_types.setdefault(task_key, Counter())[str(constraint.get("type", "unknown"))] += 1
        for job in jobs:
            task_key = str(job["tasks_index"])
            # Full metadata wins over its overlapping 200-task subset.
            if task_key in tasks:
                continue
            grouped = {}
            issues = []
            for demand in job["shape_quantities"]:
                shape_hash = str(demand["shape_hash"])
                record = mapping.get(shape_hash)
                quantity = demand.get("quantity")
                if isinstance(quantity, bool) or not isinstance(quantity, int) or quantity <= 0:
                    raise ValueError(f"Invalid source quantity in {jobs_path}, task {task_key}, shape {shape_hash}: {quantity!r}")
                geometry = record.get("geometry_hash_translation_invariant") if record else None
                valid = bool(record and record.get("valid") and demand.get("valid", True))
                actual_path = assets.get(str(geometry)) if valid and geometry else None
                key = actual_path or ("missing-geometry:" + str(geometry) if valid and geometry else "missing-shape:" + shape_hash)
                if key not in grouped:
                    grouped[key] = {
                        "sourcePath": actual_path,
                        "requestedDxf": demand.get("dxf"),
                        "requestedDxfs": [],
                        "quantity": 0,
                        "quantitySource": f"{jobs_path}#tasks_index={task_key}/shape_quantities",
                        "sourceShapeHashes": [],
                        "geometryHash": str(geometry) if geometry else None,
                    }
                part = grouped[key]
                part["quantity"] += quantity
                if shape_hash not in part["sourceShapeHashes"]:
                    part["sourceShapeHashes"].append(shape_hash)
                requested = demand.get("dxf")
                if requested and requested not in part["requestedDxfs"]:
                    part["requestedDxfs"].append(requested)
                if record is None:
                    issues.append(f"缺少形状映射：{shape_hash}，需求 {quantity} 件。")
                elif not valid:
                    reason = record.get("rejection_reason") or record.get("error") or "source marked invalid"
                    issues.append(f"源轮廓无效：{shape_hash}，需求 {quantity} 件；{reason}。")
                elif actual_path is None:
                    issues.append(f"缺少 DXF：{requested or shape_hash}，需求 {quantity} 件。")
            width = _number(job.get("sheet_width"))
            height = _number(job.get("sheet_length"))
            types = constraint_types.get(task_key, Counter())
            tasks[task_key] = {
                "id": f"textile-{task_key}",
                "label": f"纺织任务 {task_key}",
                "collection": "textile",
                "sourcePath": jobs_path,
                "sourceTaskIndex": task_key,
                "parts": list(grouped.values()),
                "sourceStock": {
                    "width": width,
                    "height": height,
                    "units": "unitless",
                    "kind": "sheet" if width is not None and width > 0 and height is not None and height > 0 else "roll",
                    "source": f"{jobs_path}#tasks_index={task_key}/sheet_width,sheet_length",
                },
                "sourceRules": {
                    "constraintsPath": _relative(constraints_path, root) if constraints_path.is_file() else None,
                    "partsPath": _relative(directory / "parts.json", root) if (directory / "parts.json").is_file() else None,
                    "sourceTaskIndex": task_key,
                    "constraintCount": sum(types.values()),
                    "constraintTypes": dict(types),
                    "rotationOverrideRequiresComparisonLabel": bool(types),
                    "coordinatePolicy": "Original unitless coordinates; no scale, rotation or reflection applied.",
                },
                "issues": issues,
            }
    return sorted(tasks.values(), key=lambda task: int(task["sourceTaskIndex"]))


def iter_source_contour_preparations(root: Path, catalog_tasks):
    """Return original-source input facts without writing files or placements.

    A missing converted DXF is not missing material when the supplied source
    record is complete. Only current textile ``missing-assets`` records are
    eligible. Existing ready inputs, stock conditions and source files are
    untouched. Coordinates come directly from the original integer arrays;
    only a repeated closing point is expressed by the native closed ring.
    """
    root = Path(root).resolve()
    tasks = [task for task in catalog_tasks
             if task.get("collection") == "textile" and task.get("status") == "missing-assets"]
    if not tasks:
        return

    directory = root / _METADATA_DIRECTORY
    files = {"sourceShapes": directory / "source_shapes.json",
             "mapping": directory / "shape_mapping_validation.json",
             "jobs": directory / "jobs_with_quantities.json"}
    data, identities = {}, {}
    def source_provenance():
        return {"path": _relative(files["sourceShapes"], root),
                "sha256": identities.get("sourceShapes", {}).get("sha256"),
                "files": copy.deepcopy(identities),
                "units": "unitless; interpreted as millimetres by the confirmed PK rule", "scaleToMm": 1,
                "coordinatePolicy": "Original integer coordinates; no scale, translation, rotation, reflection or simplification."}
    try:
        for name, path in files.items():
            raw = path.read_bytes()
            data[name] = json.loads(raw.decode("utf-8-sig"))
            if not isinstance(data[name], list):
                raise ValueError(f"Expected a source-record list: {path.name}")
            identities[name] = {"path": _relative(path, root), "sha256": hashlib.sha256(raw).hexdigest()}
        sources, mapping, jobs = {}, {}, {}
        for name, target, field in (("sourceShapes", sources, "shape_hash"),
                                     ("mapping", mapping, "shape_hash"), ("jobs", jobs, "tasks_index")):
            for record in data[name]:
                if not isinstance(record, dict) or field not in record:
                    raise ValueError(f"Malformed {name} source record")
                key = str(record[field])
                if key in target:
                    raise ValueError(f"Duplicate {name} source key: {key}")
                target[key] = record
    except (OSError, UnicodeError, ValueError) as error:
        status = "source-unavailable" if isinstance(error, OSError) else "source-invalid"
        for task in tasks:
            yield {"taskId": task["id"], "status": status,
                   "parts": copy.deepcopy(task.get("parts", [])), "nativeParts": [],
                   "missingDxfAssets": list(task.get("missingAssets", [])),
                   "sourceProvenance": source_provenance(), "issues": [str(error)], "invalidSourceShapes": []}
        return

    provenance = source_provenance()
    # Reuse the repository's pinned offline geometry dependency and exactly the
    # original conversion's normalized MultiPolygon WKB fingerprint. GEOS is
    # used only for input fidelity, never for placing or repairing material.
    try:
        from dxf_material import _geos
        shapely, Polygon, explain_validity = _geos()
        from shapely.affinity import translate
    except (ImportError, ValueError) as error:
        for task in tasks:
            yield {"taskId": task["id"], "status": "source-unavailable",
                   "parts": copy.deepcopy(task.get("parts", [])), "nativeParts": [],
                   "missingDxfAssets": list(task.get("missingAssets", [])),
                   "sourceProvenance": copy.deepcopy(provenance), "issues": [str(error)],
                   "invalidSourceShapes": []}
        return

    prepared = {}

    def source_shape(key):
        if key in prepared:
            return prepared[key]
        record, declared = sources.get(key), mapping.get(key)
        if record is None or declared is None:
            answer = (None, "source-unavailable", f"Missing original source/mapping record: {key}", None)
            prepared[key] = answer
            return answer
        values, sizes = record.get("raw"), record.get("sizes")
        issue = None
        if not isinstance(values, list) or not isinstance(sizes, list) or len(sizes) != 1:
            issue = "Source contour requires its explicit single linear ring; rings are not guessed."
        elif any(isinstance(size, bool) or not isinstance(size, int) or size <= 0 or size % 2 for size in sizes) or sum(sizes) != len(values):
            issue = "Source sizes do not exactly describe the coordinate array."
        elif any(isinstance(value, bool) or not isinstance(value, int) or abs(value) > 2**53 for value in values):
            issue = "Original coordinates must be exactly representable integers; no rounding is allowed."
        points = [values[i:i + 2] for i in range(0, len(values), 2)] if issue is None else []
        if len(points) > 1 and points[-1] == points[0]:
            points.pop()
        if issue is None and len(points) < 3:
            issue = "Original contour has fewer than three vertices."
        actual_reason, geometry_hash = None, None
        if issue is None:
            try:
                polygon = Polygon(points)
                material = shapely.MultiPolygon([polygon])
                actual_reason = explain_validity(material)
                if not material.is_valid or material.area <= 0:
                    issue = actual_reason if not material.is_valid else "Original material has no positive area."
                else:
                    x, y, *_ = material.bounds
                    geometry_hash = hashlib.sha256(shapely.normalize(translate(material, xoff=-x, yoff=-y)).wkb).hexdigest()
            except (TypeError, ValueError, shapely.errors.GEOSException) as error:
                issue = str(error)
        if not declared.get("valid"):
            source_reason = declared.get("rejection_reason") or declared.get("error") or "Source marked this contour invalid."
            issue = f"Source-invalid contour {key}: {source_reason}; independently checked: {actual_reason or issue}"
        elif issue is None and (declared.get("ring_count") != 1 or declared.get("hole_count") != 0):
            issue = "Source topology does not match its explicit single-ring, no-hole metadata."
        elif issue is None and geometry_hash != declared.get("geometry_hash_translation_invariant"):
            issue = "Original coordinate fingerprint does not match its declared source geometry."
        details = {"shapeHash": key, "sourceReason": declared.get("rejection_reason") or declared.get("error"),
                   "verifiedReason": actual_reason, "geometryHash": geometry_hash}
        answer = (points if issue is None else None, "ready" if issue is None else "source-invalid", issue, details)
        prepared[key] = answer
        return answer

    for task in tasks:
        parts = copy.deepcopy(task.get("parts", []))
        output = {"taskId": task["id"], "status": "ready", "parts": parts, "nativeParts": [],
                  "missingDxfAssets": list(task.get("missingAssets", [])),
                  "sourceProvenance": copy.deepcopy(provenance), "issues": [], "invalidSourceShapes": []}
        issues = output["issues"]
        task_index = task.get("sourceTaskIndex", (task.get("sourceRules") or {}).get("sourceTaskIndex"))
        job = jobs.get(str(task_index)) if task_index is not None else None
        if job is None:
            output["status"] = "source-unavailable"
            issues.append(f"Missing original job record: {task_index!r}")
            yield output
            continue
        original_quantities = Counter()
        for demand in job.get("shape_quantities", []):
            quantity = demand.get("quantity")
            if isinstance(quantity, bool) or not isinstance(quantity, int) or quantity <= 0:
                issues.append("Original job contains a malformed or nonpositive demand.")
                continue
            original_quantities[str(demand.get("shape_hash"))] += quantity
            if not demand.get("valid", True):
                issues.append(f"Original job marks shape {demand.get('shape_hash')} invalid.")
        used_hashes, part_ids = set(), set()
        for part in parts:
            hashes = part.get("sourceShapeHashes")
            quantity = part.get("quantity")
            if (not isinstance(hashes, list) or not hashes or any(not isinstance(key, str) for key in hashes)
                    or len(set(hashes)) != len(hashes) or used_hashes.intersection(hashes)):
                issues.append(f"Part {part.get('id')} does not identify a unique original source group.")
                continue
            used_hashes.update(hashes)
            if not isinstance(part.get("id"), str) or not part["id"] or part["id"] in part_ids:
                issues.append("Native part IDs must remain unique original catalog IDs.")
                continue
            part_ids.add(part["id"])
            if (isinstance(quantity, bool) or not isinstance(quantity, int) or quantity <= 0
                    or quantity != sum(original_quantities[key] for key in hashes)):
                issues.append(f"Part {part['id']} quantity differs from the original job.")
                continue
            shapes = [source_shape(key) for key in hashes]
            representative = hashes[0]
            original_record = sources.get(representative)
            part["geometrySource"] = {
                "kind": "source-contour", **identities["sourceShapes"],
                "shapeHash": representative, "sourceShapeHashes": list(hashes),
                "geometryHash": part.get("geometryHash"),
                "sourceRecordSha256": hashlib.sha256(json.dumps(
                    original_record, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
                    if original_record is not None else None,
                "sourceRecordHashEncoding": "canonical-json-sort-keys-compact-utf8",
                "units": {"code": 0, "label": "未标单位，按已确认规则使用毫米", "scaleToMm": 1, "assumed": True},
                "coordinatePolicy": provenance["coordinatePolicy"], "exact": False,
                "status": "source-unavailable" if any(shape[1] == "source-unavailable" for shape in shapes) else "source-invalid",
                "dxfStatus": "available" if part.get("assetId") else
                    "source-unavailable" if any(shape[1] == "source-unavailable" for shape in shapes) else "source-invalid"}
            for key, (_, status, reason, details) in zip(hashes, shapes):
                if status != "ready":
                    issues.append(reason)
                    if status == "source-unavailable":
                        output["status"] = status
                    if details:
                        output["invalidSourceShapes"].append(details)
            expected_hash = part.get("geometryHash")
            if all(shape[1] == "ready" for shape in shapes):
                if not expected_hash or any(shape[3]["geometryHash"] != expected_hash for shape in shapes):
                    issues.append(f"Part {part['id']} is not the declared translation-equivalent source group.")
                else:
                    part["geometrySource"].update(status="ready", exact=True,
                        dxfStatus="available" if part.get("assetId") else "pending-on-demand-export")
                    output["nativeParts"].append({
                        "id": part["id"], "quantity": quantity, "material": "pk-material",
                        "freeRotation": True, "allowedAngles": [],
                        "geometry": {"radius": 0, "center": [0, 0],
                                     "outer": [point[:] for point in shapes[0][0]], "holes": []}})
        if used_hashes != set(original_quantities):
            issues.append("Catalog source groups do not exactly cover the original job demands.")
        if task.get("required") != sum(original_quantities.values()):
            issues.append("Catalog required quantity differs from the complete original job.")
        if issues:
            if output["status"] == "ready":
                output["status"] = "source-invalid"
            output["nativeParts"] = []
        yield output
