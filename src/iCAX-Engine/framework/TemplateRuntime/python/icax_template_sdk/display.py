"""Display payloads contain model data, never the product's input record."""
from __future__ import annotations

from copy import deepcopy
import json
import math
from pathlib import Path
from typing import Any

from .resources import to_resource_model, expand_resource_model


SCHEMA = "icax.display-model"
_FIELDS = {"schema", "schemaVersion", "coordinateSystem", "lengthUnit",
           "resources", "items", "roots", "annotations"}
_PROCESS_FIELDS = {
    "manufacturing", "manufacturingRoute", "provenance",
    "tubeDesigner.assemblyGeometryProcesses", "tubeDesigner.endProcess",
    "tubeDesigner.manufacturingAxis", "tubeDesigner.manufacturingStartToEnd",
    "tubeDesigner.frameManufacturing", "tubeDesigner.sourceSpans",
    "tubeDesigner.designSegment", "tubeDesigner.cornerProcess",
    "tubeDesigner.jointProcess", "tubeDesigner.assemblyPlanning",
    "tubeDesigner.connectionProcess", "stockState", "stockInterval",
    "cutPlanes", "machiningFeatures", "slotFeatures",
}
_CONNECTION_FIELDS = {"recipe", "halfHole", "manufacturingCut", "stockAllowance",
    "trim", "processTemplateId", "templateId", "toolRef", "toolParameters",
    "insertionDepth", "totalClearanceDepth", "totalClearanceHeight"}
_DESCRIPTIVE_FIELDS = {"manufacturing.partKind": "partKind",
    "manufacturing.material": "material", "manufacturing.materialGrade": "materialGrade",
    "manufacturing.materialCategory": "materialCategory", "manufacturing.sourcing": "sourcing",
    "manufacturing.categoryKey": "categoryKey", "manufacturing.categoryName": "categoryName",
    "manufacturing.modelReference": "modelReference", "manufacturing.plate": "plate"}


def display_context(template_file: str) -> dict[str, Any]:
    """Construction-only context for builders shared with older entry points."""
    descriptor = json.loads(Path(template_file).with_name("template.json").read_text(encoding="utf-8"))
    return {"geometryPurpose": "display", "template": {
        "id": descriptor["id"], "version": descriptor["version"]}}


def _clean(value: Any, *, connection: bool = False) -> Any:
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if (key.startswith("manufacturing.") or key.startswith("tubeDesigner.assemblyProcess")
                    or key in _PROCESS_FIELDS or connection and key in _CONNECTION_FIELDS):
                continue
            if connection and key == "geometry" and child == "outer-envelope-cope":
                continue
            if connection and key == "kind" and child == "weld":
                child = "contact"
            result[key] = _clean(child, connection=connection)
        return result
    if isinstance(value, (list, tuple)):
        return [_clean(child, connection=connection) for child in value]
    return deepcopy(value)


def _pure(value: Any, *, connection: bool = False) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if (key.startswith("manufacturing.") or key.startswith("tubeDesigner.assemblyProcess")
                    or key in _PROCESS_FIELDS or connection and key in _CONNECTION_FIELDS
                    or connection and key == "geometry" and child == "outer-envelope-cope"
                    or connection and key == "kind" and child == "weld"):
                raise ValueError(f"display data contains machining field: {key}")
            _pure(child, connection=connection)
    elif isinstance(value, (list, tuple)):
        for child in value:
            _pure(child, connection=connection)


def _hierarchy(items: list[dict[str, Any]], roots: Any) -> list[str]:
    if not isinstance(items, list) or not isinstance(roots, list):
        raise ValueError("display items and roots must be arrays")
    by_key, parents = {}, {}
    for item in items:
        if not isinstance(item, dict) or set(item) - {"key", "displayName", "geometry", "children", "properties"}:
            raise ValueError("invalid display item fields")
        key = item.get("key")
        if not isinstance(key, str) or not key or key in by_key:
            raise ValueError("invalid or duplicate display item key")
        if not isinstance(item.get("displayName"), str):
            raise ValueError("display item name must be a string")
        if not isinstance(item.get("properties", {}), dict):
            raise ValueError("display item properties must be an object")
        by_key[key] = item
    for key, item in by_key.items():
        children = item.get("children", [])
        if not isinstance(children, list):
            raise ValueError("display children must be an array")
        for child in children:
            if not isinstance(child, str) or child not in by_key or child in parents:
                raise ValueError("display child must exist and have one parent")
            parents[child] = key
    if any(not isinstance(root, str) for root in roots) or len(set(roots)) != len(roots):
        raise ValueError("invalid or duplicate display roots")
    if set(roots) != set(by_key) - set(parents):
        raise ValueError("display roots must be the instance tree roots")
    visited, active, visible = set(), set(), []
    def visit(key):
        if key in active:
            raise ValueError("cyclic display instance hierarchy")
        if key in visited:
            return
        active.add(key)
        if "geometry" in by_key[key]:
            visible.append(key)
        for child in by_key[key].get("children", []):
            visit(child)
        active.remove(key)
        visited.add(key)
    for root in roots:
        visit(root)
    if visited != set(by_key):
        raise ValueError("unreachable or cyclic display items")
    return visible


def _plate_component(value: Any) -> None:
    if not isinstance(value, dict) or set(value) - {'width', 'height', 'thickness', 'areaMm2', 'center', 'xAxis', 'yAxis', 'outline'}:
        raise ValueError('shared plate component must be an object')
    dimensions = [value.get(name) for name in ('width', 'height', 'thickness')]
    if any(type(number) not in (int, float) or not math.isfinite(number) or number <= 0
           for number in dimensions) or not math.isfinite(dimensions[0]*dimensions[1]):
        raise ValueError('shared plate dimensions must be finite and positive')
    expected_area = dimensions[0] * dimensions[1]
    if 'outline' in value:
        outline = value['outline']
        if (not isinstance(outline, list) or len(outline) < 3
                or any(not isinstance(point, list) or len(point) != 2
                       or any(type(number) not in (int, float) or not math.isfinite(number)
                              for number in point) for point in outline)
                or len({tuple(point) for point in outline}) != len(outline)):
            raise ValueError('shared plate outline requires at least three distinct finite 2D points')
        expected_area = abs(sum(a[0] * b[1] - b[0] * a[1]
                                for a, b in zip(outline, outline[1:] + outline[:1]))) / 2
        if not math.isfinite(expected_area) or expected_area <= 0:
            raise ValueError('shared plate outline must have finite positive area')
    if 'areaMm2' in value:
        area = value['areaMm2']
        if type(area) not in (int, float) or not math.isfinite(area) or not math.isclose(
                area, expected_area, rel_tol=1.e-8, abs_tol=1.e-8):
            raise ValueError('shared plate area disagrees with its dimensions or outline')
    axes = ('center', 'xAxis', 'yAxis')
    if any(name in value for name in axes):
        if any(not isinstance(value.get(name), list) or len(value[name]) != 3
               or any(type(number) not in (int, float) or not math.isfinite(number) for number in value[name])
               for name in axes):
            raise ValueError('shared plate placement requires a finite center and both axes')
        x, y = value['xAxis'], value['yAxis']
        if (abs(sum(number * number for number in x) - 1) > 1.e-7
                or abs(sum(number * number for number in y) - 1) > 1.e-7
                or abs(sum(a * b for a, b in zip(x, y))) > 1.e-7):
            raise ValueError('shared plate placement axes must be unit and orthogonal')


def expand_display_model(document: dict[str, Any]) -> dict[str, Any]:
    """Adapt pure display data for existing geometry tools, without input echo."""
    version = document.get("schemaVersion")
    if (document.get("schema") != SCHEMA or type(version) is not int or version != 2
            or set(document) != _FIELDS):
        raise ValueError("unsupported or impure display model")
    items = deepcopy(document.get("items"))
    visible = _hierarchy(items, document.get("roots"))
    for item in items:
        _pure(item.get("properties", {}))
        if 'plate' in item.get('properties', {}):
            _plate_component(item['properties']['plate'])
        geometry = item.pop("geometry", None)
        item["representations"] = {} if geometry is None else {"result": geometry}
    annotations = deepcopy(document.get("annotations", []))
    if not isinstance(annotations, list):
        raise ValueError("display annotations must be an array")
    _pure(annotations)
    result = {"schema": "icax.neutral-model", "schemaVersion": 2,
        "template": {"id": "display-model", "version": str(version), "packageDigest": ""},
        "coordinateSystem": document.get("coordinateSystem", "right-handed-x-width-y-depth-z-height"),
        "lengthUnit": document.get("lengthUnit", "mm"), "parameters": {},
        "resources": deepcopy(document.get("resources")), "items": items,
        "outputs": [{"key": "result", "purpose": "result", "items": visible, "properties": {}}],
        "relationships": [], "tables": [], "diagnostics": [],
        "extensions": {"tubeDesigner.geometryPurpose": "display",
            "tubeDesigner.specificationAnnotations": annotations}}
    expand_resource_model(result)
    return result


def to_display_model(document: dict[str, Any]) -> dict[str, Any]:
    """Publish the shared base model; manufacturing owns its connections."""
    if document.get("schema") == SCHEMA:
        expand_display_model(document)
        result = deepcopy(document)
        expand_display_model(result)
        return result
    model = to_resource_model(document)
    outputs = [output for output in model.get("outputs", []) if output.get("purpose") == "display"]
    purpose = "display"
    if not outputs:
        outputs = [output for output in model.get("outputs", []) if output.get("purpose") == "result"]
        purpose = "result"
    if len(outputs) != 1:
        raise ValueError("display must publish one display model")
    by_key = {item["key"]: item for item in model["items"]}
    selected = set()
    def select(key):
        if key not in by_key:
            raise ValueError(f"display references unknown item: {key}")
        if key in selected:
            return
        selected.add(key)
        for child in by_key[key].get("children", []):
            select(child)
    for key in outputs[0]["items"]:
        select(key)
    items = []
    for item in model["items"]:
        if item["key"] not in selected:
            continue
        properties = _clean(item.get("properties", {}))
        for old, new in _DESCRIPTIVE_FIELDS.items():
            if old in item.get("properties", {}):
                properties.setdefault(new, deepcopy(item["properties"][old]))
        result = {"key": item["key"], "displayName": deepcopy(item["displayName"]),
            "children": deepcopy(item.get("children", [])), "properties": properties}
        reference = item.get("representations", {}).get(purpose)
        if reference is not None:
            result["geometry"] = deepcopy(reference)
        items.append(result)
    children = {child for item in items for child in item["children"]}
    roots = [item["key"] for item in items if item["key"] not in children]
    nodes = {node["key"]: node for node in model["resources"]}
    needed = set()
    def resource(key):
        if key in needed:
            return
        needed.add(key)
        node = nodes[key]
        dependencies = list(node.get("inputs", []))
        if node.get("operator") == "boolean":
            dependencies += [node["arguments"]["target"]] if "target" in node["arguments"] else []
            dependencies += node["arguments"].get("tools", [])
        for dependency in dependencies:
            resource(dependency)
    for item in items:
        if "geometry" in item:
            resource(item["geometry"]["resource"])
        section = item.get('properties', {}).get('tubeDesigner.profile', {}).get('sectionResource')
        if section is not None:
            if section not in nodes or nodes[section].get('operator') != 'profile2d':
                raise ValueError('display tube profile requires its actual section resource')
            resource(section)
    result = {"schema": SCHEMA, "schemaVersion": 2,
        "coordinateSystem": model.get("coordinateSystem", "right-handed-x-width-y-depth-z-height"),
        "lengthUnit": model.get("lengthUnit", "mm"),
        "resources": [node for key, node in nodes.items() if key in needed],
        "items": items, "roots": roots,
        "annotations": deepcopy(model.get("extensions", {}).get("tubeDesigner.specificationAnnotations", []))}
    expand_display_model(result)
    return result
