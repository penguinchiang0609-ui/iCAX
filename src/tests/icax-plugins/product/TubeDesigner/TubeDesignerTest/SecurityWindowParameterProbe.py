"""Persistent neutral-geometry oracle for the native all-parameter acceptance suite.

Native previews are always exercised by the JS runner. This additional oracle
compares the entire declarative solid graph, because preview bounds cannot
detect changes to wall thickness, holes, or other internal geometry.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
PACKAGE = ROOT / "src/apps/tube-designer/templates/product/single_face_security_window"
DESCRIPTOR = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
SPEC = importlib.util.spec_from_file_location("security_window_parameter_probe", PACKAGE / "template.py")
SUBJECT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUBJECT
SPEC.loader.exec_module(SUBJECT)
import icax_template_worker as worker
from icax_template_sdk import expand_resource_model


def digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                    separators=(",", ":"), allow_nan=False).encode("utf-8")).hexdigest()


def finite(value, path="document"):
    if isinstance(value, float) and not math.isfinite(value):
        raise AssertionError(f"Non-finite value: {path}")
    if isinstance(value, dict):
        for key, child in value.items():
            finite(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            finite(child, f"{path}[{index}]")


def physical_properties(properties):
    # Identity changes must not masquerade as changes to stock or processing.
    ignored = {"partNumber", "productCode", "name", "displayName", "description", "label"}
    if isinstance(properties, dict):
        return {key: physical_properties(value) for key, value in properties.items() if key not in ignored}
    if isinstance(properties, list):
        return [physical_properties(value) for value in properties]
    return properties


def probe(parameters, purpose, component_prefix=None):
    original = deepcopy(parameters)
    request = {"templatePath": str(PACKAGE / "template.py"), "template": DESCRIPTOR,
               "parameters": parameters, "context": {"geometryPurpose": "display"}}
    design = worker._evaluate(request)
    display = expand_resource_model(design)
    document = display
    physical_plan = None
    if purpose == "manufacturing":
        declaration = worker._evaluate({**request, "context": {"geometryPurpose": "manufacturing"}})
        document = expand_resource_model(worker._execute_manufacturing({
            "manufacturingDefinition": declaration, "designModel": design,
            "context": {"sharedRoot": str(PACKAGE.parents[1] / "_shared"),
                        "template": {"id": DESCRIPTOR["id"], "version": DESCRIPTOR["version"]}}}))
        source = document.get("extensions", {}).get("tubeDesigner.assemblyProcessSource")
        if source:
            runtime_path = PACKAGE.parents[1] / "_shared/assembly_template_runtime.py"
            runtime = worker._load_template(str(runtime_path), hashlib.sha256(runtime_path.read_bytes()).hexdigest())
            plan = runtime.resolve_process_plan(source["stocks"], source["instances"])
            # Execute the selected providers before comparing effects. Cached
            # identities and retained inputs do not prove that a cutter changed.
            physical_plan = {
                "parts": [{"stockId": part["stockId"], "request": physical_properties(part["request"]),
                           "matrix": part["matrix"]} for part in plan["manufacturingParts"]],
                "forming": physical_properties(plan["forming"]),
            }
    assert parameters == original, "Generator mutated its input"
    # Pure display/manufacturing documents do not own the host input record.
    # The native runner separately requires its preview to echo normalization.
    assert "parameters" not in design and "template" not in design
    finite(document)
    geometry = sorted(document["geometry"], key=lambda node: node["key"])
    nodes = {node["key"] for node in geometry}
    assert len(nodes) == len(geometry), "Duplicate geometry keys"
    for node in geometry:
        for reference in node.get("inputs", []):
            assert reference in nodes, f"Dangling geometry input: {node['key']} -> {reference}"
    items = sorted(document["items"], key=lambda item: item["key"])
    keys = {item["key"] for item in items}
    assert len(keys) == len(items), "Duplicate item keys"
    assert items, "Empty product"
    # Only declarations reachable from actual part representations count as
    # geometry effects. Unused helper nodes cannot prove that a part changed.
    graph = {node["key"]: node for node in geometry}
    reachable = set()

    def visit(key):
        assert key in graph, f"Dangling part representation: {key}"
        if key in reachable:
            return
        reachable.add(key)
        for reference in graph[key].get("inputs", []):
            visit(reference)

    for item in items:
        for reference in item.get("representations", {}).values():
            visit(reference)
    assert reachable, "Product has no solid representations"
    geometry = [node for node in geometry if node["key"] in reachable]
    shapes = [{key: value for key, value in item.items() if key not in (
        "properties", "name", "displayName", "description", "label", "material")}
              for item in items]
    component_hash = None
    if component_prefix:
        component_items = [item for item in items if item['key'].startswith(component_prefix)]
        assert component_items, f'No actual component matches {component_prefix}'
        reachable.clear()
        for item in component_items:
            for reference in item.get('representations', {}).values():
                visit(reference)
        component_hash = digest({
            'geometry': [node for node in geometry if node['key'] in reachable],
            'items': [shape for shape in shapes if shape['key'].startswith(component_prefix)],
        })
    relationships = document.get("relationships", [])
    for relation in relationships:
        assert set(relation.get("items", [])) <= keys, "Dangling relationship item"
    return {
        "geometryHash": digest({"geometry": geometry, "items": shapes}),
        "manufacturingHash": digest({"geometry": geometry, "items": shapes, "physicalPlan": physical_plan,
            "properties": [{key: physical_properties(item.get("properties", {}).get(key))
                for key in ("length", "quantity", "tubeDesigner.profile", "material", "materialGrade")}
                for item in items]}),
        "itemCount": len(display["items"]), "geometryNodeCount": len(geometry),
        "outerFrameCount": sum(item["key"].startswith("outer_frame.") for item in display["items"]),
        "manufacturingItemCount": len(items) if purpose == "manufacturing" else None,
        "keys": sorted(keys), "parameters": original, "componentHash": component_hash,
    }


if __name__ == "__main__":
    for line in sys.stdin:
        request = None
        try:
            request = json.loads(line)
            result = probe(request["payload"]["parameters"], request["payload"].get("purpose", "display"),
                           request["payload"].get("componentPrefix"))
            response = {"id": request["id"], "ok": True, "result": result}
        except Exception as error:
            response = {"id": request.get("id") if request else None, "ok": False,
                        "error": f"{type(error).__name__}: {error}"}
        print(json.dumps(response, ensure_ascii=False, allow_nan=False), flush=True)
