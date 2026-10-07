"""Dynamic public-entry audit of non-window products and archived wrappers.

This writes isolated evidence, never changes templates or the deployed runtime.
Each case keeps an explicit old-generator validity result; failures are not skips.
"""
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback
import unittest
from unittest.mock import patch

from WindowCatalogueTests import SRC
import icax_template_worker as worker
from icax_template_sdk import expand_resource_model
from icax_template_sdk.manufacturing import compose_manufacturing_model
from icax_template_sdk.resources import _compose
from ProductGeometryResourceTests import assert_display_model
from ProductManufacturingDeclarationTests import assert_declaration, canonical_geometry, execute_definition, forbid_processing

ROOT = SRC / "apps/tube-designer/templates/product"
OUT = SRC.parent / "output/tests/assembly-process"
TEST = unittest.TestCase()


def descriptor(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    return data, {entry["key"]: deepcopy(entry["defaultValue"]) for entry in data["parameters"]}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return worker._load_template(str(path), "generic-recheck-" + digest(path))


def scenarios(name, data, defaults, verified):
    cases = []
    base = {}
    if name == "aluminium_window":
        base = {"systemSource": "file", "systemFile": str(verified)}
    if name == "straight_steel_staircase":
        base = {"totalRiserCount": 6, "floorHeight": 1080, "firstFlightRiserCount": 3}
    def add(label, changes=None):
        cases.append({"label": label, "overrides": {**base, **(changes or {})}})
    add("baseline-real-entry")
    for field in data["parameters"]:
        key = field["key"]
        alternatives = [choice["value"] for choice in field.get("choices", [])]
        if field.get("valueType") == "boolean":
            alternatives = [False, True]
        for value in alternatives:
            if value == defaults[key]:
                continue
            activation = {}
            if name == "decorative_door":
                if key == "motherSide": activation = {"doorType": "mother"}
                if key == "regionProcess": activation = {"pattern": "diamond"}
                if key == "trimEnabled": activation = {"pattern": "panels"}
            if name == "aluminium_window":
                if key.startswith("cell"):
                    activation = {"windowType": "mixed", "columns": 3, "rows": 3, "width": 3000, "height": 3000}
                if key == "mergeTopLight": activation = {"columns": 2, "rows": 2, "width": 2400, "height": 2400}
                if key in {"openingSide", "openingDirection"}: activation = {"windowType": "hinged"}
            if name == "minimal_protective_grille":
                if key == "pitchAlignment": activation = {"barLayoutMode": "fixed_pitch", "barPitch": 125}
                if key == "frameCornerJoint": activation = {"frameType": "closed_frame"}
                if key == "maleCornerType": activation = {"frameType": "closed_frame", "frameCornerJoint": "horizontal_wrap"}
                if key == "handleDistribution": activation = {"barLayoutMode": "fixed_pitch", "barPitch": 120}
            if name == "straight_steel_staircase":
                if key in {"turnDirection", "landingColumns"}: activation = {"stairRoute": "l_turn"}
                if key in {"handrailProfileType", "postProfileType", "infillProfileType", "handrailConnection"}:
                    activation = {"stairRoute": "l_turn", "railingInfill": "vertical" if key == "infillProfileType" else "horizontal"}
                if key == "bracketType": activation = {"bracketThickness": 12}
            if name.startswith("modular_guardrail"):
                if key == "elevationSource": activation = {"pathMode": "continuous", "treadCount1": 10}
                if key == "cornerPostMode": activation = {"layout": "left_l"}
                if key == "postCapEnabled": activation = {"largePostMode": "middle"}
                if key == "spearTipEnabled": activation = {"guardrailUse": "wall"}
                if key == "glassClipEnabled" and "glass" in name: activation = {"infillType": "glass"}
            add("choice:" + key + "=" + str(value), {**activation, key: value})
    if name == "decorative_door":
        for pattern in ("lines", "diamond", "octagon", "round_scene", "panels", "glass_lattice"):
            for layout in ("center_band", "two_blocks"):
                add("pattern-layout:" + pattern + ":" + layout,
                    {"pattern": pattern, "layout": layout, "doorType": "mother", "motherSide": "right", "composition": "continuous", "width": 3000})
        add("v-both-surfaces", {"lineTool": "v", "cutDepth": 7, "machiningSide": "both"})
        add("multi-repeat-panel-trim", {"doorType": "multi", "leafCount": 3, "width": 3000, "pattern": "panels", "composition": "repeat", "trimEnabled": True})
        add("inactive-drafts", {"doorType": "single", "pattern": "diamond", "regionProcess": "through", "cutDepth": 99, "minSkin": 99, "machiningSide": "both", "leafCount": 6, "vAngle": 10, "glassThickness": 99})
        add("invalid-both-sides-skin", {"cutDepth": 18, "machiningSide": "both"})
        add("invalid-trim-web", {"pattern": "panels", "trimWidth": 30, "webWidth": 40})
    if name == "aluminium_window":
        for mode in ("fixed", "sliding", "hinged", "mixed"):
            add("multi-cell:" + mode, {"windowType": mode, "columns": 2, "rows": 2, "width": 2400, "height": 2400, "cell11": "hinged", "cell12": "sliding", "hingedCount": 2})
        add("demonstration-production-rejected", {"systemSource": "demonstration", "systemFile": ""})
        add("real-file-missing", {"systemFile": str(OUT / "all-products-recheck-does-not-exist.json")})
        add("relative-file-rejected", {"systemFile": "../demonstration.json"})
        add("screen-track-conflict", {"trackCount": 2, "screenEnabled": True})
        add("multiple-screen-panels", {"screenCount": 2})
        add("sliding-four-panels", {"slidingCount": 4, "screenEnabled": False, "trackCount": 2})
        add("external-four-junctions", {"windowType": "fixed", "columns": 2, "rows": 2, "assemblyPlanningMode": "external_templates"})
    if name == "louver_window":
        for connection in ("face_weld", "slot_insert", "through_insert"):
            for support in ("split", "through"):
                add("blade-support:" + connection + ":" + support, {"bladeConnection": connection, "supportMode": support})
        for angle in (-30, 45): add("blade-angle:" + str(angle), {"bladeDirectionAngle": angle})
        add("asymmetric-frame-section", {"frameWidth": 60, "frameDepth": 40})
    if name == "minimal_protective_grille":
        for recipe in ("miter_45", "horizontal_wrap", "vertical_wrap"):
            for orientation in ("as_defined", "rotate_90"):
                add("frame-recipe-section:" + recipe + ":" + orientation, {"frameType": "closed_frame", "frameCornerJoint": recipe, "frameProfileOrientation": orientation, "frameWidth": 60, "frameDepth": 40})
        add("hole-side-auto-avoid", {"installHoleOrientation": "side", "installHoleAutoAvoid": True})
        add("inactive-holes-handle", {"handleEnabled": False, "installHoleEnabled": False, "installHoleLargeDiameter": -1, "handleReferenceY": -1})
    if name.startswith("modular_guardrail"):
        for mode in ("continuous", "stepped"):
            for install in ("embedded", "base_plate", "side_plate"):
                add("slope-install:" + mode + ":" + install, {"pathMode": mode, "slopeAngle": 30, "installation": install})
        add("multi-slope-u", {"layout": "u", "pathMode": "continuous", "slopeAngle": 30, "slopeAngle2": 0, "slopeAngle3": -20})
        add("tread-elevation-left-l", {"layout": "left_l", "pathMode": "continuous", "elevationSource": "treads", "treadCount1": 10, "treadCount2": 5})
        add("continuous-top-rail", {"handrailMode": "continuous", "pathMode": "level", "layout": "left_l"})
        add("round-side-saddle", {"postProfileType": "round", "installation": "side_plate"})
        add("nonsquare-u-profile", {"layout": "u", "postProfileType": "rect", "postWidth": 60, "postDepth": 40, "handrailProfileType": "rect", "handrailWidth": 70, "handrailDepth": 40})
        add("true-template-cap", {"largePostMode": "middle", "postCapEnabled": True, "postCapModelReference": "template:post-cap"})
        add("unsupported-elevation-double-post", {"layout": "left_l", "pathMode": "continuous", "cornerPostMode": "double"})
        add("invalid-clamp-thickness", {"glassThickness": 12, "glassClipEnabled": True})
    if name == "straight_steel_staircase":
        for route in ("straight", "straight_landing", "l_turn", "u_turn"):
            for kind in ("rect", "channel", "round", "oval"):
                add("route-profile-bracket:" + route + ":" + kind, {"stairRoute": route, "stringerProfileType": kind, "bracketType": "plate"})
            add("route-zigzag:" + route, {"stairRoute": route, "stringerConstruction": "zigzag"})
            add("route-tube-frame-right:" + route, {"stairRoute": route, "treadSupport": "tube_frame", "turnDirection": "right"})
        for route in ("straight_landing", "l_turn", "u_turn"):
            for kind in ("rect", "round", "oval", "racetrack"):
                add("route-continuous-rail:" + route + ":" + kind, {"stairRoute": route, "handrailProfileType": kind, "postProfileType": kind, "infillProfileType": kind})
        add("thick-plate-brackets", {"bracketType": "plate", "bracketThickness": 30})
        add("thick-narrow-plate-brackets", {"bracketType": "plate", "bracketThickness": 12, "treadWidth": 10})
        add("inactive-drafts", {"railingSide": "none", "stairRoute": "straight", "connectionType": "weld", "treadType": "none", "landingLength": -1, "boltHoleDiameter": -1, "treadThickness": -1, "railingHeight": -1})
        add("narrow-u-return", {"stairRoute": "u_turn", "wellGap": 100, "postProfileType": "oval", "postWidth": 100, "postDepth": 20})
        add("invalid-well", {"stairRoute": "u_turn", "wellGap": 0})
    if name.startswith("assembly_"):
        parameters = {p["key"]: p for p in data["parameters"]}
        dims = {key: defaults[key] * 1.2 for key in parameters if key.endswith("Length") and type(defaults[key]) in (int, float)}
        if dims: add("nondefault-member-lengths", dims)
        if "frameWidth" in parameters: add("asymmetric-frame", {"frameWidth": 60, "frameDepth": 40})
    seen, unique = set(), []
    for case in cases:
        signature = json.dumps(case["overrides"], sort_keys=True)
        if signature not in seen:
            seen.add(signature); unique.append(case)
    return unique


def semantic(value, hashes):
    if isinstance(value, dict):
        return {key: semantic(child, hashes) for key, child in value.items() if key not in {
            "functionDigest", "planDigest", "identity", "executionSignature", "sourceIdentity", "sourceExecutionSignature"}}
    if isinstance(value, list): return [semantic(child, hashes) for child in value]
    if isinstance(value, str) and value in hashes: return hashes[value]
    if type(value) in (int, float): return float(round(value, 7)) or 0.0
    return value


def world_geometry(document, include_nodes=False):
    """Compose adjacent transforms exactly; preserve every shape operation.

    Shared resources can flatten a pair of instance placements into one matrix.
    These are equivalent coordinates, not a licence to ignore geometry changes.
    """
    expanded = expand_resource_model(document)
    nodes = {node["key"]: node for node in expanded["geometry"]}
    for node in nodes.values():
        seen = set()
        while node["operator"] == "transform" and len(node["inputs"]) == 1:
            inner = nodes[node["inputs"][0]]
            if (inner["operator"] != "transform" or len(inner["inputs"]) != 1
                    or set(node["arguments"]) != {"placement"}
                    or set(inner["arguments"]) != {"placement"}):
                break
            TEST.assertNotIn(inner["key"], seen, "cyclic transform chain")
            seen.add(inner["key"])
            node["arguments"]["placement"] = _compose(
                node["arguments"]["placement"], inner["arguments"]["placement"])
            node["inputs"] = deepcopy(inner["inputs"])
    return canonical_geometry(expanded, include_nodes)


def missing_relationship_references(document):
    expanded = expand_resource_model(document)
    keys = {item["key"] for item in expanded["items"]}
    return [{"connectionKey": connection["key"], "missingItemKeys": sorted(set(connection["items"]) - keys)}
            for connection in expanded.get("relationships", [])
            if not set(connection["items"]) <= keys]


def part_facts(item):
    properties = item.get("properties", {})
    result = {key: properties[key] for key in ("partNumber", "quantity", "length") if key in properties}
    for fact in ("partKind", "sourcing", "categoryKey", "categoryName"):
        for key in ("manufacturing." + fact, fact):
            if key in properties:
                result[fact] = properties[key]
                break
    for key in ("manufacturing.materialGrade", "manufacturing.material", "materialGrade", "material"):
        if key in properties:
            result["material"] = properties[key]
            break
    return semantic(result, {})


def source_digests():
    paths = set((SRC / "apps/tube-designer/templates").rglob("*.py"))
    paths.update((SRC / "apps/tube-designer/templates").rglob("*.json"))
    paths.update(Path(worker.__file__).parent.rglob("*.py"))
    paths.add(Path(__file__))
    return {str(path.relative_to(SRC)).replace("\\", "/"): digest(path) for path in sorted(paths)}


def audit_digests():
    """Protect audit dependencies separately from the existing source list."""
    directory = Path(__file__).parent.resolve()
    paths = {Path(__file__).resolve()}
    for module in tuple(sys.modules.values()):
        filename = getattr(module, "__file__", None)
        if filename:
            path = Path(filename).resolve()
            if path.suffix == ".py" and path.parent == directory:
                paths.add(path)
    return {str(path.relative_to(SRC)).replace("\\", "/"): digest(path) for path in sorted(paths)}


def frozen_source_digests():
    checkpoint = OUT / "all-products-recheck-source-freeze.json"
    if not checkpoint.exists():
        return {}
    expected = json.loads(checkpoint.read_text(encoding="utf-8"))["sources"]
    return {key: digest(SRC.parent / key) for key in expected}


def check_transform_comparison():
    """A rotated placement chain agrees, but changed shape facts do not."""
    from icax_template_sdk import NeutralModel
    def document(nested, shift=0, vector=10, subtract=False):
        model = NeutralModel(template_id="audit-comparison-control", template_version="1", package_digest="", parameters={})
        section = model.geometry("section", "profile2d", arguments={"contours": [{"kind": "path", "closed": True,
            "segments": [{"kind": "line", "start": a, "end": b} for a, b in
                         (([0, 0], [2, 0]), ([2, 0], [2, 3]), ([2, 3], [0, 3]), ([0, 3], [0, 0]))]}]})
        shape = model.geometry("body", "extrude", inputs=[section], arguments={"vector": [0, 0, vector]})
        if subtract:
            shape = model.geometry("cut", "boolean", inputs=[shape, shape], arguments={"operation": "subtract"})
        outer = {"origin": [11 + shift, 12, 13], "xAxis": [0, 1, 0], "yAxis": [-1, 0, 0], "zAxis": [0, 0, 1]}
        inner = {"origin": [2, 3, 4], "xAxis": [1, 0, 0], "yAxis": [0, 0, 1], "zAxis": [0, -1, 0]}
        if nested:
            shape = model.geometry("inner", "transform", inputs=[shape], arguments={"placement": inner})
            frame = outer
        else:
            frame = _compose(outer, inner)
        shape = model.geometry("outer", "transform", inputs=[shape], arguments={"placement": frame})
        model.item("part", "Control", representations={"result": shape}, properties={})
        return model.build()
    nested = world_geometry(document(True))
    TEST.assertEqual(nested, world_geometry(document(False)))
    for changed in (document(False, shift=0.1), document(False, vector=11), document(False, subtract=True)):
        TEST.assertNotEqual(nested, world_geometry(changed))
    return {"rotatedTransformComposition": True, "changedPositionRejected": True,
            "changedExtrusionRejected": True, "changedBooleanRejected": True}


def attempt(action):
    try: return action(), None
    except Exception as exc:
        return None, {"type": type(exc).__name__, "message": str(exc), "trace": traceback.format_exc(limit=8)}


def audit_case(path, data, defaults, scenario):
    started = time.monotonic()
    module = load(path)
    values = {**deepcopy(defaults), **deepcopy(scenario["overrides"])}
    record = {"template": str(path.parent.relative_to(ROOT)).replace("\\", "/"), **scenario,
              "entryDigest": digest(path), "parameters": deepcopy(values)}
    record["readOnlyOverrides"] = [entry["key"] for entry in data["parameters"]
        if entry.get("readOnly") and entry["key"] in scenario["overrides"]
        and scenario["overrides"][entry["key"]] != entry["defaultValue"]]
    frozen_values = deepcopy(values)
    before, old_error = attempt(lambda: module.generate(values, {"template": data, "geometryPurpose": "manufacturing"}))
    TEST.assertEqual(values, frozen_values)
    record["oldManufacturing"] = {"valid": old_error is None, "error": old_error}
    if before is not None:
        TEST.assertEqual(before["parameters"], frozen_values)
        old_missing = missing_relationship_references(before)
        record["oldManufacturing"].update(referencesValid=not old_missing, missingReferences=old_missing)
    design, design_error = attempt(lambda: module.display(values))
    TEST.assertEqual(values, frozen_values)
    record["display"] = {"valid": design_error is None, "error": design_error}
    if design is not None:
        assert_display_model(TEST, design)
        record["display"].update(items=len(design["items"]), resources=len(design["resources"]),
            sharedResourceInstances=max(Counter(item["geometry"]["resource"] for item in design["items"] if "geometry" in item).values(), default=0))
    def public_declaration():
        with patch.object(module, "display", side_effect=AssertionError("manufacturing called display")):
            with forbid_processing() as calls:
                declaration = module.manufacturing(values)
            TEST.assertEqual(calls, [])
        assert_declaration(TEST, declaration)
        for connection in declaration["connections"]:
            anchors = connection.get("properties", {}).get("participantAnchors", [])
            anchor_keys = [anchor["itemKey"] for anchor in anchors]
            TEST.assertTrue(set(anchor_keys) <= set(connection["items"]), connection["key"])
            TEST.assertEqual(len(anchor_keys), len(set(anchor_keys)), connection["key"])
        return declaration
    declaration, declaration_error = attempt(public_declaration)
    TEST.assertEqual(values, frozen_values)
    record["manufacturing"] = {"valid": declaration_error is None, "error": declaration_error}
    actual, execution_error = None, None
    if design is not None and declaration is not None:
        frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
        def execute():
            assert_declaration(TEST, declaration, design)
            original_loader = worker._load_template
            executor_path = SRC / "apps/tube-designer/templates/_shared/assembly_manufacturing_executor.py"
            def load_executor_only(path, *arguments, **keywords):
                TEST.assertEqual(Path(path).resolve(), executor_path.resolve(), "executor loaded product script")
                return original_loader(path, *arguments, **keywords)
            with patch.object(worker, "_load_template", side_effect=load_executor_only):
                return execute_definition(declaration, design=design)
        actual, execution_error = attempt(execute)
        TEST.assertEqual(design, frozen_design)
        TEST.assertEqual(declaration, frozen_declaration)
        record["manufacturing"].update(processes=len(declaration["processes"]), connections=len(declaration["connections"]))
    record["execution"] = {"valid": actual is not None, "error": execution_error}
    if old_error is not None:
        record["status"] = "existing-invalid" if actual is None else "old-invalid-now-accepted"
    elif design_error or declaration_error or execution_error:
        record["status"] = "existing-invalid-references" if old_missing else "regression-rejected"
    else:
        expected_hashes, actual_hashes = world_geometry(before), world_geometry(actual)
        changed = sorted(key for key in set(expected_hashes) | set(actual_hashes)
                         if expected_hashes.get(key) != actual_hashes.get(key))
        record["geometryCompatibility"] = {"equal": not changed, "changedItemKeys": changed}
        expected_items, actual_items = {i["key"]: i for i in before["items"]}, {i["key"]: i for i in actual["items"]}
        record["identityCompatibility"] = set(expected_items) == set(actual_items)
        changed_facts = sorted(key for key in set(expected_items) | set(actual_items)
            if part_facts(expected_items.get(key, {})) != part_facts(actual_items.get(key, {})))
        record["partFactsCompatibility"] = {"equal": not changed_facts, "changedItemKeys": changed_facts}
        expected_processes = before.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
        actual_processes = actual.get("extensions", {}).get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
        expected_nodes, actual_nodes = world_geometry(before, True), world_geometry(actual, True)
        expected_results = {i["instanceId"]: semantic(i["result"], expected_nodes) for i in expected_processes}
        actual_results = {i["instanceId"]: semantic(i["result"], actual_nodes) for i in actual_processes}
        changed_results = sorted(key for key in set(expected_results) | set(actual_results)
                                 if expected_results.get(key) != actual_results.get(key))
        record["processResultsCompatibility"] = {"equal": not changed_results, "changedInstanceIds": changed_results}
        record["execution"]["items"] = len(actual["items"])
        if changed or changed_results or changed_facts or not record["identityCompatibility"]:
            record["status"] = "regression-different"
            record["expectedGeometryHashes"] = expected_hashes
            record["actualGeometryHashes"] = actual_hashes
        else:
            record["status"] = "passed"
    record["inputsUnchanged"] = values == frozen_values
    record["seconds"] = round(time.monotonic() - started, 3)
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", action="append", default=[])
    parser.add_argument("--label", default="")
    parser.add_argument("--suffix", default="")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    verified = OUT / "all-products-recheck-generic-verified-synthetic-system.json"
    demo = json.loads((SRC / "apps/tube-designer/docs/deferred-products/aluminium-window/reference/systems/demonstration.json").read_text(encoding="utf-8"))
    demo.update(status="verified", manufacturer="UNIT TEST ONLY", source="Synthetic public-entry audit fixture; not production manufacturer data")
    verified.write_text(json.dumps(demo, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    report = {"schema": "icax.all-products-generic-source-audit", "sourceOnly": True,
              "scope": "All non-security public filesystem packages and all archived wrappers; host discovery verified separately",
              "syntheticVerifiedSystem": str(verified), "templates": [], "archived": [], "cases": []}
    report["comparisonControls"] = check_transform_comparison()
    report["sourceDigestsStart"] = source_digests()
    report["auditDigestsStart"] = audit_digests()
    report["fixtureDigestStart"] = digest(verified)
    report["frozenSourceDigestsStart"] = frozen_source_digests()
    if args.suffix == "-final-frozen":
        checkpoint = json.loads((OUT / "all-products-recheck-source-freeze.json").read_text(encoding="utf-8"))
        TEST.assertEqual(report["frozenSourceDigestsStart"], checkpoint["sources"], "Sources differ from frozen checkpoint")
    for path in sorted((ROOT / "_legacy").glob("*/template.json")):
        data, defaults = descriptor(path)
        entry = path.with_name("template.py")
        module, error = attempt(lambda: load(entry))
        report["archived"].append({"template": str(path.parent.relative_to(ROOT)).replace("\\", "/"),
            "entryDigest": digest(entry), "descriptorId": data["id"], "loadable": error is None,
            "error": error, "hasDisplay": bool(module and hasattr(module, "display")),
            "hasManufacturing": bool(module and hasattr(module, "manufacturing")),
            "descriptorChoices": {p["key"]: [c["value"] for c in p.get("choices", [])]
                                  for p in data["parameters"] if p.get("choices")},
            "status": "archived-existing-unreachable" if error else "archived-old-entry-only"})
    planned = []
    for path in sorted(ROOT.glob("*/template.json")):
        name = path.parent.name
        if "security_window" in name or args.only and name not in args.only: continue
        data, defaults = descriptor(path)
        cases = scenarios(name, data, defaults, verified)
        for case in cases:
            unknown = set(case["overrides"]) - set(defaults)
            if unknown:
                raise AssertionError((name, case["label"], "audit used undeclared parameter", sorted(unknown)))
        if args.label: cases = [case for case in cases if args.label in case["label"]]
        report["templates"].append({"template": name, "descriptorId": data["id"], "scenarioCount": len(cases),
            "defaults": defaults, "descriptorChoices": {p["key"]: [c["value"] for c in p.get("choices", [])]
                          for p in data["parameters"] if p.get("choices")},
            "parameterConditions": {p["key"]: {key: p[key] for key in ("readOnly", "visibleWhen", "enabledWhen") if key in p}
                                    for p in data["parameters"] if any(key in p for key in ("readOnly", "visibleWhen", "enabledWhen"))},
            "booleanKeys": [p["key"] for p in data["parameters"] if p.get("valueType") == "boolean"]})
        planned.extend((path.with_name("template.py"), data, defaults, case) for case in cases)
    destination = OUT / ("all-products-recheck-generic" + args.suffix + ".json")
    report["plannedCases"] = len(planned)
    def save():
        report["statusCounts"] = dict(Counter(case["status"] for case in report["cases"]))
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    save()
    print("PLANNED", len(planned), "TEMPLATES", len(report["templates"]), "ARCHIVED", len(report["archived"]), flush=True)
    for index, arguments in enumerate(planned, 1):
        result, error = attempt(lambda: audit_case(*arguments))
        if error:
            path, _, _, case = arguments
            result = {"template": path.parent.name, **case, "status": "audit-assertion-failed", "error": error}
        report["cases"].append(result)
        save()
        print(index, "/", len(planned), result["template"], result["label"], result["status"], flush=True)
        if result["status"].startswith("regression") or error:
            print(json.dumps({"overrides": result["overrides"], "failure": error or result.get("execution", {}).get("error") or result.get("manufacturing", {}).get("error") or result.get("geometryCompatibility")}, ensure_ascii=True), flush=True)
    report["sourceDigestsEnd"] = source_digests()
    report["changedSourceFiles"] = sorted(key for key in set(report["sourceDigestsStart"]) | set(report["sourceDigestsEnd"])
        if report["sourceDigestsStart"].get(key) != report["sourceDigestsEnd"].get(key))
    report["sourcesUnchangedDuringRun"] = not report["changedSourceFiles"]
    report["auditDigestsEnd"] = audit_digests()
    report["changedAuditFiles"] = sorted(key for key in set(report["auditDigestsStart"]) | set(report["auditDigestsEnd"])
        if report["auditDigestsStart"].get(key) != report["auditDigestsEnd"].get(key))
    report["auditUnchangedDuringRun"] = not report["changedAuditFiles"]
    report["fixtureDigestEnd"] = digest(verified)
    report["fixtureUnchangedDuringRun"] = report["fixtureDigestStart"] == report["fixtureDigestEnd"]
    report["frozenSourceDigestsEnd"] = frozen_source_digests()
    report["frozenSourcesUnchangedDuringRun"] = report["frozenSourceDigestsStart"] == report["frozenSourceDigestsEnd"]
    save()
    print("SUMMARY", json.dumps(report["statusCounts"], sort_keys=True), flush=True)
    print("REPORT", str(destination), flush=True)
    return int(not report["sourcesUnchangedDuringRun"] or not report["frozenSourcesUnchangedDuringRun"]
        or not report["auditUnchangedDuringRun"]
        or not report["fixtureUnchangedDuringRun"] or any(case["status"] in {
        "regression-rejected", "regression-different", "audit-assertion-failed", "old-invalid-now-accepted",
        "existing-invalid-references"} for case in report["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
