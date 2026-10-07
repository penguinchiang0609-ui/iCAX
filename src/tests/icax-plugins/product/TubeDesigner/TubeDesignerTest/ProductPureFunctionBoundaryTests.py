"""Every active finished product can be generated without executing machining."""
from copy import deepcopy
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

from WindowCatalogueTests import SRC, package
from icax_template_sdk import NeutralModel
from ProductManufacturingDeclarationTests import assert_declaration, forbid_processing, execute_definition


class ProductPureFunctionBoundaryTests(unittest.TestCase):
    def test_manufacturing_products_declare_without_processing_and_execute_only_downstream(self):
        cases = [(name, {}) for name in (
            "minimal_protective_grille", "modular_guardrail",
            "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight",
            "modular_guardrail_glass-straight", "straight_steel_staircase")]
        cases.extend([
            ("minimal_protective_grille", {"frameType": "closed_frame",
                "frameCornerJoint": "miter_45", "maleCornerType": "square",
                "installHoleOrientation": "side", "installHoleAutoAvoid": True,
                "installHoleMaximumShift": 30, "handleEnabled": False}),
            ("modular_guardrail", {"layout": "left_l", "handrailProfileType": "round",
                "postProfileType": "round", "railProfileType": "round",
                "infillProfileType": "round"}),
        ])
        emit = NeutralModel.geometry
        emitted_booleans = []
        def process_geometry(model, key, operator, *args, **kwargs):
            if operator == "boolean":
                caller = sys._getframe(1)
                self.assertEqual(Path(caller.f_code.co_filename).name,
                                 "assembly_geometry_process_runtime.py", key)
                self.assertEqual(caller.f_code.co_name, "apply", key)
                emitted_booleans.append(key)
            return emit(model, key, operator, *args, **kwargs)
        with patch.object(NeutralModel, "geometry", process_geometry), tempfile.TemporaryDirectory(
                prefix="icax-pure-boundary-") as directory:
            for name, changes in cases:
                with self.subTest(product=name, changes=changes):
                    descriptor, defaults, template = package(name)
                    values = {**defaults, **changes}
                    saved = deepcopy(values)
                    before = len(emitted_booleans)
                    with forbid_processing() as calls:
                        declaration = template.manufacturing(values)
                    self.assertEqual(calls, [])
                    assert_declaration(self, declaration)
                    frozen = deepcopy(declaration)
                    design = template.display(values)
                    frozen_design = deepcopy(design)
                    document = execute_definition(declaration, design=design)
                    self.assertEqual(design, frozen_design)
                    self.assertEqual(declaration, frozen)
                    self.assertEqual(values, saved)
                    instances = document.get("extensions", {}).get(
                        "tubeDesigner.assemblyGeometryProcesses", {}).get("instances", [])
                    if len(emitted_booleans) > before:
                        self.assertTrue(instances, "Manufacturing operations need process provenance")
                    for instance in instances:
                        self.assertTrue(instance["result"]["applicable"])
                        self.assertEqual(instance["parameters"], instance["result"]["parameters"])
                        self.assertIn("stock", instance["processInput"]["parts"])
                        self.assertTrue(instance["stockId"])
        self.assertTrue(emitted_booleans, "The boundary test must exercise actual manufacturing")

    def test_all_active_display_templates_avoid_manufacturing_operations_and_plans(self):
        root = SRC / "apps/tube-designer/templates/product"
        names = sorted(path.parent.name for path in root.glob("*/template.json")
                       if not path.parent.name.startswith("_"))
        self.assertEqual(len(names), 13)
        emit = NeutralModel.geometry
        def finished_geometry(model, key, operator, *args, **kwargs):
            if operator == "boolean":
                raise AssertionError("Finished product executed a manufacturing Boolean: " + key)
            return emit(model, key, operator, *args, **kwargs)
        with patch.object(NeutralModel, "geometry", finished_geometry):
            for name in names:
                with self.subTest(product=name):
                    descriptor, parameters, template = package(name)
                    saved = deepcopy(parameters)
                    result = template.display(parameters)
                    self.assertEqual(parameters, saved)
                    self.assertEqual(result["schema"], "icax.display-model")
                    self.assertEqual(result["schemaVersion"], 2)
                    self.assertNotIn("connections", result)
                    self.assertEqual(set(result), {"schema", "schemaVersion", "coordinateSystem", "lengthUnit",
                        "resources", "items", "roots", "annotations"})
                    self.assertTrue(result["items"])
                    for item in result["items"]:
                        for key in item["properties"]:
                            self.assertFalse(key.startswith("manufacturing."), key)
                            self.assertFalse(key.startswith("tubeDesigner.assemblyProcess"), key)
                            self.assertNotIn(key, ("tubeDesigner.endProcess", "tubeDesigner.connectionProcess"))

    def test_security_process_drafts_do_not_define_finished_three_face_members(self):
        descriptor, defaults, template = package("single_face_security_window")
        values = dict(defaults, faceType="three", accessDoorEnabled=False)
        def finished(changes):
            result = template.display(dict(values, **changes))
            return [(item["key"], item["properties"]["assemblyFrame.member"])
                    for item in result["items"]]
        baseline = finished({"frameManufacturingMode": "segment_weld"})
        for mode in ("plane_v_notch", "spatial_v_notch"):
            self.assertEqual(finished({"frameManufacturingMode": mode,
                                      "assemblyClearance": 0.7,
                                      "horizontalBranchReserve": 8,
                                      "verticalBranchReserve": 9}), baseline)
        self.assertEqual(sum(key.startswith("outer_frame.") for key, _ in baseline), 10)


if __name__ == "__main__":
    unittest.main()
