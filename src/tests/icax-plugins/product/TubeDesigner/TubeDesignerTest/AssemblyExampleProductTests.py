"""Template-owned example products stay independent of geometry and machining."""
import copy
import json
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[5]
RUNTIME = SOURCE / "apps/tube-designer/templates/_shared/assembly_template_runtime.py"


class AssemblyExampleProductTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(RUNTIME))
        cls.example = staticmethod(cls.runtime["get_example_product"])
        cls.check = staticmethod(cls.runtime["check_applicability"])

    def test_all_templates_provide_applicable_process_free_examples_without_generation(self):
        manifests = list((RUNTIME.parent.parent / "assembly").glob("*/assembly.json"))
        self.assertGreaterEqual(len(manifests), 22)
        self.assertIn("orthogonal-corner", {manifest.parent.name for manifest in manifests})
        def forbidden(*_args, **_kwargs):
            self.fail("示例成品函数不得生成成品模型、下料计划或成形网格")
        for manifest in manifests:
            descriptor = json.loads(manifest.read_text(encoding="utf-8"))
            with self.subTest(template=descriptor["id"]):
                self.assertTrue(manifest.with_name("example.py").is_file())
                parameters = {p["key"]: copy.deepcopy(p["defaultValue"])
                              for p in descriptor["parameters"]}
                before = copy.deepcopy(parameters)
                provider = self.runtime["_load_example_product_script"](descriptor["id"])
                with patch.dict(self.example.__globals__, {key: forbidden for key in
                                ("preview_plan", "finished_product_plan", "_load_assembly_script")}):
                    independent = provider(copy.deepcopy(parameters))
                # Public example adaptation may compute request poses; the
                # provider itself returns only independent product data.
                result = self.example(descriptor["id"], parameters)
                self.assertEqual(set(result), {"schema", "schemaVersion", "templateId", "finishedProduct"})
                self.assertEqual(result["schema"], "icax.assembly-example-product")
                self.assertEqual(result["templateId"], descriptor["id"])
                product = result["finishedProduct"]
                self.assertEqual(product, independent)
                self.assertEqual(set(product), {"schema", "schemaVersion", "shapeId", "parameters", "spans"})
                self.assertEqual(product["shapeId"], descriptor["exampleInput"]["shapeId"])
                compiled = self.runtime["_template_by_id"](descriptor["id"])
                local = self.runtime["process_contract"].from_product(
                    compiled, product, self.runtime["finished_product_plan"](product))
                local = self.runtime["process_contract"].select_example_anchors(compiled, local)
                self.assertTrue(self.check(descriptor["id"], supplied_values=parameters, process_input=local)["applicable"])
                self.assertEqual(parameters, before)

    def test_switch_can_reuse_one_l_product_and_fall_back_only_when_inapplicable(self):
        product = self.example("bend")["finishedProduct"]
        before = copy.deepcopy(product)
        for template in ("bend", "flexible-slit-bend-integrated", "node-edge-arc-integrated",
                         "node-embedded-arc-integrated", "node-v-notch-integrated", "segmented-bend",
                         "two-end-end-angle", "wrap-a-over-b"):
            self.assertTrue(self.check(template, product)["applicable"], template)
        self.assertEqual(product, before)
        product["parameters"]["angle"] = 170
        self.assertFalse(self.check("two-end-end-angle", product)["applicable"])
        replacement = self.example("two-end-end-angle")["finishedProduct"]
        self.assertTrue(self.check("two-end-end-angle", replacement)["applicable"])
        self.assertEqual(product["parameters"]["angle"], 170)

    def test_template_examples_own_required_dimensions_and_preserve_inactive_drafts(self):
        parameters = {"maleFemale": True, "pairCount": "four", "tabWidth": 24,
                      "tabLength": 30, "sideClearance": 1.5}
        before = copy.deepcopy(parameters)
        product = self.example("wrap-a-over-b", parameters)["finishedProduct"]
        self.assertTrue(self.check("wrap-a-over-b", product, parameters)["applicable"])
        self.assertGreater(product["spans"]["armA"]["parameters"]["width"],
                           product["spans"]["armB"]["parameters"]["depth"])
        self.assertEqual(parameters, before)
        inactive = {"maleFemale": False, "tabWidth": -1, "tabLength": -2,
                    "sideClearance": -3, "pairCount": "draft"}
        self.assertTrue(self.check("wrap-a-over-b", self.example(
            "wrap-a-over-b", inactive)["finishedProduct"], inactive)["applicable"])
        product = self.example("insert-sleeve", {"fitClearance": 1.2})["finishedProduct"]
        self.assertAlmostEqual(product["spans"]["second"]["parameters"]["width"], 36.4)
        self.assertTrue(self.check("insert-sleeve", product, {"fitClearance": 1.2})["applicable"])

    def test_provider_cannot_mutate_inputs_or_return_process_fields_or_rejected_product(self):
        parameters = {"bendRadius": 55}
        before = copy.deepcopy(parameters)
        product = self.runtime["finished_products"].create("l")
        def mutate(values):
            values["bendRadius"] = 40
            return product
        invalid = copy.deepcopy(product)
        invalid["parameters"]["bendRadius"] = 55
        wrong = self.runtime["finished_products"].create("t")
        for provider in (mutate, lambda _: invalid, lambda _: wrong):
            with self.subTest(provider=provider), patch.dict(self.example.__globals__, {
                    "_load_example_product_script": lambda _: provider}):
                with self.assertRaises(ValueError):
                    self.example("bend", parameters)
                self.assertEqual(parameters, before)

    def test_fastening_and_welding_examples_cover_large_group_envelopes(self):
        cases = (("through-bolt", {"boltCount": 20, "pitch": 1000}),
                 ("mechanical-fastener", {"count": 20, "pitch": 1000}),
                 ("slot-bolt-adjustable", {"adjustAxis": "y", "adjustment": 200}),
                 ("weld-interface", {"weldType": "slot", "slotLength": 200,
                                     "slotWidth": 80, "weldCount": 2, "weldPitch": 200}))
        for template, parameters in cases:
            with self.subTest(template=template):
                product = self.example(template, parameters)["finishedProduct"]
                check = self.check(template, product, parameters)
                self.assertTrue(check["applicable"], check["reason"])

    def test_dispatch_missing_provider_and_invalid_inputs_are_explicit(self):
        result = self.runtime["generate"]({"action": "example-product", "templateId": "bend"}, {})
        self.assertEqual(result, self.example("bend"))
        for template, values in (("absent", {}), ("bend", {"angle": 90}), ("bend", [])):
            with self.subTest(template=template, values=values), self.assertRaises(ValueError):
                self.example(template, values)
        def missing(_):
            raise ValueError("该模板未提供有效的示例成品函数")
        with patch.dict(self.example.__globals__, {"_load_example_product_script": missing}):
            with self.assertRaisesRegex(ValueError, "未提供"):
                self.example("bend")


if __name__ == "__main__":
    unittest.main()
