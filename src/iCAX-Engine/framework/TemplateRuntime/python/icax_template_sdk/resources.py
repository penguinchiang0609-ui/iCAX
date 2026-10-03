"""Script-owned geometry resources and independently placed item instances.

Version 2 is a public result format. Templates can keep using the version 1
construction graph internally and publish resources once at their boundary.
Neither conversion executes geometry or changes a part's identity.
"""
from __future__ import annotations

from copy import deepcopy
import math
from typing import Any


def _placement(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != {"origin", "xAxis", "yAxis", "zAxis"}:
        raise ValueError(f"{path} requires origin and three rigid axes")
    for key, vector in value.items():
        if (not isinstance(vector, (list, tuple)) or len(vector) != 3
                or any(isinstance(v, bool) or not isinstance(v, (int, float))
                       or not math.isfinite(v) for v in vector)):
            raise ValueError(f"{path}.{key} requires three finite numbers")
    x, y, z = (value[key] for key in ("xAxis", "yAxis", "zAxis"))
    dot = lambda a, b: sum(u * v for u, v in zip(a, b))
    if (any(abs(dot(axis, axis) - 1) > 1.e-7 for axis in (x, y, z))
            or any(abs(dot(a, b)) > 1.e-7 for a, b in ((x, y), (x, z), (y, z)))
            or abs(dot([x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2],
                        x[0]*y[1]-x[1]*y[0]], z) - 1) > 1.e-7):
        raise ValueError(f"{path} requires a unit orthogonal right-handed frame")
    return deepcopy(value)


def _nodes(values: Any, path: str) -> dict[str, dict[str, Any]]:
    if not isinstance(values, list):
        raise ValueError(f"{path} must be an array")
    result = {}
    for value in values:
        if not isinstance(value, dict) or not isinstance(value.get("key"), str) or not value["key"]:
            raise ValueError(f"{path} entries require a nonempty key")
        if value["key"] in result:
            raise ValueError(f"duplicate {path} key: {value['key']}")
        result[value["key"]] = value
    return result


def _dependencies(node: dict[str, Any]) -> list[str]:
    dependencies = list(node.get("inputs", []))
    if node.get("operator") == "boolean":
        arguments = node.get("arguments", {})
        if "target" in arguments:
            dependencies.append(arguments["target"])
        dependencies.extend(arguments.get("tools", []))
    return dependencies


def _compose(outer: dict[str, Any], inner: dict[str, Any]) -> dict[str, Any]:
    """Compose explicit rigid frames; keep geometry in the resource's frame."""
    def rotate(vector: Any) -> list[float]:
        return [sum(outer[axis][i] * vector[j]
                    for j, axis in enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]
    origin = rotate(inner["origin"])
    return {"origin": [outer["origin"][i] + origin[i] for i in range(3)],
            **{axis: rotate(inner[axis]) for axis in ("xAxis", "yAxis", "zAxis")}}


def _referenced_names(value: Any) -> set[str]:
    if isinstance(value, str):
        return {value}
    if isinstance(value, dict):
        return set().union(*(_referenced_names(child)
                             for pair in value.items() for child in pair), set())
    if isinstance(value, (list, tuple)):
        return set().union(*(_referenced_names(child) for child in value), set())
    return set()


def _validate_graph(nodes: dict[str, dict[str, Any]]) -> None:
    visited, active = set(), set()
    def visit(key: str) -> None:
        if not isinstance(key, str) or key not in nodes:
            raise ValueError(f"unknown geometry resource: {key}")
        if key in active:
            raise ValueError(f"cyclic geometry resource: {key}")
        if key in visited:
            return
        active.add(key)
        for dependency in _dependencies(nodes[key]):
            visit(dependency)
        active.remove(key)
        visited.add(key)
    for key in nodes:
        visit(key)


def to_resource_model(document: dict[str, Any]) -> dict[str, Any]:
    """Publish a v2 resource table with per-representation instance placement.

    Existing explicit shared prototypes retain their keys. A transform that is
    only an item's placement moves onto that item. Transforms needed by other
    resource definitions remain in the resource graph. No geometry comparison,
    parameter rounding, part merging or manufacturing decision is performed.
    """
    if document.get("schema") != "icax.neutral-model":
        raise ValueError("unsupported neutral model schema")
    if document.get("schemaVersion") == 2:
        expand_resource_model(document)
        return deepcopy(document)
    if document.get("schemaVersion") != 1 or "resources" in document:
        raise ValueError("unsupported or mixed neutral model schema version")
    result = deepcopy(document)
    nodes = _nodes(result.get("geometry"), "geometry")
    _validate_graph(nodes)
    incoming = {key for node in nodes.values() for key in _dependencies(node)}
    removed = set()
    instance_transforms = set()
    for item in result.get("items", []):
        representations = {}
        for purpose, key in item.get("representations", {}).items():
            if not isinstance(key, str) or key not in nodes:
                raise ValueError(f"item {item.get('key')} references unknown geometry: {key}")
            node = nodes[key]
            arguments = node.get("arguments", {})
            if (node.get("operator") == "transform" and key not in incoming
                    and len(node.get("inputs", [])) == 1
                    and set(arguments) == {"placement"}):
                placement = _placement(arguments["placement"], f"item.{item.get('key')}.{purpose}")
                resource_key = node["inputs"][0]
                # A product placement can wrap a part-local placement. Publish
                # their composed frame so equal parts directly reference the
                # same prototype, rather than separate positioned resources.
                while True:
                    resource = nodes[resource_key]
                    resource_arguments = resource.get("arguments", {})
                    if (resource.get("operator") != "transform"
                            or len(resource.get("inputs", [])) != 1
                            or set(resource_arguments) != {"placement"}):
                        break
                    frame = _placement(resource_arguments["placement"], f"geometry.{resource_key}")
                    composed = _compose(placement, frame)
                    try:
                        _placement(composed, f"item.{item.get('key')}.{purpose}.placement")
                    except ValueError:
                        # Individually valid frames may accumulate numerical
                        # error. Keep the remaining transform as a resource.
                        break
                    placement = composed
                    instance_transforms.add(resource_key)
                    resource_key = resource["inputs"][0]
                representations[purpose] = {
                    "resource": resource_key,
                    "placement": placement,
                    "instanceKey": key,
                }
                removed.add(key)
            else:
                representations[purpose] = {"resource": key}
        item["representations"] = representations
    # Keep aliases used by machining provenance, relationships or other
    # geometry. Only disposable intermediate instance placements are removed.
    metadata = {key: value for key, value in result.items() if key not in ("geometry", "items")}
    metadata["itemProperties"] = [item.get("properties", {}) for item in result.get("items", [])]
    protected = _referenced_names(metadata)
    protected.update(reference["resource"] for item in result.get("items", [])
                     for reference in item["representations"].values())
    while True:
        used = {dependency for key, node in nodes.items() if key not in removed
                for dependency in _dependencies(node)}
        disposable = instance_transforms - removed - used - protected
        if not disposable:
            break
        removed.update(disposable)
    result["resources"] = [node for key, node in nodes.items() if key not in removed]
    result.pop("geometry")
    result["schemaVersion"] = 2
    # Validate the self-contained published graph and all instance references.
    expand_resource_model(result)
    return result


def expand_resource_model(document: dict[str, Any]) -> dict[str, Any]:
    """Expand current resources into the private construction graph.

    Public display and manufacturing documents use their current schemas.
    Neutral graph versions 1 and 2 are current private representations.
    Repeated references never copy their resource.
    """
    if document.get("schema") == "icax.display-model":
        from .display import expand_display_model
        return expand_resource_model(expand_display_model(document))
    if document.get("schema") == "icax.manufacturing-model":
        from .manufacturing import expand_manufacturing_model
        return expand_resource_model(expand_manufacturing_model(document))
    if document.get("schema") != "icax.neutral-model":
        raise ValueError("unsupported neutral model schema")
    if document.get("schemaVersion") == 1:
        return deepcopy(document)
    if document.get("schemaVersion") != 2 or "geometry" in document:
        raise ValueError("unsupported or mixed neutral model schema version")
    result = deepcopy(document)
    nodes = _nodes(result.get("resources"), "resources")
    _validate_graph(nodes)
    instances = {}
    item_keys = set()
    for item in result.get("items", []):
        item_key = item.get("key")
        if not isinstance(item_key, str) or not item_key or item_key in item_keys:
            raise ValueError("invalid or duplicate item key")
        item_keys.add(item_key)
        representations = {}
        for purpose, reference in item.get("representations", {}).items():
            if not isinstance(reference, dict) or set(reference) - {"resource", "placement", "instanceKey"}:
                raise ValueError(f"item.{item_key}.{purpose} requires a geometry resource reference")
            resource = reference.get("resource")
            if not isinstance(resource, str) or resource not in nodes:
                raise ValueError(f"item.{item_key}.{purpose} references unknown resource: {resource}")
            if "placement" not in reference:
                if "instanceKey" in reference:
                    raise ValueError("instanceKey requires instance placement")
                representations[purpose] = resource
                continue
            placement = _placement(reference["placement"], f"item.{item_key}.{purpose}.placement")
            key = reference.get("instanceKey", f"instance.{item_key}.{purpose}")
            if not isinstance(key, str) or not key or key in nodes:
                raise ValueError(f"invalid or colliding instanceKey: {key}")
            node = {"key": key, "operator": "transform", "inputs": [resource],
                    "arguments": {"placement": placement}}
            if key in instances and instances[key] != node:
                raise ValueError(f"conflicting instanceKey: {key}")
            instances[key] = node
            representations[purpose] = key
        item["representations"] = representations
    result["geometry"] = list(nodes.values()) + list(instances.values())
    result.pop("resources")
    result["schemaVersion"] = 1
    return result
