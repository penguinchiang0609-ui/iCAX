"""Public formed previews must use the same effective section as their blank."""
import copy
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5] / "apps" / "tube-designer" / "templates"
TEMPLATES = ("flexible-slit-bend-integrated", "segmented-bend", "node-embedded-arc-integrated")


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AssemblyFormedEffectiveSectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load(ROOT / "_shared" / "assembly_template_runtime.py", "formed_effective_runtime")
        cls.scripts = {name: load(ROOT / "assembly" / name / "assembly.py", name.replace("-", "_"))
                       for name in TEMPLATES}

    def product(self, name):
        return self.runtime.get_example_product(name)["finishedProduct"]

    def preview(self, name, product):
        before = copy.deepcopy(product)
        defaults = {p["key"]: p["defaultValue"] for p in self.runtime._template_by_id(name)["parameters"]
                    if p.get("scope") != "scene"}
        parameters_before = copy.deepcopy(defaults)
        plan = self.runtime.preview_plan(name, defaults, finished_product=product)
        self.assertEqual(product, before, "preview must keep the original product and inactive drafts")
        self.assertEqual(defaults, parameters_before)
        self.assertEqual(plan["finishedProduct"], before)
        self.assertEqual(plan["parameters"], parameters_before, "effective section values are internal queries")
        return plan

    def test_independent_uniform_radii_match_the_effective_blank_and_formed_mesh(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                independent = self.product(name)
                uniform = copy.deepcopy(independent)
                for part in independent["spans"].values():
                    p = part["parameters"]
                    p.update(useOuterRadii=True, useInnerRadii=True)
                    p.update({f"outerRadius{i}": 7 for i in range(1, 5)})
                    p.update({f"innerRadius{i}": 5 for i in range(1, 5)})
                for part in uniform["spans"].values():
                    part["parameters"].update(cornerRadius=7, innerRadius=5)
                actual = self.preview(name, independent)
                reference = self.preview(name, uniform)
                section = self.scripts[name]._section(actual)
                self.assertEqual((section["outerRadius"], section["innerRadius"]), (7, 5))
                self.assertEqual(actual["formedPreviewMesh"], reference["formedPreviewMesh"])
                old_uniform = self.preview(name, self.product(name))
                self.assertNotEqual(actual["formedPreviewMesh"]["positions"], old_uniform["formedPreviewMesh"]["positions"])

    def test_inactive_uniform_radius_drafts_do_not_block_independent_radii(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                product = self.product(name)
                for index, part in enumerate(product["spans"].values()):
                    p = part["parameters"]
                    p.update(useOuterRadii=True, useInnerRadii=True,
                             cornerRadius=-99999 - index, innerRadius=-88888 - index)
                    p.update({f"outerRadius{i}": 7 for i in range(1, 5)})
                    p.update({f"innerRadius{i}": 5 for i in range(1, 5)})
                plan = self.preview(name, product)
                self.assertEqual(self.scripts[name]._section(plan)["outerRadius"], 7)
                self.assertEqual(self.scripts[name]._section(plan)["innerRadius"], 5)

    def test_inactive_independent_radius_drafts_do_not_change_uniform_preview(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                clean = self.product(name)
                product = copy.deepcopy(clean)
                for index, part in enumerate(product["spans"].values()):
                    p = part["parameters"]
                    p.update(useOuterRadii=False, useInnerRadii=False)
                    p.update({f"outerRadius{i}": -99999 - index * i for i in range(1, 5)})
                    p.update({f"innerRadius{i}": -88888 - index * i for i in range(1, 5)})
                actual = self.preview(name, product)
                reference = self.preview(name, clean)
                self.assertEqual(actual["formedPreviewMesh"], reference["formedPreviewMesh"])

    def test_active_section_differences_are_still_rejected(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                product = self.product(name)
                product["spans"]["armB"]["parameters"]["cornerRadius"] = 7
                before = copy.deepcopy(product)
                self.assertFalse(self.runtime.check_applicability(name, product)["applicable"])
                with self.assertRaises(ValueError):
                    self.runtime.preview_plan(name, finished_product=product)
                self.assertEqual(product, before)

    def test_formed_script_preserves_uniform_and_centered_section_requirements(self):
        for name in TEMPLATES:
            with self.subTest(template=name):
                plan = self.preview(name, self.product(name))
                for part in plan["designParts"]:
                    p = part["request"]["parameters"]
                    p.update(useOuterRadii=True)
                    p.update({f"outerRadius{i}": 7 + (i == 1) for i in range(1, 5)})
                with self.assertRaisesRegex(ValueError, "统一"):
                    self.scripts[name]._section(plan)
                for part in plan["designParts"]:
                    p = part["request"]["parameters"]
                    p.update(useOuterRadii=False, innerOffsetX=0.5)
                with self.assertRaisesRegex(ValueError, "居中"):
                    self.scripts[name]._section(plan)


if __name__ == "__main__":
    unittest.main()
