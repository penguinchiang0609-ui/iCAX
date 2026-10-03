"""The lightweight assembly plan preserves machining and immutable inputs."""
import copy
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[5]
TEMPLATES = SOURCE / "apps/tube-designer/templates"


class AssemblyManufacturingPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(TEMPLATES / "_shared/assembly_template_runtime.py"))

    def test_all_templates_keep_the_exact_manufacturing_plan_and_echo(self):
        manifests = list((TEMPLATES / "assembly").glob("*/assembly.json"))
        self.assertEqual(len(manifests), 22)
        for manifest in manifests:
            descriptor = json.loads(manifest.read_text(encoding="utf-8"))
            template = descriptor["id"]
            with self.subTest(template=template):
                values = {p["key"]: copy.deepcopy(p["defaultValue"]) for p in descriptor["parameters"]}
                product = self.runtime["get_example_product"](template, values)["finishedProduct"]
                before = copy.deepcopy((values, product))
                full = self.runtime["preview_plan"](template, values, finished_product=product)
                lean = self.runtime["generate"]({"action": "preview-plan", "templateId": template,
                    "parameters": values, "finishedProduct": product, "manufacturingOnly": True}, {})
                full.pop("formedPreviewMesh", None)
                self.assertEqual(lean, full)
                self.assertEqual((values, product), before)
                self.assertEqual(lean["parameters"], values)
                self.assertEqual(lean["finishedProduct"], product)

    def test_skip_mesh_does_not_skip_the_stock_builder(self):
        load = self.runtime["_load_assembly_script"]
        calls = []
        def module(template):
            result = load(template)
            builder = result.build_plan
            def stock(plan):
                calls.append(template)
                return builder(plan)
            result.build_plan = stock
            result.build_formed_preview = lambda *_: self.fail("制造模式不得生成成形示意网格")
            return result
        with patch.dict(self.runtime["preview_plan"].__globals__, {"_load_assembly_script": module}):
            for template in ("two-end-end-angle", "flexible-slit-bend-integrated", "segmented-bend",
                             "node-embedded-arc-integrated"):
                product = self.runtime["get_example_product"](template)["finishedProduct"]
                plan = self.runtime["preview_plan"](template, finished_product=product, manufacturing_only=True)
                self.assertNotIn("formedPreviewMesh", plan)
                self.assertIn(template, calls)
                if template == "two-end-end-angle":
                    self.assertGreater(plan["manufacturingParts"][0]["request"]["length"],
                                       product["spans"]["armA"]["length"])

    def test_plan_mode_is_explicit_and_does_not_enter_parameter_echo(self):
        product = self.runtime["get_example_product"]("segmented-bend")["finishedProduct"]
        self.assertIn("formedPreviewMesh", self.runtime["preview_plan"]("segmented-bend", finished_product=product))
        for mode in (None, 1, "true", {}, []):
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "manufacturingOnly"):
                self.runtime["generate"]({"action": "preview-plan", "templateId": "segmented-bend",
                    "finishedProduct": product, "manufacturingOnly": mode}, {})
        with self.assertRaises(ValueError):
            self.runtime["generate"]({"action": "preview-plan", "finishedProduct": product,
                "finishedOnly": True, "manufacturingOnly": True}, {})

    def test_public_cache_digest_changes_with_executable_dependencies(self):
        catalogue = self.runtime["catalogue"]()
        self.assertEqual(catalogue["errors"], [])
        for descriptor in catalogue["assemblies"]:
            self.assertRegex(descriptor["generationDigest"], r"^[0-9a-f]{64}$")
            self.assertNotIn("generationDigest", json.loads((TEMPLATES / "assembly" / descriptor["id"] /
                                                             "assembly.json").read_text(encoding="utf-8")))
        digest = self.runtime["_assembly_generation_digest"]
        with tempfile.TemporaryDirectory(prefix="icax-assembly-cache-") as temp:
            root = Path(temp)
            directory = root / "assembly/sample"
            tool = root / "mold/tool"
            directory.mkdir(parents=True)
            tool.mkdir(parents=True)
            descriptor = {"partProcesses": [{"resource": {"id": "tool"}}]}
            manifest = directory / "assembly.json"
            manifest.write_text("{}", encoding="utf-8")
            script = directory / "assembly.py"
            script.write_text("pass\n", encoding="utf-8")
            cutter = tool / "tool.py"
            cutter.write_text("pass\n", encoding="utf-8")
            with patch.dict(digest.__globals__, {"ROOT": root / "assembly", "PROCESS_ROOT": root / "mold"}):
                initial = digest(directory, descriptor, "shared-1")
                self.assertEqual(initial, digest(directory, descriptor, "shared-1"))
                script.write_text("pass # changed\n", encoding="utf-8")
                changed = digest(directory, descriptor, "shared-1")
                self.assertNotEqual(initial, changed)
                cutter.write_text("pass # changed tool\n", encoding="utf-8")
                self.assertNotEqual(changed, digest(directory, descriptor, "shared-1"))
                self.assertNotEqual(digest(directory, descriptor, "shared-1"),
                                    digest(directory, descriptor, "shared-2"))


if __name__ == "__main__":
    unittest.main()
