"""Pure-Python declaration/placement regressions; no OCC or application process."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
TEMPLATES = ROOT / "src/apps/tube-designer/templates"
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_sdk import NeutralModel
from icax_template_worker import _load_template

_SPEC = importlib.util.spec_from_file_location(
    "shared_tube_geometry_test_subject", TEMPLATES / "_shared/shared_tube_geometry.py")
assert _SPEC is not None and _SPEC.loader is not None
_MODULE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MODULE)
SharedTubeGeometry = _MODULE.SharedTubeGeometry


FIVE_FACE_WITH_OPENING = {
    "height": 1800.0, "frontWidth": 1200.0, "depth": 600.0,
    "horizontalCount": 4, "maximumVerticalClearGap": 110.0,
    "accessDoorEnabled": True, "doorUse": "escape",
    "doorClearWidth": 800.0, "doorClearHeight": 1000.0,
    "doorUOffset": 150.0, "doorVOffset": 350.0, "doorHingeCount": 2,
}


def make_model():
    return NeutralModel(template_id="test.shared-tubes", template_version="1.0.0",
                        package_digest="test", parameters={})


def profile_arguments(origin=(0, 0, 0), x_axis=(1, 0, 0), y_axis=(0, 1, 0)):
    # Deliberately non-symmetric, with an inner contour that must not disappear.
    return {
        "placement": {"origin": list(origin), "xAxis": list(x_axis), "yAxis": list(y_axis)},
        "contours": [
            {"kind": "polygon", "points": [[0, 0], [40, 0], [35, 20], [0, 8]]},
            {"kind": "circle", "center": [8, 4], "radius": 1.5},
        ],
    }


def nodes(model):
    document = model.build() if isinstance(model, NeutralModel) else model
    return {node["key"]: node for node in document["geometry"]}


def transformed_vector(placement, vector):
    return [sum(placement[axis][component] * value
                for axis, value in zip(("xAxis", "yAxis", "zAxis"), vector))
            for component in range(3)]


def purpose_closure(document, purpose):
    graph = nodes(document)
    items = {item["key"]: item for item in document["items"]}
    roots = [items[key]["representations"][purpose]
             for output in document["outputs"] if output["purpose"] == purpose
             for key in output["items"]]
    visited = set()

    def visit(key):
        if key in visited:
            return
        visited.add(key)
        node = graph[key]
        dependencies = list(node.get("inputs", []))
        if node["operator"] == "boolean":
            arguments = node.get("arguments", {})
            if "target" in arguments:
                dependencies.append(arguments["target"])
            dependencies.extend(arguments.get("tools", []))
        for dependency in dependencies:
            visit(dependency)

    for root in roots:
        visit(root)
    return visited


def metadata_without_display(document):
    result = deepcopy({key: value for key, value in document.items() if key != "geometry"})
    for item in result["items"]:
        item["representations"].pop("display", None)
    return result


class SharedTubeGeometryDeclarationTests(unittest.TestCase):
    def assert_vector_equal(self, actual, expected):
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=9)

    def test_same_geometry_at_different_positions_and_rotations_shares_prototype(self):
        model = make_model()
        shared = SharedTubeGeometry(model)
        shared.emit_tube("first", profile_arguments=profile_arguments(),
                         extrude_arguments={"vector": [0, 0, 120]})
        rotated = profile_arguments((100, 20, 30), (0, 1, 0), (0, 0, 1))
        shared.emit_tube("second", profile_arguments=rotated,
                         extrude_arguments={"vector": [120, 0, 0]})
        graph = nodes(model)
        self.assertEqual(4, len(graph))
        self.assertEqual(graph["first.solid"]["inputs"], graph["second.solid"]["inputs"])
        placement = graph["second.solid"]["arguments"]["placement"]
        self.assertEqual([100, 20, 30], placement["origin"])
        extrusion = graph[graph["second.solid"]["inputs"][0]]
        self.assert_vector_equal(transformed_vector(placement, extrusion["arguments"]["vector"]), [120, 0, 0])
        self.assertEqual(profile_arguments()["contours"], graph[extrusion["inputs"][0]]["arguments"]["contours"])

    def test_same_section_different_lengths_only_shares_profile(self):
        model = make_model()
        shared = SharedTubeGeometry(model)
        for index, length in enumerate((120, 121)):
            shared.emit_tube(f"part{index}", profile_arguments=profile_arguments(),
                             extrude_arguments={"vector": [0, 0, length]})
        graph = nodes(model)
        self.assertEqual(Counter(profile2d=1, extrude=2, transform=2),
                         Counter(node["operator"] for node in graph.values()))
        self.assertNotEqual(graph["part0.solid"]["inputs"], graph["part1.solid"]["inputs"])

    def test_complete_contour_and_extrusion_arguments_participate_in_keys(self):
        variants = []
        for change in ("inner-radius", "outer-point", "extra-contour", "custom-data", "tiny-change"):
            arguments = profile_arguments()
            if change == "inner-radius":
                arguments["contours"][1]["radius"] = 1.6
            elif change == "outer-point":
                arguments["contours"][0]["points"][1][0] = 41
            elif change == "extra-contour":
                arguments["contours"].append({"kind": "circle", "center": [20, 4], "radius": 1})
            elif change == "custom-data":
                arguments["contours"][0]["customGeometry"] = {"weights": [1, 0.7, 1]}
            else:
                arguments["contours"][1]["radius"] += 1.0e-10
            variants.append(arguments)
        model = make_model()
        shared = SharedTubeGeometry(model)
        for index, arguments in enumerate([profile_arguments(), *variants]):
            shared.emit_tube(f"profile{index}", profile_arguments=arguments,
                             extrude_arguments={"vector": [0, 0, 120]})
        for index, extra in enumerate(({"extendStart": 1}, {"extendEnd": 1}, {"custom": {"value": 1}})):
            shared.emit_tube(f"extrude{index}", profile_arguments=profile_arguments(),
                             extrude_arguments={"vector": [0, 0, 120], **extra})
        counts = Counter(node["operator"] for node in nodes(model).values())
        self.assertEqual(6, counts["profile2d"])
        self.assertEqual(9, counts["extrude"])

    def test_negative_and_oblique_extrusion_preserve_signed_vector_without_mirroring(self):
        model = make_model()
        shared = SharedTubeGeometry(model)
        for name, vector in (("positive", [0, 0, 120]), ("negative", [0, 0, -120]),
                             ("oblique", [7, 3, -120])):
            shared.emit_tube(name, profile_arguments=profile_arguments(),
                             extrude_arguments={"vector": vector})
        graph = nodes(model)
        self.assertEqual(1, sum(node["operator"] == "profile2d" for node in graph.values()))
        self.assertEqual(3, sum(node["operator"] == "extrude" for node in graph.values()))
        for name, expected in (("positive", [0, 0, 120]), ("negative", [0, 0, -120]), ("oblique", [7, 3, -120])):
            instance = graph[f"{name}.solid"]
            self.assertEqual([0, 0, 1], instance["arguments"]["placement"]["zAxis"])
            self.assert_vector_equal(graph[instance["inputs"][0]]["arguments"]["vector"], expected)

    def test_arbitrary_rotation_is_right_handed_and_preserves_full_world_vector(self):
        s = math.sqrt(0.5)
        arguments = profile_arguments((12, -7, 30), (s, s, 0), (0, 0, 1))
        model = make_model()
        SharedTubeGeometry(model).emit_tube("angled", profile_arguments=arguments,
                                           extrude_arguments={"vector": [31, -37, 2]})
        graph = nodes(model)
        instance = graph["angled.solid"]
        placement = instance["arguments"]["placement"]
        self.assert_vector_equal(placement["zAxis"], [s, -s, 0])
        extrusion = graph[instance["inputs"][0]]
        self.assert_vector_equal(transformed_vector(placement, extrusion["arguments"]["vector"]), [31, -37, 2])

    def test_cache_is_per_build_and_input_documents_are_not_mutated(self):
        profile = profile_arguments()
        extrusion = {"vector": [0, 0, 120]}
        before = deepcopy((profile, extrusion))
        first, second = make_model(), make_model()
        for model in (first, second):
            SharedTubeGeometry(model).emit_tube("part", profile_arguments=profile,
                                               extrude_arguments=extrusion)
        self.assertEqual(before, (profile, extrusion))
        self.assertEqual(nodes(first), nodes(second))
        self.assertEqual(3, len(nodes(second)))

    def test_invalid_placement_and_nonfinite_declarations_are_rejected(self):
        variants = []
        for name in ("zero", "skew", "nan", "boolean", "bad-contour"):
            arguments = profile_arguments()
            if name == "zero":
                arguments["placement"]["xAxis"] = [0, 0, 0]
            elif name == "skew":
                arguments["placement"]["yAxis"] = [1, 1, 0]
            elif name == "nan":
                arguments["placement"]["origin"][0] = math.nan
            elif name == "boolean":
                arguments["placement"]["xAxis"][0] = True
            else:
                arguments["contours"][1]["radius"] = math.inf
            variants.append(arguments)
        for arguments in variants:
            with self.subTest(arguments=arguments):
                model = make_model()
                with self.assertRaises(ValueError):
                    SharedTubeGeometry(model).emit_tube("part", profile_arguments=arguments,
                                                       extrude_arguments={"vector": [0, 0, 120]})
                self.assertEqual({}, nodes(model))


class SharedTubeGeometryTemplateTests(unittest.TestCase):
    def assert_legacy_equivalent(self, template_name, overrides=None):
        directory = TEMPLATES / template_name
        descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
        parameters = {item["key"]: item["defaultValue"] for item in descriptor["parameters"]}
        parameters.update(overrides or {})
        module = _load_template(str(directory / "template.py"), "shared-tube-regression")
        context = {"template": {"id": descriptor["id"], "version": descriptor["version"], "packageDigest": "test"}}
        current = module.generate(deepcopy(parameters), context)
        owner = module if template_name == "single_face_security_window" else module._shared_module
        method = "_emit_tube_geometry" if template_name == "single_face_security_window" else "_emit_tube"

        def legacy_emitter(model, part, shared_geometry):
            arguments = (owner._profile_arguments(part.start, part.end, part.profile)
                         if method == "_emit_tube_geometry" else owner._profile_arguments(part))
            profile = model.geometry(f"{part.key}.profile", "profile2d", arguments=arguments)
            vector = [part.end[index] - part.start[index] for index in range(3)]
            solid = model.geometry(f"{part.key}.solid", "extrude", inputs=[profile], arguments={"vector": vector})
            return (solid, solid) if method == "_emit_tube_geometry" else solid

        with patch.object(owner, method, legacy_emitter):
            legacy = module.generate(deepcopy(parameters), context)
        # Before display/manufacturing separation, straight parts displayed their
        # processed export shape. Continuous frames already had two different roots.
        for item in legacy["items"]:
            if not item["representations"]["display"].endswith(".display.compound"):
                item["representations"]["display"] = item["representations"]["export"]
        self.assertEqual(metadata_without_display(legacy), metadata_without_display(current))
        graph, old_graph = nodes(current), nodes(legacy)
        replaced_profiles = set()
        for key, instance in graph.items():
            if instance["operator"] != "transform":
                continue
            original_solid = old_graph[key]
            original_profile = old_graph[original_solid["inputs"][0]]
            replaced_profiles.add(original_profile["key"])
            local_solid = graph[instance["inputs"][0]]
            local_profile = graph[local_solid["inputs"][0]]
            placement = instance["arguments"]["placement"]
            self.assertEqual(original_profile["arguments"]["contours"], local_profile["arguments"]["contours"])
            for component in range(3):
                for field in ("origin", "xAxis", "yAxis"):
                    self.assertAlmostEqual(original_profile["arguments"]["placement"][field][component],
                                           placement[field][component], places=9, msg=f"{template_name}: {key} {field}")
                self.assertAlmostEqual(original_solid["arguments"]["vector"][component],
                                       transformed_vector(placement, local_solid["arguments"]["vector"])[component],
                                       places=9, msg=f"{template_name}: {key} vector")
            for field, value in original_profile["arguments"].items():
                if field != "placement":
                    self.assertEqual(value, local_profile["arguments"][field])
            self.assertEqual([0, 0, 0], local_profile["arguments"]["placement"]["origin"])
        for key, original in old_graph.items():
            if key in replaced_profiles or graph.get(key, {}).get("operator") == "transform":
                continue
            # In particular, every cutter/boolean/groove keeps its exact original node.
            self.assertEqual(original, graph[key], f"{template_name}: {key} changed unexpectedly")
        self.assertGreater(len(replaced_profiles), 0)
        self.assertLess(sum(node["operator"] == "extrude" for node in graph.values()),
                        sum(node["operator"] == "extrude" for node in old_graph.values()))
        display = purpose_closure(current, "display")
        export = purpose_closure(current, "export")
        self.assertFalse(any(graph[key]["operator"] == "boolean" for key in display))
        self.assertFalse(any(any(marker in key for marker in (".through.", ".miter.", ".export."))
                             for key in display))
        processed = {key for key, node in graph.items() if node["operator"] == "boolean"}
        self.assertTrue(processed)
        self.assertTrue(processed.issubset(export))
        self.assertEqual({key for key, node in old_graph.items() if node["operator"] == "boolean"}, processed)
        self.assertEqual({key for key in purpose_closure(legacy, "export") if ".through." in key or ".miter." in key},
                         {key for key in export if ".through." in key or ".miter." in key})
        counts = Counter(node["operator"] for node in graph.values())
        print(f"[shared-tubes] {template_name}: items={len(current['items'])}, "
              f"profiles={counts['profile2d']}, extrusions={counts['extrude']}, instances={counts['transform']}, "
              f"booleans={counts['boolean']}, displayNodes={len(display)}, exportNodes={len(export)}")
        return current

    def test_single_face_without_opening_preserves_parts_and_all_cutters(self):
        model = self.assert_legacy_equivalent("single_face_security_window", {"accessDoorEnabled": False})
        self.assertEqual(17, len(model["items"]))

    def test_continuous_frame_and_miter_variants_preserve_display_and_export(self):
        for join in ("v_groove_90:sharp_v", "miter_45"):
            with self.subTest(join=join):
                self.assert_legacy_equivalent("single_face_security_window", {
                    "width": 1400.0, "height": 1800.0, "frameLayout": "four_sides", "frameJoinType": join,
                })

    def test_all_multi_face_layouts_preserve_positions_and_independent_holes(self):
        for name in ("two_face_security_window", "three_face_security_window", "five_face_security_window"):
            with self.subTest(template=name):
                self.assert_legacy_equivalent(name)

    def test_five_face_with_clear_opening_preserves_all_parts(self):
        model = self.assert_legacy_equivalent("five_face_security_window", FIVE_FACE_WITH_OPENING)
        # The clear opening includes the hinge/leaf allowance and its own
        # automatic infill spacing, in addition to the clipped main grid.
        self.assertEqual(89, len(model["items"]))


class RequestSpecificGeometryTests(unittest.TestCase):
    def template_input(self, name, overrides=None):
        directory = TEMPLATES / name
        descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
        parameters = {item["key"]: item["defaultValue"] for item in descriptor["parameters"]}
        parameters.update(overrides or {})
        module = _load_template(str(directory / "template.py"), "request-specific-regression")
        context = {"template": {"id": descriptor["id"], "version": descriptor["version"], "packageDigest": "test"}}
        return module, parameters, context

    def assert_single_result(self, result, legacy, purpose):
        self.assertEqual(1, len(result["outputs"]))
        output = result["outputs"][0]
        self.assertEqual("result", output["key"])
        self.assertEqual("result", output["purpose"])
        self.assertEqual(purpose, result["extensions"]["tubeDesigner.geometryPurpose"])
        manufacturing = next(output for output in legacy["outputs"] if output["purpose"] == "export")
        self.assertEqual(len(manufacturing["items"]), result["extensions"]["tubeDesigner.manufacturingPartCount"])
        source = "display" if purpose == "display" else "export"
        original_items = {item["key"]: item for item in legacy["items"]}
        for item in result["items"]:
            self.assertEqual({"result": original_items[item["key"]]["representations"][source]},
                             item["representations"])
            self.assertEqual({key: value for key, value in original_items[item["key"]].items() if key != "representations"},
                             {key: value for key, value in item.items() if key != "representations"})
        self.assertEqual([item["key"] for item in result["items"]], output["items"])
        expected_keys = purpose_closure(legacy, source)
        old_graph = nodes(legacy)
        self.assertEqual({key: old_graph[key] for key in expected_keys}, nodes(result))
        self.assertEqual(set(nodes(result)), purpose_closure(result, "result"))
        self.assertEqual(legacy["parameters"], result["parameters"])
        self.assertEqual(legacy["tables"], result["tables"])
        self.assertEqual(legacy["relationships"], result["relationships"])
        if purpose == "display":
            self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
            self.assertFalse(any(any(marker in node["key"] for marker in (".through.", ".miter.", ".export."))
                                 for node in result["geometry"]))

    def test_every_builtin_supports_single_result_and_legacy_compatibility(self):
        names = [path.parent.name for path in sorted(TEMPLATES.glob("*/template.json"))]
        required = {
            "single_face_security_window", "two_face_security_window",
            "three_face_security_window", "five_face_security_window",
            "straight_stair_railing", "straight_steel_staircase",
            "l_turn_steel_staircase", "u_turn_steel_staircase",
        }
        self.assertTrue(required.issubset(names), f"Missing built-in packages: {required - set(names)}")
        for name in names:
            with self.subTest(template=name):
                module, parameters, context = self.template_input(name)
                legacy = module.generate(deepcopy(parameters), context)
                self.assertEqual(["display", "export"], [output["purpose"] for output in legacy["outputs"]])
                for purpose in ("display", "manufacturing"):
                    result = module.generate(deepcopy(parameters), {**context, "geometryPurpose": purpose})
                    self.assert_single_result(result, legacy, purpose)

    def test_display_never_constructs_holes_booleans_or_unfolded_v_grooves(self):
        cases = [
            ("single_face_security_window", {}),
            ("single_face_security_window", {"width": 1400.0, "frameLayout": "four_sides", "frameJoinType": "v_groove_90:sharp_v"}),
            ("single_face_security_window", {"frameLayout": "four_sides", "frameJoinType": "miter_45"}),
            ("two_face_security_window", {}),
            ("three_face_security_window", {}),
            ("three_face_security_window", {"frameCornerJoin": "rail_miter"}),
            ("five_face_security_window", FIVE_FACE_WITH_OPENING),
        ]
        original_geometry = NeutralModel.geometry

        def checked_geometry(model, key, operator, **arguments):
            self.assertNotEqual("boolean", operator, f"display tried to construct {key}")
            self.assertFalse(any(marker in key for marker in (".through.", ".miter.", ".export.")), key)
            return original_geometry(model, key, operator, **arguments)

        for name, overrides in cases:
            with self.subTest(template=name, overrides=overrides):
                module, parameters, context = self.template_input(name, overrides)
                legacy = module.generate(deepcopy(parameters), context)
                with patch.object(NeutralModel, "geometry", checked_geometry):
                    display = module.generate(deepcopy(parameters), {**context, "geometryPurpose": "display"})
                self.assert_single_result(display, legacy, "display")
                manufacture = module.generate(deepcopy(parameters), {**context, "geometryPurpose": "manufacturing"})
                self.assert_single_result(manufacture, legacy, "manufacturing")
                self.assertTrue(any(node["operator"] == "boolean" for node in manufacture["geometry"]))
                if name == "five_face_security_window":
                    self.assertEqual(89, len(display["items"]))
                    self.assertEqual(113, len(display["geometry"]))
                    # Butt-ended leaf horizontals no longer drill the left/right
                    # leaf uprights. Vertical insert holes remain independent.
                    self.assertEqual(32, sum(node["operator"] == "boolean" for node in manufacture["geometry"]))

    def test_manufacturing_continuous_frame_never_builds_display_assembly(self):
        module, parameters, context = self.template_input("single_face_security_window", {
            "width": 1400.0, "height": 1800.0, "frameLayout": "four_sides",
            "frameJoinType": "v_groove_90:sharp_v",
        })
        legacy = module.generate(deepcopy(parameters), context)
        original_geometry = NeutralModel.geometry

        def checked_geometry(model, key, operator, **arguments):
            self.assertNotIn(".display.", key)
            return original_geometry(model, key, operator, **arguments)

        with patch.object(NeutralModel, "geometry", checked_geometry):
            result = module.generate(parameters, {**context, "geometryPurpose": "manufacturing"})
        self.assert_single_result(result, legacy, "manufacturing")
        self.assertTrue(any(".export.groove." in node["key"] for node in result["geometry"]))

    def test_unknown_explicit_purpose_is_rejected_instead_of_returning_legacy_model(self):
        for path in sorted(TEMPLATES.glob("*/template.json")):
            module, parameters, context = self.template_input(path.parent.name)
            for purpose in (None, "export", "", "unknown"):
                with self.subTest(template=path.parent.name, purpose=purpose):
                    with self.assertRaises(ValueError):
                        module.generate(parameters, {**context, "geometryPurpose": purpose})

    def test_request_adapter_counts_manufacturing_items_not_piece_quantities(self):
        model = make_model()
        root = SharedTubeGeometry(model).emit_tube("part", profile_arguments=profile_arguments(),
                                                   extrude_arguments={"vector": [0, 0, 120]})
        model.item("part", "part", representations={"display": root, "export": root}, properties={"quantity": 7})
        model.output("display.default", "display", ["part"])
        model.output("export.manufacturing", "export", ["part"])
        result = _MODULE.finish_geometry_request(model, {"geometryPurpose": "display"})
        self.assertEqual(1, result["extensions"]["tubeDesigner.manufacturingPartCount"])
        self.assertEqual(7, result["items"][0]["properties"]["quantity"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
