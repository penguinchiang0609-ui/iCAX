import importlib.util
from copy import deepcopy
from itertools import product
import json
import math
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[5] / "apps/tube-designer/templates/profile"
sys.dont_write_bytecode = True
RUNTIME_SPEC = importlib.util.spec_from_file_location(
    "condition_profile_runtime", ROOT.parent / "_shared/profile_package_runtime.py")
RUNTIME = importlib.util.module_from_spec(RUNTIME_SPEC)
RUNTIME_SPEC.loader.exec_module(RUNTIME)


def evaluate_profile(directory, values):
    descriptor = json.loads((directory / "profile.json").read_text(encoding="utf-8"))
    return RUNTIME.generate({
        "action": "evaluate", "descriptor": descriptor,
        "scriptSource": (directory / "profile.py").read_text(encoding="utf-8"),
        "resources": RUNTIME._directory_resources(directory),
        "values": values, "packageDigest": "condition-profile-regression",
    }, {})["profile"]


def condition_contexts(definition, defaults, fields):
    """Enumerate only descriptor-declared controlling choices, without names."""
    references = {}

    def visit(condition):
        if not isinstance(condition, dict):
            return
        key = condition.get("parameter")
        if key in fields:
            values = references.setdefault(key, [defaults[key]])
            candidates = condition.get("values", [condition.get("value")])
            candidates += [option.get("value") if isinstance(option, dict) else option
                           for option in fields[key].get("options", [])]
            if fields[key]["valueType"] == "boolean":
                candidates += [True, False]
            for value in candidates:
                if value is not None and not any(type(value) is type(old) and value == old for old in values):
                    values.append(value)
        for child in condition.get("conditions", []):
            visit(child)
        for child in condition.get("all", []) + condition.get("any", []):
            visit(child)
        visit(condition.get("condition", condition.get("not")))

    visit(definition.get("visibleWhen"))
    visit(definition.get("enabledWhen"))
    for choices in product(*references.values()):
        yield {**defaults, **dict(zip(references, choices))}


class ProfileParameterConditionsTests(unittest.TestCase):
    def test_rect_runtime_retains_disabled_radii_without_changing_contours(self):
        directory = ROOT / "rect"
        descriptor = json.loads((directory / "profile.json").read_text(encoding="utf-8"))
        defaults = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
        baseline = evaluate_profile(directory, defaults)
        draft = {**defaults, "useOuterRadii": False, "outerRadius1": -5,
                 "useInnerRadii": False, "innerRadius4": -7}
        before = deepcopy(draft)
        profile = evaluate_profile(directory, draft)
        self.assertEqual(profile["contours"], baseline["contours"])
        self.assertEqual(profile["parameters"], draft)
        self.assertEqual(draft, before)
        with self.assertRaisesRegex(ValueError, "outerRadius1 小于允许的最小值"):
            evaluate_profile(directory, {**draft, "useOuterRadii": True})
        with self.assertRaisesRegex(ValueError, "innerRadius4 小于允许的最小值"):
            evaluate_profile(directory, {**draft, "useInnerRadii": True})

    def test_every_profile_conditional_numeric_bound_keeps_inactive_drafts(self):
        paths = sorted(ROOT.glob("*/profile.json"))
        visited, checked = set(), []
        self.assertTrue(paths)
        for path in paths:
            descriptor, defaults = RUNTIME._validate_descriptor(json.loads(path.read_text(encoding="utf-8")))
            fields = {field["key"]: field for field in descriptor["parameters"]}
            self.assertEqual(RUNTIME._normalize_parameters(descriptor, defaults), defaults)
            baseline = evaluate_profile(path.parent, defaults)
            self.assertTrue(baseline["contours"])
            self.assertEqual(baseline["parameters"], defaults)
            visited.add(descriptor["id"])
            for field in descriptor["parameters"]:
                if field["valueType"] not in ("number", "integer") or not (field.get("visibleWhen") or field.get("enabledWhen")):
                    continue
                contexts = list(condition_contexts(field, defaults, fields))
                active = lambda values: (RUNTIME._condition_matches(field.get("visibleWhen"), values)
                                         and RUNTIME._condition_matches(field.get("enabledWhen"), values))
                disabled = next((values for values in contexts if not active(values)), None)
                enabled = next((values for values in contexts if active(values)), None)
                self.assertIsNotNone(disabled, f"{descriptor['id']}.{field['key']} must have an inactive condition")
                self.assertIsNotNone(enabled, f"{descriptor['id']}.{field['key']} must have an active condition")
                inactive_baseline = evaluate_profile(path.parent, disabled)
                for side, boundary in (("min", field.get("min", field.get("minimum"))),
                                       ("max", field.get("max", field.get("maximum")))):
                    if boundary is None:
                        continue
                    invalid = (math.floor(boundary) - 1 if side == "min" else math.ceil(boundary) + 1)
                    with self.subTest(profile=descriptor["id"], parameter=field["key"], bound=side):
                        draft = {**disabled, field["key"]: invalid}
                        before = deepcopy(draft)
                        normalized = RUNTIME._normalize_parameters(descriptor, draft)
                        self.assertEqual(normalized[field["key"]], invalid)
                        self.assertEqual(draft, before)
                        generated = evaluate_profile(path.parent, draft)
                        self.assertEqual(generated["parameters"], normalized)
                        self.assertEqual(generated["contours"], inactive_baseline["contours"])
                        message = "小于允许的最小值" if side == "min" else "大于允许的最大值"
                        with self.assertRaisesRegex(ValueError, field["key"] + " " + message):
                            evaluate_profile(path.parent, {**enabled, field["key"]: invalid})
                    checked.append((descriptor["id"], field["key"], side))
        self.assertEqual(visited, {json.loads(path.read_text(encoding="utf-8"))["id"] for path in paths})
        self.assertTrue(checked, "The catalog must contain actual conditional numeric bounds")

    def test_inactive_runtime_values_still_require_type_finiteness_and_choices(self):
        descriptor = {"parameters": [
            {"key": "enabled", "valueType": "boolean", "defaultValue": False},
            {"key": "number", "valueType": "number", "defaultValue": 3, "min": 0, "max": 4,
             "visibleWhen": {"op": "eq", "parameter": "enabled", "value": True}},
            {"key": "count", "valueType": "integer", "defaultValue": 1, "min": 1,
             "enabledWhen": {"op": "eq", "parameter": "enabled", "value": True}},
            {"key": "choice", "valueType": "string", "defaultValue": "one", "options": ["one", "two"],
             "visibleWhen": {"op": "eq", "parameter": "enabled", "value": True}},
        ]}
        retained = RUNTIME._normalize_parameters(descriptor, {"number": -5, "count": -7})
        self.assertEqual(retained["number"], -5)
        self.assertEqual(retained["count"], -7)
        for value in (float("nan"), float("inf"), "bad", True):
            with self.subTest(number=value), self.assertRaises(ValueError):
                RUNTIME._normalize_parameters(descriptor, {"number": value})
        with self.assertRaisesRegex(ValueError, "必须是整数"):
            RUNTIME._normalize_parameters(descriptor, {"count": 1.5})
        with self.assertRaisesRegex(ValueError, "不在允许选项中"):
            RUNTIME._normalize_parameters(descriptor, {"choice": "bad"})
        with self.assertRaisesRegex(ValueError, "未知参数"):
            RUNTIME._normalize_parameters(descriptor, {"undeclared": 0})
        hidden = deepcopy(descriptor)
        hidden["parameters"][1]["presentation"] = {"visible": False}
        self.assertEqual(RUNTIME._normalize_parameters(hidden, {"enabled": True, "number": -5})["number"], -5)

    def test_shared_profile_condition_matcher_handles_compound_and_membership(self):
        values = {"enabled": True, "mode": "two", "number": 1}
        one = {"op": "eq", "parameter": "enabled", "value": True}
        two = {"op": "in", "parameter": "mode", "values": ["one", "two"]}
        self.assertTrue(RUNTIME._condition_matches({"op": "all", "conditions": [one, two]}, values))
        self.assertTrue(RUNTIME._condition_matches({"op": "any", "conditions": [one, {"op": "ne", "parameter": "mode", "value": "two"}]}, values))
        self.assertFalse(RUNTIME._condition_matches({"op": "not", "condition": one}, values))
        self.assertTrue(RUNTIME._condition_matches({"op": "notIn", "parameter": "mode", "values": ["one"]}, values))
        self.assertFalse(RUNTIME._condition_matches({"op": "eq", "parameter": "number", "value": True}, values))
        self.assertFalse(RUNTIME._condition_matches({"op": "in", "parameter": "number", "values": [True]}, values))

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
                    result = module.generate(parameters, {"geometryPurpose": purpose, "template": descriptor})
                    self.assertTrue(result["items"])
                    self.assertEqual(parameters, before, "Template must not mutate caller parameters")
                    self.assertEqual(result["parameters"], before, "Returned parameters must match the host normalized inputs exactly")
            count += 1
        self.assertEqual(count, 10)

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
