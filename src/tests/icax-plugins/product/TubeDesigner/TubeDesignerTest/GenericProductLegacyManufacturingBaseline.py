"""Freeze a same-input old generator reference for targeted native branches."""
from copy import deepcopy
import json
from pathlib import Path
import sys
from unittest.mock import patch

from AllProductsGenericRecheck import (ROOT, SRC, TEST, descriptor, digest, load,
    missing_relationship_references, part_facts, semantic, world_geometry)
from ProductGeometryResourceTests import assert_display_model
from ProductManufacturingDeclarationTests import assert_declaration, execute_definition, forbid_processing
import icax_template_worker as worker


def generate_reference(request):
    entry = ROOT / request["directory"] / "template.py"
    data, _ = descriptor(entry.with_name("template.json"))
    TEST.assertEqual(data["id"], request["templateId"])
    template = load(entry)
    values = deepcopy(request["parameters"])
    frozen = deepcopy(values)
    with patch.object(template, "manufacturing", side_effect=AssertionError("Reference must use legacy generate")):
        with patch.object(template, "display", side_effect=AssertionError("Reference must not call public display")):
            original = template.generate(values, {"template": request["template"], "geometryPurpose": "manufacturing"})
    TEST.assertEqual(values, frozen)
    TEST.assertEqual(original["parameters"], frozen)
    TEST.assertEqual(missing_relationship_references(original), [])
    design = template.display(values)
    assert_display_model(TEST, design)
    with patch.object(template, "display", side_effect=AssertionError("Manufacturing cannot call display")):
        with forbid_processing() as processing:
            declaration = template.manufacturing(values)
        TEST.assertEqual(processing, [])
    assert_declaration(TEST, declaration, design)
    keys = {item["key"] for item in design["items"]}
    for connection in declaration["connections"]:
        TEST.assertTrue(set(connection["items"]) <= keys, connection["key"])
        anchors = [anchor["itemKey"] for anchor in connection.get("properties", {}).get("participantAnchors", [])]
        TEST.assertEqual(len(anchors), len(set(anchors)))
        TEST.assertTrue(set(anchors) <= set(connection["items"]))
    source_design, source_declaration = deepcopy(design), deepcopy(declaration)
    original_loader = worker._load_template
    def executor_only(path, *args, **kwargs):
        TEST.assertEqual(Path(path).resolve(), (SRC / "apps/tube-designer/templates/_shared/assembly_manufacturing_executor.py").resolve())
        return original_loader(path, *args, **kwargs)
    with patch.object(worker, "_load_template", side_effect=executor_only):
        current = execute_definition(declaration, design=design)
    TEST.assertEqual(design, source_design)
    TEST.assertEqual(declaration, source_declaration)
    TEST.assertEqual(values, frozen)
    TEST.assertEqual(world_geometry(original), world_geometry(current))
    old_items = {item["key"]: item for item in original["items"]}
    current_items = {item["key"]: item for item in current["items"]}
    TEST.assertEqual(set(old_items), set(current_items))
    TEST.assertEqual({key: part_facts(item) for key, item in old_items.items()},
                     {key: part_facts(item) for key, item in current_items.items()})
    def process_results(model):
        hashes = world_geometry(model, True)
        instances = model.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
        return {instance["instanceId"]: semantic(instance["result"], hashes) for instance in instances}
    TEST.assertEqual(process_results(original), process_results(current))
    required_contacts = []
    if request["directory"] == "modular_guardrail":
        built = template.build_layout(values)
        actual_tubes = {tube.key for tube in built.tubes}
        for bay in built.bays:
            cap = bay.key + ".elevation.cap"
            if cap not in actual_tubes:
                cap = bay.key.split(".bay.")[0] + ".continuous.cap"
            for beam in (cap, bay.key + ".bottom", bay.key + ".upper"):
                TEST.assertIn(beam, actual_tubes)
                for post in (bay.left, bay.right):
                    pair = sorted((beam, post.key))
                    if pair not in required_contacts:
                        required_contacts.append(pair)
        actual_pairs = {tuple(sorted(connection["items"])) for connection in declaration["connections"]}
        for pair in required_contacts:
            TEST.assertIn(tuple(pair), actual_pairs, "A real cap/bottom/upper-to-post contact was lost")
        TEST.assertGreater(len(required_contacts), 0)
    paths = [entry, entry.with_name("template.json")]
    paths.extend((SRC / "apps/tube-designer/templates/_shared" / name) for name in (
        "modular_guardrail.py", "guardrail_elevation.py", "steel_staircase.py", "stair_nodes.py", "assembly_manufacturing_executor.py"))
    return {"templateId": data["id"], "parameters": frozen, "document": original,
            "display": design, "manufacturing": declaration, "sourceExecution": current,
            "requiredPostContacts": required_contacts,
            "sourceDigests": {str(path.relative_to(SRC)).replace("\\", "/"): digest(path) for path in paths},
            "sourceChecks": {"legacyEntry": "generate", "publicDisplayVersion": 2, "publicManufacturingVersion": 4,
                "pureDeclaration": True, "independentExecutor": True, "hostInputsUnchanged": True,
                "strictWorldCSGEqual": True, "processResultsEqual": True, "partFactsEqual": True,
                "realPostContactsRetained": bool(required_contacts)}}


if __name__ == "__main__":
    request_path, output_path = map(Path, sys.argv[1:])
    reference = generate_reference(json.loads(request_path.read_text(encoding="utf-8")))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(reference, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(json.dumps({"templateId": reference["templateId"], "parts": len(reference["document"]["items"]),
                      "requiredPostContacts": len(reference["requiredPostContacts"]), "sourceChecks": reference["sourceChecks"]}))
