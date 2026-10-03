"""Host-owned materials and repeatable local assembly-process execution."""
import copy
import importlib.util
import json
import math
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[6]
TEMPLATES = ROOT / "src/apps/tube-designer/templates"
SPEC = importlib.util.spec_from_file_location(
    "assembly_process_reuse_runtime", TEMPLATES / "_shared/assembly_template_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)
FOLD_IDS = {"bend", "segmented-bend", "node-v-notch-integrated",
            "node-embedded-arc-integrated", "node-edge-arc-integrated",
            "flexible-slit-bend-integrated"}


def stock(length=1000, stock_id="tube-1"):
    return {"id": stock_id, "profileRef": {"scope": "system", "id": "rect"},
            "parameters": {"width": 60, "depth": 40, "wallThickness": 2,
                           "cornerRadius": 3, "innerRadius": 1}, "length": length}


def process(station=500, rotation=0, angle=90):
    return {"schema": "icax.assembly-process-input", "schemaVersion": 1,
            "parts": {"stock": stock()},
            "geometry": {"angle": angle, "station": station, "rotation": rotation}}


def instance(instance_id, station, rotation=0, angle=90, drafts=None):
    return {"instanceId": instance_id, "templateId": "node-v-notch-integrated",
            "targets": {"stock": "tube-1"}, "processInput": process(station, rotation, angle),
            "parameters": {}, "processDrafts": drafts or {}}


def defaults(template_id):
    descriptor = runtime._template_by_id(template_id)
    descriptor = {**descriptor, "parameters": [p for p in descriptor["parameters"]
                                               if p.get("scope", "process") != "scene"]}
    return runtime._validated_values(descriptor, {})


def example_input(template_id):
    """The host chooses actual processing ends/faces before calling a function."""
    descriptor = runtime._template_by_id(template_id)
    product = runtime.finished_products.validate(
        runtime._load_example_product_script(template_id)(defaults(template_id)))
    local = runtime.process_contract.from_product(
        descriptor, product, runtime.finished_product_plan(product))
    anchors = descriptor.get("productBinding", {}).get("anchors", {})
    for role, part in local["parts"].items():
        if anchors.get(role) == "end":
            matrix = part["matrix"]
            start = [matrix[3], matrix[7], matrix[11]]
            end = [start[i]+matrix[i*4]*part["length"] for i in range(3)]
            selected = "start" if sum(v*v for v in start) <= sum(v*v for v in end) else "end"
            part["anchor"] = {"kind": "end", "end": selected}
        else:
            matrix = part["matrix"]
            center = [matrix[4*i+3]+matrix[4*i]*part["length"]/2 for i in range(3)]
            transverse = -sum(matrix[4*i+1]*center[i] for i in range(3))
            radial = -sum(matrix[4*i+2]*center[i] for i in range(3))
            face = ("top" if radial >= 0 else "bottom") if abs(radial) >= abs(transverse) else (
                "right" if transverse >= 0 else "left")
            part["anchor"] = {"kind": "side", "reference": "start",
                              "station": part["length"]/2, "face": face}
    return local


class AssemblyProcessReuseTests(unittest.TestCase):
    def test_all_22_public_example_products_remain_available(self):
        for manifest in sorted((TEMPLATES / "assembly").glob("*/assembly.json")):
            template_id = json.loads(manifest.read_text(encoding="utf-8"))["id"]
            with self.subTest(template=template_id):
                result = runtime.get_example_product(template_id)
                self.assertEqual(template_id, result["templateId"])
                self.assertEqual("icax.finished-product", result["finishedProduct"]["schema"])

    def test_all_22_templates_evaluate_without_finished_shape_contract(self):
        manifests = sorted((TEMPLATES / "assembly").glob("*/assembly.json"))
        self.assertEqual(22, len(manifests))
        catalogue = runtime.catalogue()
        self.assertEqual([], catalogue["errors"])
        self.assertEqual(17, len(catalogue["assemblies"]))
        tested = []
        for manifest in manifests:
            raw = json.loads(manifest.read_text(encoding="utf-8"))
            template_id = raw["id"]
            with self.subTest(template=template_id):
                self.assertNotIn("productInput", raw)
                self.assertIn("inputContract", raw)
                self.assertIn("exampleInput", raw)
                supplied = defaults(template_id)
                if template_id in FOLD_IDS:
                    local = process()
                else:
                    local = example_input(template_id)
                self.assertNotIn("shapeId", local)
                original = copy.deepcopy((local, supplied))
                result = runtime.evaluate_process(template_id, local, supplied, {})
                self.assertTrue(result["applicable"], result["reason"])
                self.assertEqual(supplied, result["parameters"])
                self.assertEqual(original, (local, supplied))
                self.assertNotIn("finishedProduct", result)
                for _ in range(3):
                    self.assertEqual(result, runtime.evaluate_process(template_id, local, supplied, {}))
                    self.assertEqual(original, (local, supplied))
                tested.append(template_id)
        self.assertEqual(22, len(tested))

    def test_all_six_fold_functions_accept_multiple_stations_angles_and_frames(self):
        for template_id in sorted(FOLD_IDS):
            supplied = defaults(template_id)
            if "bendRadius" in supplied:
                supplied["bendRadius"] = 120
            for station, rotation, angle in ((200, 0, 45), (500, 90, 75), (800, 180, -110)):
                with self.subTest(template=template_id, station=station, rotation=rotation, angle=angle):
                    local = process(station, rotation, angle)
                    before = copy.deepcopy((local, supplied))
                    result = runtime.evaluate_process(template_id, local, supplied, {})
                    self.assertTrue(result["applicable"], result["reason"])
                    self.assertEqual(before, (local, supplied))
                    self.assertEqual(supplied, result["parameters"])
                    self.assertEqual(1, len(result["materialRequirements"]))
                    requirement, forming = result["materialRequirements"][0], result["forming"][0]
                    self.assertEqual(0, requirement["lengthAddition"])
                    self.assertLessEqual(0, requirement["interval"][0])
                    self.assertGreaterEqual(1000, requirement["interval"][1])
                    self.assertEqual(station, forming["station"])
                    self.assertEqual(angle, forming["angle"])
                    target = forming["targetTransform"]
                    radians = math.radians(rotation)
                    direction = [0, -math.sin(radians), math.cos(radians)]
                    self.assertAlmostEqual(math.cos(math.radians(angle)), target[0])
                    self.assertAlmostEqual(direction[1]*math.sin(math.radians(angle)), target[4])
                    self.assertAlmostEqual(direction[2]*math.sin(math.radians(angle)), target[8])
                    for cut in result["operations"]:
                        self.assertEqual(station, cut["requestFeature"]["station"])

    def test_different_fold_functions_share_one_host_stock_without_adding_material(self):
        raw = stock(2000)
        instances = []
        ids = ("bend", "segmented-bend", "node-v-notch-integrated", "node-embedded-arc-integrated",
               "node-edge-arc-integrated", "flexible-slit-bend-integrated")
        for index, template_id in enumerate(ids):
            local = process(150+300*index, (index % 3)*90, -70 if index % 2 else 70)
            local["parts"]["stock"] = copy.deepcopy(raw)
            supplied = defaults(template_id)
            if "bendRadius" in supplied:
                supplied["bendRadius"] = 120
            instances.append({"instanceId": f"mixed-{index}", "templateId": template_id,
                              "targets": {"stock": raw["id"]}, "processInput": local,
                              "parameters": supplied})
        before = copy.deepcopy((raw, instances))
        plan = runtime.resolve_process_plan([raw], instances)
        self.assertEqual(before, (raw, instances))
        self.assertEqual(1, len(plan["stocks"]))
        self.assertEqual(1, len(plan["manufacturingParts"]))
        self.assertEqual(6, len(plan["instances"]))
        self.assertEqual(6, len(plan["forming"]))
        self.assertEqual(5, len(plan["manufacturingParts"][0]["request"]["features"]))
        self.assertEqual(2000, plan["manufacturingParts"][0]["request"]["length"])
        self.assertTrue(all(item["lengthAddition"] == 0 for item in plan["materialRequirements"]))
        self.assertEqual(plan, runtime.resolve_process_plan([raw], instances))
        cleared = runtime.resolve_process_plan([raw], [])
        self.assertEqual(2000, cleared["manufacturingParts"][0]["request"]["length"])
        self.assertEqual([], cleared["manufacturingParts"][0]["request"]["features"])
        self.assertEqual([], cleared["forming"])

    def test_single_stock_operations_preserve_rotation_and_signed_direction(self):
        for station, rotation, angle in ((150, 0, 90), (500, 90, 90), (850, 180, -70)):
            with self.subTest(station=station, rotation=rotation, angle=angle):
                local, supplied, drafts = process(station, rotation, angle), {}, {}
                original = copy.deepcopy((local, supplied, drafts))
                result = runtime.evaluate_process("node-v-notch-integrated", local, supplied, drafts)
                self.assertTrue(result["applicable"], result["reason"])
                self.assertEqual({}, result["parameters"])
                self.assertEqual(original, (local, supplied, drafts))
                self.assertEqual(1, len(result["operations"]))
                operation = result["operations"][0]
                self.assertEqual(station, operation["requestFeature"]["station"])
                expected_rotation = rotation+(180 if angle < 0 else 0)
                actual_rotation = operation["requestFeature"]["rotation"]
                self.assertAlmostEqual(0, (actual_rotation-expected_rotation+180) % 360-180)
                self.assertEqual(angle, result["forming"][0]["angle"])
                transform = result["forming"][0]["targetTransform"]
                radians = math.radians(rotation)
                direction = [0, -math.sin(radians), math.cos(radians)]
                expected_z = math.sin(math.radians(angle))
                self.assertAlmostEqual(math.cos(math.radians(angle)), transform[0])
                self.assertAlmostEqual(direction[1]*expected_z, transform[4])
                self.assertAlmostEqual(direction[2]*expected_z, transform[8])

    def assert_preview_matches_function(self, template_id, local):
        before = copy.deepcopy(local)
        evaluated = runtime.evaluate_process(template_id, local)
        self.assertTrue(evaluated["applicable"], evaluated["reason"])
        preview = runtime.preview_plan(template_id, process_input=local)
        self.assertEqual(before, local)
        self.assertEqual(evaluated["operations"], preview["resolvedWorkflow"]["operations"])
        by_role = {part["sourceRole"]: part["request"] for part in preview["manufacturingParts"]}
        for operation in evaluated["operations"]:
            request = by_role[operation["role"]]
            if "requestFeature" in operation:
                actual = next(f for f in request["features"] if f["id"] == operation["id"])
                self.assertEqual(actual, {**operation["requestFeature"], "id": operation["id"]})
            else:
                self.assertEqual(operation["requestEnd"], request["ends"][operation["end"]])
        return preview

    def test_public_preview_places_fastener_arrays_at_the_actual_selected_sites(self):
        descriptor = runtime._template_by_id("mechanical-fastener")
        product = runtime.get_example_product("mechanical-fastener")["finishedProduct"]
        local = runtime.process_contract.from_product(
            descriptor, product, runtime.finished_product_plan(product), example_selection=True)
        for station in (140, 300):
            with self.subTest(station=station):
                selected = copy.deepcopy(local)
                for part in selected["parts"].values():
                    part["anchor"]["station"] = station
                preview = self.assert_preview_matches_function("mechanical-fastener", selected)
                for part in preview["manufacturingParts"]:
                    feature, = part["request"]["features"]
                    self.assertEqual("start", feature["reference"])
                    self.assertEqual(2, feature["arrayCount"])
                    self.assertEqual([station-25, station+25],
                                     [feature["station"]+i*feature["arrayPitch"] for i in range(2)])

    def test_public_preview_respects_swapped_tab_ends_and_explicit_trim(self):
        descriptor = runtime._template_by_id("tab-slot-lock")
        product = runtime.get_example_product("tab-slot-lock")["finishedProduct"]
        local = runtime.process_contract.from_product(
            descriptor, product, runtime.finished_product_plan(product), example_selection=True)
        for swapped in (False, True):
            with self.subTest(swapped=swapped):
                selected = copy.deepcopy(local)
                for role, part in selected["parts"].items():
                    if swapped:
                        part["anchor"]["end"] = "end" if part["anchor"]["end"] == "start" else "start"
                    part["anchor"]["trim"] = 3 if role == "tab" else 5
                preview = self.assert_preview_matches_function("tab-slot-lock", selected)
                for part in preview["manufacturingParts"]:
                    role = part["sourceRole"]
                    end = selected["parts"][role]["anchor"]["end"]
                    cut = part["request"]["ends"][end]
                    process_id = "tab-end" if role == "tab" else "slot-end"
                    tool_id = next(item["resource"]["id"] for item in descriptor["partProcesses"]
                                   if item["id"] == process_id)
                    self.assertEqual(tool_id, cut["toolRef"]["id"])
                    self.assertEqual("male" if role == "tab" else "female", cut["toolParameters"]["gender"])
                    self.assertGreaterEqual(cut["trim"], selected["parts"][role]["anchor"]["trim"])
                    other = "end" if end == "start" else "start"
                    self.assertEqual("keep", part["request"]["ends"][other]["type"])

    def test_reverse_stock_mapping_mirrors_the_whole_hole_array(self):
        descriptor = runtime._template_by_id("mechanical-fastener")
        product = runtime.get_example_product("mechanical-fastener")["finishedProduct"]
        local = runtime.process_contract.from_product(
            descriptor, product, runtime.finished_product_plan(product), example_selection=True)
        original = runtime.evaluate_process("mechanical-fastener", local)
        stocks = [{"id": role, **{key: copy.deepcopy(part[key])
                   for key in ("profileRef", "parameters", "length", "matrix")}}
                  for role, part in local["parts"].items()]
        targets = {role: {"stockId": role, "start": 0, "reverse": True} for role in local["parts"]}
        instances = [{"instanceId": "mirrored", "templateId": "mechanical-fastener",
                      "processInput": local, "targets": targets, "parameters": {}}]
        before = copy.deepcopy((stocks, instances))
        plan = runtime.resolve_process_plan(stocks, instances)
        self.assertEqual(before, (stocks, instances))
        original_ops = {op["role"]: op["requestFeature"] for op in original["operations"]}
        for part in plan["manufacturingParts"]:
            length = part["request"]["length"]
            source = original_ops[part["stockId"]]
            first = source["station"] if source["reference"] == "start" else length-source["station"]
            expected = sorted(length-first-i*source["arrayPitch"] for i in range(source["arrayCount"]))
            feature, = part["request"]["features"]
            actual = sorted(feature["station"]+i*feature["arrayPitch"] for i in range(feature["arrayCount"]))
            self.assertEqual(expected, actual)

    def test_actual_end_trim_cannot_remove_the_entire_stock(self):
        descriptor = runtime._template_by_id("t-contact-fit")
        product = runtime.get_example_product("t-contact-fit")["finishedProduct"]
        local = runtime.process_contract.from_product(
            descriptor, product, runtime.finished_product_plan(product), example_selection=True)
        local["parts"]["branch"]["anchor"]["trim"] = local["parts"]["branch"]["length"]
        before = copy.deepcopy(local)
        result = runtime.evaluate_process("t-contact-fit", local)
        self.assertEqual(before, local)
        self.assertFalse(result["applicable"])
        self.assertTrue(result["reason"])
        self.assertEqual([], result["operations"])

    def plan(self, instances, stocks=None):
        return runtime.resolve_process_plan([stock()] if stocks is None else stocks, instances)

    def test_three_repeated_bends_share_one_material_and_namespaced_features(self):
        instances = [instance("left", 150), instance("middle", 500, 90), instance("right", 850, 180)]
        stocks = [stock()]
        original = copy.deepcopy((stocks, instances))
        plan = self.plan(instances, stocks)
        self.assertEqual(original, (stocks, instances))
        self.assertEqual(1, len(plan["stocks"]))
        self.assertEqual(1, len(plan["manufacturingParts"]))
        self.assertEqual(3, len(plan["instances"]))
        self.assertEqual(3, len(plan["operations"]))
        part = plan["manufacturingParts"][0]
        self.assertEqual("tube-1", part["stockId"])
        self.assertEqual(1000, part["request"]["length"])
        self.assertEqual(3, len(part["request"]["features"]))
        self.assertEqual([150, 500, 850], [f["station"] for f in part["request"]["features"]])
        self.assertEqual([0, 90, 180], [round(f["rotation"]) for f in part["request"]["features"]])
        identifiers = [f["id"] for f in part["request"]["features"]]
        self.assertEqual(3, len(set(identifiers)))
        self.assertEqual(set(part["operationIds"]), set(identifiers))
        self.assertEqual(1, len(part["sourceMappings"]))
        self.assertEqual(["left", "middle", "right"], [f["instanceId"] for f in plan["forming"]])
        first = plan["forming"][0]
        second = plan["forming"][1]
        matrix = first["cumulativeTransform"]
        point = second["hingePoint"]
        transformed = [sum(matrix[4*i+j]*point[j] for j in range(3))+matrix[4*i+3]
                       for i in range(3)]
        for actual, expected in zip(second["formedHingePoint"], transformed):
            self.assertAlmostEqual(expected, actual, places=10)

    def test_replace_remove_and_repeat_rebuild_from_original_material(self):
        instances = [instance("left", 150), instance("middle", 500, 90), instance("right", 850, 180)]
        initial = self.plan(instances)
        revised = copy.deepcopy(instances)
        revised[1]["processInput"]["geometry"]["station"] = 550
        replaced = self.plan(revised)
        self.assertEqual([150, 550, 850], [f["station"] for f in replaced["manufacturingParts"][0]["request"]["features"]])
        self.assertNotEqual(initial["planDigest"], replaced["planDigest"])
        removed = self.plan([revised[0], revised[2]])
        self.assertEqual([150, 850], [f["station"] for f in removed["manufacturingParts"][0]["request"]["features"]])
        empty = self.plan([])
        self.assertEqual([], empty["manufacturingParts"][0]["request"]["features"])
        self.assertEqual([], empty["forming"])
        for _ in range(7):
            replayed = self.plan(instances)
            self.assertEqual(initial, replayed)
        contaminated = copy.deepcopy(stock())
        contaminated["features"] = initial["manufacturingParts"][0]["request"]["features"]
        with self.assertRaisesRegex(ValueError, "原始母材"):
            self.plan(instances, [contaminated])

    def test_identical_instance_retries_deduplicate_and_different_content_conflicts(self):
        first = instance("left", 150)
        once = self.plan([first])
        repeated = self.plan([first, copy.deepcopy(first), copy.deepcopy(first)])
        self.assertEqual(once, repeated)
        conflicting = copy.deepcopy(first)
        conflicting["processInput"]["geometry"]["station"] += 1
        with self.assertRaisesRegex(ValueError, "同一实例"):
            self.plan([first, conflicting])

    def test_overlapping_fold_ranges_reject_before_native_execution(self):
        with self.assertRaisesRegex(ValueError, "重叠"):
            self.plan([instance("first", 500), instance("second", 505, 90)])
        with self.assertRaisesRegex(ValueError, "不存在"):
            dangling = instance("missing", 500)
            dangling["targets"]["stock"] = "absent-tube"
            self.plan([dangling])

    def test_plan_digest_tracks_frame_tool_parameters_and_source_dependency(self):
        baseline = instance("fold", 500)
        original = self.plan([baseline])
        rotated = copy.deepcopy(baseline)
        rotated["processInput"]["geometry"]["rotation"] = 90
        self.assertNotEqual(original["planDigest"], self.plan([rotated])["planDigest"])
        rounded = copy.deepcopy(baseline)
        rounded["processDrafts"] = {"node-slot": {"v-notch-sharp": {
            "bottomStrategy": "rounded", "roundRadius": 2}}}
        self.assertNotEqual(original["planDigest"], self.plan([rounded])["planDigest"])
        larger_relief = copy.deepcopy(rounded)
        larger_relief["processDrafts"]["node-slot"]["v-notch-sharp"]["roundRadius"] = 4
        self.assertNotEqual(self.plan([rounded])["planDigest"], self.plan([larger_relief])["planDigest"])
        source = (TEMPLATES / "assembly/node-v-notch-integrated/applicability.py").resolve()
        read_bytes = Path.read_bytes
        def changed_source(path):
            content = read_bytes(path)
            return content+b"\n# changed executable dependency\n" if path.resolve() == source else content
        # Simulate a changed dependency read without touching the user's source.
        with mock.patch.object(Path, "read_bytes", new=changed_source):
            changed = self.plan([baseline])
        self.assertNotEqual(original["instances"][0]["functionDigest"],
                            changed["instances"][0]["functionDigest"])
        self.assertNotEqual(original["planDigest"], changed["planDigest"])
        self.assertEqual(original, self.plan([baseline]))


if __name__ == "__main__":
    unittest.main()
