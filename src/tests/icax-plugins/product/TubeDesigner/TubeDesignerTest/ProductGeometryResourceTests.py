"""Public resource declarations, shared definitions and independent item placement."""
from collections import Counter
import contextlib
from copy import deepcopy
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from WindowCatalogueTests import SRC, package
from icax_template_sdk import (NeutralModel, to_resource_model, expand_resource_model,
                               to_display_model, expand_display_model,
                               to_manufacturing_model, expand_manufacturing_model, display_context)
import icax_template_worker as worker
from icax_template_sdk.manufacturing import manufacturing_context, _execution_scope, compose_manufacturing_model, validate_manufacturing_model
from ProductManufacturingDeclarationTests import (FIELDS as MANUFACTURING_FIELDS, BASELINE_FILE,
    assert_declaration, forbid_processing, execute_definition, canonical_geometry)
from SharedTubeGeometryTests import SharedTubeGeometry, profile_arguments
from ManufacturingExecutorBoundaryTests import load_executor


def instance_document():
    model = NeutralModel(template_id="test.product-resource", template_version="1.0.0",
                         package_digest="test", parameters={"length": 120})
    shared = SharedTubeGeometry(model)
    for key, profile, vector in (
            ("part.a", profile_arguments(), [0, 0, 120]),
            ("part.b", profile_arguments((100, 20, 30), (0, 1, 0), (0, 0, 1)), [120, 0, 0])):
        root = shared.emit_tube(key, profile_arguments=profile,
                                extrude_arguments={"vector": vector})
        model.item(key, key, representations={"display": root},
                   properties={"partNumber": key})
    model.output("display.default", "display", ["part.a", "part.b"])
    return model.build()


def nested_alias_document():
    document = instance_document()
    first_root = document["items"][0]["representations"]["display"]
    second_root = document["items"][1]["representations"]["display"]
    document["geometry"] = [node for node in document["geometry"] if node["key"] != second_root]
    inner = next(node for node in document["geometry"] if node["key"] == first_root)
    inner["key"] = "inner"
    base = inner["inputs"][0]
    document["geometry"].append({"key": "outer", "operator": "transform", "inputs": ["inner"],
        "arguments": {"placement": {"origin": [10, 20, 30], "xAxis": [0, 1, 0],
            "yAxis": [-1, 0, 0], "zAxis": [0, 0, 1]}}})
    document["items"][0]["representations"]["display"] = "outer"
    document["items"][1]["representations"]["display"] = "inner"
    return document, base


DISPLAY_FIELDS = {"schema", "schemaVersion", "coordinateSystem", "lengthUnit", "resources",
                  "items", "roots", "annotations"}


def assert_display_model(test, document):
    test.assertEqual(document["schema"], "icax.display-model")
    test.assertEqual(document["schemaVersion"], 2)
    test.assertEqual(set(document), DISPLAY_FIELDS)
    resources = {node["key"]: node for node in document["resources"]}
    test.assertEqual(len(resources), len(document["resources"]))
    test.assertTrue(resources)
    test.assertTrue(document["items"])
    for item in document["items"]:
        test.assertTrue({"key", "displayName", "children", "properties"} <= set(item))
        test.assertFalse(set(item) - {"key", "displayName", "geometry", "children", "properties"})
        if "geometry" in item:
            test.assertIn(item["geometry"]["resource"], resources)
        test.assertIsInstance(item["children"], list)
    def properties_are_design_only(value):
        if isinstance(value, dict):
            for key, child in value.items():
                test.assertFalse(key.startswith("manufacturing."), key)
                test.assertFalse(key.startswith("tubeDesigner.assemblyProcess"), key)
                test.assertFalse(key.startswith("tubeDesigner.assemblyGeometryProcess"), key)
                test.assertNotIn(key, ("tubeDesigner.endProcess", "tubeDesigner.connectionProcess",
                    "tubeDesigner.manufacturingPartCount", "stockState", "stockInterval"))
                properties_are_design_only(child)
        elif isinstance(value, list):
            for child in value:
                properties_are_design_only(child)
    properties_are_design_only(document)
    expanded = expand_resource_model(expand_display_model(document))
    test.assertEqual(expanded["parameters"], {})
    return expanded


def manufacturing_document():
    document = to_display_model(instance_document())
    document.update(schema="icax.manufacturing-model", schemaVersion=2)
    document.pop("connections", None)
    document.pop("annotations")
    document["sourceMappings"] = [{"itemKey": "part.a", "sources": [{"itemKey": "design.left"}]}]
    document["processes"] = [{"key": "cut.1", "kind": "assembly-process", "definition": {
        "templateId": "local.geometry", "processInput": {"schema": "icax.assembly-process-input",
            "schemaVersion": 2, "parts": {"stock": {"scope": "manufacturing", "itemKey": "part.a", "state": "initial"}},
            "geometry": {}}, "parameters": {"offset": 3}, "processDrafts": {},
        "targets": {"stock": "part.a"}, "dependencies": []}}]
    return document


def assert_manufacturing_model(test, document, design=None):
    if document['schemaVersion'] == 4:
        assert_declaration(test, document, design)
        if design is None:
            return document
        document = compose_manufacturing_model(document,design)
    else:
        validate_manufacturing_model(document)
        test.assertEqual(set(document), MANUFACTURING_FIELDS)
    if any("manufacturingInput" in item or "componentReference" in item or "designInput" in item for item in document["items"]):
        with test.assertRaisesRegex(ValueError, "assembly execution"):
            expand_manufacturing_model(document)
        return document
    return expand_manufacturing_model(document)


class ProductGeometryResourceTests(unittest.TestCase):
    def test_one_definition_has_two_independent_item_placements(self):
        original = instance_document()
        before = deepcopy(original)
        result = to_resource_model(original)
        self.assertEqual(original, before)
        self.assertEqual(result["schemaVersion"], 2)
        self.assertNotIn("geometry", result)
        self.assertEqual(Counter(profile2d=1, extrude=1),
                         Counter(node["operator"] for node in result["resources"]))
        first, second = [item["representations"]["display"] for item in result["items"]]
        self.assertEqual(first["resource"], second["resource"])
        self.assertEqual(first["placement"]["origin"], [0, 0, 0])
        self.assertEqual(second["placement"]["origin"], [100, 20, 30])
        self.assertEqual(second["placement"]["xAxis"], [0, 1, 0])
        self.assertEqual(second["placement"]["yAxis"], [0, 0, 1])
        self.assertEqual(second["placement"]["zAxis"], [1, 0, 0])
        self.assertEqual(first["instanceKey"], "part.a.solid")
        self.assertEqual(second["instanceKey"], "part.b.solid")
        expanded = expand_resource_model(result)
        self.assertEqual(expanded, original)
        self.assertEqual(to_resource_model(result), result)
        self.assertEqual(result["parameters"], original["parameters"])

    def test_length_and_complete_section_differences_never_share_a_solid_resource(self):
        model = NeutralModel(template_id="test.unequal-resources", template_version="1.0.0",
                             package_digest="test", parameters={})
        shared = SharedTubeGeometry(model)
        altered = profile_arguments()
        altered["contours"][1]["radius"] += 1.e-10
        for key, length, profile in (("same.a", 120, profile_arguments()),
                                     ("same.b", 120, profile_arguments((20, 0, 0))),
                                     ("longer", 121, profile_arguments()),
                                     ("different.section", 120, altered)):
            root = shared.emit_tube(key, profile_arguments=profile,
                                    extrude_arguments={"vector": [0, 0, length]})
            model.item(key, key, representations={"display": root})
        result = to_resource_model(model.build())
        refs = {item["key"]: item["representations"]["display"]["resource"]
                for item in result["items"]}
        self.assertEqual(refs["same.a"], refs["same.b"])
        self.assertEqual(len({refs["same.a"], refs["longer"], refs["different.section"]}), 3)
        self.assertEqual(Counter(profile2d=2, extrude=3),
                         Counter(node["operator"] for node in result["resources"]))

    def test_transform_consumed_by_other_geometry_stays_in_resource_graph(self):
        document = instance_document()
        document["geometry"].append({"key": "assembly", "operator": "compound",
            "inputs": ["part.a.solid", "part.b.solid"], "arguments": {}})
        result = to_resource_model(document)
        keys = {resource["key"] for resource in result["resources"]}
        self.assertTrue({"part.a.solid", "part.b.solid", "assembly"} <= keys)
        self.assertEqual(result["items"][0]["representations"]["display"],
                         {"resource": "part.a.solid"})
        self.assertEqual(expand_resource_model(result), document)

    def test_nested_product_and_part_placements_share_prototype_with_correct_world_pose(self):
        original = instance_document()
        frames = (
            {"origin": [10, 20, 30], "xAxis": [0, 1, 0], "yAxis": [-1, 0, 0], "zAxis": [0, 0, 1]},
            {"origin": [-10, 0, 15], "xAxis": [-1, 0, 0], "yAxis": [0, -1, 0], "zAxis": [0, 0, 1]},
        )
        for item, frame in zip(original["items"], frames):
            local = item["representations"]["display"]
            world = item["key"] + ".world"
            original["geometry"].append({"key": world, "operator": "transform",
                "inputs": [local], "arguments": {"placement": frame}})
            item["representations"]["display"] = world
        before = deepcopy(original)
        result = to_resource_model(original)
        self.assertEqual(original, before)
        self.assertEqual(Counter(profile2d=1, extrude=1),
                         Counter(node["operator"] for node in result["resources"]))
        first, second = [item["representations"]["display"] for item in result["items"]]
        self.assertEqual(first["resource"], second["resource"])
        self.assertEqual(first["placement"], frames[0])
        self.assertEqual(second["placement"], {
            "origin": [-110, -20, 45], "xAxis": [0, -1, 0],
            "yAxis": [0, 0, 1], "zAxis": [-1, 0, 0]})
        # The world pose is checked with points; intermediate placement nodes
        # may disappear without changing the actual resource or instance pose.
        def place(frame, point):
            return [frame["origin"][i] + sum(frame[axis][i] * point[j]
                for j, axis in enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]
        original_nodes = {node["key"]: node for node in original["geometry"]}
        for index, reference in enumerate((first, second)):
            local = original_nodes[original_nodes[reference["instanceKey"]]["inputs"][0]]["arguments"]["placement"]
            for point in ([0, 0, 0], [2, 3, 4], [-7, 11, 120]):
                self.assertEqual(place(reference["placement"], point), place(frames[index], place(local, point)))
        expanded = expand_resource_model(result)
        self.assertEqual(len(expanded["geometry"]), 4)
        self.assertEqual([item["key"] for item in expanded["items"]], ["part.a", "part.b"])

    def test_nested_instance_alias_referenced_by_machining_provenance_is_retained(self):
        original = instance_document()
        for item in original["items"]:
            root = item["representations"]["display"]
            world = item["key"] + ".world"
            original["geometry"].append({"key": world, "operator": "transform", "inputs": [root],
                "arguments": {"placement": {"origin": [50, 0, 0], "xAxis": [1, 0, 0],
                    "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}}})
            item["representations"]["display"] = world
        original["extensions"] = {"processProvenance": {"targetGeometry": "part.a.solid"}}
        alias = next(node for node in original["geometry"] if node["key"] == "part.a.solid")
        result = to_resource_model(original)
        resources = {node["key"]: node for node in result["resources"]}
        self.assertEqual(resources["part.a.solid"], alias)
        self.assertNotIn("part.b.solid", resources)
        first, second = [item["representations"]["display"] for item in result["items"]]
        self.assertEqual(first["resource"], second["resource"])
        self.assertEqual(result["extensions"], original["extensions"])
        self.assertIn("part.a.solid", {node["key"] for node in expand_resource_model(result)["geometry"]})

    def test_nested_alias_used_by_another_item_remains_a_valid_resource(self):
        original, base = nested_alias_document()
        before = deepcopy(original)
        result = to_resource_model(original)
        resources = {node["key"]: node for node in result["resources"]}
        self.assertEqual(original, before)
        self.assertEqual(resources["inner"], next(node for node in original["geometry"] if node["key"] == "inner"))
        first, second = [item["representations"]["display"] for item in result["items"]]
        self.assertEqual(first["resource"], base)
        self.assertEqual(first["instanceKey"], "outer")
        self.assertEqual(second, {"resource": "inner"})
        self.assertEqual(result["outputs"], original["outputs"])
        expand_resource_model(result)

    def test_nested_alias_referenced_as_metadata_dictionary_key_is_retained(self):
        original, base = nested_alias_document()
        original["items"][1]["representations"]["display"] = base
        original["extensions"] = {"byGeometry": {"inner": {"operation": "bend", "sequence": 1}}}
        before = deepcopy(original)
        result = to_resource_model(original)
        resources = {node["key"]: node for node in result["resources"]}
        self.assertEqual(original, before)
        self.assertEqual(resources["inner"], next(node for node in original["geometry"] if node["key"] == "inner"))
        self.assertEqual(result["extensions"], original["extensions"])
        self.assertEqual(result["items"][0]["representations"]["display"]["resource"], base)
        expanded = expand_resource_model(result)
        self.assertIn("inner", {node["key"] for node in expanded["geometry"]})

    def test_legal_frames_whose_composition_exceeds_tolerance_keep_remaining_transform(self):
        original, base = nested_alias_document()
        original["items"][1]["representations"]["display"] = base
        frame = {"origin": [5, 7, 11], "xAxis": [1 + 4.e-8, 0, 0],
                 "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}
        for node in original["geometry"]:
            if node["key"] in ("inner", "outer"):
                node["arguments"]["placement"] = deepcopy(frame)
        before = deepcopy(original)
        result = to_resource_model(original)
        resources = {node["key"]: node for node in result["resources"]}
        self.assertEqual(original, before)
        reference = result["items"][0]["representations"]["display"]
        self.assertEqual(reference, {"resource": "inner", "placement": frame, "instanceKey": "outer"})
        self.assertEqual(resources["inner"], next(node for node in original["geometry"] if node["key"] == "inner"))
        self.assertNotIn("outer", resources)
        self.assertEqual(expand_resource_model(result), original)

    def test_invalid_resource_graph_or_instance_frame_is_rejected_without_mutating_input(self):
        correct = to_resource_model(instance_document())
        mutations = {
            "unknown item resource": lambda d: d["items"][0]["representations"]["display"].update(resource="missing"),
            "duplicate resource": lambda d: d["resources"].append(deepcopy(d["resources"][0])),
            "unknown graph dependency": lambda d: d["resources"][1].update(inputs=["missing"]),
            "cyclic dependency": lambda d: d["resources"][0].update(inputs=[d["resources"][1]["key"]]),
            "duplicate item": lambda d: d["items"].append(deepcopy(d["items"][0])),
            "colliding instance key": lambda d: d["items"][0]["representations"]["display"].update(instanceKey=d["resources"][0]["key"]),
            "conflicting instance key": lambda d: d["items"][1]["representations"]["display"].update(instanceKey="part.a.solid"),
            "scaled frame": lambda d: d["items"][0]["representations"]["display"]["placement"].update(xAxis=[2, 0, 0]),
            "left handed frame": lambda d: d["items"][0]["representations"]["display"]["placement"].update(zAxis=[0, 0, -1]),
            "nonfinite origin": lambda d: d["items"][0]["representations"]["display"]["placement"].update(origin=[float("nan"), 0, 0]),
            "boolean coordinate": lambda d: d["items"][0]["representations"]["display"]["placement"].update(origin=[True, 0, 0]),
            "mixed schema": lambda d: d.update(geometry=[]),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                document = deepcopy(correct)
                mutate(document)
                before = deepcopy(document)
                with self.assertRaises(ValueError):
                    expand_resource_model(document)
                # A failed validation must not remove or rewrite declarations.
                self.assertEqual(document, before)

    def test_all_ten_public_display_entries_return_only_design_resources_and_instances(self):
        root = SRC / "apps/tube-designer/templates/product"
        names = sorted(path.parent.name for path in root.glob("*/template.json")
                       if not path.parent.name.startswith("_"))
        self.assertEqual(len(names), 10)
        for name in names:
            with self.subTest(product=name):
                descriptor, values, template = package(name)
                before = deepcopy(values)
                document = template.display(values)
                self.assertEqual(values, before)
                assert_display_model(self, document)
                self.assertEqual(template.generate(values, {"template": descriptor,
                    "geometryPurpose": "display"}), document)
                self.assertEqual(values, before)

    def test_display_and_manufacturing_topologies_remain_independent_of_resource_sharing(self):
        descriptor, defaults, template = package("single_face_security_window")
        values = dict(defaults, faceType="three", frameManufacturingMode="spatial_v_notch",
                      accessDoorEnabled=False, verticalMaximumCenterSpacing=600,
                      sideVerticalMaximumCenterSpacing=600, topBottomRodMaximumCenterSpacing=600)
        display = template.display(values)
        manufacturing = template.manufacturing(values)
        assert_display_model(self, display)
        assert_manufacturing_model(self, manufacturing, display)
        self.assertEqual(sum(item["key"].startswith("outer_frame.") for item in display["items"]), 10)
        self.assertNotIn("items",manufacturing)
        self.assertNotIn("resources",manufacturing)
        self.assertFalse(any(node["operator"] == "boolean" for node in display["resources"]))
        self.assertNotIn("connections",display)
        executed = execute_definition(manufacturing, design=display)
        self.assertEqual(sum(item['key'].startswith('outer_frame.') for item in executed['items']), 3)
        source = executed["extensions"]["tubeDesigner.assemblyProcessSource"]
        self.assertTrue(source["instances"], "Native assembly must receive the declared frame processes")
        self.assertEqual(len(source["stocks"]), len(executed["items"]))
        placements = {}
        for item in display["items"]:
            reference = item["geometry"]
            if "placement" in reference:
                placements.setdefault(reference["resource"], []).append(reference["placement"])
        self.assertTrue(any(len(group) > 1 and group[0] != group[1] for group in placements.values()),
                        "The real product must share a resource across separately positioned parts")
        # Changing process decisions must preserve the complete pure display,
        # including resources, placement and logical members.
        for mode in ("segment_weld", "plane_v_notch"):
            changed = dict(values, frameManufacturingMode=mode, assemblyClearance=0.7,
                           horizontalBranchReserve=8, verticalBranchReserve=9)
            self.assertEqual(template.display(changed), display)


class ProductDisplayModelTests(unittest.TestCase):
    @unittest.skip("Deferred product reference; no current active product generation")
    def test_shared_plate_rejects_missing_axes_nonpositive_dimensions_and_invalid_frames(self):
        _, values, template = package("decorative_door")
        design, declaration = template.display(values), template.manufacturing(values)
        before_design, before_declaration = deepcopy(design), deepcopy(declaration)
        plate_index = next(index for index, item in enumerate(design["items"])
                           if "plate" in item["properties"])
        plate = design["items"][plate_index]["properties"]["plate"]
        self.assertTrue({"width", "height", "thickness", "center", "xAxis", "yAxis"} <= set(plate))
        compose_manufacturing_model(declaration, design)
        self.assertEqual(design, before_design)
        self.assertEqual(declaration, before_declaration)
        mutations = {
            "extra-field": lambda plate: plate.update(processId="hidden-machining"),
            "missing-axis": lambda plate: plate.pop("xAxis"),
            "zero-width": lambda plate: plate.update(width=0),
            "zero-thickness": lambda plate: plate.update(thickness=0),
            "negative-thickness": lambda plate: plate.update(thickness=-1),
            "wrong-rectangle-area": lambda plate: plate.update(areaMm2=plate["width"] * plate["height"] - 1),
            "invalid-center": lambda plate: plate.update(center=[0, float("inf"), 0]),
            "nonunit-axis": lambda plate: plate.update(xAxis=[2, 0, 0]),
            "parallel-axes": lambda plate: plate.update(yAxis=deepcopy(plate["xAxis"])),
        }
        for case, mutate in mutations.items():
            with self.subTest(case=case):
                invalid = deepcopy(design)
                mutate(invalid["items"][plate_index]["properties"]["plate"])
                frozen = deepcopy(invalid)
                with self.assertRaisesRegex(ValueError, "shared plate"):
                    compose_manufacturing_model(declaration, invalid)
                with forbid_processing() as calls:
                    with self.assertRaisesRegex(ValueError, "shared plate"):
                        execute_definition(declaration, design=invalid)
                self.assertEqual(calls, [])
                self.assertEqual(invalid, frozen)
                self.assertEqual(declaration, before_declaration)

    def test_sloped_guardrail_panel_preserves_polygon_area_shared_geometry_and_manufacturing_references(self):
        descriptor, defaults, template = package("modular_guardrail_glass-straight")
        values = dict(defaults, pathMode="continuous", slopeAngle=30)
        frozen_values = deepcopy(values)
        old_display = template._generate_resource_document(values, display_context(template.__file__))
        design = template.display(values)
        expanded = assert_display_model(self, design)
        with forbid_processing() as calls:
            declaration = template.manufacturing(values)
        self.assertEqual(calls, [])
        assert_declaration(self, declaration, design)
        panels = [item for item in design["items"] if item["properties"].get("plate", {}).get("outline")]
        self.assertEqual(len(panels), 3)
        first_plate = panels[0]["properties"]["plate"]
        self.assertAlmostEqual(first_plate["width"], 939.3333333333334, places=7)
        self.assertAlmostEqual(first_plate["height"], 1562.7891664504969, places=7)
        self.assertEqual(first_plate["thickness"], 8)
        self.assertAlmostEqual(first_plate["areaMm2"], 958556.6149004782, places=7)
        self.assertLess(first_plate["areaMm2"], first_plate["width"] * first_plate["height"])
        self.assertEqual(panels[0]["properties"]["plate"], panels[1]["properties"]["plate"])
        self.assertEqual(panels[0]["geometry"]["resource"], panels[1]["geometry"]["resource"],
                         "Exactly equal sloped panels must retain one shared prototype")
        self.assertEqual(len({json.dumps(item["geometry"]["placement"], sort_keys=True)
                              for item in panels}), 3)
        previous_items = {item["key"]: item for item in old_display["items"]}
        for item in panels:
            old_properties = previous_items[item["key"]]["properties"]
            plate = item["properties"]["plate"]
            self.assertEqual(plate["outline"], old_properties["manufacturing.plate"]["outline"])
            self.assertEqual(plate["areaMm2"], old_properties["manufacturing.plate"]["areaMm2"])
            self.assertEqual(item["properties"]["partKind"], "plate")
            self.assertEqual(item["properties"]["sourcing"], "purchased")
            self.assertEqual(item["properties"]["material"], old_properties["manufacturing.material"])
            outline = plate["outline"]
            self.assertAlmostEqual(abs(sum(a[0] * b[1] - b[0] * a[1]
                for a, b in zip(outline, outline[1:] + outline[:1]))) / 2, plate["areaMm2"], places=7)
        self.assertEqual(canonical_geometry(expanded), canonical_geometry(old_display))
        frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
        executed = execute_definition(declaration, design=design)
        previous = template.generate(values, {"template": descriptor, "geometryPurpose": "manufacturing"})
        self.assertEqual(canonical_geometry(executed), canonical_geometry(previous))
        combined = compose_manufacturing_model(declaration, design)
        combined_resources = {node["key"]: node for node in combined["resources"]}
        self.assertEqual([combined_resources[node["key"]] for node in design["resources"]], design["resources"])
        combined_items = {item["key"]: item for item in combined["items"]}
        for item in panels:
            self.assertEqual(combined_items[item["key"]]["geometry"], item["geometry"])
        self.assertEqual(combined["sourceMappings"], [])
        self.assertEqual(values, frozen_values)
        self.assertEqual(design, frozen_design)
        self.assertEqual(declaration, frozen_declaration)

    def test_shared_polygon_plate_rejects_invalid_outline_and_area_before_processing(self):
        _, defaults, template = package("modular_guardrail_glass-straight")
        values = dict(defaults, pathMode="continuous", slopeAngle=30)
        frozen_values = deepcopy(values)
        design, declaration = template.display(values), template.manufacturing(values)
        frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
        index = next(index for index, item in enumerate(design["items"])
                     if item["properties"].get("plate", {}).get("outline"))
        compose_manufacturing_model(declaration, design)
        mutations = {
            "nonfinite-outline": lambda plate: plate["outline"][0].__setitem__(0, float("inf")),
            "too-few-points": lambda plate: plate.update(outline=plate["outline"][:2]),
            "degenerate-outline": lambda plate: plate.update(outline=[[0, 0], [1, 0], [2, 0]], areaMm2=0),
            "malformed-point": lambda plate: plate["outline"].__setitem__(0, [0, 0, 0]),
            "wrong-polygon-area": lambda plate: plate.update(areaMm2=plate["areaMm2"] + 1),
            "duplicate-closing-vertex": lambda plate: plate["outline"].append(deepcopy(plate["outline"][0])),
        }
        for case, mutate in mutations.items():
            with self.subTest(case=case):
                invalid = deepcopy(design)
                mutate(invalid["items"][index]["properties"]["plate"])
                frozen = deepcopy(invalid)
                with self.assertRaisesRegex(ValueError, "shared plate"):
                    compose_manufacturing_model(declaration, invalid)
                with forbid_processing() as calls:
                    with self.assertRaisesRegex(ValueError, "shared plate"):
                        execute_definition(declaration, design=invalid)
                self.assertEqual(calls, [])
                self.assertEqual(invalid, frozen)
                self.assertEqual(declaration, frozen_declaration)
        self.assertEqual(values, frozen_values)
        self.assertEqual(design, frozen_design)

    def test_shared_staircase_plate_keeps_legal_thick_brackets_and_original_machining(self):
        descriptor, defaults, template = package("straight_steel_staircase")
        for overrides in ({"bracketType": "plate", "bracketThickness": 30},
                          {"bracketType": "plate", "bracketThickness": 12, "treadWidth": 10}):
            with self.subTest(parameters=overrides):
                values = dict(defaults, **overrides)
                frozen_values = deepcopy(values)
                previous = template.generate(values, {"template": descriptor, "geometryPurpose": "manufacturing"})
                design = template.display(values)
                assert_display_model(self, design)
                with forbid_processing() as calls:
                    declaration = template.manufacturing(values)
                self.assertEqual(calls, [])
                assert_declaration(self, declaration, design)
                brackets = [item for item in design["items"] if ".support." in item["key"]
                            and item["properties"].get("partKind") == "plate"]
                self.assertTrue(brackets)
                for item in brackets:
                    plate = item["properties"]["plate"]
                    self.assertEqual(plate["thickness"], overrides["bracketThickness"])
                    self.assertGreaterEqual(plate["thickness"], min(plate["width"], plate["height"]),
                                            "The product accepts these thick bracket dimensions")
                frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
                executed = execute_definition(declaration, design=design)
                self.assertEqual(canonical_geometry(executed), canonical_geometry(previous))
                self.assertEqual(values, frozen_values)
                self.assertEqual(design, frozen_design)
                self.assertEqual(declaration, frozen_declaration)

    def test_surface_stock_preparation_preserves_real_sloped_panel_in_world_and_process_coordinates(self):
        _, defaults, template = package("modular_guardrail_glass-straight")
        values = dict(defaults, pathMode="continuous", slopeAngle=30)
        frozen_values = deepcopy(values)
        panel = next(plate for plate in template.build_layout(values).plates if plate.outline)
        old_display = template._generate_resource_document(values, display_context(template.__file__))
        previous = expand_resource_model(old_display)
        previous_item = next(item for item in previous["items"] if item["key"] == panel.key)
        item = deepcopy(next(item for item in template.display(values)["items"] if item["key"] == panel.key))
        item["properties"]["plate"].update(center=list(panel.center),
            xAxis=list(panel.x_axis), yAxis=list(panel.y_axis))
        frozen_item = deepcopy(item)
        call = {"templateId": "surface-feature-cut", "processInput": {"parts": {"stock": {"data": {}}}}}
        frozen_call = deepcopy(call)
        executor = load_executor()

        def place(frame, point):
            return [frame["origin"][i] + sum(frame[axis][i] * point[j]
                for j, axis in enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]

        def extrusion_vertices(document, root):
            nodes = {node["key"]: node for node in document["geometry"]}
            def vertices(key):
                node, arguments = nodes[key], nodes[key]["arguments"]
                if node["operator"] == "profile2d":
                    frame = arguments["placement"]
                    return [[frame["origin"][i] + frame["xAxis"][i] * segment["start"][0]
                        + frame["yAxis"][i] * segment["start"][1] for i in range(3)]
                        for contour in arguments["contours"] for segment in contour["segments"]]
                if node["operator"] == "extrude":
                    bottom = vertices(node["inputs"][0])
                    return bottom + [[point[i] + arguments["vector"][i] for i in range(3)] for point in bottom]
                self.assertEqual(node["operator"], "transform", "The fixture is one polygon extrusion")
                return [place(arguments["placement"], point) for point in vertices(node["inputs"][0])]
            return vertices(root)

        expected_vertices = extrusion_vertices(previous, previous_item["representations"]["result"])
        self.assertEqual(len(expected_vertices), 8)
        poses = (
            {"origin": [0, 0, 0], "xAxis": [1, 0, 0], "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]},
            {"origin": [25, -75, 120], "xAxis": [0, 1, 0], "yAxis": [-1, 0, 0], "zAxis": [0, 0, 1]},
        )
        for index, placement in enumerate(poses):
            with self.subTest(process_placement=placement):
                frozen_placement = deepcopy(placement)
                model = NeutralModel(template_id="polygon-stock-regression", template_version="1",
                                     package_digest="", parameters={})
                result = executor._prepare_shared_stock(model, "panel.surface", call, item, placement)
                self.assertIsNotNone(result)
                model.item(panel.key, panel.name, representations={"result": result})
                model.output("manufacturing.default", "result", [panel.key])
                prepared = model.build()
                if index == 0:
                    self.assertEqual(canonical_geometry(prepared)[panel.key]["result"],
                                     canonical_geometry(previous)[panel.key]["result"])
                actual_vertices = [place(placement, point) for point in extrusion_vertices(prepared, result)]
                self.assertEqual(sorted(tuple(round(number, 7) for number in point) for point in actual_vertices),
                                 sorted(tuple(round(number, 7) for number in point) for point in expected_vertices))
                self.assertEqual(placement, frozen_placement)
                self.assertEqual(item, frozen_item)
                self.assertEqual(call, frozen_call)
        self.assertEqual(values, frozen_values)

    def test_converter_keeps_exact_geometry_and_design_properties_without_host_or_process_data(self):
        original = instance_document()
        design = {"partNumber": "part.a", "tubeDesigner.profile": {"templateId": "test.section",
                  "parameters": {"wall": 2}}, "assemblyFrame.member": {"start": [0, 0, 0],
                  "end": [0, 0, 120]}, "tubeDesigner.componentRef": {"key": "test.component"}}
        original["items"][0]["properties"] = {**deepcopy(design),
            "manufacturing.rawStockLength": 125, "manufacturing.sourceMembers": [{"itemKey": "part.a"}],
            "manufacturing.partKind": "tube", "manufacturing.material": "steel",
            "manufacturing.materialGrade": "S235", "manufacturing.categoryKey": "outer-frame",
            "manufacturing.categoryName": "Outer frame", "manufacturing.modelReference": "tube.reference",
            "tubeDesigner.assemblyProcessStockId": "raw.a", "tubeDesigner.endProcess": {"cut": "miter"},
            "tubeDesigner.connectionProcess": {"processTemplateId": "test.process"}}
        original["extensions"] = {"tubeDesigner.assemblyProcessSource": {"stocks": ["raw.a"]},
            "tubeDesigner.specificationAnnotations": [{"id": "length", "kind": "linear",
                "start": [0, 0, 0], "end": [0, 0, 120], "label": "Length"}]}
        original["relationships"] = [{"key": "joint", "kind": "weld",
            "items": ["part.a", "part.b"], "properties": {"point": [0, 0, 0],
                "recipe": {"process": "end-cut"}, "processTemplateId": "process.weld",
                "stockAllowance": 5, "trim": {"end": 2}}}]
        before = deepcopy(original)
        result = to_display_model(original)
        self.assertEqual(original, before)
        expanded = assert_display_model(self, result)
        self.assertEqual(result["items"][0]["properties"], {**design, "partKind": "tube", "material": "steel",
            "materialGrade": "S235", "categoryKey": "outer-frame", "categoryName": "Outer frame",
            "modelReference": "tube.reference"})
        self.assertNotIn("connections",result)
        self.assertEqual(expanded["relationships"],[])
        self.assertEqual(result["annotations"], original["extensions"]["tubeDesigner.specificationAnnotations"])
        self.assertEqual(result["roots"], ["part.a", "part.b"])
        self.assertEqual(expanded["geometry"], original["geometry"])
        for current, previous in zip(expanded["items"], original["items"]):
            self.assertEqual(current["representations"]["result"], previous["representations"]["display"])
        self.assertEqual(to_display_model(result), result)

    def test_display_rejects_mixed_envelopes_invalid_references_and_hierarchy(self):
        correct = to_display_model(instance_document())
        mutations = {
            **{f"neutral field {key}": lambda d, key=key: d.update({key: {}})
               for key in ("template", "parameters", "outputs", "tables", "diagnostics", "extensions")},
            "unknown resource": lambda d: d["items"][0]["geometry"].update(resource="missing"),
            "duplicate item": lambda d: d["items"].append(deepcopy(d["items"][0])),
            "unknown root": lambda d: d.update(roots=["missing"]),
            "missing root": lambda d: d.update(roots=["part.a"]),
            "unknown child": lambda d: d["items"][0].update(children=["missing"]),
            "self child": lambda d: d["items"][0].update(children=["part.a"]),
            "invalid display name": lambda d: d["items"][0].update(displayName=[]),
            "invalid properties": lambda d: d["items"][0].update(properties=[]),
            "invalid annotations": lambda d: d.update(annotations={}),
            "annotation process pollution": lambda d: d.update(annotations=[{"manufacturing.stockId": "raw.a"}]),
            "process property": lambda d: d["items"][0]["properties"].update({"manufacturing.stockId": "raw.a"}),
            "unknown connection item": lambda d: d.update(connections=[{"key": "joint", "kind": "contact",
                "items": ["part.a", "missing"], "properties": {}}]),
            "connection process recipe": lambda d: d.update(connections=[{"key": "joint", "kind": "contact",
                "items": ["part.a", "part.b"], "properties": {"recipe": {"process": "weld"}}}]),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                document = deepcopy(correct)
                mutate(document)
                before = deepcopy(document)
                with self.assertRaises(ValueError):
                    expand_display_model(document)
                self.assertEqual(document, before)

    def test_display_tree_has_real_roots_and_preserves_connected_geometry_descendants(self):
        document = to_display_model(instance_document())
        document["items"].insert(0, {"key": "frame", "displayName": "Frame", "children": ["part.a", "part.b"],
                                     "properties": {"kind": "assembly"}})
        document["roots"] = ["frame"]
        document["schemaVersion"] = 1  # Explicit saved legacy display protocol retains its connections.
        document["connections"] = [{"key": "joint", "kind": "assembly_node", "items": ["part.a", "part.b"],
                                     "properties": {"point": [100, 20, 30]}}]
        expanded = expand_resource_model(expand_display_model(document))
        self.assertEqual(expanded["outputs"][0]["items"], ["part.a", "part.b"])
        self.assertEqual(expanded["relationships"], document["connections"])
        group = next(item for item in expanded["items"] if item["key"] == "frame")
        self.assertEqual(group["children"], ["part.a", "part.b"])
        self.assertEqual(group["representations"], {})

    def test_worker_invokes_display_only_and_appends_descriptor_annotations_to_pure_result(self):
        with tempfile.TemporaryDirectory(prefix="icax-display-worker-") as directory:
            path = Path(directory) / "template.py"
            path.write_text("from icax_template_sdk import to_display_model, NeutralModel\n"
                "def generate(parameters, context):\n    raise AssertionError('generate must not be called')\n"
                "def display(parameter_values):\n"
                "    print('display diagnostic')\n"
                "    model = NeutralModel(template_id='internal', template_version='1', package_digest='x', parameters=parameter_values)\n"
                "    resource = model.geometry('solid', 'box', arguments={'size': [parameter_values['length'], 2, 3]})\n"
                "    model.item('part', 'Part', representations={'display': resource})\n"
                "    model.output('display.default', 'display', ['part'])\n"
                "    return to_display_model(model.build())\n", encoding="utf-8")
            request = {"templatePath": str(path), "template": {"id": "host.template", "version": "1",
                "packageDigest": "display-only-regression", "parameters": [{"key": "length", "displayName": "Length"}],
                "extensions": {"sceneSpecificationAnnotations": {"annotations": [{"parameter": "length", "axis": "x"}]}}},
                "parameters": {"length": 120}, "context": {"geometryPurpose": "display"}}
            before = deepcopy(request)
            messages = io.StringIO()
            with contextlib.redirect_stderr(messages):
                result = worker._evaluate(request)
            self.assertEqual(request, before)
            self.assertEqual(messages.getvalue().strip(), "display diagnostic")
            assert_display_model(self, result)
            self.assertEqual(result["annotations"], [{"id": "descriptor.length", "parameter": "length",
                "kind": "linear", "start": [0, 0, 0], "end": [120, 0, 0], "offset": [0, 0, 0],
                "label": "Length", "editable": True, "generatedValue": 120}])


class ProductManufacturingModelTests(unittest.TestCase):
    def test_declaration_round_trip_preserves_initial_geometry_bindings_and_local_drafts(self):
        original = manufacturing_document()
        before = deepcopy(original)
        result = to_manufacturing_model(original)
        self.assertEqual(original, before)
        assert_manufacturing_model(self, result)
        self.assertEqual(result, original)
        self.assertEqual(result["processes"][0]["definition"]["parameters"], {"offset": 3})
        self.assertEqual(result["sourceMappings"], [{"itemKey": "part.a", "sources": [{"itemKey": "design.left"}]}])
        self.assertTrue(all("manufacturing.sourceMembers" not in item["properties"] for item in result["items"]))
        self.assertNotIn("partLists", result)
        self.assertNotIn("checks", result)

    def test_unconsumed_intrinsic_geometry_aliases_are_not_discarded(self):
        original = manufacturing_document()
        base = original["items"][0]["geometry"]["resource"]
        original["resources"].append({"key": "observer.alias", "operator": "transform", "inputs": [base],
            "arguments": {"placement": {"origin": [7, 8, 9], "xAxis": [1, 0, 0],
                "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}}})
        before = deepcopy(original)
        result = to_manufacturing_model(original)
        self.assertEqual(result["resources"], original["resources"])
        self.assertEqual(original, before)
        assert_manufacturing_model(self, result)

    def test_invalid_declaration_envelope_mapping_process_or_resource_is_rejected(self):
        correct = manufacturing_document()
        mutations = {
            **{f"nondeclarative field {key}": lambda d, key=key: d.update({key: {}})
               for key in ("template", "parameters", "outputs", "tables", "diagnostics", "extensions", "geometry", "partLists", "checks")},
            "boolean schema version": lambda d: d.update(schemaVersion=True),
            "invalid item key": lambda d: d["items"][0].update(key="invalid item key"),
            "unknown item resource": lambda d: d["items"][0]["geometry"].update(resource="missing"),
            "unknown mapping item": lambda d: d["sourceMappings"][0].update(itemKey="missing"),
            "legacy source property": lambda d: d["items"][0]["properties"].update({"manufacturing.sourceMembers": []}),
            "duplicate mapping": lambda d: d["sourceMappings"].append(deepcopy(d["sourceMappings"][0])),
            "invalid source array": lambda d: d["sourceMappings"][0].update(sources={}),
            "duplicate display source": lambda d: d["sourceMappings"][0]["sources"].append(deepcopy(d["sourceMappings"][0]["sources"][0])),
            "duplicate process key": lambda d: d["processes"].append(deepcopy(d["processes"][0])),
            "executed process kind": lambda d: d["processes"][0].update(kind="geometry-function"),
            "invalid process definition": lambda d: d["processes"][0].update(definition=[]),
            "executed process result": lambda d: d["processes"][0]["definition"].update(result={"applicable": True}),
            "unknown bound manufacturing item": lambda d: d["processes"][0]["definition"]["processInput"]["parts"]["stock"].update(itemKey="missing"),
            "unknown dependency": lambda d: d["processes"][0]["definition"].update(dependencies=["missing"]),
            "self dependency": lambda d: d["processes"][0]["definition"].update(dependencies=["cut.1"]),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                document = deepcopy(correct)
                mutate(document)
                before = deepcopy(document)
                with self.assertRaises(ValueError):
                    expand_manufacturing_model(document)
                self.assertEqual(document, before)

    def test_all_ten_declarations_execute_to_equivalent_geometry_parts_and_process_results(self):
        baselines = [entry for entry in json.loads(BASELINE_FILE.read_text(encoding="utf-8"))
                     if entry["name"] not in {"louver_window", "aluminium_window", "decorative_door"}]
        self.assertEqual(len(baselines), 10)
        def semantic(value, nodes):
            if isinstance(value, dict):
                return {key: semantic(child, nodes) for key, child in value.items() if key not in {
                    "functionDigest", "planDigest", "identity", "executionSignature", "sourceIdentity", "sourceExecutionSignature"}}
            if isinstance(value, list):
                return [semantic(child, nodes) for child in value]
            if isinstance(value, str) and value in nodes:
                return nodes[value]
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                return float(round(value, 7)) or 0.0
            return value
        for baseline in baselines:
            with self.subTest(product=baseline["name"]):
                _, _, template = package(baseline["name"])
                values = deepcopy(baseline["parameters"])
                before = deepcopy(values)
                with forbid_processing() as calls:
                    definition = template.manufacturing(values)
                self.assertEqual(calls, [])
                self.assertEqual(values, before)
                design = template.display(values)
                frozen_design = deepcopy(design)
                assert_manufacturing_model(self, definition, design)
                saved_definition = deepcopy(definition)
                executed = expand_resource_model(execute_definition(definition, design=design))
                self.assertEqual(design,frozen_design)
                self.assertEqual(definition, saved_definition)
                legacy = expand_resource_model(baseline["document"])
                window = baseline["name"] in {"single_face_security_window", "three_face_security_window", "five_face_security_window"}
                if window:
                    source = executed["extensions"]["tubeDesigner.assemblyProcessSource"]
                    self.assertTrue(source["instances"])
                    self.assertEqual({stock["id"] for stock in source["stocks"]},
                                     {item["key"] for item in executed["items"]})
                    # Window cutter bodies are executed by the native pipeline;
                    # ProductManufacturingCatalogue.native.mjs compares the final BReps.
                else:
                    self.assertEqual(canonical_geometry(executed), canonical_geometry(legacy))
                    self.assertNotIn('tubeDesigner.productStockMappingVersion', executed['extensions'])
                    for fixed_item in executed['items']:
                        self.assertNotIn('manufacturing.sourceMembers', fixed_item['properties'], fixed_item['key'])
                self.assertEqual([(item["key"], item["children"]) for item in executed["items"]],
                                 [(item["key"], item["children"]) for item in legacy["items"]])
                self.assertEqual(executed["outputs"][0]["items"], legacy["outputs"][0]["items"])
                current_items = {item["key"]: item for item in executed["items"]}
                for item in legacy["items"]:
                    properties = current_items[item["key"]]["properties"]
                    for key in ("length", "quantity", "partNumber", "manufacturing.material", "manufacturing.materialGrade", "manufacturing.sourceMembers"):
                        if key in item["properties"]:
                            if key == "length":
                                self.assertAlmostEqual(properties.get(key), item["properties"][key], places=7,
                                                       msg=(item["key"], key))
                            elif key == "manufacturing.sourceMembers" and window:
                                def source_stations(sources):
                                    sources = deepcopy(sources)
                                    for source in sources:
                                        for span in source.get("spans", []):
                                            if "sourceRange" in span:
                                                fractions = span.pop("sourceRange")
                                                length = math.dist(span["start"], span["end"])
                                                span["sourceStartStation"] = fractions[0] * length
                                                span["sourceEndStation"] = fractions[1] * length
                                    return semantic(sources, {})
                                self.assertEqual(source_stations(properties[key]),
                                                 source_stations(item["properties"][key]), item["key"])
                            else:
                                self.assertEqual(properties.get(key), item["properties"][key], (item["key"], key))
                results = executed["extensions"].get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
                previous = legacy["extensions"].get("tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
                if not window:
                    current_records = {record["instanceId"]: record for record in results}
                    previous_records = {record["instanceId"]: record for record in previous}
                    self.assertEqual(set(current_records), set(previous_records))
                    current_nodes = canonical_geometry(executed, include_nodes=True)
                    previous_nodes = canonical_geometry(legacy, include_nodes=True)
                    for key, record in current_records.items():
                        current_result = deepcopy(record["result"])
                        previous_result = deepcopy(previous_records[key]["result"])
                        if current_result["materialRequirements"] != previous_result["materialRequirements"]:
                            # Joint rules now report their actual end-plane facts;
                            # the former inline joint calculation reported none.
                            self.assertEqual(baseline["name"], "straight_steel_staircase")
                            self.assertEqual(previous_result["materialRequirements"], [])
                            requirements = current_result["materialRequirements"]
                            self.assertTrue(requirements)
                            self.assertTrue(all(requirement["kind"] == "joint-plane" and
                                len(requirement["point"]) == len(requirement["inwardNormal"]) == 3
                                for requirement in requirements))
                            current_result["materialRequirements"] = []
                        self.assertEqual(semantic(current_result, current_nodes),
                                         semantic(previous_result, previous_nodes), key)

    def test_worker_invokes_manufacturing_only_without_display_annotations_or_input_echo(self):
        with tempfile.TemporaryDirectory(prefix="icax-manufacturing-worker-") as directory:
            path = Path(directory) / "template.py"
            path.write_text("from icax_template_sdk import to_manufacturing_model, NeutralModel\n"
                "from icax_template_sdk.manufacturing import manufacturing_context\n"
                "from pathlib import Path\n"
                "def generate(parameters, context):\n    raise AssertionError('generate must not be called')\n"
                "def manufacturing(parameter_values):\n"
                "    environment = manufacturing_context(__file__)\n"
                "    assert environment['userMouldRoot'] == str(Path(__file__).parent)\n"
                "    assert environment['privateFixture'] == 'current-run'\n"
                "    environment['privateFixture'] = 'local-change'\n"
                "    assert parameter_values['length'] > 0, 'invalid fixture length'\n"
                "    print('manufacturing diagnostic')\n"
                "    model = NeutralModel(template_id='internal', template_version='1', package_digest='x', parameters=parameter_values)\n"
                "    resource = model.geometry('solid', 'box', arguments={'size': [parameter_values['length'], 2, 3]})\n"
                "    model.item('part', 'Part', representations={'result': resource})\n"
                "    model.output('result.manufacturing', 'result', ['part'])\n"
                "    return to_manufacturing_model(model.build())\n", encoding="utf-8")
            path.with_name("template.json").write_text(json.dumps({"id": "fixture.manufacturing", "version": "1"}), encoding="utf-8")
            request = {"templatePath": str(path), "template": {"id": "host.template", "version": "1",
                "packageDigest": "manufacturing-only-regression", "parameters": [{"key": "length", "displayName": "Length"}],
                "extensions": {"sceneSpecificationAnnotations": {"annotations": [{"parameter": "length", "axis": "x"}]}}},
                "parameters": {"length": 120}, "context": {"geometryPurpose": "manufacturing",
                    "userMouldRoot": str(directory), "privateFixture": "current-run"}}
            before = deepcopy(request)
            environment_before = manufacturing_context(str(path))
            previous_environment = {"userMouldRoot": "previous-run", "privateFixture": "previous-run"}
            messages = io.StringIO()
            with _execution_scope(previous_environment):
                active_environment = manufacturing_context(str(path))
                with contextlib.redirect_stderr(messages):
                    result = worker._evaluate(request)
                self.assertEqual(manufacturing_context(str(path)), active_environment)
                invalid_request = deepcopy(request)
                invalid_request["parameters"]["length"] = -1
                invalid_before = deepcopy(invalid_request)
                with self.assertRaisesRegex(AssertionError, "invalid fixture length"):
                    worker._evaluate(invalid_request)
                self.assertEqual(invalid_request, invalid_before)
                self.assertEqual(manufacturing_context(str(path)), active_environment)
            self.assertEqual(manufacturing_context(str(path)), environment_before)
            self.assertEqual(previous_environment, {"userMouldRoot": "previous-run", "privateFixture": "previous-run"})
            self.assertEqual(request, before)
            self.assertEqual(messages.getvalue().strip(), "manufacturing diagnostic")
            assert_manufacturing_model(self, result)
            self.assertEqual(result["processes"], [])
            self.assertEqual(result["connections"], [])


if __name__ == "__main__":
    unittest.main()
