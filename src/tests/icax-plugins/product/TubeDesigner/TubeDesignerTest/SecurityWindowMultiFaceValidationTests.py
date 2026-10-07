"""Public-input regressions for active tube roles and real insertion boundaries.

These tests use the current descriptor field set as the host. Expectations follow
physical joint dimensions; no template validation helper is called as an oracle.
Native descriptor and BRep acceptance are covered by the companion JS suite.
"""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
PACKAGE = ROOT / "src/apps/tube-designer/templates/product/single_face_security_window"
DESCRIPTOR = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
DEFINITIONS = {parameter["key"]: parameter for parameter in DESCRIPTOR["parameters"]}
DEFAULTS = {key: deepcopy(parameter["defaultValue"]) for key, parameter in DEFINITIONS.items()}
SPEC = importlib.util.spec_from_file_location("security_window_validation_subject", PACKAGE / "template.py")
assert SPEC is not None and SPEC.loader is not None
SUBJECT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUBJECT
SPEC.loader.exec_module(SUBJECT)
from WindowManufacturingInputTests import execute, forbid_product_processing
from icax_template_sdk.resources import expand_resource_model
FACES = ("single", "two", "three", "five")
PATTERNS = ("vertical", "horizontal", "grid")
PURPOSES = ("display", "manufacturing")


def parameters(**updates):
    result = deepcopy(DEFAULTS)
    result.update(tubeSpecificationPreset="custom", accessDoorEnabled=False)
    result.update(updates)
    assert set(result) == set(DEFINITIONS), "Test input must stay within the public schema"
    for key, value in updates.items():
        constraints = DEFINITIONS[key].get("constraints", {})
        if isinstance(value, (float, int)) and not isinstance(value, bool):
            assert value >= constraints.get("minimum", -float("inf")), key
            assert value <= constraints.get("maximum", float("inf")), key
    return result


def physical_shape(document):
    """Compare actual solids and part placements, excluding the input bag."""
    return {
        "geometry": document["geometry"],
        "parts": [{key: value for key, value in item.items()
                   if key in {"key", "representations", "transform", "placement"}}
                  for item in document["items"]],
    }


def component_shape(document, prefix):
    """Compare only real geometry reachable from one independent component."""
    parts = [item for item in document['items'] if item['key'].startswith(prefix)]
    assert parts, prefix
    nodes = {node['key']: node for node in document['geometry']}
    pending = [root for item in parts for root in item['representations'].values()
               if isinstance(root, str)]
    assert pending, prefix
    reachable = {}
    while pending:
        key = pending.pop()
        if key in reachable:
            continue
        reachable[key] = nodes[key]
        pending.extend(nodes[key]['inputs'])
    return {'geometry': reachable, 'parts': [{key: value for key, value in item.items()
            if key in {'key', 'representations', 'transform', 'placement'}} for item in parts]}


class SecurityWindowMultiFaceValidationTests(unittest.TestCase):
    def build(self, purpose, **updates):
        original = parameters(**updates)
        request = deepcopy(original)
        try:
            with forbid_product_processing():
                design = SUBJECT.display(request)
                declaration = SUBJECT.manufacturing(request) if purpose == 'manufacturing' else None
                construction = SUBJECT._generate_resource_document(request, {
                    'template': DESCRIPTOR, 'geometryPurpose': 'display'})
            self.assertEqual(original, construction['parameters'])
            # Current declarations intentionally omit the host parameter bag.
            # The test envelope uses the actual independent construction echo;
            # downstream geometry is supplied only by the public declaration.
            document = expand_resource_model(design if purpose == 'display'
                else execute(declaration, design))
            self.assertEqual(document['parameters'], {})
            document['parameters'] = deepcopy(construction['parameters'])
        finally:
            self.assertEqual(original, request, "Generation must not mutate host input, including on rejection")
        self.assertEqual(original, document["parameters"], "Internal construction fields must not leak")
        self.assertEqual(set(DEFINITIONS), set(document['parameters']))
        self.assertTrue(document["items"])
        return document

    def assert_inactive(self, purpose, baseline, draft):
        a = self.build(purpose, **baseline)
        b = self.build(purpose, **dict(baseline, **draft))
        self.assertEqual(physical_shape(a), physical_shape(b), "Hidden draft changed actual geometry")

    def assert_insertion_rejected(self, purpose, **updates):
        if purpose == "display":
            # Insertion is a manufacturing operation. Even an impossible
            # reserve must not change or reject the independent product.
            document = self.build(purpose, **updates)
            baseline = self.build(purpose, **{key: value for key, value in updates.items()
                                             if key not in {"verticalBranchReserve", "horizontalBranchReserve"}})
            self.assertEqual(physical_shape(document), physical_shape(baseline))
            return
        with self.assertRaisesRegex(ValueError, "入榫.*(管壁|对侧)|插接入榫.*大于0"):
            self.build(purpose, **updates)

    def test_all_face_direction_and_opening_combinations_preserve_public_input(self):
        for face in FACES:
            for pattern in PATTERNS:
                for opening in (False, True):
                    for purpose in PURPOSES:
                        with self.subTest(face=face, pattern=pattern, opening=opening, purpose=purpose):
                            self.build(purpose, faceType=face, infillPattern=pattern,
                                       accessDoorEnabled=opening)

    def test_unused_main_tube_roles_keep_impossible_drafts_without_effect(self):
        drafts = {
            "vertical": [{"horizontalWidth": 100}, {"horizontalDepth": 100},
                         {"horizontalCornerRadius": 50}, {"horizontalWallThickness": 50}],
            "horizontal": [{"verticalWidth": 100}, {"verticalWallThickness": 50},
                           {"verticalProfileType": "rect", "verticalDepth": 100},
                           {"verticalProfileType": "rect", "verticalCornerRadius": 50}],
        }
        # Five-face roofs and floors always retain both tube families.
        for face in ("single", "two", "three"):
            for pattern, values in drafts.items():
                for purpose in PURPOSES:
                    for draft in values:
                        with self.subTest(face=face, pattern=pattern, purpose=purpose, draft=draft):
                            self.assert_inactive(purpose, dict(faceType=face, infillPattern=pattern), draft)

    def test_disabled_opening_profiles_are_not_loaded_or_applied(self):
        drafts = []
        for prefix in ("doorFrame", "doorLeafFrame", "doorHorizontal"):
            drafts.extend([{prefix + "Width": 100}, {prefix + "Depth": 100},
                           {prefix + "CornerRadius": 50}, {prefix + "WallThickness": 50}])
        drafts.extend([{"doorVerticalWidth": 100}, {"doorVerticalWallThickness": 50}])
        for face in FACES:
            for pattern in PATTERNS:
                for purpose in PURPOSES:
                    for draft in drafts:
                        with self.subTest(face=face, pattern=pattern, purpose=purpose, draft=draft):
                            self.assert_inactive(purpose, dict(faceType=face, infillPattern=pattern), draft)

    def test_unused_opening_bar_roles_ignore_impossible_drafts(self):
        choices = {
            "vertical": [{"doorHorizontalWidth": 100}, {"doorHorizontalDepth": 100},
                         {"doorHorizontalCornerRadius": 50}, {"doorHorizontalWallThickness": 50}],
            "horizontal": [{"doorVerticalWidth": 100}, {"doorVerticalWallThickness": 50}],
        }
        for face in FACES:
            for pattern, drafts in choices.items():
                for purpose in PURPOSES:
                    for draft in drafts:
                        with self.subTest(face=face, pattern=pattern, purpose=purpose, draft=draft):
                            self.assert_inactive(purpose,
                                dict(faceType=face, infillPattern=pattern, accessDoorEnabled=True), draft)

    def test_five_face_cap_profiles_remain_active_for_every_facade_direction(self):
        for purpose in PURPOSES:
            for pattern in PATTERNS:
                for prefix in ("horizontal", "vertical"):
                    with self.subTest(pattern=pattern, purpose=purpose, prefix=prefix):
                        with self.assertRaisesRegex(ValueError, "壁厚|内腔|杆件|尺寸必须为大于零"):
                            self.build(purpose, faceType="five", infillPattern=pattern,
                                       **{prefix + "WallThickness": 50})

    def test_round_main_tube_ignores_rectangular_depth_and_radius_drafts(self):
        for face in FACES:
            for pattern in ("vertical", "grid"):
                for purpose in PURPOSES:
                    with self.subTest(face=face, pattern=pattern, purpose=purpose):
                        self.assert_inactive(purpose,
                            dict(faceType=face, infillPattern=pattern, verticalProfileType="round"),
                            dict(verticalDepth=100, verticalCornerRadius=50))

    def test_inner_wall_boundary_is_strict_for_every_face_and_both_bar_directions(self):
        # Frame wall 1.2 + unilateral clearance 0.1 = 1.3 mm.
        for face in FACES:
            for key, pattern in (("horizontalBranchReserve", "horizontal"),
                                 ("verticalBranchReserve", "vertical")):
                for purpose in PURPOSES:
                    for depth in (1.29, 1.3):
                        with self.subTest(face=face, key=key, purpose=purpose, rejected_depth=depth):
                            self.assert_insertion_rejected(purpose, faceType=face, infillPattern=pattern,
                                                           **{key: depth})
                    with self.subTest(face=face, key=key, purpose=purpose, accepted_depth=1.31):
                        self.build(purpose, faceType=face, infillPattern=pattern, **{key: 1.31})

    def test_zero_depth_retains_vertical_butt_but_rejects_horizontal_insertion(self):
        for face in FACES:
            for purpose in PURPOSES:
                with self.subTest(face=face, purpose=purpose):
                    self.assert_insertion_rejected(purpose, faceType=face, infillPattern="horizontal",
                                                   horizontalBranchReserve=0)
                    self.build(purpose, faceType=face, infillPattern="grid", verticalBranchReserve=0)

    def test_far_wall_boundary_uses_available_receiver_span(self):
        # Smaller sections expose the far-wall boundary inside the public 20 mm limit.
        fixture = dict(frameWidth=20, frameDepth=20, frameCornerRadius=1,
                       horizontalWidth=12, horizontalDepth=12, horizontalCornerRadius=1,
                       verticalWidth=8, verticalWallThickness=0.8)
        # 20 - wall 1.2 - clearance 0.1 = 18.7 mm; touching is rejected.
        for face in FACES:
            for purpose in PURPOSES:
                for depth in (18.7, 18.71):
                    with self.subTest(face=face, purpose=purpose, rejected_depth=depth):
                        self.assert_insertion_rejected(purpose, faceType=face, infillPattern="vertical",
                                                       verticalBranchReserve=depth, **fixture)
                with self.subTest(face=face, purpose=purpose, accepted_depth=18.69):
                    self.build(purpose, faceType=face, infillPattern="vertical",
                               verticalBranchReserve=18.69, **fixture)
        for purpose in PURPOSES:
            for depth in (18.7, 18.71):
                self.assert_insertion_rejected(purpose, faceType="single", infillPattern="horizontal",
                                               horizontalBranchReserve=depth, **fixture)
            self.build(purpose, faceType="single", infillPattern="horizontal",
                       horizontalBranchReserve=18.69, **fixture)

    def test_corner_post_insertion_checks_depth_axis_for_rectangular_sections(self):
        fixture = dict(frameWidth=38, frameDepth=15, horizontalWidth=8, horizontalDepth=8,
                       horizontalCornerRadius=1, horizontalWallThickness=0.8,
                       verticalWidth=6, verticalWallThickness=0.5)
        # Side rods enter one corner post across depth, not its wider 38 mm face.
        # 15 - wall 1.2 - clearance 0.1 = 13.7 mm is the relevant far wall.
        for face in ("two", "three", "five"):
            for purpose in PURPOSES:
                self.build(purpose, faceType=face, infillPattern="horizontal",
                           horizontalBranchReserve=5, **fixture)
                self.build(purpose, faceType=face, infillPattern="horizontal",
                           horizontalBranchReserve=13.69, **fixture)
                for depth in (13.7, 13.71):
                    with self.subTest(face=face, purpose=purpose, depth=depth):
                        self.assert_insertion_rejected(purpose, faceType=face, infillPattern="horizontal",
                                                       horizontalBranchReserve=depth, **fixture)

    def test_leaf_receiver_limits_use_leaf_wall_and_clearance(self):
        fixture = dict(accessDoorEnabled=True, infillPattern="vertical",
                       doorLeafFrameWidth=10, doorLeafFrameDepth=20, doorLeafFrameCornerRadius=1,
                       doorVerticalWidth=6, doorVerticalWallThickness=0.5)
        # The 10 mm leaf receiver owns its mid-section insertion (5 mm),
        # which crosses its 1 mm wall and preserves the opposite wall. A main
        # grid's 8.9 mm reserve must not be applied to this independent leaf.
        for face in ("two", "three", "five"):
            for purpose in PURPOSES:
                with self.subTest(face=face, purpose=purpose):
                    baseline = self.build(purpose, faceType=face, verticalBranchReserve=8.89, **fixture)
                    changed = self.build(purpose, faceType=face, verticalBranchReserve=8.91, **fixture)
                    self.assertEqual(component_shape(baseline, 'access_door.leaf.'),
                                     component_shape(changed, 'access_door.leaf.'))

    def test_leaf_inner_wall_boundary_uses_actual_receiver_wall(self):
        fixture = dict(accessDoorEnabled=True, infillPattern="vertical",
                       doorLeafFrameWidth=25, doorLeafFrameDepth=25,
                       doorVerticalWidth=6,
                       doorVerticalWallThickness=0.5)
        # The actual 25 mm leaf owns a 12.5 mm mid-section insertion. A 6 mm
        # bar with .1 mm clearance needs strictly more than 6.2 mm receiver
        # inner space: wall 9.4 mm touches that limit; 9.39 mm remains valid.
        for face in ("two", "three", "five"):
            for purpose in PURPOSES:
                for wall in (9.4, 9.41):
                    with self.subTest(face=face, purpose=purpose, wall=wall):
                        if purpose == 'display':
                            self.build(purpose, faceType=face, doorLeafFrameWallThickness=wall, **fixture)
                        else:
                            with self.assertRaisesRegex(ValueError, '无法安全穿管|入榫.*管壁'):
                                self.build(purpose, faceType=face, doorLeafFrameWallThickness=wall, **fixture)
                with self.subTest(face=face, purpose=purpose, acceptedWall=9.39):
                    self.build(purpose, faceType=face, doorLeafFrameWallThickness=9.39, **fixture)


if __name__ == "__main__":
    unittest.main()
