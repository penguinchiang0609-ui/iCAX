"""Template-owned product preflight stays independent of model generation."""
import copy
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[5]
RUNTIME = SOURCE / "apps/tube-designer/templates/_shared/assembly_template_runtime.py"


class AssemblyApplicabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(RUNTIME))
        cls.products = cls.runtime["finished_products"]
        cls.check = staticmethod(cls.runtime["check_applicability"])

    def product_for(self, descriptor):
        product = self.products.create(descriptor["exampleInput"]["shapeId"])
        if descriptor["id"] == "insert-sleeve":
            for name, width in (("first", 30), ("second", 34.6)):
                product["spans"][name]["profileRef"] = {"scope": "system", "id": "round"}
                product["spans"][name]["parameters"] = {
                    "width": width, "wallThickness": 2, "innerOffsetX": 0, "innerOffsetY": 0}
        return product

    def local_input(self, template_id, product=None):
        descriptor = self.runtime["_template_by_id"](template_id)
        if product is None:
            values = {p["key"]: copy.deepcopy(p["defaultValue"])
                      for p in descriptor["parameters"] if p.get("scope", "process") != "scene"}
            product = self.runtime["_load_example_product_script"](template_id)(values)
        local = self.runtime["process_contract"].from_product(
            descriptor, product, self.runtime["finished_product_plan"](product))
        return self.runtime["process_contract"].select_example_anchors(descriptor, local)

    def test_all_templates_check_inputs_without_generation_or_mutation(self):
        manifests = list((RUNTIME.parent.parent / "assembly").glob("*/assembly.json"))
        self.assertGreaterEqual(len(manifests), 21)
        def forbidden(*_args, **_kwargs):
            self.fail("适用性检查不得生成成品、工艺计划或成形网格")
        globals_ = self.check.__globals__
        cases = []
        for manifest in manifests:
            descriptor = json.loads(manifest.read_text(encoding="utf-8"))
            parameters = {p["key"]: copy.deepcopy(p["defaultValue"])
                          for p in descriptor["parameters"]}
            cases.append((manifest, descriptor, self.local_input(descriptor["id"]), parameters))
        with patch.dict(globals_, {key: forbidden for key in
                        ("preview_plan", "finished_product_plan", "_load_assembly_script")}):
            for manifest, descriptor, local, parameters in cases:
                with self.subTest(template=descriptor["id"]):
                    self.assertTrue(manifest.with_name("applicability.py").is_file())
                    before = copy.deepcopy((local, parameters))
                    result = self.check(descriptor["id"], supplied_values=parameters, process_input=local)
                    self.assertTrue(result["applicable"], result["reason"])
                    self.assertEqual(result["reason"], "")
                    self.assertEqual((local, parameters), before)
                    incomplete = copy.deepcopy(local)
                    incomplete["parts"].pop(next(iter(incomplete["parts"])))
                    rejected = self.check(descriptor["id"], supplied_values=parameters, process_input=incomplete)
                    self.assertFalse(rejected["applicable"])
                    self.assertTrue(rejected["reason"])

    def test_same_product_accepts_cold_bend_and_rejects_miter_limit(self):
        product = self.products.create("l")
        product["parameters"]["angle"] = 170
        before = copy.deepcopy(product)
        self.assertTrue(self.check("bend", product)["applicable"])
        result = self.check("two-end-end-angle", product)
        self.assertFalse(result["applicable"])
        self.assertIn("几何量", result["reason"])
        self.assertEqual(product, before)

    def test_section_and_curvature_constraints_are_checked_directly(self):
        product = self.products.create("l")
        product["spans"]["armB"]["parameters"]["width"] += 1
        for template in ("bend", "segmented-bend", "two-end-end-angle"):
            with self.subTest(template=template):
                self.assertFalse(self.check(template, product)["applicable"])
        product = self.products.create("l")
        result = self.check("flexible-slit-bend-integrated", product, {"bendRadius": 1})
        self.assertFalse(result["applicable"])
        self.assertTrue(result["reason"])
        result = self.check("flexible-slit-bend-integrated", product, {"bendRadius": 5})
        self.assertFalse(result["applicable"])
        self.assertIn("半径", result["reason"])
        for span in product["spans"].values():
            span["profileRef"] = {"scope": "user", "id": "unmeasured-contour"}
        self.assertFalse(self.check("two-end-end-angle", product)["applicable"])

    def test_disabled_tabs_keep_inactive_drafts_without_rejecting_product(self):
        product = self.products.create("l")
        parameters = {"maleFemale": False, "tabWidth": -123, "tabLength": -456,
                      "sideClearance": -789, "pairCount": "old-draft"}
        before = copy.deepcopy(parameters)
        result = self.check("wrap-a-over-b", product, parameters)
        self.assertTrue(result["applicable"], result["reason"])
        self.assertEqual(parameters, before)

    def test_hook_cannot_modify_product_or_parameters_or_claim_invalid_result(self):
        product = self.products.create("l")
        parameters = {"bendRadius": 55}
        before = copy.deepcopy((product, parameters))
        def mutate_product(value, _parameters):
            value["parts"]["segmentA"]["length"] += 1
            return {"applicable": True, "reason": ""}
        def mutate_parameters(_value, process_parameters):
            process_parameters["bendRadius"] += 1
            return {"applicable": True, "reason": ""}
        hooks = [mutate_product, mutate_parameters,
                 lambda *_: {"applicable": 1, "reason": ""},
                 lambda *_: {"applicable": False, "reason": ""},
                 lambda *_: {"applicable": True, "reason": "", "geometry": {}}]
        for hook in hooks:
            with self.subTest(hook=hook), patch.dict(self.check.__globals__, {
                    "_load_applicability_script": lambda _id: hook}):
                self.assertFalse(self.check("bend", product, parameters)["applicable"])
                self.assertEqual((product, parameters), before)

    def test_preview_uses_same_hook_before_generating_any_product_geometry(self):
        product = self.products.create("l")
        local = self.local_input("bend", product)
        def checker(_product, _parameters):
            return {"applicable": False, "reason": "测试工艺拒绝当前成品"}
        def forbidden(*_):
            self.fail("拒绝的工艺不得开始生成成品几何")
        with patch.dict(self.check.__globals__, {
                "_load_applicability_script": lambda _id: checker,
                "finished_product_plan": forbidden}):
            result = self.check("bend", process_input=local)
            self.assertFalse(result["applicable"])
            self.assertEqual(result["reason"], "测试工艺拒绝当前成品")
            with self.assertRaisesRegex(ValueError, "测试工艺拒绝当前成品"):
                self.runtime["preview_plan"]("bend", process_input=local)

    def test_dispatch_and_invalid_inputs_return_explicit_reasons(self):
        product = self.products.create("l")
        result = self.runtime["generate"]({"action": "check-applicability", "templateId": "bend",
                                           "finishedProduct": product}, {})
        self.assertEqual(result, self.check("bend", product))
        for template_id, value, parameters in (("absent", product, {}),
                                                ("bend", {}, {}),
                                                ("bend", product, {"angle": 90})):
            with self.subTest(template=template_id, parameters=parameters):
                result = self.check(template_id, value, parameters)
                self.assertFalse(result["applicable"])
                self.assertTrue(result["reason"])
        def missing(_id):
            raise ValueError("该模板未提供有效的成品适用性函数")
        with patch.dict(self.check.__globals__, {"_load_applicability_script": missing}):
            result = self.check("bend", product)
            self.assertFalse(result["applicable"])
            self.assertIn("未提供", result["reason"])


    def test_invalid_template_script_returns_reason_without_changing_input(self):
        product = self.products.create("l")
        before = copy.deepcopy(product)
        checker = self.check.__globals__["_load_applicability_script"]
        source = Path(checker.__globals__["ROOT"]) / "bend/applicability.py"
        # Load the descriptor from the real library and redirect only its hook.
        scripts = ["def broken(:\n", "raise RuntimeError('template dependency missing')\n",
                   "def check_applicability(product, parameters):\n    raise ValueError()\n"]
        for script in scripts:
            with self.subTest(script=script), tempfile.TemporaryDirectory() as directory:
                broken = Path(directory) / "applicability.py"
                broken.write_text(script, encoding="utf-8")
                real_file_location = self.check.__globals__["importlib"].util.spec_from_file_location

                def redirected(name, path):
                    return real_file_location(name, broken if Path(path) == source else path)

                with patch.object(self.check.__globals__["importlib"].util,
                                  "spec_from_file_location", redirected):
                    result = self.check("bend", product)
                self.assertFalse(result["applicable"])
                self.assertTrue(result["reason"].strip())
                self.assertEqual(product, before)


if __name__ == "__main__":
    unittest.main()
