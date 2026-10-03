import importlib.util
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def load_runtime():
    spec = importlib.util.spec_from_file_location("assembly_template_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AssemblyTemplateRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load_runtime()
        cls.catalogue = cls.runtime.catalogue()

    def _finished_product(self, template_id):
        """A complete, independently owned specimen supplied to a process."""
        descriptor = self.runtime._template_by_id(template_id)
        product = self.runtime.finished_products.create(descriptor["exampleInput"]["shapeId"])
        if template_id == "insert-sleeve":
            for key, diameter in (("first", 30), ("second", 34.6)):
                product["spans"][key].update({
                    "profileRef": {"scope": "system", "id": "round"},
                    "parameters": {"width": diameter, "wallThickness": 2,
                                   "innerOffsetX": 0, "innerOffsetY": 0}})
            product["spans"]["second"]["length"] = 360
        if template_id == "two-end-middle":
            product["spans"]["branch"].update({
                "profileRef": {"scope": "system", "id": "round"},
                "parameters": {"width": 30, "wallThickness": 2,
                               "innerOffsetX": 0, "innerOffsetY": 0}})
        return product

    def _product_preview(self, template_id, parameters=None, process_drafts=None, product=None):
        product = self._finished_product(template_id) if product is None else product
        before = copy.deepcopy(product)
        plan = self.runtime.preview_plan(template_id, parameters, process_drafts,
                                         finished_product=product)
        self.assertEqual(product, before)
        self.assertEqual(plan["finishedProduct"], before)
        independent = {part["role"]: part
                       for part in self.runtime.finished_product_plan(product)["designParts"]}
        roles = self.runtime._template_by_id(template_id)["exampleInput"]["roles"]
        for part in plan["designParts"]:
            expected = independent[roles[part["role"]]]
            self.assertEqual(part["request"], expected["request"])
            self.assertEqual(part["matrix"], expected["matrix"])
        return plan

    def test_all_descriptors_are_valid(self):
        self.assertEqual(self.catalogue["errors"], [])
        self.assertGreaterEqual(len(self.catalogue["assemblies"]), 5)

    def test_product_topology_declarations_are_explicit_and_parameter_aware(self):
        descriptors = self.catalogue["assemblies"]
        for descriptor in descriptors:
            if descriptor.get("productBinding", {}).get("mode") == "incremental-cut":
                self.assertTrue(descriptor["productBinding"].get("compatibleProductTopologies"),
                                descriptor["id"])
        angle_capability = self.runtime._template_by_id("two-end-end-angle")["productBinding"]
        self.assertEqual(self.runtime._active_product_topologies(angle_capability,
            {"jointAngle": 90}), ["L"])
        self.assertEqual(self.runtime._active_product_topologies(angle_capability,
            {"jointAngle": 0}), ["straight"])
        for template_id, topology, parameters in (
            ("tab-slot-lock", "L", {}),
            ("two-end-end-angle", "L", {"jointAngle": 0}),
            ("two-end-middle", "L", {}),
            ("two-end-end-angle", "T", {"jointAngle": 90}),
        ):
            with self.subTest(template=template_id, topology=topology), self.assertRaisesRegex(ValueError, "成品拓扑不兼容"):
                self.runtime.resolve_bound_plan(template_id, parameters, product_topology=topology)

        descriptor = copy.deepcopy(self.runtime._template_by_id("tab-slot-lock"))
        directory = self.runtime.ROOT / descriptor["id"]
        del descriptor["productBinding"]["compatibleProductTopologies"]
        self.runtime._validate_template(descriptor, directory)  # Legacy descriptor remains readable.
        self.assertIn("成品连接拓扑", self.runtime._bound_unsupported_reason(descriptor, {}))
        for declaration in ([{"topology": "made-up"}], [{"topology": "L", "when":
                            {"op": "eq", "parameter": "missing", "value": 1}}]):
            descriptor["productBinding"]["compatibleProductTopologies"] = declaration
            with self.assertRaises(ValueError):
                self.runtime._validate_template(descriptor, directory)

    def test_bound_axis_angle_rule_is_declared_and_digested(self):
        descriptor = copy.deepcopy(self.runtime._template_by_id("two-end-end-angle"))
        binding = descriptor["productBinding"]
        self.assertEqual(binding["axisAngle"], {
            "roles": ["memberA", "memberB"],
            "directions": ["awayFromAnchor", "awayFromAnchor"],
            "measure": "supplement", "expected": {"parameter": "jointAngle"},
        })
        self.assertEqual(self.runtime.resolve_bound_plan("two-end-end-angle",
            {"jointAngle": 0}, participants=self._actual_participants("two-end-end-angle"),
            product_topology="straight")["axisAngle"], binding["axisAngle"])
        original_digest = self.runtime._bound_template_digest(descriptor, descriptor["partProcesses"])
        binding["axisAngle"]["expected"] = {"degrees": 90}
        with self.assertRaisesRegex(ValueError, "节点斜接方向来源"):
            self.runtime._validate_template(descriptor, self.runtime.ROOT / descriptor["id"])
        self.assertNotEqual(original_digest,
            self.runtime._bound_template_digest(descriptor, descriptor["partProcesses"]))
        for bad_expected in ({"parameter": "fitGap"}, {"degrees": -1}, {"degrees": 181},
                             {"parameter": "missing"}, {"degrees": 90, "parameter": "jointAngle"}):
            broken = copy.deepcopy(descriptor)
            broken["productBinding"]["axisAngle"]["expected"] = bad_expected
            with self.subTest(expected=bad_expected), self.assertRaises(ValueError):
                self.runtime._validate_template(broken, self.runtime.ROOT / descriptor["id"])
        wrap = copy.deepcopy(self.runtime._template_by_id("wrap-a-over-b"))
        self.assertEqual(wrap["productBinding"]["axisAngle"]["expected"], {"degrees": 90})
        wrap["productBinding"]["axisAngle"]["expected"] = {"degrees": -90}
        with self.assertRaises(ValueError):
            self.runtime._validate_template(wrap, self.runtime.ROOT / wrap["id"])

    def test_real_four_corner_miters_use_the_committed_part_axes_and_stock(self):
        import sys

        framework = str(ROOT / "iCAX-Engine/framework/TemplateRuntime/python")
        sys.path.insert(0, framework)
        try:
            from icax_template_worker import _load_template
            directory = ROOT / "apps/tube-designer/templates/product/minimal_protective_grille"
            descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
            values = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
            values.update(frameType="closed_frame", assemblyPlanningMode="external_templates",
                          frameWidth=60, frameDepth=40, frameWallThickness=2,
                          barLayoutMode="fixed_count", barCount=1, handleEnabled=False)
            product = _load_template(str(directory / "template.py"),
                "real-four-corner-miter-tests").generate(values,
                    {"template": descriptor, "geometryPurpose": "manufacturing"})
        finally:
            sys.path.remove(framework)

        def load_module(path, name):
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module

        miter = load_module(ROOT / "apps/tube-designer/templates/mold/end-miter/tool.py",
                            "real_corner_end_miter")
        miter.section_geometry = load_module(
            ROOT / "apps/tube-designer/templates/_shared/section_geometry.py",
            "real_corner_section_geometry")
        items = {item["key"]: item for item in product["items"]}

        def dot(left, right):
            return sum(a * b for a, b in zip(left, right))

        def unit(start, finish):
            direction = [b - a for a, b in zip(start, finish)]
            magnitude = math.sqrt(dot(direction, direction))
            return [value / magnitude for value in direction]

        corners = [row for row in product["relationships"]
                   if row["properties"]["topology"] == "L"]
        self.assertEqual(len(corners), 4)
        for connection in corners:
            declarations = connection["properties"]["participantAnchors"]
            node = connection["properties"]["centerlinePoint"]
            for order in ((0, 1), (1, 0)):
                with self.subTest(node=connection["key"], roles=order):
                    participants = []
                    world_away = []
                    world_to_part = []
                    for role_index, declaration_index in enumerate(order):
                        declaration = declarations[declaration_index]
                        other = declarations[order[1 - role_index]]
                        item, other_item = items[declaration["itemKey"]], items[other["itemKey"]]
                        frame = item["properties"]["assemblyFrame.member"]
                        other_frame = other_item["properties"]["assemblyFrame.member"]
                        axis = unit(frame["start"], frame["end"])
                        other_axis = unit(other_frame["start"], other_frame["end"])
                        away = axis if declaration["anchor"]["end"] == "start" else [-v for v in axis]
                        other_away = (other_axis if other["anchor"]["end"] == "start"
                                      else [-v for v in other_axis])
                        # Native manufacturing normalization sends +Z to +X
                        # and keeps +X as +X for these committed frame members.
                        if axis[2]:
                            self.assertEqual(axis, [0, 0, 1])
                            to_part = lambda v: [v[2], v[1], -v[0]]
                        else:
                            self.assertEqual(abs(axis[0]), 1)
                            to_part = lambda v: list(v)
                        stock = frame["stockInterval"]
                        physical_start = [frame["start"][i] + axis[i] * stock["startStation"]
                                          for i in range(3)]
                        physical_end = [frame["start"][i] + axis[i] * stock["endStation"]
                                        for i in range(3)]
                        part_center = [(a + b) / 2 for a, b in
                                       zip(to_part(physical_start), to_part(physical_end))]
                        node_part = [a - b for a, b in zip(to_part(node), part_center)]
                        anchor = copy.deepcopy(declaration["anchor"])
                        if axis[0] < 0:
                            anchor["end"] = "end" if anchor["end"] == "start" else "start"
                        section = copy.deepcopy(item["properties"]["tubeDesigner.profile"])
                        section["schemaVersion"] = 1
                        participants.append({"role": "memberA" if role_index == 0 else "memberB",
                            "memberEntityId": item["key"], "itemKey": item["key"],
                            "section": section, "length": item["properties"]["length"],
                            "anchor": anchor, "nodeFrame": {
                                "selfAwayPart": to_part(away),
                                "otherAwayPart": to_part(other_away),
                                "selfNodePart": node_part}})
                        world_away.append(away)
                        world_to_part.append(to_part)

                    supplied = {"jointAngle": 90, "fitGap": 0, "planeRotation": 0}
                    original = copy.deepcopy(participants)
                    plan = self.runtime.resolve_bound_plan("two-end-end-angle", supplied,
                        participants=participants, product_topology="L")
                    self.assertEqual(participants, original)
                    self.assertEqual(plan["parameters"], supplied)
                    self.assertEqual(plan["assemblyFitStatus"], "not-verified")
                    parts = {part["role"]: part for part in plan["parts"]}
                    signed_sides = []
                    for index, role in enumerate(("memberA", "memberB")):
                        member = participants[index]
                        selected_end = member["anchor"]["end"]
                        cut = parts[role]["ends"][selected_end]
                        allowance = member["anchor"]["stockAllowance"]
                        self.assertEqual(cut["trim"], allowance)
                        self.assertEqual(cut["datum"], "center")
                        length = member["length"]
                        section = member["section"]
                        bounds = {"min": [-length / 2, -section["width"] / 2, -section["depth"] / 2],
                                  "max": [length / 2, section["width"] / 2, section["depth"] / 2]}
                        cutter = miter.generate(cut["toolParameters"], {"bounds": bounds,
                            "placement": {key: cut[key] for key in ("trim", "rotation", "datum")},
                            "end": selected_end, "targetSection": section})
                        self.assertAlmostEqual(cutter["datumCenter"], member["nodeFrame"]["selfNodePart"][0])
                        cutter_profile = next(node for node in cutter["model"]["geometry"]
                                              if node["key"] == "tool-profile")
                        self.assertAlmostEqual(cutter_profile["arguments"]["placement"]["origin"][0],
                                               cutter["datumCenter"])
                        slope_axis = cutter_profile["arguments"]["placement"]["yAxis"]
                        transverse = math.hypot(slope_axis[1], slope_axis[2])
                        slope = slope_axis[0] / transverse
                        cross_direction = [slope_axis[1] / transverse, slope_axis[2] / transverse]
                        def retained_side(world_delta, part_transform=world_to_part[index],
                                          axial_sign=member["nodeFrame"]["selfAwayPart"][0],
                                          slope=slope, cross_direction=cross_direction):
                            x, y, z = part_transform(world_delta)
                            return axial_sign * (x - slope * (cross_direction[0] * y
                                                                + cross_direction[1] * z))
                        signed_sides.append(retained_side)
                    bisector = [sum(away[i] for away in world_away) for i in range(3)]
                    for side in signed_sides:
                        self.assertAlmostEqual(side(bisector), 0, places=6)
                    self.assertGreater(signed_sides[0](world_away[0]), 0)
                    self.assertLess(signed_sides[0](world_away[1]), 0)
                    self.assertLess(signed_sides[1](world_away[0]), 0)
                    self.assertGreater(signed_sides[1](world_away[1]), 0)

    def test_bound_miter_rejects_incomplete_or_contradictory_node_frames(self):
        base = self._actual_participants("two-end-end-angle", length=800, width=60, depth=40)
        cases = (
            ("missing-frame", lambda parts: parts[0].pop("nodeFrame"), "制造节点坐标"),
            ("missing-vector", lambda parts: parts[0]["nodeFrame"].pop("selfNodePart"), "制造节点坐标"),
            ("nonfinite-vector", lambda parts: parts[0]["nodeFrame"]["selfAwayPart"].__setitem__(0, float("inf")), "selfAwayPart"),
            ("nonunit-vector", lambda parts: parts[0]["nodeFrame"].update(selfAwayPart=[2, 0, 0]), "单位向量"),
            ("off-center", lambda parts: parts[0]["nodeFrame"]["selfNodePart"].__setitem__(1, 1), "横向中心"),
            ("wrong-node-station", lambda parts: parts[0]["nodeFrame"]["selfNodePart"].__setitem__(0, -370), "纵向位置"),
            ("wrong-end-direction", lambda parts: parts[0]["nodeFrame"].update(selfAwayPart=[-1, 0, 0]), "物理端不一致"),
            ("not-transverse", lambda parts: parts[0]["nodeFrame"].update(otherAwayPart=[1, 0, 0]), "横向投影"),
            ("missing-allowance", lambda parts: parts[0]["anchor"].pop("stockAllowance"), "库存余量"),
            ("missing-inset", lambda parts: parts[0]["anchor"].pop("contactInset"), "近侧外表面"),
            ("mismatched-sections", lambda parts: parts[1]["section"]["contours"][0]["points"].__setitem__(0, [-35, -20]), "截面不同"),
        )
        for label, mutate, message in cases:
            with self.subTest(label=label):
                participants = copy.deepcopy(base)
                mutate(participants)
                with self.assertRaisesRegex(ValueError, message):
                    self.runtime.resolve_bound_plan("two-end-end-angle", {"jointAngle": 90},
                        participants=participants, product_topology="L")

    def test_zero_angle_with_uncut_physical_ends_is_connection_only(self):
        participants = self._actual_participants("two-end-end-angle", length=800)
        for member in participants:
            member["nodeFrame"]["otherAwayPart"] = [-1, 0, 0]
        original = copy.deepcopy(participants)
        supplied = {"jointAngle": 0, "fitGap": 0, "planeRotation": 35}
        plan = self.runtime.resolve_bound_plan("two-end-end-angle", supplied,
            participants=participants, product_topology="straight")
        self.assertEqual(participants, original)
        self.assertEqual(plan["parameters"], supplied)
        self.assertEqual(plan["manufacturingEffect"], "connection-only")
        self.assertEqual(plan["parts"], [])
        self.assertEqual(plan["workflow"]["partOperations"], [])
        self.assertEqual(plan["fitChecks"][0]["id"], "miter-straight")
        self.assertEqual(plan["assemblyFitStatus"], "not-verified")
        gap_values = {"jointAngle": 0, "fitGap": 1.5, "planeRotation": 35}
        gap_plan = self.runtime.resolve_bound_plan("two-end-end-angle", gap_values,
            participants=participants, product_topology="straight")
        self.assertEqual(gap_plan["parameters"], gap_values)
        self.assertEqual(gap_plan["manufacturingEffect"], "incremental-cut")
        self.assertEqual(len(gap_plan["parts"]), 2)
        self.assertEqual([part["ends"]["start"]["trim"] for part in gap_plan["parts"]],
                         [0.75, 0.75])
        participants[0]["anchor"]["stockAllowance"] = 5
        participants[0]["nodeFrame"]["selfNodePart"][0] += 5
        with self.assertRaisesRegex(ValueError, "零角度、零余量"):
            self.runtime.resolve_bound_plan("two-end-end-angle", supplied,
                participants=participants, product_topology="straight")

    def test_miter_normal_gap_converts_to_both_actual_axial_end_trims(self):
        for angle in (0, 60, 90, 120):
            with self.subTest(angle=angle):
                parameters = {"jointAngle": angle, "fitGap": 2, "planeRotation": 0}
                expected_trim = 1 / math.cos(math.radians(angle / 2))
                example = self.runtime.preview_plan("two-end-end-angle", parameters)
                self.assertEqual(example["parameters"], parameters)
                self.assertTrue(example["resolvedWorkflow"]["parameterEffects"]["fitGap"]
                                ["manufacturingGeometry"])
                self.assertEqual(len(example["manufacturingParts"]), 2)
                for blank in example["manufacturingParts"]:
                    cut = next(end for end in blank["request"]["ends"].values()
                               if end["type"] == "end-miter")
                    self.assertAlmostEqual(cut["trim"], expected_trim)
                separated = self.runtime.preview_plan("two-end-end-angle",
                    {"fitGap": 2}, scene_parameters={"jointAngle": angle, "planeRotation": 0})
                self.assertEqual(separated["parameters"], {"fitGap": 2})
                self.assertEqual(separated["sceneParameters"],
                                 {"jointAngle": angle, "planeRotation": 0})
                for blank in separated["manufacturingParts"]:
                    cut = next(end for end in blank["request"]["ends"].values()
                               if end["type"] == "end-miter")
                    self.assertAlmostEqual(cut["trim"], expected_trim)

                participants = self._actual_participants("two-end-end-angle", length=800)
                transverse = math.sin(math.radians(angle))
                axial = -math.cos(math.radians(angle))
                for part in participants:
                    part["nodeFrame"]["otherAwayPart"] = [axial, 0, transverse]
                original = copy.deepcopy(participants)
                topology = "straight" if angle == 0 else "L"
                bound = self.runtime.resolve_bound_plan("two-end-end-angle", parameters,
                    participants=participants, product_topology=topology)
                self.assertEqual(participants, original)
                self.assertEqual(bound["parameters"], parameters)
                self.assertEqual(bound["manufacturingEffect"], "incremental-cut")
                for part in bound["parts"]:
                    self.assertAlmostEqual(part["ends"]["start"]["trim"], expected_trim)
                for part in participants:
                    part["anchor"].update(end="end")
                    part["nodeFrame"].update(selfAwayPart=[-1, 0, 0],
                        selfNodePart=[part["length"] / 2, 0, 0])
                reversed_bound = self.runtime.resolve_bound_plan("two-end-end-angle", parameters,
                    participants=participants, product_topology=topology)
                for part in reversed_bound["parts"]:
                    self.assertAlmostEqual(part["ends"]["end"]["trim"], expected_trim)

    def test_bound_miter_node_sources_are_a_complete_declarative_pair(self):
        descriptor = self.runtime._template_by_id("two-end-end-angle")
        directory = self.runtime.ROOT / descriptor["id"]
        for label, mutate in (
            ("missing-rotation", lambda binding: binding["processes"]["miter-a"]["parameterSources"].pop("rotation")),
            ("wrong-role", lambda binding: binding["processes"]["miter-a"]["parameterSources"]["angle"].update(role="memberB")),
            ("wrong-angle", lambda binding: binding["processes"]["miter-a"]["parameterSources"]["angle"].update(angleParameter="fitGap")),
            ("wrong-datum", lambda binding: binding["processes"]["miter-a"]["parameterSources"]["datum"].update(value="long")),
        ):
            with self.subTest(label=label):
                broken = copy.deepcopy(descriptor)
                mutate(broken["productBinding"])
                with self.assertRaises(ValueError):
                    self.runtime._validate_template(broken, directory)

        participants = self._actual_participants("two-end-end-angle")
        plan_zero = self.runtime.resolve_bound_plan("two-end-end-angle",
            {"planeRotation": 0}, participants=participants, product_topology="L")
        plan_turn = self.runtime.resolve_bound_plan("two-end-end-angle",
            {"planeRotation": 73}, participants=participants, product_topology="L")
        self.assertEqual(plan_zero["parts"], plan_turn["parts"])
        self.assertEqual(plan_zero["parameters"]["planeRotation"], 0)
        self.assertEqual(plan_turn["parameters"]["planeRotation"], 73)
        self.assertEqual(plan_turn["assemblyFitStatus"], "not-verified")

    def test_miter_gap_requires_matching_declarative_sources_for_both_cuts(self):
        descriptor = self.runtime._template_by_id("two-end-end-angle")
        directory = self.runtime.ROOT / descriptor["id"]

        def source(template, role):
            return template["productBinding"]["processes"][f"miter-{role}"]["parameterSources"]["trim"]

        for label, mutate in (
            ("missing-projection", lambda template: source(template, "a").pop("offsetProjection")),
            ("wrong-angle", lambda template: source(template, "a").update(offsetAngleParameter="planeRotation")),
            ("wrong-gap", lambda template: source(template, "a").update(offsetParameter="jointAngle")),
            ("wrong-role", lambda template: source(template, "a").update(role="memberB")),
            ("missing-other-share", lambda template: source(template, "b").pop("offsetParameter")),
            ("unbalanced-shares", lambda template: source(template, "a").update(offsetFactor=0.4)),
            ("example-mismatch", lambda template: template["partProcesses"][0]
                ["parameterBindings"]["trim"].update(share=0.4)),
            ("unknown-example-source", lambda template: template["partProcesses"][0]
                ["parameterBindings"]["trim"].update(kind="unclear")),
            ("fixed-gap-without-cut-source", lambda template: template["productBinding"]
                ["fitChecks"][0].update(gap={"millimeters": 2})),
        ):
            with self.subTest(label=label):
                broken = copy.deepcopy(descriptor)
                mutate(broken)
                with self.assertRaises(ValueError):
                    self.runtime._validate_template(broken, directory)

    def test_bound_miter_accepts_centered_provided_rectangular_tube_not_profile_id(self):
        system = json.loads((ROOT / "apps/tube-designer/templates/product/aluminium_window"
                            / "systems/demonstration.json").read_text(encoding="utf-8"))
        frame = system["profiles"]["frame"]
        section = {"schema": "icax.tube-profile", "schemaVersion": 1,
                   "id": "system:frame", "kind": "fixed-section",
                   "geometrySource": "providedBoundary", "width": frame["face"],
                   "depth": frame["depth"], "contours": copy.deepcopy(frame["contours"])}
        participants = self._actual_participants("two-end-end-angle", length=800,
                                                 width=frame["face"], depth=frame["depth"])
        for member in participants:
            member["section"] = copy.deepcopy(section)
        plan = self.runtime.resolve_bound_plan("two-end-end-angle", {"jointAngle": 90},
            participants=participants, product_topology="L")
        self.assertTrue(plan["supported"])
        self.assertEqual(plan["manufacturingEffect"], "incremental-cut")
        self.assertEqual(len(plan["parts"]), 2)
        self.assertEqual(plan["assemblyFitStatus"], "not-verified")

        def shift_inner(parts):
            for segment in parts[0]["section"]["contours"][1]["segments"]:
                segment["start"][0] += 1
                segment["end"][0] += 1

        def narrower_inner(parts):
            for segment in parts[1]["section"]["contours"][1]["segments"]:
                for key in ("start", "end"):
                    segment[key][0] += 0.5 if segment[key][0] < frame["face"] / 2 else -0.5

        for label, mutate, message in (
            ("eccentric-hole", shift_inner, "未同心"),
            ("incorrect-size", lambda parts: parts[0]["section"].update(width=49), "声明尺寸"),
            ("different-material-section", narrower_inner, "截面不同"),
            ("extra-loop", lambda parts: parts[0]["section"]["contours"].append(
                copy.deepcopy(parts[0]["section"]["contours"][1])), "一外一内"),
            ("unknown-boundary", lambda parts: parts[0]["section"].update(geometrySource="unverified"), "居中方矩管"),
        ):
            with self.subTest(label=label):
                changed = copy.deepcopy(participants)
                mutate(changed)
                with self.assertRaisesRegex(ValueError, message):
                    self.runtime.resolve_bound_plan("two-end-end-angle", {"jointAngle": 90},
                        participants=changed, product_topology="L")

    def test_saved_binding_rechecks_digest_and_finished_topology(self):
        descriptor = self.runtime._template_by_id("two-end-end-angle")
        digest = self.runtime._bound_template_digest(descriptor, descriptor["partProcesses"])
        saved = {"bindingId": "corner-1", "templateId": descriptor["id"],
                 "templateDigest": digest, "parameters": {"jointAngle": 90},
                 "productTopology": "L"}
        self.assertTrue(self.runtime.validate_existing_bindings([saved])["checks"][0]["current"])
        for changed, message in (
            ({"templateDigest": "old-template"}, "模板已更新"),
            ({"parameters": {"jointAngle": 0}}, "形状"),
            ({"productTopology": "T"}, "形状"),
            ({"productTopology": ""}, "形状"),
        ):
            with self.subTest(changed=changed):
                row = self.runtime.validate_existing_bindings([{**saved, **changed}])["checks"][0]
                self.assertFalse(row["current"])
                self.assertIn(message, row["detail"])

    def test_product_fit_rules_resolve_exact_active_branch_without_changing_parameters(self):
        cases = (
            ("two-end-end-angle", {"jointAngle": 90, "fitGap": 0.4, "planeRotation": 25},
             "L", "miter-corner", {"gapMm": 0.4, "planeRotationDeg": 25}),
            ("two-end-end-angle", {"jointAngle": 0, "fitGap": 1.5},
             "straight", "miter-straight", {"gapMm": 1.5}),
            ("wrap-a-over-b", {"maleFemale": False, "pairCount": "unused-draft"},
             "L", "wrap-butt", {"gapMm": 0}),
            ("wrap-a-over-b", {"maleFemale": True, "pairCount": "two",
                               "sideClearance": 0.3, "tabLength": 15},
             "L", "wrap-tabs-two", {"clearanceMm": 0.3, "insertionMm": 15}),
            ("wrap-a-over-b", {"maleFemale": True, "pairCount": "four"},
             "L", "wrap-tabs-four", {}),
            ("two-end-middle", {"interfaceMode": "singleInsert", "fitGap": 0.4},
             "T", "single-insert", {"gapMm": 0.4}),
            ("two-end-middle", {"interfaceMode": "throughInsert"},
             "T", "through-insert", {}),
            ("two-end-middle", {"interfaceMode": "saddle"},
             "T", "saddle", {}),
        )
        for template_id, supplied, topology, check_id, expected in cases:
            with self.subTest(template=template_id, check=check_id):
                original = copy.deepcopy(supplied)
                descriptor = self.runtime._template_by_id(template_id)
                plan = self.runtime.resolve_bound_plan(template_id, supplied,
                    participants=self._actual_participants(template_id),
                    product_topology=topology)
                self.assertEqual(supplied, original)
                self.assertEqual(plan["parameters"],
                                 self.runtime._validated_values(descriptor, supplied))
                self.assertEqual(len(plan["fitChecks"]), 1)
                resolved = plan["fitChecks"][0]
                self.assertEqual(resolved["id"], check_id)
                self.assertEqual(resolved["topology"], topology)
                declared = next(check for check in descriptor["productBinding"]["fitChecks"]
                                if check["id"] == check_id)
                self.assertEqual(resolved["kind"], declared["kind"])
                self.assertEqual(resolved["roles"], declared["roles"])
                self.assertEqual(set(resolved["processIds"]),
                                 {process["id"] for process in descriptor["partProcesses"]
                                  if self.runtime._condition_matches(process.get("appliesWhen"),
                                                                      plan["parameters"])})
                for field, value in expected.items():
                    self.assertEqual(resolved[field], value)
                self.assertEqual(plan["workflow"]["fitChecks"], plan["fitChecks"])
                self.assertEqual(plan["assemblyFitStatus"], "not-verified")

    def test_product_fit_rules_reject_ambiguous_or_mismatched_branches(self):
        descriptor = copy.deepcopy(self.runtime._template_by_id("wrap-a-over-b"))
        directory = self.runtime.ROOT / descriptor["id"]
        for mutation in ("unknown-kind", "wrong-role", "wrong-source", "invalid-choice"):
            broken = copy.deepcopy(descriptor)
            rule = broken["productBinding"]["fitChecks"][1]
            if mutation == "unknown-kind":
                rule["kind"] = "magic-fit"
            elif mutation == "wrong-role":
                rule["roles"]["host"] = "memberB"
            elif mutation == "wrong-source":
                rule["clearance"] = {"parameter": "tabLength", "millimeters": 1}
            else:
                rule["when"]["conditions"][1]["value"] = "not-a-choice"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.runtime._validate_template(broken, directory)
        participants = self._actual_participants("wrap-a-over-b")
        duplicated = copy.deepcopy(descriptor)
        duplicate_rule = copy.deepcopy(duplicated["productBinding"]["fitChecks"][1])
        duplicate_rule["id"] = "other-two"
        duplicated["productBinding"]["fitChecks"].append(duplicate_rule)
        with patch.object(self.runtime, "_template_by_id", return_value=duplicated), \
                self.assertRaisesRegex(ValueError, "恰好命中一条"):
            self.runtime.resolve_bound_plan("wrap-a-over-b",
                {"maleFemale": True, "pairCount": "two"}, participants=participants,
                product_topology="L")
        missing = copy.deepcopy(descriptor)
        missing["productBinding"]["fitChecks"] = [check for check in
            missing["productBinding"]["fitChecks"] if check["id"] != "wrap-tabs-two"]
        with patch.object(self.runtime, "_template_by_id", return_value=missing), \
                self.assertRaisesRegex(ValueError, "恰好命中一条"):
            self.runtime.resolve_bound_plan("wrap-a-over-b",
                {"maleFemale": True, "pairCount": "two"}, participants=participants,
                product_topology="L")
        mismatched = copy.deepcopy(descriptor)
        mismatched["productBinding"]["fitChecks"][1]["processIds"] = ["short-butt-end"]
        with patch.object(self.runtime, "_template_by_id", return_value=mismatched), \
                self.assertRaisesRegex(ValueError, "实际参数分支不一致"):
            self.runtime.resolve_bound_plan("wrap-a-over-b",
                {"maleFemale": True, "pairCount": "two"}, participants=participants,
                product_topology="L")

    def test_preview_preferred_view_uses_supported_camera_directions(self):
        for template_id in ("wrap-a-over-b",):
            with self.subTest(template=template_id):
                descriptor = copy.deepcopy(self.runtime._template_by_id(template_id))
                directory = self.runtime.ROOT / template_id
                self.assertEqual(descriptor["previewScene"]["preferredView"], "back-top-right")
                self.runtime._validate_template(descriptor, directory)
                for invalid in ("", "top-top", "front-back", "right-left", "behind", "Back-top-right"):
                    descriptor["previewScene"]["preferredView"] = invalid
                    with self.subTest(view=invalid), self.assertRaisesRegex(ValueError, "preferredView"):
                        self.runtime._validate_template(descriptor, directory)

    def test_scene_slots_are_valid_unique_identifiers(self):
        template_id = "two-end-end-angle"
        directory = self.runtime.ROOT / template_id
        descriptor = copy.deepcopy(self.runtime._template_by_id(template_id))
        parts = descriptor["previewScene"]["designParts"]
        parts[0]["sceneSlot"] = "primary"
        parts[1]["sceneSlot"] = "branch"
        self.runtime._validate_template(descriptor, directory)
        for invalid in ("", "bad slot", "1first", None):
            with self.subTest(scene_slot=invalid):
                broken = copy.deepcopy(descriptor)
                broken["previewScene"]["designParts"][0]["sceneSlot"] = invalid
                with self.assertRaisesRegex(ValueError, "sceneSlot"):
                    self.runtime._validate_template(broken, directory)
        parts[1]["sceneSlot"] = "primary"
        with self.assertRaisesRegex(ValueError, "sceneSlot"):
            self.runtime._validate_template(descriptor, directory)

    def test_scene_part_inputs_are_separate_from_assembly_process_parameters(self):
        template_id = "two-end-end-angle"
        descriptor = self.runtime._template_by_id(template_id)
        baseline = self.runtime.preview_plan(template_id, scene_parameters={})
        self.assertEqual(set(baseline["parameters"]),
                         {definition["key"] for definition in descriptor["parameters"]
                          if definition.get("scope", "process") != "scene"})
        self.assertEqual(set(baseline["sceneParameters"]),
                         {definition["key"] for definition in descriptor["parameters"]
                          if definition.get("scope", "process") == "scene"})
        for part in baseline["designParts"]:
            scene_part = baseline["sceneParts"][part["role"]]
            self.assertEqual(scene_part["profileRef"], part["request"]["profileRef"])
            self.assertEqual(scene_part["parameters"], part["request"]["parameters"])
            self.assertEqual(scene_part["length"], part["request"]["length"])

        section = {"width": 72, "depth": 44, "wallThickness": 2,
                   "cornerRadius": 3, "innerRadius": 1}
        scene_parts = {
            "memberA": {"parameters": {"width": 72, "depth": 44}, "length": 310},
            "memberB": {"parameters": {"width": 72, "depth": 44}, "length": 190},
        }
        original = copy.deepcopy(scene_parts)
        process_values = {"fitGap": 1}
        scene_values = {"jointAngle": 90, "planeRotation": 0}
        plan = self.runtime.preview_plan(template_id, process_values, {}, scene_parts, scene_values)
        self.assertEqual(scene_parts, original)
        self.assertEqual(plan["parameters"], process_values)
        self.assertEqual(plan["sceneParameters"], scene_values)
        self.assertEqual(set(plan["sceneParts"]), {"memberA", "memberB"})
        for part in plan["designParts"]:
            role = part["role"]
            self.assertEqual(part["request"]["parameters"], section)
            self.assertEqual(part["request"]["length"], scene_parts[role]["length"])
            self.assertEqual(plan["sceneParts"][role]["profileRef"],
                             {"scope": "system", "id": "rect"})
        for part in plan["manufacturingParts"]:
            self.assertEqual(part["request"]["parameters"], section)
            # A 45 degree miter needs half the section depth beyond its centreline.
            self.assertEqual(part["request"]["length"],
                             scene_parts[part["sourceRole"]]["length"] + section["depth"] / 2)
        dispatched = self.runtime.generate({
            "action": "preview-plan", "templateId": template_id,
            "parameters": process_values, "processDrafts": {},
            "sceneParts": scene_parts, "sceneParameters": scene_values,
        }, None)
        self.assertEqual(dispatched["sceneParts"], plan["sceneParts"])
        self.assertEqual(dispatched["parameters"], process_values)
        self.assertEqual(dispatched["sceneParameters"], scene_values)

    def test_scene_part_inputs_reject_unknown_roles_fields_and_invalid_dimensions(self):
        invalid = [
            [], {"unknown": {"length": 200}},
            {"memberA": []}, {"memberA": {"pose": {}}},
            {"memberA": {"length": 0}}, {"memberA": {"length": 100001}},
            {"memberA": {"length": float("nan")}},
            {"memberA": {"length": float("inf")}},
            {"memberA": {"length": True}}, {"memberA": {"length": "300"}},
            {"memberA": {"parameters": []}},
            {"memberA": {"profileRef": {"scope": "system", "id": ""}}},
            {"memberA": {"profileRef": {"scope": "system", "id": "rect", "pose": {}}}},
            {"memberA": {"profileRef": {"scope": "system", "id": "round"}}},
        ]
        for scene_parts in invalid:
            with self.subTest(scene_parts=scene_parts), self.assertRaises(ValueError):
                self.runtime.preview_plan("two-end-end-angle", scene_parts=scene_parts)

    def test_scene_parameters_validate_their_own_scope_and_keep_legacy_inputs(self):
        legacy = self.runtime.preview_plan("two-end-end-angle", {"jointAngle": 75, "fitGap": 2})
        self.assertEqual(legacy["parameters"],
                         {"jointAngle": 75, "fitGap": 2, "planeRotation": 0})
        self.assertEqual(legacy["sceneParameters"],
                         {"jointAngle": 75, "planeRotation": 0})
        modern = self.runtime.preview_plan("two-end-end-angle", {"fitGap": 2},
                                           scene_parameters={"jointAngle": 75})
        self.assertEqual(modern["parameters"], {"fitGap": 2})
        self.assertEqual(modern["sceneParameters"], legacy["sceneParameters"])
        for invalid in ([], {"unknown": 1}, {"jointAngle": 200}, {"fitGap": 2}):
            with self.subTest(scene_parameters=invalid), self.assertRaises(ValueError):
                self.runtime.preview_plan("two-end-end-angle", scene_parameters=invalid)
        with self.assertRaises(ValueError):
            self.runtime.preview_plan("two-end-middle", scene_parameters={"interfaceMode": "saddle"})
        with self.assertRaisesRegex(ValueError, "工艺参数不能包含成品场景字段"):
            self.runtime.preview_plan("two-end-end-angle", {"jointAngle": 75},
                                      scene_parameters={})

    def test_miter_example_accepts_same_round_section_with_independent_lengths(self):
        profile_ref = {"scope": "system", "id": "round"}
        section = {"width": 50, "wallThickness": 2,
                   "innerOffsetX": 0, "innerOffsetY": 0}
        scene_parts = {
            "memberA": {"profileRef": profile_ref, "parameters": section, "length": 340},
            "memberB": {"profileRef": profile_ref, "parameters": section, "length": 210},
        }
        process = {"fitGap": 0}
        scene_parameters = {"jointAngle": 90, "planeRotation": 0}
        plan = self.runtime.preview_plan("two-end-end-angle", process, {},
                                         scene_parts, scene_parameters)
        self.assertEqual(plan["parameters"], process)
        self.assertEqual(plan["sceneParameters"], scene_parameters)
        self.assertEqual({part["role"]: part["request"]["length"]
                          for part in plan["designParts"]},
                         {"memberA": 340, "memberB": 210})
        self.assertEqual({part["sourceRole"]: part["request"]["length"]
                         for part in plan["manufacturingParts"]},
                         {"memberA": 365, "memberB": 235})
        for role in scene_parts:
            self.assertEqual(plan["sceneParts"][role]["profileRef"], profile_ref)
            self.assertEqual(plan["sceneParts"][role]["parameters"], section)

    def test_sparse_scene_part_drafts_cannot_derive_product_dimensions_from_process_values(self):
        descriptor = copy.deepcopy(self.runtime._template_by_id("two-end-end-angle"))
        for part in descriptor["previewScene"]["designParts"]:
            part["parameters"]["depth"] = "40 + $fitGap"
        drafts = (
            {"memberA": {"length": 320}},
            {"memberA": {"parameters": {"width": 72}},
             "memberB": {"parameters": {"width": 72}}},
        )
        with patch.object(self.runtime, "_template_by_id", return_value=descriptor):
            for scene_parts in drafts:
                with self.subTest(scene_parts=scene_parts):
                    baseline = self.runtime.preview_plan("two-end-end-angle", {"fitGap": 0},
                        scene_parts=scene_parts, scene_parameters={})
                    changed = self.runtime.preview_plan("two-end-end-angle", {"fitGap": 1},
                        scene_parts=scene_parts, scene_parameters={})
                    self.assertEqual(baseline["sceneParts"]["memberA"]["parameters"]["depth"], 40)
                    self.assertEqual(changed["sceneParts"]["memberA"]["parameters"]["depth"], 40)
                    self.assertEqual(changed["sceneParts"]["memberB"]["parameters"]["depth"], 40)
                    self.assertEqual(changed["sceneParts"], baseline["sceneParts"])
                    self.assertEqual(changed["designParts"], baseline["designParts"])
                    self.assertEqual(changed["sceneParts"]["memberA"]["length"],
                                     scene_parts["memberA"].get("length", 260))
                    if "parameters" in scene_parts["memberA"]:
                        self.assertEqual(changed["sceneParts"]["memberA"]["parameters"]["width"], 72)

    def test_legacy_profile_change_replaces_parameters_independently_of_field_order(self):
        profile_ref = {"scope": "system", "id": "round"}
        section = {"width": 50, "wallThickness": 2, "innerOffsetX": 0, "innerOffsetY": 0}
        expected = None
        for keys in (("profileRef", "parameters", "length"), ("parameters", "length", "profileRef")):
            with self.subTest(keys=keys):
                parts = {}
                for role, length in (("memberA", 340), ("memberB", 210)):
                    fields = {"profileRef": profile_ref, "parameters": section, "length": length}
                    parts[role] = {key: copy.deepcopy(fields[key]) for key in keys}
                before = copy.deepcopy(parts)
                plan = self.runtime.preview_plan("two-end-end-angle", {"fitGap": 0},
                    scene_parts=parts, scene_parameters={"jointAngle": 90})
                self.assertEqual(before, parts)
                for role in parts:
                    self.assertEqual(section, plan["sceneParts"][role]["parameters"])
                    self.assertEqual(profile_ref, plan["sceneParts"][role]["profileRef"])
                if expected is None:
                    expected = plan
                else:
                    self.assertEqual(expected, plan)

    def test_integrated_blank_requires_matching_section_but_allows_distinct_lengths(self):
        same_section = {
            "segmentA": {"parameters": {"width": 72}, "length": 320},
            "segmentB": {"parameters": {"width": 72}, "length": 190},
        }
        plan = self.runtime.preview_plan("bend", scene_parts=same_section)
        self.assertEqual(plan["sceneParts"]["segmentA"]["length"], 320)
        self.assertEqual(plan["sceneParts"]["segmentB"]["length"], 190)
        self.assertEqual(plan["manufacturingParts"][0]["request"]["parameters"]["width"], 72)

        mismatched_section = copy.deepcopy(same_section)
        mismatched_section["segmentB"]["parameters"]["width"] = 60
        with self.assertRaisesRegex(ValueError, "母材.*相同管型及截面参数"):
            self.runtime.preview_plan("bend", scene_parts=mismatched_section)

        mismatched_profile = copy.deepcopy(same_section)
        mismatched_profile["segmentB"] = {
            "profileRef": {"scope": "system", "id": "round"},
            "parameters": {"width": 72, "wallThickness": 2,
                           "innerOffsetX": 0, "innerOffsetY": 0},
            "length": 190,
        }
        with self.assertRaisesRegex(ValueError, "母材.*相同管型及截面参数"):
            self.runtime.preview_plan("bend", scene_parts=mismatched_profile)

    def test_every_system_template_declares_connection_shape(self):
        expected_shapes = {shape["layoutShape"] for shape in self.catalogue["finishedShapes"]}
        paths = list(self.runtime.ROOT.glob("*/assembly.json"))
        self.assertGreaterEqual(len(paths), len(self.catalogue["assemblies"]))
        for path in paths:
            with self.subTest(template=path.parent.name):
                descriptor = json.loads(path.read_text(encoding="utf-8"))
                self.assertIn(descriptor.get("layoutShape"), expected_shapes)
        visible = {item["id"]: item["layoutShape"] for item in self.catalogue["assemblies"]}
        self.assertEqual(visible["bend"], "l")
        self.assertEqual(visible["t-profile-insert"], "t")
        self.assertNotIn("two-end-middle", visible)
        self.assertEqual(visible["cross-through"], "cross")
        self.assertNotIn("four-end-end-end-end", visible)
        self.assertNotIn("three-end-end-middle", visible)
        self.assertEqual(visible["weld-interface"], "parallel")
        self.assertEqual(visible["insert-sleeve"], "straight")

        legacy = json.loads((self.runtime.ROOT / "bend" / "assembly.json").read_text(encoding="utf-8"))
        legacy.pop("layoutShape")
        self.runtime._validate_template(legacy, self.runtime.ROOT / "bend")
        legacy["layoutShape"] = "unknown"
        with self.assertRaisesRegex(ValueError, "layoutShape"):
            self.runtime._validate_template(legacy, self.runtime.ROOT / "bend")

    def test_catalogue_is_grouped_by_part_count_and_connection_position(self):
        ids = {item["id"] for item in self.catalogue["assemblies"]}
        self.assertTrue({
            "two-end-end-angle", "t-profile-insert", "mechanical-fastener",
            "cross-through",
            "insert-sleeve", "weld-interface", "bend", "tab-slot-lock",
            "wrap-a-over-b",
            "node-v-notch-integrated", "node-edge-arc-integrated",
            "segmented-bend", "node-embedded-arc-integrated",
            "flexible-slit-bend-integrated",
        }.issubset(ids))
        self.assertNotIn("two-end-middle", ids)
        self.assertNotIn("three-end-end-middle", ids)
        self.assertNotIn("wrap-b-over-a", ids)
        self.assertTrue({"through-bolt", "slot-bolt-adjustable", "saddle-weld",
                         "four-end-end-end-end"}.isdisjoint(ids))
        categories = {item["category"] for item in self.catalogue["assemblies"]}
        self.assertEqual(categories, {
            "integrated", "two-end-end", "two-end-near-side", "two-end-middle", "two-middle-middle",
            "cross-through",
        } | ({"three-end-orthogonal"} if "orthogonal-corner" in ids else set()))

    def test_cold_bend_and_each_notched_process_are_parallel_templates(self):
        by_id = {item["id"]: item for item in self.catalogue["assemblies"]}
        processes = {
            "segmented-bend": "segmented-bend",
            "node-v-notch-integrated": "v-notch-sharp",
            "node-embedded-arc-integrated": "embedded-arc-notch",
            "node-edge-arc-integrated": "edge-arc-groove",
            "flexible-slit-bend-integrated": "flexible-slit-bend",
        }
        cold = by_id["bend"]
        self.assertEqual(cold["displayName"], "冷折弯")
        self.assertEqual(cold["category"], "integrated")
        self.assertEqual(cold["partProcesses"], [])
        self.assertEqual({item["key"] for item in cold["parameters"]},
                         {"bendRadius", "bendFactor"})
        self.assertIn("stock-operation", cold["inputContract"]["supportedInputModes"])
        self.assertIn("angle", {p["key"] for p in cold["inputContract"]["geometryParameters"]})
        for template_id, tool_id in processes.items():
            with self.subTest(template=template_id):
                descriptor = by_id[template_id]
                self.assertEqual(descriptor["category"], cold["category"])
                self.assertEqual(descriptor["manufacturingPlan"]["realization"], "integrated")
                self.assertEqual(len(descriptor["manufacturingPlan"]["blankParts"]), 1)
                self.assertNotIn("bendMethod", {item["key"] for item in descriptor["parameters"]})
                self.assertNotIn("slotProcess", {item["key"] for item in descriptor["parameters"]})
                self.assertEqual(len(descriptor["partProcesses"]), 1)
                self.assertEqual(descriptor["partProcesses"][0]["resource"]["id"], tool_id)
                self.assertTrue(descriptor["partProcesses"][0]["resource"]["descriptor"]["parameters"])

    def test_formed_preview_recipe_and_annotations_follow_template_conditions(self):
        cold = self.runtime.preview_plan("bend", {"bendFactor": 0.8})
        self.assertEqual(cold["formedPreviewRecipe"]["kind"], "continuous-cold-bend")
        self.assertEqual(cold["formedPreviewRecipe"]["factor"], 0.8)
        self.assertIn("bend-factor", [item["id"] for item in cold["previewAnnotations"]])
        notched = self.runtime.preview_plan("segmented-bend")
        self.assertIsNone(notched["formedPreviewRecipe"])
        self.assertIsNotNone(notched["formedPreviewMesh"])
        self.assertNotIn("bend-factor", [item["id"] for item in notched["previewAnnotations"]])
        for template_id in ("node-v-notch-integrated", "node-edge-arc-integrated"):
            plan = self.runtime.preview_plan(template_id)
            self.assertEqual(plan["formedPreviewRecipe"]["kind"], "node-groove-fold")
            self.assertEqual(plan["formedPreviewRecipe"]["processId"], "node-slot")

    def test_segmented_slot_target_uses_template_script_and_real_slot_parameters(self):
        inputs = {"bendRadius": 40}
        defaults = self.runtime.preview_plan("segmented-bend", inputs,
                                            scene_parameters={"angle": 90})
        mesh = defaults["formedPreviewMesh"]
        self.assertEqual(defaults["parameters"], inputs)
        self.assertEqual(defaults["sceneParameters"], {"angle": 90, "planeRotation": 0})
        self.assertIsNone(defaults["formedPreviewRecipe"])
        self.assertEqual(mesh["metadata"]["bendProcess"], "segmented-bend")
        self.assertEqual(mesh["metadata"]["segmentCount"], 6)
        self.assertEqual(len(mesh["metadata"]["annotations"]["anchors"]["hingeCenters"]), 6)
        self.assertGreater(len(mesh["indices"]), 6 * 3)
        self.assertTrue(mesh["metadata"]["approximate"])
        self.assertEqual(mesh["metadata"]["formingValidation"], "not-performed")
        self.assertAlmostEqual(defaults["manufacturingParts"][0]["request"]["length"],
                               520 + mesh["metadata"]["patternLength"])
        anchors = mesh["metadata"]["annotations"]["anchors"]
        self.assertAlmostEqual(anchors["firstUncutEdge"][0] - anchors["straightStart"][0], 260)
        denser = self.runtime.preview_plan("segmented-bend", inputs,
            {"node-slot": {"segmented-bend": {"segmentCount": 9}}},
            scene_parameters={"angle": 90})
        self.assertEqual(denser["parameters"], inputs)
        self.assertEqual(denser["sceneParameters"], {"angle": 90, "planeRotation": 0})
        self.assertEqual(denser["formedPreviewMesh"]["metadata"]["segmentCount"], 9)
        self.assertEqual(len(denser["formedPreviewMesh"]["metadata"]["annotations"]["anchors"]["hingeCenters"]), 9)
        self.assertNotEqual(denser["manufacturingParts"][0]["request"]["length"],
                            defaults["manufacturingParts"][0]["request"]["length"])
        self.assertGreater(len(denser["formedPreviewMesh"]["positions"]), len(mesh["positions"]))

    def test_derived_dimensions_and_per_template_script_do_not_leak_into_parameters(self):
        descriptor = json.loads((self.runtime.ROOT / "bend" / "assembly.json").read_text(encoding="utf-8"))
        descriptor["id"] = "custom-bend"
        scene = descriptor["previewScene"]
        product = self._finished_product("bend")
        product["spans"]["armA"]["length"] = 300
        product["spans"]["armB"]["length"] = 180
        for span in product["spans"].values():
            span["parameters"]["wallThickness"] = 3
        scene["formedPreviewScript"] = "assembly.py"
        script = ("def build_plan(plan):\n"
                  "    plan['manufacturingParts'][0]['request']['length'] += 5\n"
                  "    return plan\n"
                  "\n"
                  "def build_formed_preview(plan):\n"
                  "    plan['parameters']['bendFactor'] = 99\n"
                  "    return {'kind': 1, 'positions': [0,0,0, 1,0,0, 0,1,0], "
                  "'indices': [0,1,2], 'metadata': {'source': 'custom'}}\n")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "custom-bend"
            package.mkdir()
            (package / "assembly.json").write_text(json.dumps(descriptor), encoding="utf-8")
            (package / "assembly.py").write_text(script, encoding="utf-8")
            (package / "applicability.py").write_text(
                "def check_applicability(finished_product, parameters):\n"
                "    return {'applicable': True, 'reason': ''}\n", encoding="utf-8")
            with patch.object(self.runtime, "ROOT", root):
                self.assertEqual([item["id"] for item in self.runtime.catalogue()["assemblies"]],
                                 ["custom-bend"])
                plan = self._product_preview("custom-bend", {"bendFactor": 1}, product=product)
                bound_descriptor = self.runtime._template_by_id("custom-bend")
                self.assertEqual(plan["parameters"]["bendFactor"], 1)
                self.assertEqual(set(plan["parameters"]),
                                 {item["key"] for item in descriptor["parameters"]})
                self.assertAlmostEqual(plan["manufacturingParts"][0]["request"]["length"],
                                       485 + 43 * math.pi / 2, places=5)
                self.assertEqual(plan["formedPreviewMesh"]["indices"], [0, 1, 2])
                standard_plan = {key: copy.deepcopy(value) for key, value in plan.items()
                                 if key != "formedPreviewMesh"}
                combined_values = {**standard_plan["parameters"],
                                   **standard_plan["sceneParameters"]}
                standard_plan["parameters"] = copy.deepcopy(combined_values)
                changed_input = copy.deepcopy(standard_plan)
                changed_input["parameters"]["bendFactor"] = 1.0
                with self.assertRaisesRegex(ValueError, "输入参数"):
                    self.runtime._validate_script_plan(changed_input, bound_descriptor,
                                                       combined_values,
                                                       standard_plan["sceneParts"],
                                                       standard_plan["sceneParameters"], product,
                                                       standard_plan["processInput"])
                changed_scene = copy.deepcopy(standard_plan)
                changed_scene["sceneParts"]["segmentA"]["length"] += 1
                with self.assertRaisesRegex(ValueError, "成品场景参数"):
                    self.runtime._validate_script_plan(changed_scene, bound_descriptor,
                                                       combined_values,
                                                       standard_plan["sceneParts"],
                                                       standard_plan["sceneParameters"], product,
                                                       standard_plan["processInput"])
                changed_scene_values = copy.deepcopy(standard_plan)
                changed_scene_values["sceneParameters"]["angle"] += 1
                with self.assertRaisesRegex(ValueError, "成品场景参数"):
                    self.runtime._validate_script_plan(changed_scene_values, bound_descriptor,
                                                       combined_values,
                                                       standard_plan["sceneParts"],
                                                       standard_plan["sceneParameters"], product,
                                                       standard_plan["processInput"])
                unknown_tool = copy.deepcopy(standard_plan)
                unknown_tool["manufacturingParts"][0]["request"]["features"].append(
                    {"toolRef": {"id": "undeclared-tool"}})
                with self.assertRaisesRegex(ValueError, "未声明的单件工艺"):
                    self.runtime._validate_script_plan(unknown_tool, bound_descriptor,
                                                       combined_values,
                                                       standard_plan["sceneParts"],
                                                       standard_plan["sceneParameters"], product,
                                                       standard_plan["processInput"])
                (package / "assembly.json").unlink()
                self.assertEqual(self.runtime.catalogue()["assemblies"], [])
        with self.assertRaisesRegex(ValueError, "索引"):
            self.runtime._validated_formed_mesh({"positions": [0,0,0, 1,0,0, 0,1,0],
                                                 "indices": [0,1,3]})
        scene["formedPreviewScript"] = "../outside.py"
        with self.assertRaisesRegex(ValueError, "assembly.py"):
            self.runtime._validate_template(descriptor, self.runtime.ROOT / "custom-bend")

    def test_assembly_reuses_single_part_processes(self):
        for assembly in self.catalogue["assemblies"]:
            for process in assembly["partProcesses"]:
                self.assertTrue(process.get("resource") or process.get("resourceSelection"))
                if process.get("resource"):
                    self.assertEqual(process["resource"]["kind"], "part-process")
                    self.assertIn("descriptor", process["resource"])

    def test_tab_slot_binds_the_actual_joint_contract(self):
        assembly = next(item for item in self.catalogue["assemblies"] if item["id"] == "tab-slot-lock")
        keys = {item["key"] for item in assembly["parameters"]}
        self.assertIn("straightDepth", keys)
        self.assertNotIn("tipRadius", keys)
        processes = {item["id"]: item["parameterBindings"] for item in assembly["partProcesses"]}
        self.assertEqual("male", processes["tab-end"]["gender"])
        self.assertEqual("female", processes["slot-end"]["gender"])
        self.assertEqual("$straightDepth", processes["tab-end"]["straightDepth"])
        self.assertEqual("$straightDepth", processes["slot-end"]["straightDepth"])
        self.assertNotIn("cornerRadius", processes["tab-end"])
        self.assertNotIn("cornerRadius", processes["slot-end"])
        self.assertEqual("$fitClearance", processes["slot-end"]["sideClearance"])
        self.assertEqual("$fitClearance", processes["slot-end"]["axialClearance"])
        self.assertNotIn("clearance", processes["slot-end"])

    def test_wrap_main_and_branch_use_one_template_with_conditional_side_insert_tools(self):
        for template_id, long_role, short_role in (
            ("wrap-a-over-b", "memberA", "memberB"),
        ):
            with self.subTest(template=template_id):
                descriptor = self.runtime._template_by_id(template_id)
                self.assertEqual(descriptor["displayName"], "长短包接")
                self.assertEqual([part["label"] for part in descriptor["participants"]],
                                 ["主件 · 长件", "支件 · 短件"])
                self.assertEqual(descriptor["legacyBindings"], [{
                    "templateId": "wrap-b-over-a",
                    "roleMap": {"memberA": "memberB", "memberB": "memberA"},
                }])
                self.assertEqual(descriptor["productBinding"]["anchors"],
                                 {long_role: "end", short_role: "end"})
                for process_id in ("long-side-slots-two", "long-side-slots-four"):
                    self.assertEqual(descriptor["productBinding"]["processes"][process_id]["relativePlacement"], {
                        "kind": "end-inset-side", "faceSource": "anchor.approachFace",
                        "inset": {"sectionRole": short_role, "axis": "width", "factor": 0.5},
                    })
                self.assertEqual(descriptor["manufacturingPlan"]["realization"], "separate")
                defaults = {item["key"]: item["defaultValue"] for item in descriptor["parameters"]
                            if item.get("scope", "process") != "scene"}
                self.assertNotIn("setback", defaults)
                self.assertNotIn("$setback", json.dumps(descriptor, ensure_ascii=False))
                self.assertFalse(defaults["maleFemale"])
                self.assertEqual(defaults["pairCount"], "two")
                definitions = {item["key"]: item for item in descriptor["parameters"]}
                self.assertEqual([option["value"] for option in definitions["pairCount"]["options"]],
                                 ["two", "four"])
                for key in ("pairCount", "tabWidth", "tabLength", "sideClearance"):
                    self.assertEqual(definitions[key]["visibleWhen"],
                                     {"op": "eq", "parameter": "maleFemale", "value": True})

                product = self._finished_product(template_id)
                plain = self._product_preview(template_id, product=product)
                self.assertEqual(plain["parameters"], defaults)
                self.assertEqual([operation["processId"] for operation in
                                  plain["resolvedWorkflow"]["partOperations"]], ["short-butt-end"])
                short_blank = next(part for part in plain["manufacturingParts"]
                                   if part["sourceRole"] == short_role)
                long_blank = next(part for part in plain["manufacturingParts"]
                                  if part["sourceRole"] == long_role)
                long_design = next(part for part in plain["designParts"]
                                   if part["role"] == long_role)
                short_design = next(part for part in plain["designParts"]
                                    if part["role"] == short_role)
                short_end = plain["processInput"]["parts"][short_role]["anchor"]["end"]
                # These are desired centreline spans, shared by all L processes.
                # Stock registration and cut allowances must not alter them.
                self.assertWorldPointsAlmostEqual(
                    self._world_point(long_design["matrix"],
                                      [long_design["request"]["length"], 0, 0]),
                    self._translation(short_design["matrix"]))
                self.assertEqual(long_blank["request"]["features"], [])
                self.assertEqual(short_blank["request"]["ends"][short_end]["toolRef"]["id"], "end-miter")
                self.assertEqual(short_blank["request"]["ends"][short_end]["trim"], 0)
                self.assertEqual(short_blank["request"]["length"],
                                 product["spans"]["armB"]["length"])
                self.assertEqual(short_design["matrix"][8], 1)
                self.assertEqual(next(step for step in plain["resolvedWorkflow"]["assemblySteps"]
                                      if step["id"] == "fit")["distance"], 0)

                for count_name, count in (("two", 2), ("four", 4)):
                    values = {"maleFemale": True, "pairCount": count_name}
                    plan = self._product_preview(template_id, values, product=product)
                    self.assertEqual(plan["parameters"], {**defaults, **values})
                    operations = plan["resolvedWorkflow"]["partOperations"]
                    self.assertEqual({operation["processId"] for operation in operations},
                                     {f"long-side-slots-{count_name}", f"short-end-tabs-{count_name}"})
                    long_blank = next(part for part in plan["manufacturingParts"]
                                      if part["sourceRole"] == long_role)
                    short_blank = next(part for part in plan["manufacturingParts"]
                                       if part["sourceRole"] == short_role)
                    slot = long_blank["request"]["features"][0]
                    tabs = short_blank["request"]["ends"][short_end]
                    self.assertEqual(slot["toolRef"]["id"], "paired-side-slots")
                    self.assertEqual(slot["toolParameters"]["pairCount"], count)
                    self.assertIs(slot["toolParameters"]["allowEndOpening"], True)
                    self.assertEqual(slot["reference"], "end")
                    self.assertEqual(long_blank["request"]["length"] - slot["station"],
                                     product["spans"]["armA"]["length"]
                                     - product["spans"]["armB"]["parameters"]["width"] / 2)
                    self.assertEqual(slot["section"]["profileRef"]["id"], "rect")
                    self.assertEqual(tabs["toolRef"]["id"], "paired-end-tabs")
                    self.assertEqual(tabs["toolParameters"]["pairCount"], count)
                    self.assertEqual(tabs["trim"], 0)
                    self.assertEqual(short_blank["request"]["length"],
                                     product["spans"]["armB"]["length"])
                    self.assertEqual(tabs["section"]["profileRef"]["id"], "rect")
                    self.assertEqual(plan["designParts"], plain["designParts"],
                                     "开启公母插舌不得改变成品外形")
                    self.assertEqual(next(step for step in plan["resolvedWorkflow"]["assemblySteps"]
                                          if step["id"] == "fit")["distance"], defaults["tabLength"])

                # A hidden choice retains its draft but may not activate any side-slot branch.
                hidden = self._product_preview(template_id,
                    {"maleFemale": False, "pairCount": "four", "tabWidth": 11}, product=product)
                self.assertEqual(hidden["parameters"]["pairCount"], "four")
                self.assertEqual(hidden["parameters"]["tabWidth"], 11)
                self.assertEqual([operation["processId"] for operation in
                                  hidden["resolvedWorkflow"]["partOperations"]], ["short-butt-end"])
                self.assertFalse(hidden["resolvedWorkflow"]["parameterEffects"]["pairCount"]["active"])
                self.assertFalse(hidden["resolvedWorkflow"]["parameterEffects"]["tabWidth"]["manufacturingGeometry"])
                invalid_hidden = {"maleFemale": False, "pairCount": "not-an-option", "tabWidth": -2}
                hidden = self._product_preview(template_id, invalid_hidden, product=product)
                self.assertEqual(hidden["parameters"], {**defaults, **invalid_hidden})
                self.assertEqual([operation["processId"] for operation in
                                  hidden["resolvedWorkflow"]["partOperations"]], ["short-butt-end"])
                with self.assertRaisesRegex(ValueError, "pairCount"):
                    self._product_preview(template_id,
                        {**invalid_hidden, "maleFemale": True}, product=product)
                with self.assertRaisesRegex(ValueError, "tabWidth"):
                    self._product_preview(template_id,
                        {"maleFemale": True, "pairCount": "two", "tabWidth": -2}, product=product)
                with self.assertRaisesRegex(ValueError, "未声明字段"):
                    self.runtime.preview_plan(template_id, {"setback": 40})

                resized = copy.deepcopy(product)
                resized["spans"]["armA"]["length"] = 400
                resized["spans"]["armB"]["length"] = 200
                resized["spans"]["armB"]["parameters"]["width"] = 50
                resized_plan = self._product_preview(template_id, product=resized)
                resized_male = self._product_preview(template_id, {"maleFemale": True}, product=resized)
                resized_long = next(part for part in resized_plan["designParts"]
                                    if part["role"] == long_role)
                resized_short = next(part for part in resized_plan["designParts"]
                                     if part["role"] == short_role)
                self.assertEqual(resized_long["matrix"][3], -400)
                self.assertEqual(resized_short["matrix"][3], 0)
                self.assertWorldPointsAlmostEqual(
                    self._world_point(resized_long["matrix"], [400, 0, 0]),
                    self._translation(resized_short["matrix"]))
                resized_long_blank = next(part for part in resized_male["manufacturingParts"]
                                          if part["sourceRole"] == long_role)
                resized_slot = resized_long_blank["request"]["features"][0]
                self.assertEqual(resized_slot["reference"], "end")
                self.assertEqual(resized_long_blank["request"]["length"] - resized_slot["station"], 375)

    def test_node_groove_templates_merge_logical_segments_only_in_the_example(self):
        for template_id, tool_id in (
            ("node-v-notch-integrated", "v-notch-sharp"),
            ("node-edge-arc-integrated", "edge-arc-groove"),
        ):
            with self.subTest(template=template_id):
                descriptor = self.runtime._template_by_id(template_id)
                self.assertEqual(descriptor["category"], "integrated")
                self.assertEqual(descriptor["manufacturingPlan"]["realization"], "integrated")
                self.assertEqual(len(descriptor["participants"]), 2)
                self.assertEqual(len(descriptor["manufacturingPlan"]["blankParts"]), 1)
                self.assertEqual(descriptor["partProcesses"][0]["resource"]["id"], tool_id)
                self.assertEqual(descriptor["productBinding"]["mode"], "unsupported")

                plan = self.runtime.preview_plan(template_id, scene_parameters={"angle": 75})
                self.assertEqual(plan["parameters"], {})
                self.assertEqual(plan["sceneParameters"], {"angle": 75, "planeRotation": 0})
                self.assertEqual(len(plan["manufacturingParts"]), 1)
                blank = plan["manufacturingParts"][0]
                self.assertEqual(blank["participantRoles"], ["segmentA", "segmentB"])
                self.assertEqual(blank["request"]["features"][0]["toolRef"]["id"], tool_id)
                self.assertEqual(blank["request"]["features"][0]["toolParameters"]["angle"], 75)

                real = self.runtime.resolve_bound_plan(template_id, {"angle": 75})
                self.assertFalse(real["supported"])
                self.assertEqual(real["manufacturingEffect"], "none")
                self.assertEqual(real["parts"], [])
                self.assertEqual(real["parameters"], {"angle": 75, "planeRotation": 0})
                self.assertIn("归并", real["summary"])

    def test_existing_end_angle_id_is_presented_as_miter_joint(self):
        descriptor = self.runtime._template_by_id("two-end-end-angle")
        self.assertIn("斜接", descriptor["displayName"])
        self.assertEqual(descriptor["id"], "two-end-end-angle")
        self.assertEqual(descriptor["partProcesses"][0]["resource"]["id"], "end-miter")

    @staticmethod
    def _world_point(matrix, point):
        return [
            matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3],
            matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7],
            matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11],
        ]

    def assertWorldPointsAlmostEqual(self, left, right, places=7):
        for actual, expected in zip(left, right):
            self.assertAlmostEqual(actual, expected, places=places)

    @staticmethod
    def _translation(matrix):
        return [matrix[3], matrix[7], matrix[11]]

    @staticmethod
    def _axis(matrix):
        return [matrix[0], matrix[4], matrix[8]]

    def assertTranslationDelta(self, exploded, finished, expected, places=7):
        actual = [
            self._translation(exploded)[index] - self._translation(finished)[index]
            for index in range(3)
        ]
        self.assertWorldPointsAlmostEqual(actual, expected, places=places)

    def test_finished_scene_places_real_interfaces_together(self):
        product = self._finished_product("two-end-end-angle")
        angled = self._product_preview("two-end-end-angle", {"fitGap": 0}, product=product)
        desired_a, desired_b = angled["designParts"]
        self.assertWorldPointsAlmostEqual(
            self._world_point(desired_a["matrix"], [260, 0, 0]),
            self._translation(desired_b["matrix"]))
        # Put the blanks back from their declared exploded displacement. Their
        # true long-point cut faces meet while the input centreline stays fixed.
        a, b = copy.deepcopy(angled["manufacturingParts"])
        for index, part in enumerate((a, b)):
            sign = 1 if index == 0 else -1
            for position, axis in zip((3, 7, 11), self._axis(part["matrix"])):
                part["matrix"][position] += sign * 100 * axis
        self.assertEqual((a["request"]["length"], b["request"]["length"]), (280, 280))
        for a_local, b_local in (([280, 0, -20], [0, 0, -20]),
                                 ([240, 0, 20], [40, 0, 20])):
            self.assertWorldPointsAlmostEqual(
                self._world_point(a["matrix"], a_local),
                self._world_point(b["matrix"], b_local),
            )
        self.assertEqual(angled["parameters"], {"fitGap": 0})
        self.assertEqual(angled["sceneParameters"],
                         {"jointAngle": 90, "planeRotation": 0})
        a_end = angled["manufacturingParts"][0]["request"]["ends"]["end"]
        b_end = angled["manufacturingParts"][1]["request"]["ends"]["start"]
        self.assertEqual((a_end["toolParameters"]["angle"], b_end["toolParameters"]["angle"]),
                         (-45, 45))
        self.assertEqual((a_end["rotation"], b_end["rotation"]), (90, 90))

        tab_slot = self._product_preview("tab-slot-lock")
        tab, slot = tab_slot["designParts"]
        self.assertWorldPointsAlmostEqual(
            self._world_point(tab["matrix"], [tab["request"]["length"], 0, 0]),
            self._world_point(slot["matrix"], [0, 0, 0]),
        )
        tongue_depth = tab_slot["parameters"]["straightDepth"] + tab_slot["parameters"]["nominalWidth"] / 2
        tab_stock, slot_stock = tab_slot["manufacturingParts"]
        self.assertEqual(tab_stock["request"]["length"], tab["request"]["length"] + tongue_depth)
        self.assertEqual(tab_stock["request"]["length"] + slot_stock["request"]["length"] - tongue_depth,
                         tab["request"]["length"] + slot["request"]["length"],
                         "插舌搭接余量不得减少成品总长")

        product = self._finished_product("t-profile-insert")
        product["parameters"]["intersectionAngle"] = 60
        end_middle = self._product_preview("t-profile-insert", {
            "insertDepth": 12, "fitGap": 0,
        }, product=product)
        host, branch = end_middle["designParts"]
        branch_tip = self._world_point(branch["matrix"], [branch["request"]["length"], 0, 0])
        self.assertAlmostEqual(branch_tip[2], 20)
        self.assertAlmostEqual(branch_tip[0], 20 / math.tan(math.radians(60)))
        self.assertAlmostEqual(branch["matrix"][0], -0.5)
        self.assertAlmostEqual(branch["matrix"][8], -(3 ** 0.5) / 2)

    def test_scene_length_overrides_keep_finished_joints_and_exploded_offsets(self):
        lengths = [345, 275, 315, 225]
        plans = {}
        for descriptor in self.catalogue["assemblies"]:
            template_id = descriptor["id"]
            product = self._finished_product(template_id)
            role_map = descriptor["exampleInput"]["roles"]
            scene_parts = {role: {"length": lengths[index]}
                           for index, role in enumerate(role_map)}
            with self.subTest(template=template_id):
                baseline = self._product_preview(template_id, product=product)
                for role, override in scene_parts.items():
                    product["spans"][role_map[role]]["length"] = override["length"]
                if product["shapeId"] == "parallel":
                    # Keep the two hole patterns at the same axial centre.
                    product["parameters"]["overlapLength"] = (lengths[0] + lengths[1]) / 2
                resized = self._product_preview(template_id, product=product)
                plans[template_id] = resized
                self.assertEqual(resized["sceneParts"].keys(), scene_parts.keys())
                for role, override in scene_parts.items():
                    self.assertEqual(resized["sceneParts"][role]["length"], override["length"])
                baseline_design = {part["role"]: part for part in baseline["designParts"]}
                resized_design = {part["role"]: part for part in resized["designParts"]}
                if len(resized["manufacturingParts"]) == len(resized["designParts"]):
                    baseline_blank = {part["sourceRole"]: part for part in baseline["manufacturingParts"]}
                    for blank in resized["manufacturingParts"]:
                        role = blank["sourceRole"]
                        baseline_delta = [
                            baseline_blank[role]["matrix"][index] - baseline_design[role]["matrix"][index]
                            for index in (3, 7, 11)
                        ]
                        resized_delta = [
                            blank["matrix"][index] - resized_design[role]["matrix"][index]
                            for index in (3, 7, 11)
                        ]
                        if template_id == "insert-sleeve":
                            expected_x = (lengths[0] + lengths[1] / 2 + 100
                                          if role == "insertPart" else -lengths[1] / 2)
                            self.assertWorldPointsAlmostEqual(resized_delta, [expected_x, 0, 0])
                        elif template_id == "wrap-a-over-b":
                            expected = ([lengths[0] / 2, 0, 0] if role == "memberA" else
                                [lengths[0] / 2 - product["spans"]["armB"]["parameters"]["width"] / 2,
                                 0, lengths[1] + product["spans"]["armA"]["parameters"]["depth"] / 2 + 100])
                            self.assertWorldPointsAlmostEqual(resized_delta, expected)
                        elif template_id == "weld-interface" and role == "secondary":
                            self.assertWorldPointsAlmostEqual(resized_delta,
                                [-lengths[0] / 2, baseline_delta[1], baseline_delta[2]])
                        else:
                            self.assertWorldPointsAlmostEqual(resized_delta, baseline_delta)
                else:
                    self.assertEqual(len(resized["manufacturingParts"]), 1)
                    self.assertAlmostEqual(resized["manufacturingParts"][0]["matrix"][3],
                                           -(lengths[0] + lengths[1]) / 2 - 40)

        def part(plan, role):
            return next(item for item in plan["designParts"] if item["role"] == role)

        def tip(item):
            return self._world_point(item["matrix"], [item["request"]["length"], 0, 0])

        for template_id in ("bend", "segmented-bend", "node-v-notch-integrated",
                            "node-embedded-arc-integrated", "node-edge-arc-integrated",
                            "flexible-slit-bend-integrated"):
            with self.subTest(template=template_id):
                a, b = plans[template_id]["designParts"]
                self.assertWorldPointsAlmostEqual(tip(a), self._translation(b["matrix"]))
                self.assertWorldPointsAlmostEqual(tip(a), [0, 0, 0])

        cross = plans["cross-through"]["designParts"]
        for item in cross:
            self.assertWorldPointsAlmostEqual(
                self._world_point(item["matrix"], [item["request"]["length"] / 2, 0, 0]),
                [0, 0, 0])
        self.assertAlmostEqual(sum(cross[0]["matrix"][i] * cross[1]["matrix"][i]
                                   for i in (0, 4, 8)), 0)

        host, branch = plans["t-profile-insert"]["designParts"]
        self.assertWorldPointsAlmostEqual(
            self._world_point(host["matrix"], [host["request"]["length"] / 2, 0, 0]),
            [0, 0, 0])
        self.assertWorldPointsAlmostEqual(tip(branch), [0, 0, 20])

        miter_a, _ = plans["two-end-end-angle"]["designParts"]
        self.assertWorldPointsAlmostEqual(tip(miter_a), [0, 0, 0])
        tab, slot = plans["tab-slot-lock"]["designParts"]
        self.assertWorldPointsAlmostEqual(tip(tab), [0, 0, 0])
        self.assertWorldPointsAlmostEqual(self._translation(slot["matrix"]), tip(tab))

        insert, receive = plans["insert-sleeve"]["designParts"]
        self.assertWorldPointsAlmostEqual(tip(insert), self._translation(receive["matrix"]))

        long_member, short_member = plans["wrap-a-over-b"]["designParts"]
        self.assertWorldPointsAlmostEqual(tip(long_member), self._translation(short_member["matrix"]))

    def test_independent_shape_controls_change_finished_geometry(self):
        checked = 0
        for shape in self.catalogue["finishedShapes"]:
            product = self.runtime.finished_products.create(shape["id"])
            for definition in shape["parameters"]:
                key = definition["key"]
                with self.subTest(shape=shape["id"], parameter=key):
                    baseline = self.runtime.finished_product_plan(product)
                    if definition["valueType"] == "choice":
                        changed_value = next(option["value"] for option in definition["options"]
                                             if option["value"] != definition["defaultValue"])
                    else:
                        changed_value = definition["defaultValue"] + definition.get("step", 1)
                    changed_product = copy.deepcopy(product)
                    changed_product["parameters"][key] = changed_value
                    changed = self.runtime.finished_product_plan(changed_product)
                    self.assertEqual(changed["finishedProduct"], changed_product)
                    def finished_signature(plan):
                        return [(part["matrix"], part["request"]["length"],
                                 part["request"]["profileRef"], part["request"]["parameters"])
                                for part in plan["designParts"]]
                    self.assertNotEqual(finished_signature(baseline), finished_signature(changed),
                                        f"{shape['id']}.{key} must alter the finished scene")
                    checked += 1
        self.assertGreaterEqual(checked, 5)

    def test_exploded_scene_follows_each_assembly_path(self):
        tab_slot = self.runtime.preview_plan("tab-slot-lock")
        tab, slot = tab_slot["designParts"]
        tab_blank, slot_blank = tab_slot["manufacturingParts"]
        self.assertEqual(tab_blank["matrix"], tab_blank["explodedMatrix"])
        self.assertEqual(self._axis(tab_blank["matrix"]), self._axis(tab["matrix"]))
        self.assertEqual(self._axis(slot_blank["matrix"]), self._axis(slot["matrix"]))
        self.assertTranslationDelta(tab_blank["matrix"], tab["matrix"], [-100, 0, 0])
        insertion_depth = tab_slot["parameters"]["straightDepth"] + tab_slot["parameters"]["nominalWidth"] / 2
        self.assertTranslationDelta(slot_blank["matrix"], slot["matrix"], [100 - insertion_depth, 0, 0])

        angled = self.runtime.preview_plan("two-end-end-angle", {
            "jointAngle": 60, "fitGap": 0, "planeRotation": 35,
        })
        for finished, exploded in zip(angled["designParts"], angled["manufacturingParts"]):
            self.assertWorldPointsAlmostEqual(self._axis(exploded["matrix"]), self._axis(finished["matrix"]))
        self.assertTranslationDelta(angled["manufacturingParts"][0]["matrix"], angled["designParts"][0]["matrix"], [-100, 0, 0])

        end_middle = self.runtime.preview_plan("t-profile-insert", {
            "insertDepth": 12, "fitGap": 0,
        }, scene_parameters={"intersectionAngle": 60})
        self.assertTranslationDelta(end_middle["manufacturingParts"][0]["matrix"], end_middle["designParts"][0]["matrix"], [0, 0, 0])
        branch_axis = self._axis(end_middle["designParts"][1]["matrix"])
        retreat = -100 + 20 / math.sin(math.radians(60))
        self.assertTranslationDelta(
            end_middle["manufacturingParts"][1]["matrix"],
            end_middle["designParts"][1]["matrix"],
            [retreat * component for component in branch_axis],
        )

        bolted = self.runtime.preview_plan("through-bolt")
        self.assertTranslationDelta(bolted["manufacturingParts"][0]["matrix"], bolted["designParts"][0]["matrix"], [0, 0, -80])
        self.assertTranslationDelta(bolted["manufacturingParts"][1]["matrix"], bolted["designParts"][1]["matrix"], [0, 0, 80])

    def test_every_template_builds_a_two_to_four_part_interactive_preview_plan(self):
        for assembly in self.catalogue["assemblies"]:
            defaults = {item["key"]: item.get("defaultValue") for item in assembly["parameters"]}
            plan = self._product_preview(assembly["id"], defaults)
            self.assertEqual(plan["schema"], "icax.assembly-preview-plan")
            self.assertEqual(plan["templateId"], assembly["id"])
            self.assertEqual(len(plan["designParts"]), len(assembly["participants"]))
            self.assertGreaterEqual(len(plan["designParts"]), 2)
            self.assertLessEqual(len(plan["designParts"]), 4)
            self.assertEqual(len(plan["manufacturingParts"]), assembly["outputs"]["manufacturingPartCount"])
            self.assertEqual(plan["parameters"], defaults)
            aliases = assembly["exampleInput"]["parameterBindings"]
            self.assertEqual(plan["sceneParameters"], {
                definition["key"]: plan["finishedProduct"]["parameters"]
                    [aliases.get(definition["key"], definition["key"])]
                for definition in assembly["inputContract"]["geometryParameters"]})
            self.assertTrue(all(part["sourceRole"] for part in plan["manufacturingParts"]))
            expected_roles = {item["id"]: item["participants"] for item in assembly["manufacturingPlan"]["blankParts"]}
            for part in plan["manufacturingParts"]:
                self.assertEqual(part["participantRoles"], expected_roles[part["blankId"]])
                self.assertEqual(len(part["explodedMatrix"]), 16)
            for part in plan["designParts"] + plan["manufacturingParts"]:
                self.assertEqual(len(part["matrix"]), 16)
                self.assertEqual(len(part["compareMatrix"]), 16)
                self.assertGreater(part["request"]["length"], 0)
            if len(plan["manufacturingParts"]) > 1:
                blank_poses = {tuple(part["matrix"]) for part in plan["manufacturingParts"]}
                self.assertEqual(len(blank_poses), len(plan["manufacturingParts"]),
                                 f"{assembly['id']} 的下料件必须在右侧视口分开展示")

    def test_preview_plan_changes_from_logical_placement_to_actual_blank_processes(self):
        cold = self.runtime.preview_plan("bend", {"angle": 60, "bendRadius": 40, "bendFactor": 0.5})
        segmented = self.runtime.preview_plan("segmented-bend", {"angle": 60, "bendRadius": 40})
        self.assertNotEqual(cold["designParts"][1]["matrix"],
                            self.runtime.preview_plan("bend", {"angle": 90})["designParts"][1]["matrix"])
        self.assertEqual(cold["manufacturingParts"][0]["request"]["features"], [])
        self.assertEqual(segmented["manufacturingParts"][0]["request"]["features"][0]["toolRef"]["id"],
                         "segmented-bend")
        self.assertEqual(segmented["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]["angle"], 60)
        self.assertGreater(segmented["manufacturingParts"][0]["request"]["length"], 520)

        embedded = self.runtime.preview_plan("node-embedded-arc-integrated", {"angle": 90})
        embedded_tool = embedded["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(embedded_tool["angle"], 90)
        self.assertEqual(embedded_tool["arcRadius"], 10)
        self.assertNotIn("bendRadius", embedded_tool)
        customized = self.runtime.preview_plan("node-embedded-arc-integrated", {"angle": 90},
                                               {"node-slot": {"embedded-arc-notch": {"arcRadius": 12}}})
        self.assertEqual(customized["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]["arcRadius"], 12)

        edge_arc = self.runtime.preview_plan("node-edge-arc-integrated", {"angle": 90})
        edge_arc_tool = edge_arc["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(edge_arc_tool["angle"], 90)
        self.assertEqual(edge_arc_tool["bridge"], 1)
        self.assertTrue(edge_arc_tool["leftArc"])
        self.assertNotIn("bendRadius", edge_arc_tool)

        flexible = self.runtime.preview_plan("flexible-slit-bend-integrated", {"angle": 75, "bendRadius": 48})
        flexible_tool = flexible["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(flexible_tool["angle"], 75)
        self.assertEqual(flexible_tool["bendRadius"], 48)

        circle_wrap = self.runtime.preview_plan("node-v-notch-integrated", {"angle": 75},
            {"node-slot": {"v-notch-sharp": {
                "bottomStrategy": "relief", "reliefShape": "circleWrap",
                "enclosedDiameter": 16, "radialClearance": 0.1,
            }}})
        circle_tool = circle_wrap["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(circle_tool["toolRef"]["id"], "v-notch-sharp")
        self.assertEqual(circle_tool["toolParameters"]["angle"], 75)
        self.assertEqual(circle_tool["toolParameters"]["bottomStrategy"], "relief")
        self.assertEqual(circle_tool["toolParameters"]["reliefShape"], "circleWrap")
        self.assertNotIn("bendRadius", circle_tool["toolParameters"])

        bolted = self.runtime.preview_plan("mechanical-fastener", {"nominalDiameter": 12, "holeClearance": 1, "count": 3, "pitch": 45})
        self.assertEqual(len(bolted["manufacturingParts"]), 2)
        for part in bolted["manufacturingParts"]:
            hole = part["request"]["features"][0]
            self.assertEqual(hole["toolParameters"]["diameter"], 13)
            self.assertEqual(hole["arrayCount"], 3)
            self.assertEqual(hole["arrayPitch"], 45)
            self.assertTrue(hole["opposite"])

        saddle = self.runtime.preview_plan("two-end-middle", {"interfaceMode": "saddle"})
        branch_end = saddle["manufacturingParts"][1]["request"]["ends"]["end"]
        self.assertEqual(branch_end["toolRef"]["id"], "end-profile")
        self.assertEqual(branch_end["section"]["profileRef"]["id"], "rect")

    def test_end_middle_modes_cut_from_the_branch_profile_and_sleeve_stays_end_end(self):
        plans = {mode: self._product_preview("two-end-middle", {"interfaceMode": mode})
                 for mode in ("singleInsert", "throughInsert", "saddle")}
        end_feature = plans["singleInsert"]["manufacturingParts"][0]["request"]["features"][0]
        through_feature = plans["throughInsert"]["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(end_feature["toolRef"]["id"], "branch-profile")
        self.assertEqual(end_feature["direction"], "negative")
        self.assertEqual(through_feature["direction"], "through")
        self.assertEqual(end_feature["section"]["profileRef"]["id"], "round")
        self.assertEqual(plans["singleInsert"]["manufacturingParts"][1]["request"]["ends"]["end"]["toolRef"]["id"], "end-miter")
        self.assertEqual(plans["throughInsert"]["manufacturingParts"][1]["request"]["ends"]["end"]["type"], "keep")
        self.assertEqual(plans["saddle"]["manufacturingParts"][1]["request"]["ends"]["end"]["toolRef"]["id"], "end-profile")
        for mode in ("sleeve", "telescopic"):
            sleeve = self._product_preview("insert-sleeve", {"connectionMode": mode})
            self.assertEqual(sleeve["manufacturingParts"][1]["request"]["features"], [])

    def test_end_middle_real_stock_requires_allowance_only_for_end_cut_branches(self):
        for end, allowance in (("start", 30), ("end", 24)):
            with self.subTest(end=end):
                participants = self._actual_participants("two-end-middle", length=800,
                                                         width=60, depth=40)
                participants[1]["anchor"].update({"end": end, "stockAllowance": allowance})
                original = copy.deepcopy(participants)
                expected = {
                    "singleInsert": ("single-opening", "single-branch-end"),
                    "throughInsert": ("through-opening",),
                    "saddle": ("saddle-end",),
                }
                for mode, process_ids in expected.items():
                    supplied = {"interfaceMode": mode, "fitGap": 0.4}
                    plan = self.runtime.resolve_bound_plan("two-end-middle", supplied,
                        participants=participants, product_topology="T")
                    self.assertEqual(participants, original)
                    self.assertEqual(plan["parameters"]["interfaceMode"], mode)
                    self.assertEqual(plan["parameters"]["fitGap"], supplied["fitGap"])
                    self.assertEqual(tuple(plan["fitChecks"][0]["processIds"]), process_ids)
                    self.assertEqual(tuple(operation["processId"] for operation in
                                           plan["workflow"]["partOperations"]), process_ids)
                    by_role = {part["role"]: part for part in plan["parts"]}
                    if mode == "singleInsert":
                        self.assertEqual(by_role["host"]["features"][0]["direction"],
                                         "positive" if end == "start" else "negative")
                        self.assertEqual(by_role["branch"]["ends"][end]["type"], "end-miter")
                        self.assertEqual(by_role["branch"]["ends"][end]["trim"], allowance)
                    elif mode == "throughInsert":
                        self.assertEqual(set(by_role), {"host"})
                        self.assertEqual(by_role["host"]["features"][0]["direction"], "through")
                    else:
                        self.assertEqual(set(by_role), {"branch"})
                        self.assertEqual(by_role["branch"]["ends"][end]["type"], "end-profile")
                        self.assertEqual(by_role["branch"]["ends"][end]["trim"], allowance)
                    self.assertEqual(plan["assemblyFitStatus"], "not-verified")

                participants[1]["anchor"].pop("stockAllowance")
                self.assertEqual(self.runtime.resolve_bound_plan("two-end-middle",
                    {"interfaceMode": "throughInsert"}, participants=participants)["parts"][0]["role"], "host")
                for mode in ("singleInsert", "saddle"):
                    with self.assertRaisesRegex(ValueError, "库存余量"):
                        self.runtime.resolve_bound_plan("two-end-middle", {"interfaceMode": mode},
                                                        participants=participants)
                participants[1]["anchor"]["stockAllowance"] = 0
                for mode in ("singleInsert", "saddle"):
                    with self.assertRaisesRegex(ValueError, "不足以形成真实退切"):
                        self.runtime.resolve_bound_plan("two-end-middle", {"interfaceMode": mode},
                                                        participants=participants)

    def test_end_middle_clearance_cuts_the_single_insert_opening_and_checks_cavity(self):
        descriptor = self.runtime._template_by_id("two-end-middle")
        definition = next(item for item in descriptor["parameters"] if item["key"] == "fitGap")
        self.assertEqual(definition["displayName"], "开口单边配合间隙")
        self.assertEqual(definition["visibleWhen"],
                         {"op": "eq", "parameter": "interfaceMode", "value": "singleInsert"})
        checks = {item["id"]: item for item in descriptor["productBinding"]["fitChecks"]}
        self.assertEqual(checks["single-insert"]["gap"], {"parameter": "fitGap"})
        self.assertEqual(checks["through-insert"]["gap"], {"millimeters": 0})
        self.assertEqual(checks["saddle"]["gap"], {"millimeters": 0})
        annotation = next(item for item in descriptor["parameterDiagram"]["annotations"]
                          if item["parameter"] == "fitGap")
        self.assertEqual(annotation["visibleWhen"], definition["visibleWhen"])
        self.assertIn("单边放大量", annotation["description"])
        for mode in ("singleInsert", "throughInsert", "saddle"):
            with self.subTest(mode=mode):
                values = {"interfaceMode": mode, "fitGap": 2}
                baseline = self.runtime.preview_plan("two-end-middle",
                    {"interfaceMode": mode, "fitGap": 0})
                changed = self.runtime.preview_plan("two-end-middle", values)
                self.assertEqual(changed["parameters"],
                                 self.runtime._validated_values(descriptor, values))
                self.assertEqual(changed["designParts"], baseline["designParts"])
                if mode == "singleInsert":
                    opening = changed["manufacturingParts"][0]["request"]["features"][0]
                    baseline_opening = baseline["manufacturingParts"][0]["request"]["features"][0]
                    self.assertEqual(opening["clearance"], 2)
                    self.assertEqual(baseline_opening["clearance"], 0)
                else:
                    self.assertEqual(changed["manufacturingParts"], baseline["manufacturingParts"])
                self.assertEqual(changed["resolvedWorkflow"]["assemblySteps"],
                                 baseline["resolvedWorkflow"]["assemblySteps"])
                participants = self._actual_participants("two-end-middle")
                bound = self.runtime.resolve_bound_plan("two-end-middle", values,
                    participants=participants, product_topology="T")
                bound_baseline = self.runtime.resolve_bound_plan("two-end-middle",
                    {"interfaceMode": mode, "fitGap": 0}, participants=participants,
                    product_topology="T")
                self.assertEqual(bound["parameters"],
                                 self.runtime._validated_values(descriptor, values))
                if mode == "singleInsert":
                    self.assertEqual(bound["parts"][0]["features"][0]["clearance"], 2)
                    self.assertEqual(bound_baseline["parts"][0]["features"][0]["clearance"], 0)
                else:
                    self.assertEqual(bound["parts"], bound_baseline["parts"])
                self.assertEqual(bound["fitChecks"][0]["gapMm"],
                                 2 if mode == "singleInsert" else 0)
                self.assertEqual(changed["resolvedWorkflow"]["parameterEffects"]["fitGap"]["active"],
                                 mode == "singleInsert")
                self.assertEqual(changed["resolvedWorkflow"]["parameterEffects"]["fitGap"]["workflow"],
                                 mode == "singleInsert")
                self.assertEqual(changed["resolvedWorkflow"]["parameterEffects"]["fitGap"]["manufacturingGeometry"],
                                 mode == "singleInsert")
                if mode != "singleInsert":
                    draft = {"interfaceMode": mode, "fitGap": "inactive-draft"}
                    hidden = self.runtime.resolve_bound_plan("two-end-middle", draft,
                        participants=participants, product_topology="T")
                    self.assertEqual(hidden["parameters"]["fitGap"], "inactive-draft")
                    self.assertEqual(hidden["parts"], bound_baseline["parts"])
                    self.assertEqual(hidden["fitChecks"][0]["gapMm"], 0)

    def test_end_middle_opening_clearance_offsets_the_actual_outer_contour(self):
        path = ROOT / "apps/tube-designer/templates/mold/branch-profile/tool.py"
        spec = importlib.util.spec_from_file_location("end_middle_clearance_tool", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        context = {"feature": {"section": {"profile": {"contours": [
            {"kind": "circle", "center": [0, 0], "radius": 15}]}}},
            "bounds": {"min": [0, -20, -20], "max": [440, 20, 20]},
            "placement": {"clearance": 0.4}}
        original = copy.deepcopy(context)
        cutter = module.generate({}, context)
        contour = cutter["model"]["geometry"][0]["arguments"]["contours"][0]
        self.assertAlmostEqual(contour["radius"], 15.4)
        self.assertEqual(context, original)
        context["placement"]["clearance"] = 0
        self.assertEqual(module.generate({}, context)["model"]["geometry"][0]["arguments"]["contours"][0]["radius"], 15)
        context["feature"]["section"]["profile"]["contours"] = [
            {"kind": "roundedRectangle", "center": [0, 0], "width": 30, "height": 20, "radius": 2}]
        context["placement"]["clearance"] = 0.4
        rectangle = module.generate({}, context)["model"]["geometry"][0]["arguments"]["contours"][0]
        self.assertAlmostEqual(rectangle["width"], 30.8)
        self.assertAlmostEqual(rectangle["height"], 20.8)
        self.assertAlmostEqual(rectangle["radius"], 2.4)
        context["feature"]["section"]["profile"]["contours"] = [
            {"kind": "ellipse", "center": [0, 0], "radiusX": 15, "radiusY": 10}]
        with self.assertRaisesRegex(ValueError, "几何内核偏置"):
            module.generate({}, context)

    def test_end_middle_single_insert_example_fits_the_conservative_host_cavity(self):
        example = self._product_preview("two-end-middle", {"interfaceMode": "singleInsert"})
        by_role = {part["role"]: part["request"] for part in example["designParts"]}
        host = by_role["host"]["parameters"]
        branch = by_role["branch"]["parameters"]
        self.assertEqual(by_role["branch"]["profileRef"]["id"], "round")
        # The native single-insert check inscribes the host's rounded inner
        # cavity and bounds the branch's round outer profile by its square.
        for host_span in (host["width"], host["depth"]):
            conservative_cavity_span = host_span - 2 * host["wallThickness"] - 2 * host["innerRadius"]
            side_margin = (conservative_cavity_span - branch["width"]) / 2
            self.assertGreaterEqual(side_margin, example["parameters"]["fitGap"])

    def test_end_middle_real_product_tool_envelopes_reach_the_committed_t_surfaces(self):
        import sys

        framework = str(ROOT / "iCAX-Engine/framework/TemplateRuntime/python")
        sys.path.insert(0, framework)
        try:
            from icax_template_worker import _load_template
            product_directory = ROOT / "apps/tube-designer/templates/product/minimal_protective_grille"
            product_descriptor = json.loads((product_directory / "template.json").read_text(encoding="utf-8"))
            product_values = {field["key"]: field["defaultValue"] for field in product_descriptor["parameters"]}
            product_values.update(frameType="closed_frame", assemblyPlanningMode="external_templates",
                                  barLayoutMode="fixed_count", barCount=1, handleEnabled=False)
            product = _load_template(str(product_directory / "template.py"),
                                     "real-t-stock-envelope-tests").generate(product_values,
                    {"template": product_descriptor, "geometryPurpose": "manufacturing"})
        finally:
            sys.path.remove(framework)

        def tool_module(name):
            path = ROOT / "apps/tube-designer/templates/mold" / name / "tool.py"
            spec = importlib.util.spec_from_file_location(f"real_t_{name.replace('-', '_')}", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module

        end_profile = tool_module("end-profile")
        end_miter = tool_module("end-miter")
        section_path = ROOT / "apps/tube-designer/templates/_shared/section_geometry.py"
        section_spec = importlib.util.spec_from_file_location("real_t_section_geometry", section_path)
        end_miter.section_geometry = importlib.util.module_from_spec(section_spec)
        section_spec.loader.exec_module(end_miter.section_geometry)
        branch_profile = tool_module("branch-profile")
        items = {item["key"]: item for item in product["items"]}
        geometry = {node["key"]: node for node in product["geometry"]}

        def dot(left, right):
            return sum(a * b for a, b in zip(left, right))

        for relationship in product["relationships"]:
            if relationship["properties"]["topology"] != "T":
                continue
            with self.subTest(node=relationship["key"]):
                node_point = relationship["properties"]["centerlinePoint"]
                host_declaration, branch_declaration = relationship["properties"]["participantAnchors"]
                host_item, branch_item = (items[declaration["itemKey"]] for declaration in
                                          (host_declaration, branch_declaration))
                host_frame = host_item["properties"]["assemblyFrame.member"]
                branch_frame = branch_item["properties"]["assemblyFrame.member"]
                host_axis = [(finish - start) / host_frame["axisLength"] for start, finish in
                             zip(host_frame["start"], host_frame["end"])]
                self.assertEqual(host_axis, [0, 0, 1])
                branch_end = branch_declaration["anchor"]["end"]
                allowance = branch_declaration["anchor"]["stockAllowance"]
                axis = [(finish - start) / branch_frame["axisLength"] for start, finish in
                        zip(branch_frame["start"], branch_frame["end"])]
                inward = axis if branch_end == "start" else [-value for value in axis]
                branch_solid = geometry[branch_item["representations"]["result"]]
                branch_extrude = geometry[branch_solid["inputs"][0]]
                branch_pose = branch_solid["arguments"]["placement"]
                physical_end = list(branch_pose["origin"])
                if branch_end == "end":
                    physical_end = [physical_end[i] + branch_pose["zAxis"][i] *
                                    branch_extrude["arguments"]["vector"][2] for i in range(3)]
                self.assertAlmostEqual(dot([physical_end[i] - node_point[i] for i in range(3)],
                                           inward), -allowance)

                host_solid = geometry[host_item["representations"]["result"]]
                host_pose = host_solid["arguments"]["placement"]
                host_extrude = geometry[host_solid["inputs"][0]]
                host_profile = geometry[host_extrude["inputs"][0]]["arguments"]
                profile_pose = host_profile["placement"]
                def world_direction(local):
                    return [sum(host_pose[axis_name][i] * local[index] for index, axis_name in
                                enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]
                world_origin = [host_pose["origin"][i] + sum(
                    host_pose[axis_name][i] * profile_pose["origin"][index]
                    for index, axis_name in enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]
                projected_axes = (dot(world_direction(profile_pose["xAxis"]), inward),
                                  dot(world_direction(profile_pose["yAxis"]), inward))
                host_low, host_high = end_profile._support(host_profile["contours"][0], projected_axes)
                host_offset = dot([world_origin[i] - node_point[i] for i in range(3)], inward)
                host_low += host_offset
                host_high += host_offset
                self.assertAlmostEqual(host_low, -allowance)
                self.assertAlmostEqual(host_high, allowance)
                self.assertEqual(host_profile["contours"], host_item["properties"]["tubeDesigner.profile"]["contours"])

                participants = []
                for role, declaration, item in (("host", host_declaration, host_item),
                                                ("branch", branch_declaration, branch_item)):
                    section = copy.deepcopy(item["properties"]["tubeDesigner.profile"])
                    section["schemaVersion"] = 1  # Runtime input adapter for the committed v2 profile.
                    anchor = copy.deepcopy(declaration["anchor"])
                    if role == "host":
                        anchor["station"] -= host_frame["stockInterval"]["startStation"]
                    participants.append({"role": role, "memberEntityId": item["key"],
                        "itemKey": item["key"], "section": section,
                        "length": item["properties"]["length"], "anchor": anchor})
                plans = {mode: self.runtime.resolve_bound_plan("two-end-middle",
                    {"interfaceMode": mode, "fitGap": 0}, participants=participants,
                    product_topology="T") for mode in ("singleInsert", "throughInsert", "saddle")}
                bound_parts = {mode: {part["role"]: part for part in plan["parts"]}
                               for mode, plan in plans.items()}
                self.assertEqual(bound_parts["singleInsert"]["host"]["features"][0]["station"],
                                 host_declaration["anchor"]["station"] - host_frame["stockInterval"]["startStation"])
                self.assertEqual(set(bound_parts["throughInsert"]), {"host"})
                self.assertEqual(set(bound_parts["saddle"]), {"branch"})

                # At zero angle, end-miter's generated plane is exactly trim in
                # from the physical stock end: the single insert stops at the node.
                single_end = bound_parts["singleInsert"]["branch"]["ends"][branch_end]
                branch_section = participants[1]["section"]
                branch_width = branch_section["width"]
                branch_depth = branch_section["depth"]
                branch_bounds = {"min": [0, -branch_width / 2, -branch_depth / 2],
                                 "max": [participants[1]["length"], branch_width / 2, branch_depth / 2]}
                miter_cut = end_miter.generate(single_end["toolParameters"], {
                    "bounds": branch_bounds, "end": branch_end,
                    "placement": {key: single_end[key] for key in ("trim", "rotation", "datum")},
                    "targetSection": branch_section})
                expected_datum = allowance if branch_end == "start" else participants[1]["length"] - allowance
                self.assertAlmostEqual(miter_cut["datumCenter"], expected_datum)
                miter_profile = next(node for node in miter_cut["model"]["geometry"]
                                     if node["key"] == "tool-profile")
                self.assertAlmostEqual(miter_profile["arguments"]["placement"]["origin"][0], expected_datum)
                self.assertAlmostEqual(dot([physical_end[i] + inward[i] * single_end["trim"] - node_point[i]
                                            for i in range(3)], inward), 0)

                host_section = participants[0]["section"]
                host_bounds = {"min": [0, -host_section["width"] / 2, -host_section["depth"] / 2],
                               "max": [participants[0]["length"], host_section["width"] / 2,
                                       host_section["depth"] / 2]}
                for mode in ("singleInsert", "throughInsert"):
                    opening = bound_parts[mode]["host"]["features"][0]
                    cut = branch_profile.generate({}, {"bounds": host_bounds,
                        "feature": opening, "placement": opening})
                    cut_nodes = {node["key"]: node for node in cut["model"]["geometry"]}
                    self.assertEqual(cut_nodes["section"]["arguments"]["contours"],
                                     branch_section["contours"][:1])
                    origin = cut_nodes["section"]["arguments"]["placement"]["origin"]
                    vector = cut_nodes["tool"]["arguments"]["vector"]
                    cut_length = math.sqrt(dot(vector, vector))
                    cut_axis = [value / cut_length for value in vector]
                    # Legacy profiles lack a verified host face normal. The
                    # descriptor's product-end map retains their existing
                    # direction convention until a trusted face is supplied.
                    inward_part = [inward[2], inward[1], -inward[0]]
                    self.assertAlmostEqual(dot(cut_axis, inward_part),
                                           1 if branch_end == "start" else -1)
                    center = [opening["station"], 0, 0]
                    cut_start = dot([origin[i] - center[i] for i in range(3)], cut_axis)
                    cut_finish = cut_start + cut_length
                    if mode == "singleInsert":
                        if branch_end == "start":
                            self.assertAlmostEqual(cut_start, 0)
                            self.assertGreater(cut_finish, host_high)
                        else:
                            self.assertLess(cut_start, -host_high)
                            self.assertAlmostEqual(cut_finish, 0)
                    else:
                        self.assertLess(cut_start, -host_high)
                        self.assertGreater(cut_finish, -host_low)
                        self.assertAlmostEqual(dot([physical_end[i] - node_point[i]
                                                    for i in range(3)], inward), host_low)

                saddle_end = bound_parts["saddle"]["branch"]["ends"][branch_end]
                profile_cut = end_profile.generate(saddle_end["toolParameters"], {
                    "section": {"profile": host_section}, "bounds": branch_bounds,
                    "placement": saddle_end})
                cut_nodes = {node["key"]: node for node in profile_cut["model"]["geometry"]}
                self.assertEqual(cut_nodes["tool"]["inputs"], ["slab", "section-tool"])
                tool_section = cut_nodes["section"]["arguments"]
                self.assertEqual(tool_section["contours"], host_profile["contours"][:1])
                tool_pose = tool_section["placement"]
                tool_low, tool_high = end_profile._support(tool_section["contours"][0],
                    (tool_pose["xAxis"][0], tool_pose["yAxis"][0]))
                tool_low += tool_pose["origin"][0]
                tool_high += tool_pose["origin"][0]
                self.assertAlmostEqual(cut_nodes["section-tool"]["arguments"]["vector"][0], 0)
                slab_section = cut_nodes["slab-section"]["arguments"]
                _, slab_high = end_profile._support(slab_section["contours"][0],
                    (slab_section["placement"]["xAxis"][0], slab_section["placement"]["yAxis"][0]))
                self.assertAlmostEqual(slab_high + slab_section["placement"]["origin"][0], 0)
                self.assertAlmostEqual(-allowance + saddle_end["trim"] + tool_low, host_low)
                self.assertAlmostEqual(-allowance + saddle_end["trim"] + tool_high, host_high)
                self.assertEqual(plans["saddle"]["assemblyFitStatus"], "not-verified")

    def test_fastener_family_switches_only_the_required_hole_process(self):
        for kind in ("bolt", "pin", "rivet", "rivetNut", "selfTapping", "tapped"):
            plan = self.runtime.preview_plan("mechanical-fastener", {"fastenerType": kind})
            self.assertEqual(plan["manufacturingParts"][0]["request"]["features"][0]["toolRef"]["id"], "circle")
            self.assertEqual(plan["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "circle")
        adjustable = self.runtime.preview_plan("mechanical-fastener", {"fastenerType": "adjustableBolt", "adjustment": 18})
        slot = adjustable["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(slot["toolRef"]["id"], "slot")
        self.assertEqual(slot["toolParameters"]["spanAlong"], 29)

    def test_weld_family_generates_the_interface_specific_blank(self):
        lap = self.runtime.preview_plan("weld-interface", {"weldType": "lap"})
        plug = self.runtime.preview_plan("weld-interface", {"weldType": "plug"})
        slot = self.runtime.preview_plan("weld-interface", {"weldType": "slot"})
        self.assertEqual(lap["manufacturingParts"][1]["request"]["features"], [])
        self.assertEqual(plug["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "circle")
        self.assertEqual(slot["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "slot")

    def test_four_part_scene_returns_real_blanks_for_every_role(self):
        for template_id, expected in (("four-end-end-end-end", 4),):
            plan = self.runtime.preview_plan(template_id)
            self.assertEqual(len(plan["designParts"]), expected)
            self.assertEqual(len(plan["manufacturingParts"]), expected)
            self.assertEqual(
                {part["role"] for part in plan["designParts"]},
                {part["sourceRole"] for part in plan["manufacturingParts"]},
            )
            self.assertTrue(any(
                part["request"]["ends"]["start"].get("type") != "keep"
                or part["request"]["ends"]["end"].get("type") != "keep"
                for part in plan["manufacturingParts"]
            ))

    def test_cross_through_has_two_continuous_members_and_one_host_opening(self):
        plan = self.runtime.preview_plan("cross-through", {"fitGap": 0.4},
            scene_parts={"host": {"length": 500}, "through": {"length": 360}})
        design = {part["role"]: part for part in plan["designParts"]}
        blanks = {part["sourceRole"]: part for part in plan["manufacturingParts"]}
        self.assertEqual(set(design), {"host", "through"})
        self.assertEqual(set(blanks), set(design))
        for role, length in (("host", 500), ("through", 360)):
            self.assertEqual(design[role]["request"]["length"], length)
            self.assertEqual(blanks[role]["request"]["length"], length)
            self.assertWorldPointsAlmostEqual(
                self._world_point(design[role]["matrix"], [length / 2, 0, 0]), [0, 0, 0])
        host_axis = [design["host"]["matrix"][index] for index in (0, 4, 8)]
        through_axis = [design["through"]["matrix"][index] for index in (0, 4, 8)]
        self.assertAlmostEqual(sum(a * b for a, b in zip(host_axis, through_axis)), 0)
        opening, = blanks["host"]["request"]["features"]
        self.assertEqual(opening["toolRef"]["id"], "branch-profile")
        self.assertEqual(opening["direction"], "through")
        self.assertEqual(opening["clearance"], 0.4)
        self.assertEqual(opening["section"]["profileRef"],
                         design["through"]["request"]["profileRef"])
        self.assertEqual(blanks["through"]["request"]["features"], [])
        for blank in blanks.values():
            self.assertEqual(blank["request"]["ends"],
                             {"start": {"type": "keep"}, "end": {"type": "keep"}})

        participants = self._actual_participants("cross-through")
        original = copy.deepcopy(participants)
        bound = self.runtime.resolve_bound_plan("cross-through", {"fitGap": 0.4},
            participants=participants, product_topology="X")
        self.assertEqual(participants, original)
        self.assertTrue(bound["supported"])
        self.assertEqual(bound["manufacturingEffect"], "incremental-cut")
        self.assertEqual(len(bound["parts"]), 1)
        self.assertEqual(bound["parts"][0]["role"], "host")
        self.assertEqual(bound["parts"][0]["features"][0]["toolRef"]["id"],
                         "branch-profile")
        self.assertEqual(bound["parts"][0]["features"][0]["section"]["profile"],
                         participants[1]["section"])
        participants[0]["anchor"]["face"] = "right"
        right_face = self.runtime.resolve_bound_plan("cross-through", {"fitGap": 0.4},
            participants=participants, product_topology="X")
        self.assertEqual(right_face["parts"][0]["features"][0]["azimuth"], 90)
        self.assertEqual(right_face["parts"][0]["features"][0]["face"], "right")

    def test_cold_bend_has_one_planar_bend_without_orientation_controls(self):
        plan = self.runtime.preview_plan("bend", scene_parameters={"angle": 75})
        self.assertEqual(plan["sceneParameters"], {"angle": 75})
        self.assertAlmostEqual(plan["designParts"][1]["matrix"][1], 0.0)
        self.assertNotIn("plane", plan["formedPreviewRecipe"])
        self.assertNotIn("planeRotation", plan["formedPreviewRecipe"])

    def test_all_templates_resolve_example_workflows_from_preview_operations(self):
        template_ids = {path.parent.name for path in self.runtime.ROOT.glob("*/assembly.json")}
        self.assertEqual(template_ids - {"through-bolt", "slot-bolt-adjustable", "saddle-weld",
                                         "four-end-end-end-end", "two-end-middle"},
                         {item["id"] for item in self.catalogue["assemblies"]})
        for template_id in template_ids:
            with self.subTest(template=template_id):
                plan = self._product_preview(template_id)
                workflow = plan["resolvedWorkflow"]
                self.assertEqual(workflow["schema"], "icax.assembly-workflow")
                descriptor = self.runtime._template_by_id(template_id)
                formal = descriptor["manufacturingPlan"]["realization"] == "separate"
                self.assertEqual(workflow["scope"], "process-instance" if formal else "template-example")
                self.assertEqual(len(workflow["blankParts"]), len(plan["manufacturingParts"]))
                self.assertTrue(workflow["assemblySteps"])
                self.assertIsInstance(workflow["bom"], list)
                self.assertIsInstance(workflow["checks"], list)
                self.assertEqual({entry["id"] for entry in workflow["blankParts"]},
                                 {entry["blankId"] for entry in plan["manufacturingParts"]})
                for operation in workflow.get("operations", []) if formal else workflow["partOperations"]:
                    if formal:
                        blank = next(part for part in plan["manufacturingParts"]
                                     if part["sourceRole"] == operation["role"])
                        if "requestFeature" in operation:
                            tool = next(feature for feature in blank["request"]["features"]
                                        if feature["id"] == operation["id"])
                            self.assertEqual(tool, {**operation["requestFeature"], "id": operation["id"]})
                        else:
                            self.assertEqual(blank["request"]["ends"][operation["end"]],
                                             operation["requestEnd"])
                        continue
                    blank = next(part for part in plan["manufacturingParts"]
                                 if part["blankId"] == operation["blankId"])
                    self.assertTrue(set(operation["participantRoles"]).issubset(blank["participantRoles"]))
                    if operation["placement"]["target"] == "end":
                        tool = blank["request"]["ends"][operation["placement"]["end"]]
                    else:
                        tool = next(feature for feature in blank["request"]["features"]
                                    if feature["id"] == operation["processId"])
                    self.assertEqual(tool["toolRef"], operation["resourceRef"])
                    self.assertEqual(tool["toolParameters"], operation["toolParameters"])
                self.assertEqual(workflow["validationStatus"],
                                 "dimension-checked-example" if workflow["checks"] else "example-unchecked")

    def test_every_template_choice_branch_resolves_without_changing_the_parameter_echo(self):
        cases = 0
        for path in self.runtime.ROOT.glob("*/assembly.json"):
            descriptor = json.loads(path.read_text(encoding="utf-8"))
            for parameter in descriptor["parameters"]:
                if parameter["valueType"] != "choice":
                    continue
                for option in parameter["options"]:
                    selected = option["value"]
                    with self.subTest(template=descriptor["id"], parameter=parameter["key"], value=selected):
                        unavailable = ((descriptor["id"], parameter["key"], selected) in {
                            ("mechanical-fastener", "fastenerType", "rivetNut"),
                            ("mechanical-fastener", "fastenerType", "selfTapping"),
                            ("mechanical-fastener", "fastenerType", "tapped"),
                            ("tab-slot-lock", "lockMethod", "bolt"),
                        })
                        if unavailable:
                            product = self._finished_product(descriptor["id"])
                            local = self.runtime.process_contract.from_product(
                                self.runtime._template_by_id(descriptor["id"]), product,
                                self.runtime.finished_product_plan(product), example_selection=True)
                            before = copy.deepcopy(local)
                            result = self.runtime.evaluate_process(descriptor["id"], local,
                                                                  {parameter["key"]: selected})
                            self.assertFalse(result["applicable"])
                            self.assertEqual(selected, result["parameters"][parameter["key"]])
                            self.assertEqual([], result["operations"])
                            self.assertTrue(any(check["status"] == "requires-definition"
                                                for check in result["checks"]))
                            self.assertEqual(before, local)
                            with self.assertRaises(ValueError):
                                self._product_preview(descriptor["id"], {parameter["key"]: selected},
                                                      product=product)
                            cases += 1
                            continue
                        plan = self._product_preview(descriptor["id"], {parameter["key"]: selected})
                        self.assertEqual(plan["parameters"][parameter["key"]], selected)
                        self.assertEqual(plan["resolvedWorkflow"]["scope"],
                            "process-instance" if descriptor["manufacturingPlan"]["realization"] == "separate"
                            else "template-example")
                        self.assertFalse(any(check["status"] == "fail" for check in plan["resolvedWorkflow"]["checks"]))
                    cases += 1
        expected_cases = sum(len(parameter["options"])
                             for path in self.runtime.ROOT.glob("*/assembly.json")
                             for parameter in json.loads(path.read_text(encoding="utf-8"))["parameters"]
                             if parameter["valueType"] == "choice")
        self.assertEqual(cases, expected_cases)
        self.assertGreater(cases, 0)

    def test_insert_fit_clearance_checks_independent_stock_and_depth_preserves_product(self):
        product = self._finished_product("insert-sleeve")
        baseline = self._product_preview("insert-sleeve", product=product)
        changed = self._product_preview("insert-sleeve", {"insertDepth": 150}, product=product)
        self.assertNotIn("outerDiameter", changed["parameters"])
        self.assertNotIn("innerDiameter", changed["parameters"])
        self.assertNotIn("wallThickness", changed["parameters"])
        self.assertEqual(changed["parameters"]["fitClearance"], 0.3)
        base_outer = next(part for part in baseline["designParts"] if part["role"] == "receivePart")
        outer = next(part for part in changed["designParts"] if part["role"] == "receivePart")
        inner = next(part for part in changed["designParts"] if part["role"] == "insertPart")
        self.assertAlmostEqual(outer["request"]["parameters"]["width"]
                               - base_outer["request"]["parameters"]["width"], 0.0)
        self.assertAlmostEqual((outer["request"]["parameters"]["width"]
                                 - 2 * outer["request"]["parameters"]["wallThickness"]
                                 - inner["request"]["parameters"]["width"]) / 2, 0.3)
        self.assertEqual([part["matrix"] for part in baseline["designParts"]],
                         [part["matrix"] for part in changed["designParts"]],
                         "插入深度与配合间隙不能移动成品中的两根管")
        self.assertWorldPointsAlmostEqual(
            self._world_point(inner["matrix"], [inner["request"]["length"], 0, 0]),
            self._translation(outer["matrix"]))
        for plan in (baseline, changed):
            inserted = next(part for part in plan["manufacturingParts"] if part["sourceRole"] == "insertPart")
            received = next(part for part in plan["manufacturingParts"] if part["sourceRole"] == "receivePart")
            self.assertEqual(inserted["request"]["length"],
                             product["spans"]["first"]["length"] + plan["parameters"]["insertDepth"])
            self.assertEqual(inserted["request"]["length"] + received["request"]["length"]
                             - plan["parameters"]["insertDepth"], 640,
                             "工艺搭接不能减少成品总长")
        workflow = changed["resolvedWorkflow"]
        self.assertEqual(next(step for step in workflow["assemblySteps"] if step["id"] == "insert")["distance"], 150)
        self.assertAlmostEqual(next(check for check in workflow["checks"] if check["id"] == "section-fit")["actual"], 0.3)
        self.assertAlmostEqual(next(check for check in workflow["checks"]
                                    if check["id"] == "insert-depth-inner-limit")["actual"], 280)
        self.assertAlmostEqual(next(check for check in workflow["checks"]
                                    if check["id"] == "insert-depth-outer-limit")["actual"], 210)
        self.assertEqual(workflow["validationStatus"], "dimension-checked-example")
        self.assertFalse(workflow["parameterEffects"]["fitClearance"]["manufacturingGeometry"])
        self.assertTrue(workflow["parameterEffects"]["fitClearance"]["workflow"])
        self.assertTrue(workflow["parameterEffects"]["insertDepth"]["manufacturingGeometry"])
        before = copy.deepcopy(product)
        rejected = self.runtime.check_applicability("insert-sleeve", product,
                                                    {"fitClearance": 0.8, "insertDepth": 150})
        self.assertFalse(rejected["applicable"])
        self.assertIn("间隙", rejected["reason"])
        self.assertEqual(product, before)
        with self.assertRaisesRegex(ValueError, "间隙"):
            self._product_preview("insert-sleeve", {"fitClearance": 0.8}, product=product)
        product["spans"]["second"]["parameters"]["width"] = 35.6
        fitted = self._product_preview("insert-sleeve", {"fitClearance": 0.8, "insertDepth": 150},
                                       product=product)
        fit_check = next(check for check in fitted["resolvedWorkflow"]["checks"] if check["id"] == "section-fit")
        self.assertAlmostEqual(fit_check["actual"], 0.8)
        self.assertEqual(fit_check["status"], "pass")
        self.assertEqual(fitted["resolvedWorkflow"]["validationStatus"], "dimension-checked-example")

    def test_failed_example_checks_are_reviewable_before_manufacturing(self):
        product = self._finished_product("insert-sleeve")
        baseline = self._product_preview("insert-sleeve", product=product)
        descriptor = self.runtime._template_by_id("insert-sleeve")
        invalid_travel = {
            "connectionMode": "telescopic", "insertDepth": 120, "travelLength": 110,
        }
        rejected = self.runtime.check_applicability("insert-sleeve", product, invalid_travel)
        self.assertFalse(rejected["applicable"])
        self.assertIn("剩余啮合深度", rejected["reason"])
        with self.assertRaisesRegex(ValueError, "剩余啮合深度"):
            self._product_preview("insert-sleeve", invalid_travel, product=product)
        def dimension_checks(values):
            normalized = self.runtime._validated_values(descriptor, values)
            expressions = self.runtime._design_expression_values(normalized, baseline["designParts"])
            return self.runtime._resolved_dimension_checks(descriptor, expressions,
                baseline["designParts"], baseline["manufacturingParts"], [])
        residual = next(check for check in dimension_checks(invalid_travel)
                        if check["id"] == "residual-engagement")
        self.assertEqual(residual["status"], "fail")
        self.assertTrue(residual["blocking"])
        self.assertEqual(residual["actual"], 10)
        rejected = self.runtime.check_applicability("insert-sleeve", product, {"insertDepth": 370})
        self.assertFalse(rejected["applicable"])
        self.assertIn("承接件长度", rejected["reason"])
        overlap = next(check for check in dimension_checks({"insertDepth": 370})
                       if check["id"] == "insert-depth-outer-limit")
        self.assertEqual(overlap["status"], "fail")
        self.assertAlmostEqual(overlap["actual"], -10)
        welded = self._product_preview("insert-sleeve", {"lockMethod": "weld"}, product=product)
        self.assertEqual(welded["resolvedWorkflow"]["validationStatus"], "dimension-checked-example")
        self.assertEqual(next(step for step in welded["resolvedWorkflow"]["assemblySteps"]
                              if step["id"] == "lock")["kind"], "weld")
        with self.assertRaisesRegex(ValueError, "lockMethod"):
            self._product_preview("insert-sleeve", {"lockMethod": "bolt"}, product=product)
        product["spans"]["first"]["parameters"].update({"width": 4, "wallThickness": 3})
        before = copy.deepcopy(product)
        invalid_wall = self.runtime.check_applicability("insert-sleeve", product)
        self.assertFalse(invalid_wall["applicable"])
        self.assertEqual(product, before)
        self.assertEqual(len(self.runtime.finished_product_plan(product)["designParts"]), 2)
        # The dimensional checker still produces a reviewable blocking report;
        # public preflight prevents this invalid input from entering machining.
        designs = copy.deepcopy(baseline["designParts"])
        designs[0]["request"]["parameters"].update({"width": 4, "wallThickness": 3})
        checks = self.runtime._resolved_dimension_checks(descriptor,
            self.runtime._design_expression_values(baseline["parameters"], designs),
            designs, baseline["manufacturingParts"], [])
        check = next(item for item in checks
                     if item["id"] == "section-fit")
        self.assertEqual(check["status"], "fail")
        self.assertTrue(check["blocking"])

    def test_coaxial_section_fit_rejects_incompatible_stock_without_rewriting_it(self):
        reference = {"scope": "system", "id": "rect"}
        parts = {
            "insertPart": {"profileRef": reference, "parameters": {
                "width": 30, "depth": 20, "wallThickness": 2,
                "cornerRadius": 3, "innerRadius": 1,
                "innerOffsetX": 0, "innerOffsetY": 0,
            }},
            "receivePart": {"profileRef": reference, "parameters": {
                "width": 34.6, "depth": 24.6, "wallThickness": 2,
                "cornerRadius": 3, "innerRadius": 1,
                "innerOffsetX": 0, "innerOffsetY": 0,
            }},
        }
        fitted = self.runtime.preview_plan("insert-sleeve", scene_parts=parts)
        check = next(item for item in fitted["resolvedWorkflow"]["checks"] if item["id"] == "section-fit")
        self.assertEqual(check["status"], "pass")
        self.assertAlmostEqual(check["widthClearance"], 0.3)
        self.assertAlmostEqual(check["depthClearance"], 0.3)
        self.assertEqual(len(fitted["designParts"]), 2)
        self.assertEqual(len(fitted["manufacturingParts"]), 2)
        source = next(item for item in self.runtime._template_by_id("insert-sleeve")["dimensionChecks"]
                      if item["id"] == "section-fit")

        def fit_check(actual_parts):
            before = copy.deepcopy(actual_parts)
            design = copy.deepcopy(fitted["designParts"])
            for part in design:
                part["request"]["profileRef"] = copy.deepcopy(actual_parts[part["role"]]["profileRef"])
                part["request"]["parameters"] = copy.deepcopy(actual_parts[part["role"]]["parameters"])
            by_role = {part["role"]: part for part in design}
            result = self.runtime._coaxial_section_fit_check(
                source, by_role["insertPart"], by_role["receivePart"], fitted["parameters"])
            self.assertEqual(actual_parts, before)
            return result

        too_small = copy.deepcopy(parts)
        too_small["receivePart"]["parameters"]["width"] = 30
        check = fit_check(too_small)
        self.assertEqual(check["status"], "fail")
        self.assertTrue(check["blocking"])
        self.assertLess(check["actual"], 0)
        before = copy.deepcopy(too_small)
        with self.assertRaisesRegex(ValueError, "实际最小单侧间隙"):
            self.runtime.preview_plan("insert-sleeve", scene_parts=too_small)
        self.assertEqual(too_small, before)

        rounded_bore = copy.deepcopy(parts)
        rounded_bore["insertPart"]["parameters"]["cornerRadius"] = 0
        rounded_bore["receivePart"]["parameters"]["innerRadius"] = 5
        check = fit_check(rounded_bore)
        self.assertEqual(check["status"], "fail")
        self.assertLess(check["cornerMargin"], 0)

        eccentric_bore = copy.deepcopy(parts)
        eccentric_bore["receivePart"]["parameters"]["innerOffsetX"] = 0.1
        check = fit_check(eccentric_bore)
        self.assertEqual(check["status"], "fail")
        self.assertIn("偏心", check["detail"])

        mixed = copy.deepcopy(parts)
        mixed["insertPart"] = {"profileRef": {"scope": "system", "id": "round"},
                               "parameters": {"width": 30, "wallThickness": 2}}
        check = fit_check(mixed)
        self.assertEqual(check["status"], "fail")
        with self.assertRaises(ValueError):
            self.runtime.preview_plan("insert-sleeve", scene_parts=mixed)

    def test_coaxial_rectangular_fit_checks_section_orientation(self):
        reference = {"scope": "system", "id": "rect"}
        parts = {
            "insertPart": {"profileRef": reference, "parameters": {
                "width": 30, "depth": 20, "wallThickness": 2,
                "cornerRadius": 3, "innerRadius": 1}},
            "receivePart": {"profileRef": reference, "parameters": {
                "width": 34.6, "depth": 24.6, "wallThickness": 2,
                "cornerRadius": 3, "innerRadius": 1}},
        }
        plan = self.runtime.preview_plan("insert-sleeve", scene_parts=parts)
        source = next(item for item in self.runtime._template_by_id("insert-sleeve")["dimensionChecks"]
                      if item["id"] == "section-fit")
        design = copy.deepcopy(plan["designParts"])
        matrix = design[0]["matrix"]
        angle = math.radians(45)
        matrix[5], matrix[9] = math.cos(angle), math.sin(angle)
        matrix[6], matrix[10] = -math.sin(angle), math.cos(angle)
        check = self.runtime._coaxial_section_fit_check(source, design[0], design[1], plan["parameters"])
        self.assertEqual(check["status"], "fail")
        self.assertIn("旋转", check["detail"])

    def test_parameter_effects_follow_independent_bend_templates(self):
        cold = self.runtime.preview_plan("bend")["resolvedWorkflow"]
        notched = self.runtime.preview_plan("segmented-bend")["resolvedWorkflow"]
        self.assertFalse(cold["partOperations"])
        self.assertTrue(cold["parameterEffects"]["bendFactor"]["manufacturingGeometry"])
        self.assertEqual(notched["partOperations"][0]["resourceRef"]["id"], "segmented-bend")
        self.assertTrue(notched["parameterEffects"]["bendRadius"]["manufacturingGeometry"])
        self.assertNotIn("bendFactor", notched["parameterEffects"])
        self.assertNotIn("slotProcess", cold["parameterEffects"])
        self.assertNotIn("bendMethod", cold["parameterEffects"])
        self.assertNotIn("bendPlane", cold["parameterEffects"])
        self.assertNotIn("planeRotation", cold["parameterEffects"])

    def test_selected_connection_modes_resolve_steps_bom_and_capability(self):
        bolt = self.runtime.preview_plan("mechanical-fastener", {
            "fastenerType": "bolt", "count": 3,
        })["resolvedWorkflow"]
        self.assertEqual(next(item for item in bolt["bom"] if item["id"] == "bolt")["quantity"], 3)
        self.assertFalse(any(check.get("blocking") for check in bolt["checks"]))
        rivet_nut = self.runtime.preview_plan("mechanical-fastener", {
            "fastenerType": "rivetNut",
        })["resolvedWorkflow"]
        self.assertEqual(rivet_nut["validationStatus"], "requires-definition")
        self.assertTrue(any(check["kind"] == "required-process" for check in rivet_nut["checks"]))
        self.assertNotEqual(next(step for step in bolt["assemblySteps"] if step["id"] == "lock")["label"],
                            next(step for step in rivet_nut["assemblySteps"] if step["id"] == "lock")["label"])
        self.assertEqual({item["id"] for item in rivet_nut["bom"]}, {"rivet-nut", "rivet-nut-screw"})

    def test_adjustment_axis_changes_the_actual_slot_without_moving_finished_parts(self):
        horizontal = self.runtime.preview_plan("slot-bolt-adjustable", {"adjustAxis": "x"})
        vertical = self.runtime.preview_plan("slot-bolt-adjustable", {"adjustAxis": "y"})
        h_slot = next(item for item in horizontal["resolvedWorkflow"]["partOperations"] if item["processId"] == "slot-a")
        v_slot = next(item for item in vertical["resolvedWorkflow"]["partOperations"] if item["processId"] == "slot-a")
        self.assertEqual(h_slot["placement"]["rotation"], 0)
        self.assertEqual(v_slot["placement"]["rotation"], 90)
        h_datum = next(item for item in horizontal["designParts"] if item["role"] == "datum")
        v_datum = next(item for item in vertical["designParts"] if item["role"] == "datum")
        self.assertEqual(h_datum["matrix"], v_datum["matrix"],
                         "槽孔方向属于工艺，不应移动成品构件")

    def _actual_participants(self, template_id, length=1000, width=120, depth=100):
        descriptor = self.runtime._template_by_id(template_id)
        anchors = descriptor["productBinding"].get("anchors", {})
        result = []
        for index, participant in enumerate(descriptor["participants"]):
            role = participant["role"]
            kind = anchors.get(role, "end")
            section = {"schema": "icax.tube-profile", "schemaVersion": 1,
                       "id": "rect", "kind": "rect", "width": width, "depth": depth,
                       "parameters": {"width": width, "depth": depth, "wallThickness": 3},
                       "contours": [{"isHole": False, "points": [[-width/2, -depth/2],
                           [width/2, -depth/2], [width/2, depth/2], [-width/2, depth/2]]}]}
            anchor = {"kind": "end", "end": "start", "rotation": 0, "trim": 0} if kind == "end" else {
                "kind": "side", "face": "top", "reference": "start", "station": length / 2,
                "offset": 0, "rotation": 0}
            if kind == "end" and any(
                    binding.get("relativePlacement")
                    and next((process for process in descriptor["partProcesses"]
                              if process["id"] == process_id), {}).get("participants") == [role]
                    for process_id, binding in descriptor["productBinding"].get("processes", {}).items()):
                anchor["approachFace"] = "top"
            if kind == "end" and template_id == "wrap-a-over-b":
                anchor["stockAllowance"] = width / 2
                anchor["contactInset"] = width / 2
            if kind == "end" and template_id == "two-end-end-angle":
                anchor["stockAllowance"] = 0
                anchor["contactInset"] = width / 2
            if kind == "end" and template_id == "two-end-middle" and role == "branch":
                anchor["stockAllowance"] = width / 2
            member = {"role": role, "memberEntityId": f"member-{index}", "itemKey": f"actual-{index}",
                      "section": section, "length": length, "anchor": anchor}
            if template_id == "two-end-end-angle":
                member["nodeFrame"] = {
                    "selfAwayPart": [1, 0, 0],
                    "otherAwayPart": [0, 0, 1 if index == 0 else -1],
                    "selfNodePart": [-length / 2, 0, 0],
                }
            result.append(member)
        return result

    def test_wrap_end_node_derives_only_the_host_side_tool_location(self):
        for end, face, expected_station in (("start", "bottom", 30),
                                            ("end", "right", 770)):
            with self.subTest(end=end, face=face):
                participants = self._actual_participants("wrap-a-over-b", length=800,
                                                         width=60, depth=40)
                participants[0]["anchor"].update({"end": end, "approachFace": face})
                original = copy.deepcopy(participants)
                result = self.runtime.resolve_bound_plan("wrap-a-over-b",
                    {"maleFemale": True, "pairCount": "two"}, participants=participants)
                self.assertEqual(participants, original)
                self.assertEqual(result["parts"][0]["features"][0]["face"], face)
                self.assertEqual(result["parts"][0]["features"][0]["station"], expected_station)
                operation = next(item for item in result["workflow"]["partOperations"]
                                 if item["processId"] == "long-side-slots-two")
                self.assertEqual(operation["anchor"], original[0]["anchor"])
                self.assertEqual(operation["placement"]["station"], expected_station)

        participants = self._actual_participants("wrap-a-over-b")
        del participants[0]["anchor"]["approachFace"]
        self.assertEqual(self.runtime.resolve_bound_plan("wrap-a-over-b",
            {"maleFemale": False}, participants=participants)["manufacturingEffect"], "incremental-cut")
        with self.assertRaisesRegex(ValueError, "朝向侧面"):
            self.runtime.resolve_bound_plan("wrap-a-over-b",
                {"maleFemale": True}, participants=participants)

    def test_wrap_relative_placement_requires_template_rule_and_end_identity(self):
        participants = self._actual_participants("wrap-a-over-b", length=800, width=60, depth=40)
        participants[0]["anchor"]["trim"] = 1
        with self.assertRaisesRegex(ValueError, "无余量"):
            self.runtime.resolve_bound_plan("wrap-a-over-b",
                {"maleFemale": True}, participants=participants)
        descriptor = self.runtime._template_by_id("wrap-a-over-b")
        descriptor["productBinding"]["processes"]["long-side-slots-two"].pop("relativePlacement")
        with self.assertRaisesRegex(ValueError, "缺少明确的端点至侧壁"):
            self.runtime._validate_template(descriptor, self.runtime.ROOT / "wrap-a-over-b")

    def test_wrap_plain_uses_declared_stock_allowance_for_a_real_branch_end_cut(self):
        participants = self._actual_participants("wrap-a-over-b", length=800, width=60, depth=40)
        original = copy.deepcopy(participants)
        for end, allowance, contact_inset in (("start", 30, 30), ("end", 24, 17)):
            with self.subTest(end=end):
                participants[1]["anchor"].update({"end": end,
                    "stockAllowance": allowance, "contactInset": contact_inset})
                result = self.runtime.resolve_bound_plan("wrap-a-over-b", {"maleFemale": False},
                                                         participants=participants)
                branch = next(part for part in result["parts"] if part["role"] == "memberB")
                self.assertEqual(branch["ends"][end]["trim"], allowance + contact_inset)
                self.assertEqual({item["processId"] for item in result["workflow"]["partOperations"]},
                                 {"short-butt-end"})
                self.assertEqual(result["manufacturingEffect"], "incremental-cut")
                self.assertEqual(result["parameters"], {
                    item["key"]: item["defaultValue"]
                    for item in self.runtime._template_by_id("wrap-a-over-b")["parameters"]
                })
        self.assertEqual(original[0], participants[0])
        participants[1]["anchor"].pop("stockAllowance")
        with self.assertRaisesRegex(ValueError, "库存余量"):
            self.runtime.resolve_bound_plan("wrap-a-over-b", {"maleFemale": False},
                                            participants=participants)
        participants[1]["anchor"]["stockAllowance"] = 0
        participants[1]["anchor"]["contactInset"] = 0
        with self.assertRaisesRegex(ValueError, "不足以形成真实退切"):
            self.runtime.resolve_bound_plan("wrap-a-over-b", {"maleFemale": False},
                                            participants=participants)
        participants[1]["anchor"].pop("contactInset")
        participants[1]["anchor"]["stockAllowance"] = 30
        with self.assertRaisesRegex(ValueError, "接头近侧距离"):
            self.runtime.resolve_bound_plan("wrap-a-over-b", {"maleFemale": False},
                                            participants=participants)

    def test_bound_supported_templates_return_only_incremental_tools_for_real_members(self):
        cases = [(name, {}) for name in ("two-end-end-angle", "cross-through", "four-end-end-end-end",
                 "saddle-weld", "through-bolt", "tab-slot-lock")]
        cases += [("mechanical-fastener", {"fastenerType": kind}) for kind in ("bolt", "pin", "rivet")]
        cases += [("two-end-middle", {"interfaceMode": mode}) for mode in ("singleInsert", "throughInsert", "saddle")]
        cases += [("weld-interface", {"weldType": mode}) for mode in ("plug", "slot")]
        cases += [(template_id, values)
                  for template_id in ("wrap-a-over-b",)
                  for values in ({}, {"maleFemale": True, "pairCount": "two"},
                                 {"maleFemale": True, "pairCount": "four"})]
        for template_id, values in cases:
            with self.subTest(template=template_id, values=values):
                participants = self._actual_participants(template_id)
                original_participants = copy.deepcopy(participants)
                result = self.runtime.resolve_bound_plan(template_id, values, participants=participants)
                self.assertEqual(participants, original_participants)
                self.assertTrue(result["supported"])
                self.assertEqual(result["manufacturingEffect"], "incremental-cut")
                self.assertEqual(result["assemblyFitStatus"], "not-verified")
                self.assertEqual(len(result["templateDigest"]), 64)
                self.assertTrue(result["parts"])
                for part in result["parts"]:
                    self.assertTrue(part["features"] or part["ends"])
                    self.assertTrue({"length", "profileRef", "request", "parameters"}.isdisjoint(part))
                    self.assertIn(part["memberEntityId"], {source["memberEntityId"] for source in participants})
                self.assertEqual(result["workflow"]["scope"], "product-members")
                self.assertEqual(result["workflow"]["validationStatus"], "host-validation-required")
                defaults = {item["key"]: item["defaultValue"] for item in self.runtime._template_by_id(template_id)["parameters"]}
                self.assertEqual(result["parameters"], {**defaults, **values})

    def test_wrap_real_members_receive_only_active_branch_and_keep_input_unchanged(self):
        for template_id, long_role, short_role in (
            ("wrap-a-over-b", "memberA", "memberB"),
        ):
            participants = self._actual_participants(template_id, length=800, width=60, depth=40)
            original = copy.deepcopy(participants)
            for values, expected_ids in (
                ({"maleFemale": False, "pairCount": "not-an-option", "tabWidth": -2}, {"end-miter"}),
                ({"maleFemale": True, "pairCount": "two"},
                 {"paired-side-slots", "paired-end-tabs"}),
                ({"maleFemale": True, "pairCount": "four"},
                 {"paired-side-slots", "paired-end-tabs"}),
            ):
                with self.subTest(template=template_id, values=values):
                    result = self.runtime.resolve_bound_plan(template_id, values,
                                                             participants=participants)
                    self.assertTrue(result["supported"])
                    self.assertEqual(result["assemblyFitStatus"], "not-verified")
                    self.assertEqual(participants, original)
                    self.assertEqual(result["manufacturingEffect"], "incremental-cut")
                    actual_ids = {feature["toolRef"]["id"] for part in result["parts"]
                                  for feature in part["features"]}
                    actual_ids.update(end["toolRef"]["id"] for part in result["parts"]
                                      for end in part["ends"].values())
                    self.assertEqual(actual_ids, expected_ids)
                    if values["maleFemale"]:
                        operations = result["workflow"]["partOperations"]
                        self.assertEqual({item["role"] for item in operations},
                                         {long_role, short_role})
                        self.assertEqual({item["toolParameters"]["pairCount"] for item in operations},
                                         {2 if values["pairCount"] == "two" else 4})
                        branch = next(part for part in result["parts"]
                                      if part["memberEntityId"] == participants[1]["memberEntityId"])
                        self.assertEqual(next(iter(branch["ends"].values()))["trim"],
                                         participants[1]["anchor"]["stockAllowance"] +
                                         participants[1]["anchor"]["contactInset"] -
                                         result["parameters"]["tabLength"])
                    else:
                        self.assertEqual({item["processId"] for item in result["workflow"]["partOperations"]},
                                         {"short-butt-end"})
                        short = next(part for part in result["parts"]
                                     if part["memberEntityId"] == participants[1]["memberEntityId"])
                        self.assertEqual(next(iter(short["ends"].values()))["trim"], 60)

        # A/B are template role slots: the product can map either physical tube
        # to the main/branch slot without a second assembly template.
        participants = self._actual_participants("wrap-a-over-b", length=800, width=60, depth=40)
        participants[0]["memberEntityId"] = "physical-tube-2"
        participants[0]["itemKey"] = "main"
        participants[1]["memberEntityId"] = "physical-tube-1"
        participants[1]["itemKey"] = "branch"
        result = self.runtime.resolve_bound_plan("wrap-a-over-b",
            {"maleFemale": True, "pairCount": "two"}, participants=participants)
        self.assertEqual({part["memberEntityId"] for part in result["parts"] if part["features"]},
                         {"physical-tube-2"})
        self.assertEqual({part["memberEntityId"] for part in result["parts"] if part["ends"]},
                         {"physical-tube-1"})

    def test_bound_plan_never_uses_example_lengths_profiles_poses_or_placements(self):
        template_id = "two-end-middle"
        participants = self._actual_participants(template_id, length=730, width=93, depth=57)
        values = {"interfaceMode": "throughInsert"}
        expected = self.runtime.resolve_bound_plan(template_id, values, participants=participants)
        descriptor = self.runtime._template_by_id(template_id)
        descriptor["previewScene"] = {"unusable": True}
        for process in descriptor["partProcesses"]:
            process["previewPlacement"] = {"unusable": True}
            process["previewPlacementVariants"] = [{"value": {"station": -100000}}]
        with patch.object(self.runtime, "_template_by_id", return_value=descriptor), \
                patch.object(self.runtime, "_profile_request", side_effect=AssertionError("example profile")), \
                patch.object(self.runtime, "_pose_matrix", side_effect=AssertionError("example pose")):
            actual = self.runtime.resolve_bound_plan(template_id, values, participants=participants)
        self.assertEqual(actual, expected)
        operation = actual["parts"][0]["features"][0]
        self.assertEqual(operation["length"], 730)
        self.assertEqual(operation["section"]["profile"], participants[1]["section"])
        self.assertEqual(operation["station"], 365)

    def test_bound_branch_profile_orientation_follows_host_face(self):
        expected_azimuth = {
            "end": {"top": 180, "bottom": 0, "left": 90, "right": -90},
            "start": {"top": 0, "bottom": 180, "left": -90, "right": 90},
        }
        for mode in ("singleInsert", "throughInsert"):
            for end, faces in expected_azimuth.items():
                for face, azimuth in faces.items():
                    with self.subTest(mode=mode, face=face, end=end):
                        participants = self._actual_participants("two-end-middle")
                        participants[0]["anchor"]["face"] = face
                        participants[1]["anchor"]["end"] = end
                        # The product end may differ from a reversed manufacturing
                        # end; tool direction follows the committed product frame.
                        participants[1]["productAnchor"] = {"kind": "end", "end": end}
                        result = self.runtime.resolve_bound_plan("two-end-middle",
                            {"interfaceMode": mode}, participants=participants)
                        self.assertTrue(result["supported"])
                        feature = result["parts"][0]["features"][0]
                        self.assertEqual(feature["azimuth"], azimuth)
                        self.assertEqual(feature["face"], face)
                        self.assertEqual(feature["operationParameterValues"]["azimuth"], azimuth)
                        self.assertEqual(feature["operationParameterValues"]["direction"],
                                          ("positive" if end == "start" else "negative")
                                          if mode == "singleInsert" else "through")
        reversed_participants = self._actual_participants("two-end-middle")
        reversed_participants[0]["anchor"]["face"] = "top"
        reversed_participants[1]["anchor"]["end"] = "end"
        reversed_participants[1]["productAnchor"] = {"kind": "end", "end": "start"}
        reversed_plan = self.runtime.resolve_bound_plan("two-end-middle",
            {"interfaceMode": "singleInsert"}, participants=reversed_participants)
        reversed_opening = reversed_plan["parts"][0]["features"][0]
        self.assertEqual(reversed_opening["azimuth"], 0)
        self.assertEqual(reversed_opening["direction"], "positive")
        descriptor = copy.deepcopy(self.runtime._template_by_id("two-end-middle"))
        descriptor["productBinding"]["processes"]["single-opening"]["parameterSources"]["azimuth"]["map"]["end"]["bottom"] = 5
        with patch.object(self.runtime, "_template_by_id", return_value=descriptor):
            changed = self.runtime.resolve_bound_plan("two-end-middle",
                {"interfaceMode": "singleInsert"}, participants=self._actual_participants("two-end-middle"))
        unchanged = self.runtime.resolve_bound_plan("two-end-middle",
            {"interfaceMode": "singleInsert"}, participants=self._actual_participants("two-end-middle"))
        self.assertNotEqual(changed["templateDigest"], unchanged["templateDigest"])
        descriptor["productBinding"]["processes"]["single-opening"]["parameterSources"]["azimuth"]["map"]["end"]["bottom"] = 0
        descriptor["productBinding"]["processes"]["single-opening"]["parameterSources"]["azimuth"]["map"]["end"].pop("right")
        with self.assertRaisesRegex(ValueError, "端向壁面映射"):
            self.runtime._validate_template(descriptor, self.runtime.ROOT / "two-end-middle")

    def test_supplied_section_single_opening_uses_verified_host_face_normal(self):
        # The normalized part axes can reverse between otherwise identical T
        # connections. Their committed contact normal selects the single-wall
        # cutter direction; the product-end fallback is only for old profiles.
        for product_end, normal in (("start", [0, 0, 1]),
                                    ("end", [0, 0, -1])):
            with self.subTest(product_end=product_end):
                participants = self._actual_participants("two-end-middle")
                participants[0]["anchor"]["face"] = "bottom"
                participants[1]["productAnchor"] = {"kind": "end", "end": product_end}
                branch_section = participants[1]["section"]
                branch_section["geometrySource"] = "providedBoundary"
                branch_section["sectionFrame"] = {"hostFaceNormalToolPart": normal}
                result = self.runtime.resolve_bound_plan("two-end-middle",
                    {"interfaceMode": "singleInsert"}, participants=participants)
                opening = result["parts"][0]["features"][0]
                self.assertEqual(opening["direction"], "negative")
                self.assertEqual(opening["operationParameterValues"]["direction"], "negative")
                branch_section["sectionFrame"] = {}
                with self.assertRaisesRegex(ValueError, "已核对的主管入口面方向"):
                    self.runtime.resolve_bound_plan("two-end-middle",
                        {"interfaceMode": "singleInsert"}, participants=participants)
                branch_section["sectionFrame"] = {"hostFaceNormalToolPart": [1, 0, 0]}
                with self.assertRaisesRegex(ValueError, "没有穿过主管实际入口面"):
                    self.runtime.resolve_bound_plan("two-end-middle",
                        {"interfaceMode": "singleInsert"}, participants=participants)

    def test_bound_plan_rejects_ambiguous_roles_and_missing_real_geometry(self):
        participants = self._actual_participants("through-bolt")
        for mutate in (
            lambda parts: parts.pop(),
            lambda parts: parts[1].update(role=parts[0]["role"]),
            lambda parts: parts[1].update(memberEntityId=parts[0]["memberEntityId"]),
            lambda parts: parts[1].update(itemKey=parts[0]["itemKey"]),
            lambda parts: parts[0].pop("section"),
            lambda parts: parts[0]["section"].update(contours=[]),
            lambda parts: parts[0].update(length=float("inf")),
            lambda parts: parts[0]["anchor"].update(station=float("nan")),
            lambda parts: parts[0]["anchor"].update(offset=float("inf")),
            lambda parts: parts[0]["anchor"].update(rotation=float("nan")),
            lambda parts: parts[0]["anchor"].update(reference="center"),
            lambda parts: parts[0].update(anchor={"kind": "end", "end": "start"}),
        ):
            changed = copy.deepcopy(participants)
            mutate(changed)
            with self.assertRaises(ValueError):
                self.runtime.resolve_bound_plan("through-bolt", participants=changed)

    def test_bound_side_checks_complete_tool_envelope_and_drafts_cannot_move_anchor(self):
        participants = self._actual_participants("mechanical-fastener", length=200, width=40)
        values = {"count": 3, "pitch": 80}
        result = self.runtime.resolve_bound_plan("mechanical-fastener", values, participants=participants)
        self.assertEqual(result["parts"][0]["features"][0]["station"], 20)
        for field, value in (("station", 80), ("offset", 15)):
            changed = copy.deepcopy(participants)
            changed[0]["anchor"][field] = value
            with self.assertRaisesRegex(ValueError, "完整加工包络"):
                self.runtime.resolve_bound_plan("mechanical-fastener", values, participants=changed)
        with self.assertRaisesRegex(ValueError, "草稿只能修改刀具形状"):
            self.runtime.resolve_bound_plan("mechanical-fastener", values,
                {"round-hole-a": {"circle": {"station": 0}}}, participants)
        end_parts = self._actual_participants("two-end-end-angle")
        with self.assertRaisesRegex(ValueError, "草稿只能修改刀具形状"):
            self.runtime.resolve_bound_plan("two-end-end-angle", {},
                {"miter-a": {"end-miter": {"trim": 400}}}, end_parts)

    def test_bound_unsupported_realization_or_incomplete_branches_never_emit_operations(self):
        cases = [(name, {}) for name in ("bend", "insert-sleeve", "slot-bolt-adjustable")]
        cases += [("mechanical-fastener", {"fastenerType": kind}) for kind in
                  ("adjustableBolt", "rivetNut", "selfTapping", "tapped")]
        cases += [("tab-slot-lock", {"lockMethod": "bolt"}), ("weld-interface", {"weldType": "lap"})]
        for template_id, values in cases:
            with self.subTest(template=template_id, values=values):
                result = self.runtime.resolve_bound_plan(template_id, values)
                self.assertFalse(result["supported"])
                self.assertEqual(result["supportStatus"], "unsupported")
                self.assertEqual(result["parts"], [])
                self.assertEqual(result["manufacturingEffect"], "none")
                self.assertTrue(result["summary"])

    def test_bound_end_anchor_is_explicit_and_rotated_side_envelope_is_checked(self):
        participants = self._actual_participants("two-end-end-angle")
        participants[0]["anchor"].update(end="start", rotation=30, trim=7)
        participants[1]["anchor"].update(end="end", rotation=-20, trim=3)
        participants[1]["nodeFrame"]["selfAwayPart"] = [-1, 0, 0]
        participants[1]["nodeFrame"]["selfNodePart"][0] = participants[1]["length"] / 2
        result = self.runtime.resolve_bound_plan("two-end-end-angle", participants=participants)
        self.assertEqual(set(result["parts"][0]["ends"]), {"start"})
        self.assertEqual(set(result["parts"][1]["ends"]), {"end"})
        self.assertEqual(result["parts"][0]["ends"]["start"]["rotation"], 120)
        self.assertEqual(result["parts"][0]["ends"]["start"]["trim"], 7)
        self.assertEqual(result["workflow"]["partOperations"][0]["placement"]["rotation"], 120)
        slot_parts = self._actual_participants("weld-interface", width=30)
        values = {"weldType": "slot", "weldCount": 1, "slotLength": 80, "slotWidth": 12}
        self.assertTrue(self.runtime.resolve_bound_plan("weld-interface", values, participants=slot_parts)["supported"])
        slot_parts[1]["anchor"]["rotation"] = 90
        with self.assertRaisesRegex(ValueError, "完整加工包络"):
            self.runtime.resolve_bound_plan("weld-interface", values, participants=slot_parts)

    def test_bound_role_count_and_generate_dispatch_are_strict(self):
        participants = self._actual_participants("through-bolt")
        result = self.runtime.generate({"action": "resolve-bound-plan", "templateId": "through-bolt",
                                       "parameters": {"boltCount": 2.0}, "participants": participants}, {})
        self.assertEqual(result["parameters"]["boltCount"], 2.0)
        self.assertIsInstance(result["parts"][0]["features"][0]["arrayCount"], int)
        descriptor = self.runtime._template_by_id("through-bolt")
        descriptor["participants"][0]["count"] = 2
        with patch.object(self.runtime, "_template_by_id", return_value=descriptor), self.assertRaisesRegex(ValueError, "一个真实构件"):
            self.runtime.resolve_bound_plan("through-bolt", participants=participants)


if __name__ == "__main__":
    unittest.main()
