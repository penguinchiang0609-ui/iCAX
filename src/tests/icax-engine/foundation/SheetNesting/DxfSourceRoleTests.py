"""Source-role resolution checks; no material repair or packing is performed."""
import importlib.util
from copy import deepcopy
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("dxf_source_roles", ROOT / "tools/sheet-nesting/dxf_source_roles.py")
roles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(roles)


def record(kind, *values):
    return {"type": kind, "values": list(values)}


def line(layer="0", linetype="BYLAYER", handle="A"):
    return record("LINE", (5, handle), (330, "MODEL"), (8, layer), (6, linetype),
                  (10, "-0.125"), (20, "7.25"), (11, "11.875"), (21, "7.25"))


def layer(name, linetype="CONTINUOUS"):
    return record("LAYER", (5, "L-" + name), (330, "LAYER-TABLE"), (2, name),
                  (6, linetype), (70, "0"), (62, "7"), (290, "1"))


def center(name="AXIS_STYLE"):
    return record("LTYPE", (5, "LT-AXIS"), (330, "LTYPE-TABLE"), (2, name),
                  (3, "Center (2x) long / short"), (72, "65"), (73, "4"), (40, "20.32"),
                  (49, "12.7"), (74, "0"), (49, "-2.54"), (74, "0"),
                  (49, "2.54"), (74, "0"), (49, "-2.54"), (74, "0"))


def profile_records():
    return [line(name, handle=str(i)) for i, name in enumerate(
        ("OUTER_PROFILES", "INTERIOR_PROFILES", "BEND", "BEND_EXTENT"))]


class SourceRoles(unittest.TestCase):
    def test_declared_definition_and_usage_form_only_a_candidate(self):
        original = line("0", "AXIS_STYLE")
        before = deepcopy(original)
        context = roles.SourceRoleContext([layer("0"), center()])
        annotated = context.annotate(original)
        resolved = annotated["_sourceRole"]
        self.assertEqual(resolved["roleCandidate"], "centerline")
        self.assertEqual(resolved["linetypeOrigin"], "explicit")
        self.assertEqual(resolved["linetypeDefinition"]["sourceTags"], center()["values"])
        self.assertTrue(resolved["requiresMaterialProof"])
        self.assertFalse(resolved["geometryExcluded"])
        self.assertFalse(resolved["sourceCoordinatesChanged"])
        self.assertEqual(original, before)
        self.assertEqual(annotated["values"], before["values"])

    def test_center_name_without_real_definition_is_not_a_role(self):
        resolved = roles.SourceRoleContext([layer("0")]).resolve(line("0", "CENTERX2"))
        self.assertIsNone(resolved["roleCandidate"])
        self.assertFalse(resolved["linetypeDefinitionPresent"])
        self.assertTrue(resolved["unresolvedReasons"])

    def test_center_layer_name_does_not_exclude_a_contour(self):
        resolved = roles.SourceRoleContext([layer("CENTER")]).resolve(line("CENTER"))
        self.assertEqual(resolved["effectiveLinetype"], "CONTINUOUS")
        self.assertIsNone(resolved["roleCandidate"])

    def test_center_style_does_not_reclassify_closed_holes(self):
        circle = record("CIRCLE", (8, "0"), (6, "AXIS_STYLE"), (10, "0"), (20, "0"), (40, "3"))
        self.assertIsNone(roles.SourceRoleContext([layer("0"), center()]).resolve(circle)["roleCandidate"])

    def test_bylayer_uses_the_effective_layers_definition(self):
        context = roles.SourceRoleContext([layer("AXES", "AXIS_STYLE"), center()])
        resolved = context.resolve(line("AXES"))
        self.assertEqual(resolved["roleCandidate"], "centerline")
        self.assertEqual(resolved["linetypeOrigin"], "layer")
        self.assertEqual(resolved["layerDefinition"]["name"], "AXES")

    def test_nested_layer_zero_and_byblock_inheritance(self):
        context = roles.SourceRoleContext([layer("AXES", "AXIS_STYLE"), layer("OTHER"), center()])
        outer = context.resolve(record("INSERT", (5, "I1"), (8, "AXES"), (6, "BYLAYER")), reference_path=("outer",))
        inner = context.resolve(record("INSERT", (5, "I2"), (8, "0"), (6, "BYBLOCK")), outer, ("outer", "inner"))
        child = context.resolve(line("0", "BYBLOCK"), inner, ("outer", "inner"))
        self.assertEqual(child["effectiveLayer"], "AXES")
        self.assertEqual(child["effectiveLinetype"], "AXIS_STYLE")
        self.assertEqual(child["roleCandidate"], "centerline")
        self.assertTrue(child["layerInherited"])
        self.assertEqual(child["inheritedFrom"]["sourceHandle"], "I2")
        self.assertEqual(child["referencePath"], ["outer", "inner"])
        explicit = context.resolve(line("OTHER", "BYLAYER"), inner, ("outer", "inner"))
        self.assertEqual(explicit["effectiveLayer"], "OTHER")
        self.assertEqual(explicit["effectiveLinetype"], "CONTINUOUS")
        self.assertIsNone(explicit["roleCandidate"])

    def test_two_instances_do_not_share_inherited_role_state(self):
        context = roles.SourceRoleContext([layer("AXES", "AXIS_STYLE"), layer("CUT"), center()])
        child = line("0", "BYBLOCK")
        first = context.resolve(record("INSERT", (8, "AXES")))
        second = context.resolve(record("INSERT", (8, "CUT")))
        self.assertEqual(context.resolve(child, first)["roleCandidate"], "centerline")
        self.assertIsNone(context.resolve(child, second)["roleCandidate"])
        self.assertEqual(context.resolve(child, first)["effectiveLayer"], "AXES")

    def test_unresolved_byblock_is_not_assumed_center_or_continuous(self):
        resolved = roles.SourceRoleContext([layer("0"), center()]).resolve(line("0", "BYBLOCK"))
        self.assertIsNone(resolved["effectiveLinetype"])
        self.assertIsNone(resolved["roleCandidate"])
        self.assertTrue(resolved["unresolvedReasons"])

    def test_missing_layer_definition_remains_explicit(self):
        resolved = roles.SourceRoleContext().resolve(line("UNDECLARED"))
        self.assertFalse(resolved["layerDefinitionPresent"])
        self.assertIsNone(resolved["effectiveLinetype"])
        self.assertTrue(resolved["unresolvedReasons"])

    def test_joint_profile_convention_does_not_need_invented_layer_definitions(self):
        originals = profile_records()
        context = roles.SourceRoleContext([layer("0")], originals)
        self.assertTrue(context.profile_convention)
        self.assertEqual([context.resolve(r)["roleCandidate"] for r in originals],
                         ["outer-profile", "inner-profile", "bend", "bend-extent"])
        self.assertFalse(context.resolve(originals[0])["layerDefinitionPresent"])
        self.assertTrue(context.resolve(originals[0])["roleEvidence"]["declaredByEntityLayerTags"])

    def test_single_bend_or_table_labels_do_not_enable_profile_convention(self):
        all_layers = [layer(name) for name in ("OUTER_PROFILES", "INTERIOR_PROFILES", "BEND", "BEND_EXTENT")]
        context = roles.SourceRoleContext(all_layers, [line("BEND")])
        self.assertFalse(context.profile_convention)
        self.assertIsNone(context.resolve(line("BEND"))["roleCandidate"])

    def test_unused_historical_block_cannot_establish_active_convention(self):
        blocks = [record("BLOCK", (2, "old")), *profile_records(), record("ENDBLK")]
        context = roles.SourceRoleContext([], [line()], blocks)
        self.assertFalse(context.profile_convention)
        active = roles.SourceRoleContext([], [record("INSERT", (2, "old"))], blocks)
        self.assertTrue(active.profile_convention)

    def test_cycle_does_not_enable_partial_convention(self):
        blocks = [record("BLOCK", (2, "recursive")), *profile_records(),
                  record("INSERT", (2, "recursive")), record("ENDBLK")]
        context = roles.SourceRoleContext([], [record("INSERT", (2, "recursive"))], blocks)
        self.assertFalse(context.profile_convention)
        self.assertTrue(context.diagnostics)

    def test_source_owner_excludes_reactor_handles(self):
        original = line("0", "AXIS_STYLE")
        original["values"][1:2] = [(102, "{ACAD_REACTORS"), (330, "REACTOR-A"), (330, "REACTOR-B"),
                                    (102, "}"), (330, "MODEL")]
        resolved = roles.SourceRoleContext([layer("0"), center()]).resolve(original)
        self.assertEqual(resolved["sourceOwner"], "MODEL")
        self.assertEqual(resolved["sourceOwnerCandidates"], ["MODEL"])
        self.assertEqual(resolved["sourceTags"], original["values"])

    def test_original_preprojection_tags_are_retained(self):
        original = line()
        original["_sourceValues"] = [*original["values"], (30, "0.00000000000001")]
        result = roles.SourceRoleContext([layer("0")]).resolve(original)
        original["_sourceValues"].clear()
        self.assertEqual(result["sourceTags"][-1], (30, "0.00000000000001"))

    def test_duplicate_or_forged_center_definition_remains_unclassified(self):
        duplicate = center("axis_style")
        context = roles.SourceRoleContext([layer("0"), center(), duplicate])
        result = context.resolve(line("0", "AXIS_STYLE"))
        self.assertIsNone(result["roleCandidate"])
        self.assertTrue(result["unresolvedReasons"])
        forged = center()
        forged["values"] = [(c, "Continuous" if c == 3 else v) for c, v in forged["values"]]
        self.assertIsNone(roles.SourceRoleContext([layer("0"), forged]).resolve(line("0", "AXIS_STYLE"))["roleCandidate"])

    def test_complex_or_inconsistent_pattern_does_not_prove_center_role(self):
        for change in ([(73, "3")], [(340, "SHAPE-FONT")], [(75, "1")], [(40, "99")]):
            altered = center()
            codes = {c for c, _v in change}
            altered["values"] = [pair for pair in altered["values"] if pair[0] not in codes] + change
            self.assertIsNone(roles.SourceRoleContext([layer("0"), altered]).resolve(line("0", "AXIS_STYLE"))["roleCandidate"])

    def test_frame_name_and_rectangle_never_authorize_material_removal(self):
        frame = record("LWPOLYLINE", (8, "图框"), (70, "1"), (90, "4"),
                       (10, "0"), (20, "0"), (10, "1000"), (20, "0"),
                       (10, "1000"), (20, "700"), (10, "0"), (20, "700"))
        resolved = roles.SourceRoleContext([layer("图框")], [frame]).resolve(frame)
        self.assertIsNone(resolved["roleCandidate"])
        self.assertFalse(resolved["geometryExcluded"])
        self.assertEqual(resolved["sourceTags"], frame["values"])


if __name__ == "__main__":
    unittest.main()
