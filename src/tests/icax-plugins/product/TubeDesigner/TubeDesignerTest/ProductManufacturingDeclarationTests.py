"""A product declares manufacturing; an independent assembly executor performs it."""
import contextlib
from copy import deepcopy
import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from WindowCatalogueTests import SRC, package
from icax_template_sdk import NeutralModel, expand_resource_model
from icax_template_sdk.manufacturing import validate_manufacturing_model, compose_manufacturing_model
import icax_template_worker as worker


FIELDS = {"schema", "schemaVersion", "coordinateSystem", "lengthUnit", "resources",
          "items", "roots", "sourceMappings", "processes"}
SHARED_ROOT = SRC / "apps/tube-designer/templates/_shared"
BASELINE_FILE = SRC.parent / "output/tests/assembly-process/manufacturing-before.json"
FORBIDDEN = {
    "security_window_frame_paths.py": {"plan"},
    "assembly_window_process.py": {"plan", "build_route"},
    "assembly_geometry_process_runtime.py": {"evaluate", "apply", "materialize_resolved_tool_plan", "apply_frozen_cutters"},
    "assembly_template_runtime.py": {"evaluate_process", "resolve_process_plan", "resolve_bound_plan"},
    "assembly_process_functions.py": {"evaluate", "resolve"},
    "assembly_fold_functions.py": {"evaluate_fold", "_evaluate"},
    "punch_tool_runtime.py": {"_evaluate", "generate", "_analyze_mould"},
    "punch_tool_package_runtime.py": {"generate"},
}


@contextlib.contextmanager
def forbid_processing():
    """Inspect real code paths, including providers loaded under another module name."""
    previous = sys.getprofile()
    calls = []
    emit = NeutralModel.geometry

    def guard(frame, event, unused):
        if event != "call":
            return
        filename = frame.f_code.co_filename.replace("\\", "/")
        name = frame.f_code.co_name
        basename = filename.rsplit("/", 1)[-1]
        forbidden = name in FORBIDDEN.get(basename, ())
        forbidden |= "/templates/assembly/" in filename and name in {
            "build_plan", "build_bound_operations", "build_formed_preview"}
        forbidden |= "/templates/mold/" in filename and name in {"build", "generate", "_build"}
        if basename in {"assembly_tube_machining.py", "assembly_surface_machining.py", "assembly_structural_machining.py"}:
            functions = frame.f_globals.get("FUNCTIONS", {})
            forbidden |= name in {function.__name__ for function in functions.values() if callable(function)}
            forbidden |= frame.f_code.co_qualname.startswith(("Tools.", "_Graph."))
            forbidden |= name in {"_result", "result", "_plane_cutter", "joint_plane"}
        if forbidden:
            calls.append((filename, name))
            raise AssertionError("Product declaration executed processing: " + filename + ":" + name)
        if previous is not None:
            previous(frame, event, unused)

    def geometry(model, key, operator, *args, **kwargs):
        if operator == "boolean":
            raise AssertionError("Product declaration emitted a machining Boolean: " + key)
        return emit(model, key, operator, *args, **kwargs)

    try:
        with patch.object(NeutralModel, "geometry", geometry):
            sys.setprofile(guard)
            yield calls
    finally:
        sys.setprofile(previous)


def assert_declaration(test, document, design=None):
    before = deepcopy(document)
    validate_manufacturing_model(document)
    test.assertEqual(document, before)
    test.assertEqual(document["schema"], "icax.manufacturing-model")
    test.assertEqual(document["schemaVersion"], 4)
    test.assertEqual(set(document), {"schema", "schemaVersion", "connections", "processes"})
    processes = {process["key"]: process for process in document["processes"]}
    test.assertEqual(len(processes), len(document["processes"]))
    for process in processes.values():
        test.assertEqual(set(process), {"key", "kind", "definition"})
        test.assertEqual(process["kind"], "assembly-process")
        definition = process["definition"]
        common = {"templateId", "processInput", "parameters", "processDrafts", "dependencies"}
        test.assertIn(set(definition), (common | {'targets'}, common | {'outputs'}))
        local = definition["processInput"]
        test.assertEqual(local["schema"], "icax.assembly-process-input")
        test.assertIn(local["schemaVersion"], (2, 3))
        test.assertNotIn('connections', local['geometry'])
        test.assertTrue(set(definition["dependencies"]) <= set(processes))
        test.assertNotIn(process["key"], definition["dependencies"])
        for key in ("result", "resultGeometry", "targetGeometry", "functionDigest", "executionSignature",
                    "applicable", "resolvedPlan", "operations", "forming", "materialRequirements", "bom"):
            test.assertNotIn(key, definition)
    if design is not None:
        frozen = deepcopy(design)
        test.assertEqual(set(design), {'schema', 'schemaVersion', 'coordinateSystem', 'lengthUnit',
                                      'resources', 'items', 'roots', 'annotations'})
        test.assertEqual(design['schemaVersion'], 2)
        combined = compose_manufacturing_model(document, design)
        test.assertEqual(combined['schemaVersion'], 3)
        test.assertEqual(design, frozen)
        test.assertEqual(document, before)
        expected_keys = {item['key'] for item in design['items']}
        selections = [process['definition'] for process in document['processes']
                      if process['definition']['templateId'] == 'product-manufacturing-members']
        if selections:
            test.assertEqual(len(selections), 1)
            expected_keys = set(selections[0]['targets'].values())
            parents = {child: item['key'] for item in design['items'] for child in item['children']}
            for selected in list(expected_keys):
                while selected in parents:
                    selected = parents[selected]
                    expected_keys.add(selected)
        test.assertEqual({item['key'] for item in combined['items']}, expected_keys)
        dynamic_keys = {item['key'] for item in combined['items'] if 'designInput' in item}
        test.assertEqual({mapping['itemKey'] for mapping in combined['sourceMappings']}, dynamic_keys)
        for mapping in combined['sourceMappings']:
            test.assertEqual(mapping['sources'], [{'itemKey': mapping['itemKey']}])
        if not dynamic_keys:
            test.assertEqual(combined['sourceMappings'], [])
    return document


def execute_definition(document, context=None, design=None):
    request = {"manufacturingDefinition": deepcopy(document),
               "context": {"sharedRoot": str(SHARED_ROOT), **(context or {})}}
    if design is not None:
        request['designModel'] = deepcopy(design)
    return worker._execute_manufacturing(request)


def canonical_geometry(document, include_nodes=False):
    """Compare complete DAG content, independent of aliases and inert transforms."""
    expanded = expand_resource_model(document)
    nodes = {node["key"]: node for node in expanded["geometry"]}
    hashes = {}
    identity = {"origin": [0, 0, 0], "xAxis": [1, 0, 0],
                "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}

    def value(child):
        if isinstance(child, str) and child in nodes:
            return node_hash(child)
        if isinstance(child, dict):
            return {key: value(entry) for key, entry in child.items()}
        if isinstance(child, list):
            return [value(entry) for entry in child]
        if isinstance(child, (int, float)) and not isinstance(child, bool):
            return float(round(child, 7)) or 0.0
        return child

    def node_hash(key):
        if key not in hashes:
            node = nodes[key]
            placement = node["arguments"].get("placement")
            inert = (node["operator"] == "transform" and len(node["inputs"]) == 1
                     and set(node["arguments"]) == {"placement"} and isinstance(placement, dict)
                     and set(placement) == set(identity)
                     and all(len(placement[axis]) == 3 and all(abs(a-b) < 1e-7 for a, b in
                         zip(placement[axis], identity[axis])) for axis in identity))
            if inert:
                hashes[key] = node_hash(node["inputs"][0])
            else:
                content = value({field: child for field, child in node.items() if field != "key"})
                hashes[key] = hashlib.sha256(json.dumps(content, sort_keys=True,
                    separators=(",", ":")).encode()).hexdigest()
        return hashes[key]

    if include_nodes:
        return {key: node_hash(key) for key in nodes}
    return {item["key"]: {purpose: node_hash(key) for purpose, key in item["representations"].items()}
            for item in expanded["items"]}


class ProductManufacturingDeclarationTests(unittest.TestCase):
    @unittest.skip("Deferred product reference; no current active product generation")
    def test_output_roles_require_the_same_manufacturing_binding(self):
        _, defaults, template = package("decorative_door")
        declaration = template.manufacturing(defaults)
        original = deepcopy(declaration)
        for error in ("display-observer", "different-item", "missing-role", "empty-targets"):
            with self.subTest(error=error):
                invalid = deepcopy(declaration)
                definition = invalid["processes"][0]["definition"]
                role, target = next(iter(definition["targets"].items()))
                if error == "display-observer":
                    definition["processInput"]["parts"][role] = {"scope": "display", "itemKey": target}
                elif error == "different-item":
                    other = next(item["key"] for item in template.display(defaults)["items"] if item["key"] != target)
                    definition["processInput"]["parts"][role]["itemKey"] = other
                elif error == "missing-role":
                    del definition["processInput"]["parts"][role]
                else:
                    definition["targets"] = {}
                with self.assertRaises(ValueError):
                    validate_manufacturing_model(invalid)
        self.assertEqual(declaration, original)

    def test_continuous_frame_keeps_all_contact_apertures_after_seam_allocation(self):
        _, defaults, template = package("single_face_security_window")
        values = dict(defaults, accessDoorEnabled=False, faceType="single", frameManufacturingMode="plane_v_notch",
                      verticalMaximumCenterSpacing=600, sideVerticalMaximumCenterSpacing=600,
                      topBottomRodMaximumCenterSpacing=600)
        before = deepcopy(values)
        with forbid_processing() as calls:
            declaration = template.manufacturing(values)
        self.assertEqual(calls, [])
        self.assertEqual(values, before)
        assert_declaration(self, declaration)
        saved = deepcopy(declaration)
        executed = execute_definition(declaration, design=template.display(values))
        self.assertEqual(declaration, saved)
        source = executed["extensions"]["tubeDesigner.assemblyProcessSource"]
        processes = source["instances"]
        outer_apertures = [process for process in processes if process["templateId"] == "tube-profile-aperture"
                          and process["targets"].get("stock") == "outer_frame.continuous.0001"]
        self.assertEqual(len(outer_apertures), 15, "An aperture crossing the stock seam must be split, not dropped")
        self.assertEqual(sum(process["templateId"] == "tube-profile-aperture" for process in processes), 27)
        self.assertEqual(sum(process["templateId"] == "tube-end-joint" for process in processes), 14)
        self.assertEqual(sum(process["templateId"] == "node-v-notch-integrated" for process in processes), 4)
        self.assertEqual(len(processes), 45)
        self.assertEqual(len(executed["items"]), 8)

    def test_nondefault_miter_frame_defers_end_cuts_and_executes_equivalent_geometry(self):
        descriptor, defaults, template = package("minimal_protective_grille")
        values = dict(defaults, frameType="closed_frame", frameCornerJoint="miter_45")
        before = deepcopy(values)
        with forbid_processing() as calls:
            declaration = template.manufacturing(values)
        self.assertEqual(calls, [])
        self.assertEqual(values, before)
        assert_declaration(self, declaration)
        saved = deepcopy(declaration)
        executed = execute_definition(declaration, design=template.display(values))
        previous = template.generate(values, {"template": descriptor, "geometryPurpose": "manufacturing"})
        self.assertEqual(declaration, saved)
        self.assertEqual(values, before)
        self.assertEqual(len(executed["items"]), 14)
        self.assertEqual(canonical_geometry(executed), canonical_geometry(previous))

    def test_processing_guard_rejects_actual_runtime_under_two_dynamic_aliases(self):
        path = SHARED_ROOT / "assembly_geometry_process_runtime.py"
        for alias in ("test_real_processing_guard_a", "test_real_processing_guard_b"):
            spec = importlib.util.spec_from_file_location(alias, path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with self.subTest(alias=alias):
                with self.assertRaisesRegex(AssertionError, "Product declaration executed processing"):
                    with forbid_processing():
                        module.evaluate("tube-end-joint", {}, {})
        with self.assertRaisesRegex(AssertionError, "machining Boolean"):
            with forbid_processing():
                NeutralModel(template_id="test", template_version="1", package_digest="test", parameters={}).geometry(
                    "invalid.cut", "boolean", arguments={})

    def test_all_ten_products_declare_with_actual_processing_disabled(self):
        baselines = [entry for entry in json.loads(BASELINE_FILE.read_text(encoding="utf-8"))
                     if entry["name"] not in {"louver_window", "aluminium_window", "decorative_door"}]
        self.assertEqual(len(baselines), 10)
        for baseline in baselines:
            with self.subTest(product=baseline["name"]):
                _, _, template = package(baseline["name"])
                values = deepcopy(baseline["parameters"])
                before = deepcopy(values)
                with forbid_processing() as calls:
                    declaration = template.manufacturing(values)
                self.assertEqual(calls, [])
                self.assertEqual(values, before)
                assert_declaration(self, declaration)
                design = template.display(values)
                assert_declaration(self, declaration, design)
                self.assertEqual(declaration['schemaVersion'], 4)
                self.assertNotIn('items', declaration)
                self.assertNotIn('resources', declaration)


    def test_plane_spatial_and_opening_frames_declare_stable_design_without_planning_routes(self):
        _, defaults, template = package("single_face_security_window")
        for face in ("single", "two", "three", "five"):
            for mode in ("segment_weld", "plane_v_notch", "spatial_v_notch"):
                with self.subTest(face=face, mode=mode):
                    values = dict(defaults, faceType=face, frameManufacturingMode=mode, accessDoorEnabled=True,
                        doorFrameJoinType="v_groove_90:tool_library", doorLeafFrameJoinType="v_groove_90:tool_library")
                    before = deepcopy(values)
                    with forbid_processing() as calls:
                        declaration = template.manufacturing(values)
                    self.assertEqual(calls, [])
                    self.assertEqual(values, before)
                    assert_declaration(self, declaration)
                    design = template.display(values)
                    assert_declaration(self, declaration, design)
                    self.assertTrue(all('assemblyFrame.member' in item['properties'] for item in design['items']))
                    self.assertEqual(len(declaration['processes']), 1)
                    self.assertIn('outputs', declaration['processes'][0]['definition'])

    @unittest.skip("Deferred product reference; no current active product generation")
    def test_worker_execution_never_calls_product_scripts_and_does_not_mutate_declaration(self):
        _, values, template = package("decorative_door")
        with forbid_processing():
            declaration = template.manufacturing(values)
        before = deepcopy(declaration)
        design = template.display(values)
        frozen_design = deepcopy(design)
        previous = sys.getprofile()
        def guard(frame, event, unused):
            if event == "call" and "/templates/product/" in frame.f_code.co_filename.replace("\\", "/"):
                raise AssertionError("Independent assembly execution called a product script")
        try:
            sys.setprofile(guard)
            executed = execute_definition(declaration, design=design)
        finally:
            sys.setprofile(previous)
        self.assertEqual(declaration, before)
        self.assertEqual(design, frozen_design)
        self.assertEqual(executed["schema"], "icax.neutral-model")
        self.assertEqual(executed["schemaVersion"], 2)
        expanded = expand_resource_model(executed)
        self.assertTrue(any(node["operator"] == "boolean" for node in expanded["geometry"]))


if __name__ == "__main__":
    unittest.main()
