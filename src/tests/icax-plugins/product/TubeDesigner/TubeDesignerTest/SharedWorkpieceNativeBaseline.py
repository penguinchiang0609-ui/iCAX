"""Same-input old-kernel references and checks of real native saved execution.

This helper never starts the native bridge. The JavaScript lifecycle runner
obtains the actual host documents and performs independent native BRep checks.
"""
from copy import deepcopy
import json
from pathlib import Path
import sys

from AllProductsGenericRecheck import ROOT, SRC, TEST, digest, load, part_facts, semantic, world_geometry
from GenericProductLegacyManufacturingBaseline import generate_reference
import icax_template_worker as worker


def process_results(document):
    hashes = world_geometry(document, True)
    records = document.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
    return {record["instanceId"]: semantic(record["result"], hashes) for record in records}


def preparation_results(document):
    hashes = world_geometry(document, True)
    records = document.get("extensions", {}).get("tubeDesigner.workpiecePreparations", [])
    TEST.assertEqual(len(records), len({record["itemKey"] for record in records}))
    return {record["itemKey"]: semantic(record, hashes) for record in records}


def reference(request):
    # Native NormalizeValue delivers Number parameters as double and Integer
    # parameters as integers. Preserve those types before the Python kernel
    # computes content-addressed signatures; JSON 3000 and 3000.0 otherwise
    # have different bytes despite describing the same host parameter value.
    request = deepcopy(request)
    original_values = deepcopy(request["parameters"])
    for field in request["template"]["parameters"]:
        key = field["key"]
        if field["valueType"] == "number":
            TEST.assertIsNot(type(request["parameters"][key]), bool)
            request["parameters"][key] = float(request["parameters"][key])
        elif field["valueType"] == "integer":
            value = request["parameters"][key]
            TEST.assertIsNot(type(value), bool)
            TEST.assertEqual(value, int(value))
            request["parameters"][key] = int(value)
    TEST.assertEqual(request["parameters"], original_values)
    result = generate_reference(request)
    if request["directory"] in {"aluminium_window", "minimal_protective_grille"}:
        values = deepcopy(request["parameters"])
        values["assemblyPlanningMode"] = ("external_templates"
            if values["assemblyPlanningMode"] == "legacy_processed" else "legacy_processed")
        frozen = deepcopy(values)
        template = load(ROOT / request["directory"] / "template.py")
        counterpart = template.display(values)
        TEST.assertEqual(values, frozen)
        TEST.assertEqual(result["display"], counterpart, "The entire shared display must be route independent")
        result["counterpartParameters"] = frozen
        result["counterpartDisplay"] = counterpart
    paths = set((SRC / "apps/tube-designer/templates/_shared").glob("*.py"))
    paths.update(Path(worker.__file__).parent.rglob("*.py"))
    paths.update((ROOT / request["directory"]).rglob("*.py"))
    paths.update((ROOT / request["directory"]).rglob("*.json"))
    result["sourceDigests"].update({str(path.relative_to(SRC)).replace("\\", "/"): digest(path)
                                  for path in sorted(paths)})
    result["sourceChecks"].update(sharedDisplayRouteIndependent="counterpartDisplay" in result,
                                  realPreparationSeparateFromMachining=True,
                                  nativeDescriptorNumberAndIntegerTypesApplied=True)
    return result


def verify_native(reference_document, snapshot):
    TEST.assertEqual(snapshot["display"]["document"], reference_document["display"])
    TEST.assertEqual(snapshot["manufacturing"]["document"], reference_document["manufacturing"])
    actual = snapshot["manufacturingExecution"]["document"]
    original, expected = reference_document["document"], reference_document["sourceExecution"]
    TEST.assertEqual(world_geometry(actual), world_geometry(original), "Actual native world CSG must match old kernel")
    TEST.assertEqual({item["key"]: part_facts(item) for item in actual["items"]},
                     {item["key"]: part_facts(item) for item in original["items"]})
    TEST.assertEqual(process_results(actual), process_results(original), "Actual machining results must retain old semantics")
    TEST.assertEqual(preparation_results(actual), preparation_results(expected),
                     "Real native preparation results must retain dimensions, references and pending status")
    actual_instances = actual.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
    source_instances = original.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
    old_by_id = {record["instanceId"]: record for record in source_instances}
    profile_refs = 0
    for record in actual_instances:
        old = old_by_id[record["instanceId"]]
        for role, binding in old.get("processInput", {}).get("parts", {}).items():
            if "profileRef" in binding:
                TEST.assertEqual(record["processInput"]["parts"][role]["profileRef"], binding["profileRef"])
                profile_refs += 1
    return {"actualNativeWorldCSGEqual": True, "partFactsEqual": True,
            "originalMachiningResultsEqual": True, "separatePreparationResultsEqual": True,
            "originalProfileReferencesEqual": True, "checkedProfileReferences": profile_refs,
            "parts": len(actual["items"]), "machiningResults": len(actual_instances),
            "preparationResults": len(preparation_results(actual))}


if __name__ == "__main__":
    action, input_file, output_file = sys.argv[1:]
    data = json.loads(Path(input_file).read_text(encoding="utf-8"))
    if action == "reference":
        value = reference(data)
    elif action == "verify-native":
        value = verify_native(data["reference"], data["snapshot"])
    else:
        raise ValueError("Unknown shared workpiece audit action: " + action)
    Path(output_file).write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(json.dumps(value["sourceChecks"] if action == "reference" else value))
