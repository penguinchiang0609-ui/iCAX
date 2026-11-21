"""Read mechanical collection metadata without changing original DXF files.

The caller extracts archives and handles DXF units and geometry.  This module
only assembles reproducible task inputs from the extracted source evidence.
Collections without a production BOM become one-file benchmark tasks, with the
quantity assumption recorded explicitly rather than presented as source demand.
"""
from __future__ import annotations

import csv
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any


COLLECTIONS = {
    "automotive_16_plus_mobile_equipment_20_DXF": "manifest.json",
    "hardware_mechanical_classified": "inventory_final.json",
    "keychron_plate_dxf_source_available": "plate_inventory.json",
    "paysan_stage1_53_unique_dxf_license_mapping_pending": "manifest_validation.json",
}
STEEL_COLLECTION = "steel_frame_7_parts_2_nests_and_dstv_tests"
_PIECES = re.compile(r"(?<!\d)(\d+)\s*piece(?:\(s\)|s)?(?!\w)", re.IGNORECASE)
_PCS = re.compile(r"(?<!\d)(\d+)\s*pcs?\b", re.IGNORECASE)


def _relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _task_id(collection: str, identity: str) -> str:
    digest = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:16]
    return f"{collection}--{digest}"


def _digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _metadata(directory: Path, name: str) -> tuple[dict[str, dict[str, Any]], list[str]]:
    """Match metadata by bytes; archived local paths precede reclassification."""
    path = directory / name
    if not path.is_file():
        return {}, [f"source-metadata-missing: {name}"]
    try:
        rows = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return {}, [f"source-metadata-unreadable: {name}: {error}"]
    if not isinstance(rows, list):
        return {}, [f"source-metadata-not-a-list: {name}"]
    by_hash: dict[str, dict[str, Any]] = {}
    for row in rows:
        if isinstance(row, dict) and isinstance(row.get("sha256"), str):
            by_hash.setdefault(row["sha256"].lower(), row)
    return by_hash, []


def _quantity(filename: str) -> tuple[int, str, list[str], str | None]:
    pieces = [int(match.group(1)) for match in _PIECES.finditer(filename)]
    qualifiers = [int(match.group(1)) for match in _PCS.finditer(filename)]
    counts = pieces + qualifiers
    if pieces and (any(value <= 0 for value in counts) or len(set(counts)) != 1):
        return 1, "benchmark-default-one", [
            f"filename-quantity-ambiguous: {filename}; piece/pcs counts={counts}; "
            "benchmark quantity defaults to one DXF copy"
        ], filename
    if pieces and pieces[0] > 0:
        return pieces[0], "filename-explicit-piece-count", [], filename
    return 1, "benchmark-default-one", [], None


def _source_url(row: dict[str, Any]) -> str | None:
    for key in ("source_url", "url"):
        value = row.get(key)
        if isinstance(value, str) and value:
            return value
    source = row.get("source")
    if isinstance(source, dict) and isinstance(source.get("url"), str):
        return source["url"]
    if isinstance(source, str):
        return source
    return None


def _single_task(root: Path, directory: Path, path: Path, collection: str,
                 meta_name: str, by_hash: dict[str, dict[str, Any]],
                 metadata_issues: list[str], *, reference: bool = False) -> dict[str, Any]:
    relative = _relative(path, root)
    digest = _digest(path)
    metadata = by_hash.get(digest, {})
    quantity, quantity_source, issues, evidence = _quantity(path.name)
    issues = [*metadata_issues, *issues]
    if by_hash and not metadata:
        issues.append(f"source-metadata-sha256-not-found: {meta_name}")
    member = {
        "sourcePath": relative,
        "quantity": quantity,
        "quantitySource": quantity_source,
        "sha256": digest,
    }
    if evidence is not None:
        member["quantityEvidence"] = evidence
    task: dict[str, Any] = {
        "id": _task_id(collection, relative),
        "label": path.stem,
        "collection": collection,
        "kind": "reference" if reference else "benchmark",
        "sourcePath": relative,
        "parts": [member],
        "sourceStock": None,
        "sourceRules": {},
        "issues": issues,
        "sourceMetadataPath": _relative(directory / meta_name, root),
    }
    category = path.relative_to(directory).parts[0]
    if collection == "hardware_mechanical_classified":
        task["category"] = category
    elif isinstance(metadata.get("category"), str):
        task["category"] = metadata["category"]
    url = _source_url(metadata)
    if url:
        task["sourceURL"] = url
    if reference:
        task["issues"].append("source-is-reference-drawing: not a confirmed single cutting part")
    return task


def _steel_tasks(root: Path) -> list[dict[str, Any]]:
    directory = root / STEEL_COLLECTION
    if not directory.is_dir():
        return []
    by_hash, metadata_issues = _metadata(directory, "validation.json")
    index = directory / "vanthanbk/plate_dxf/plates_index.csv"
    source_rules = {
        "edgeMarginMm": 10,
        "partGapMm": 5,
        "source": _relative(directory / "vanthanbk/README.txt", root),
    }
    source_stock = {
        "width": 500,
        "height": 300,
        "units": "mm",
        "kind": "sheet",
        "source": _relative(directory / "vanthanbk/README.txt", root),
    }
    groups: dict[tuple[str, float], list[dict[str, Any]]] = defaultdict(list)
    group_issues: dict[tuple[str, float], list[str]] = defaultdict(list)
    indexed_paths: set[Path] = set()
    index_issues: list[str] = []
    if index.is_file():
        with index.open(encoding="utf-8-sig", newline="") as stream:
            for row in csv.DictReader(stream):
                filename = (row.get("File") or "").strip()
                if not filename.lower().endswith(".dxf"):
                    continue
                # Quantity index entries describe files in the adjacent folder.
                # Do not allow metadata paths to escape that original folder.
                if Path(filename).name != filename or "/" in filename or "\\" in filename:
                    index_issues.append(f"plate-index-invalid-filename: {filename}")
                    continue
                path = index.parent / filename
                try:
                    quantity = int(row["Qty"])
                    thickness = float(row["Thickness (mm)"])
                    if quantity <= 0 or not (0 < thickness < float("inf")):
                        raise ValueError("quantity and thickness must be positive")
                except (KeyError, TypeError, ValueError) as error:
                    index_issues.append(f"plate-index-invalid-row: {filename}: {error}")
                    continue
                material = (row.get("Material") or "unspecified").strip()
                key = material, thickness
                if not path.is_file():
                    group_issues[key].append(f"plate-index-file-missing: {filename}")
                    continue
                if path in indexed_paths:
                    group_issues[key].append(f"plate-index-duplicate-row: {filename}")
                    continue
                indexed_paths.add(path)
                digest = _digest(path)
                if by_hash and digest not in by_hash:
                    group_issues[key].append(f"source-metadata-sha256-not-found: {filename}")
                groups[key].append({
                    "sourcePath": _relative(path, root),
                    "quantity": quantity,
                    "quantitySource": "source-plate-index",
                    "quantityEvidence": _relative(index, root),
                    "sha256": digest,
                    "material": material,
                    "thicknessMm": thickness,
                })
    else:
        index_issues.append("plate-index-missing: vanthanbk/plate_dxf/plates_index.csv")
    tasks: list[dict[str, Any]] = []
    for key in sorted(groups):
        material, thickness = key
        tasks.append({
            "id": _task_id(STEEL_COLLECTION, f"plate-index:{material}:{thickness:g}"),
            "label": f"Steel frame — {material} {thickness:g} mm",
            "collection": STEEL_COLLECTION,
            "kind": "benchmark",
            "sourcePath": _relative(index, root),
            "parts": sorted(groups[key], key=lambda part: part["sourcePath"]),
            "sourceStock": dict(source_stock),
            "sourceRules": dict(source_rules),
            "issues": [*metadata_issues, *index_issues, *group_issues[key]],
            "material": material,
            "thicknessMm": thickness,
            "category": "source-plate-task",
        })
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.suffix.lower() != ".dxf" or path in indexed_paths:
            continue
        is_nest = "nest_dxf" in path.relative_to(directory).parts
        task = _single_task(root, directory, path, STEEL_COLLECTION,
                            "validation.json", by_hash, metadata_issues,
                            reference=is_nest)
        task["issues"].extend(index_issues)
        task["sourceStock"] = dict(source_stock)
        task["sourceRules"] = dict(source_rules)
        if is_nest:
            task["category"] = "source-nesting-reference"
            task["issues"] = [issue for issue in task["issues"]
                              if not issue.startswith("source-is-reference-drawing:")]
            task["issues"].append("source-is-nested-sheet: reference layout, not another original part")
        else:
            task["issues"].append("plate-not-listed-in-quantity-index: default benchmark demand only")
        tasks.append(task)
    return tasks


def build_tasks(root: Path) -> list[dict[str, Any]]:
    """Return source-traceable tasks from the five extracted mechanical ZIPs."""
    root = Path(root)
    tasks: list[dict[str, Any]] = []
    for collection, meta_name in COLLECTIONS.items():
        directory = root / collection
        if not directory.is_dir():
            continue
        by_hash, metadata_issues = _metadata(directory, meta_name)
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.suffix.lower() != ".dxf":
                continue
            reference = (collection == "hardware_mechanical_classified" and
                         path.relative_to(directory).parts[0] == "03_reference_drawings_not_direct_nesting")
            tasks.append(_single_task(root, directory, path, collection, meta_name,
                                      by_hash, metadata_issues, reference=reference))
    tasks.extend(_steel_tasks(root))
    return sorted(tasks, key=lambda task: (task["collection"], task["id"]))
