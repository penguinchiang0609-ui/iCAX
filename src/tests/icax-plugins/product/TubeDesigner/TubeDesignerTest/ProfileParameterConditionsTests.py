import importlib.util
from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[5] / "apps/tube-designer/templates/profile"


class ProfileParameterConditionsTests(unittest.TestCase):
    def test_four_guardrail_families_independent_bay_counts(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "iCAX-Engine/framework/TemplateRuntime/python"))
        from icax_template_worker import _load_template
        for suffix in ("", "_glass-straight", "_diamond-straight", "_cross-straight"):
            directory = ROOT.parent / "product" / ("modular_guardrail" + suffix)
            descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
            p = {f["key"]: f["defaultValue"] for f in descriptor["parameters"]}
            module = _load_template(str(directory / "template.py"), "family-bays" + suffix)
            for shape, counts in (("straight", [2]), ("left_l", [2, 3]), ("right_l", [2, 3]), ("u", [2, 3, 2])):
                values = dict(p, layout=shape, sideBayCount1=2, sideBayCount2=3, sideBayCount3=2)
                with self.subTest(family=suffix, shape=shape):
                    built = module.build_layout(values)
                    self.assertEqual([sum(b.key.startswith(f"segment.{i + 1}.") for b in built.bays) for i in range(len(counts))], counts)
                    for purpose in ("display", "manufacturing"):
                        self.assertTrue(module.generate(values, {"template": descriptor, "geometryPurpose": purpose})["items"])
            if suffix:
                with self.assertRaises(ValueError):
                    module.build_layout(dict(p, guardrailUse="wall"))
            else:
                self.assertTrue(module.build_layout(dict(p, guardrailUse="wall")).bays)
            with self.assertRaises(ValueError):
                module.build_layout(dict(p, sideBayCount1=1.5))
            with self.assertRaises(ValueError):
                module.build_layout(dict(p, sideBayCount1=0))
            # Inactive sides must not affect a straight run; corner mode must
            # not silently add bays to an explicitly divided side.
            self.assertEqual(len(module.build_layout(dict(p, layout="straight", sideBayCount1=2, sideBayCount2=-1)).bays), 2)
            self.assertEqual(len(module.build_layout(dict(p, layout="u", cornerPostMode="double", sideBayCount1=2, sideBayCount2=3, sideBayCount3=2)).bays), 7)
            with self.assertRaises(ValueError):
                module.build_layout(dict(p, largePostMode="middle", sideBayCount1=1))
            if suffix == "_glass-straight":
                result = module.generate(dict(p, glassMaterial="亚克力", materialGrade="Q235B"), {"template": descriptor, "geometryPurpose": "manufacturing"})
                panels = [item for item in result["items"] if item.get("properties", {}).get("manufacturing.categoryName") == "挡板"]
                self.assertTrue(panels)
                for item in panels:
                    self.assertEqual(item["properties"]["manufacturing.material"], "亚克力")
                    self.assertNotEqual(item["properties"].get("manufacturing.materialGrade"), "Q235B")
                    self.assertEqual(item["properties"]["manufacturing.partKind"], "plate")

    def test_glass_layouts_generate_without_wall_options(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "iCAX-Engine/framework/TemplateRuntime/python"))
        from icax_template_worker import _load_template
        for name in ("straight", "left-l"):
            directory = ROOT.parent / "product" / ("modular_guardrail_glass-" + name)
            descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
            parameters = {p["key"]: p["defaultValue"] for p in descriptor["parameters"]}
            module = _load_template(str(directory / "template.py"), "glass-layout-" + name)
            for layout in ("straight", "left_l", "right_l", "u"):
                for purpose in ("display", "manufacturing"):
                    with self.subTest(template=name, layout=layout, purpose=purpose):
                        result = module.generate(dict(parameters, layout=layout), {"geometryPurpose": purpose, "template": descriptor})
                        self.assertTrue(result["items"])
            with self.assertRaisesRegex(ValueError, "围墙"):
                module.generate(dict(parameters, guardrailUse="wall"), {"template": descriptor})

    def test_all_product_defaults_generate_in_both_purposes(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "iCAX-Engine/framework/TemplateRuntime/python"))
        from icax_template_worker import _load_template
        count = 0
        for path in sorted((ROOT.parent / "product").glob("*/template.json")):
            descriptor = json.loads(path.read_text(encoding="utf-8"))
            parameters = {p["key"]: p["defaultValue"] for p in descriptor["parameters"]}
            module = _load_template(str(path.with_name("template.py")), "condition-product-" + path.parent.name)
            for purpose in ("display", "manufacturing"):
                with self.subTest(product=path.parent.name, purpose=purpose):
                    before = deepcopy(parameters)
                    if descriptor["id"] == "aluminium-window" and purpose == "manufacturing":
                        with self.assertRaisesRegex(ValueError, "生产拆单"):
                            module.generate(parameters, {"geometryPurpose": purpose, "template": descriptor})
                        self.assertEqual(parameters, before)
                        continue
                    result = module.generate(parameters, {"geometryPurpose": purpose, "template": descriptor})
                    self.assertTrue(result["items"])
                    self.assertEqual(parameters, before, "Template must not mutate caller parameters")
                    self.assertEqual(result["parameters"], before, "Returned parameters must match the host normalized inputs exactly")
            count += 1
        self.assertEqual(count, 40)

    def test_staircase_disabled_railings_ignore_hidden_profile_values(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "iCAX-Engine/framework/TemplateRuntime/python"))
        from icax_template_worker import _load_template
        for name in ("straight",):
            directory = ROOT.parent / "product" / (name + "_steel_staircase")
            descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
            parameters = {p["key"]: p["defaultValue"] for p in descriptor["parameters"]}
            parameters.update(railingSide="none", handrailProfileType="inactive", postProfileType="inactive",
                              railingHeight=float("nan"), maximumPostSpacing=float("nan"))
            module = _load_template(str(directory / "template.py"), "condition-stair-" + name)
            for purpose in ("display", "manufacturing"):
                result = module.generate(parameters, {"geometryPurpose": purpose, "template": {}})
                self.assertTrue(result["items"])
                self.assertFalse(any("handrail" in item["key"] or ".post." in item["key"] for item in result["items"]))

    def test_all_profile_defaults_generate_and_inactive_boundary_is_retained(self):
        count = 0
        for path in sorted(ROOT.glob("*/profile.json")):
            descriptor = json.loads(path.read_text(encoding="utf-8"))
            parameters = {p["key"]: p["defaultValue"] for p in descriptor["parameters"]}
            name = "condition_profile_" + path.parent.name.replace("-", "_")
            spec = importlib.util.spec_from_file_location(name, path.with_name("profile.py"),
                                                        submodule_search_locations=[str(path.parent)])
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
            with self.subTest(profile=path.parent.name):
                result = module.build(parameters)
                self.assertTrue(result["contours"])
                if path.parent.name in {"p-tube"}:
                    retained = dict(parameters, materialBoundary="inactive draft", sourceRevision="retained")
                    self.assertEqual(module.build(retained)["contours"], result["contours"])
                    self.assertEqual(retained["materialBoundary"], "inactive draft")
            count += 1
        self.assertEqual(count, 23)
