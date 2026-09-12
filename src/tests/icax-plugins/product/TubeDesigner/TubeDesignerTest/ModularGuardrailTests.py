"""Pure-Python dimensional, graph and catalogue contracts; no native DLL loads."""
from __future__ import annotations

from itertools import combinations
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template

DIRECTORY = ROOT / "src/apps/tube-designer/templates/product/modular_guardrail"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {parameter["key"]: parameter["defaultValue"] for parameter in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "modular-guardrail-tests")
SUBJECT = sys.modules[MODULE.generate.__module__]
STYLE_DIRECTORIES = sorted(ROOT.joinpath("src/apps/tube-designer/templates/product").glob("modular_guardrail*/template.json"))
STYLE_DESCRIPTORS = [json.loads(path.read_text(encoding="utf-8")) for path in STYLE_DIRECTORIES]
PRESETS = [
    {
        "id": "r3-straight" if descriptor["id"] == "modular-guardrail"
        else descriptor["id"].removeprefix("modular-guardrail-"),
        "displayName": descriptor["displayName"],
        "parameters": {
            field["key"]: field["defaultValue"]
            for field in descriptor["parameters"]
            if field["defaultValue"] != DEFAULTS.get(field["key"])
        },
    }
    for descriptor in STYLE_DESCRIPTORS
]
LEGACY_PRESETS = PRESETS[:20]


def values(**changes):
    return {**DEFAULTS, **changes}


def bounds(tube):
    half = [abs(tube.x_axis[i]) * tube.profile.width / 2
            + abs(tube.y_axis[i]) * tube.profile.depth / 2 for i in range(3)]
    return ([min(tube.start[i], tube.end[i]) - half[i] for i in range(3)],
            [max(tube.start[i], tube.end[i]) + half[i] for i in range(3)])


def overlap(a, b):
    amin, amax = bounds(a)
    bmin, bmax = bounds(b)
    return all(min(amax[i], bmax[i]) - max(amin[i], bmin[i]) > 1e-6 for i in range(3))


class ModularGuardrailTests(unittest.TestCase):
    def test_all_styles_are_one_real_template_and_only_declared_parameters(self):
        self.assertEqual(DESCRIPTOR["id"], "modular-guardrail")
        self.assertEqual(len(STYLE_DESCRIPTORS), 32)
        self.assertEqual(len(PRESETS), 32)
        self.assertEqual(len({preset["id"] for preset in PRESETS}), 32)
        for preset in PRESETS:
            self.assertTrue(set(preset["parameters"]).issubset(DEFAULTS), preset["id"])
            self.assertTrue(preset["displayName"])
        self.assertTrue(all("presets" not in descriptor.get("extensions", {}).get("catalog", {})
                            for descriptor in STYLE_DESCRIPTORS))

    def test_every_preset_builds_complete_display_and_export_graph(self):
        for preset in PRESETS:
            with self.subTest(preset=preset["id"]):
                document = MODULE.generate(values(**preset["parameters"]), {})
                json.dumps(document, allow_nan=False)
                keys = {node["key"] for node in document["geometry"]}
                self.assertEqual(len(keys), len(document["geometry"]))
                for node in document["geometry"]:
                    self.assertTrue(set(node["inputs"]).issubset(keys))
                items = {item["key"] for item in document["items"]}
                self.assertEqual(len(items), len(document["items"]))
                for item in document["items"]:
                    self.assertTrue(set(item["representations"].values()).issubset(keys))
                    props = item["properties"]
                    if props["manufacturing.partKind"] == "tube":
                        self.assertGreater(props["length"], 0)
                        self.assertIn("schemaVersion", props["tubeDesigner.profile"])
                    elif props["manufacturing.partKind"] in {"plate", "glass"}:
                        self.assertNotIn("tubeDesigner.profile", props)
                        self.assertGreater(props["manufacturing.plate"]["areaMm2"], 0)
                    else:
                        self.assertEqual(props["manufacturing.partKind"], "accessory")
                        self.assertNotIn("tubeDesigner.profile", props)
                        self.assertIn("manufacturing.modelReference", props)
                for output in document["outputs"]:
                    self.assertEqual(set(output["items"]), items)

    def test_two_and_three_rails_both_have_vertical_bars(self):
        for count in (2, 3):
            built = MODULE.build_layout(values(railCount=count))
            self.assertTrue(any(part.category == "guardrail.vertical_bar" for part in built.tubes))
            cross = [part for part in built.tubes if part.category == "guardrail.cross_rail"]
            self.assertEqual(len(cross), len(built.bays) * (count - 1))

    def test_all_rectangular_presets_have_no_overlapping_part_envelopes(self):
        for preset in LEGACY_PRESETS:
            if preset["id"].startswith("round-"):
                continue
            with self.subTest(preset=preset["id"]):
                built = MODULE.build_layout(values(**preset["parameters"]))
                for a, b in combinations(built.tubes, 2):
                    self.assertFalse(overlap(a, b), (a.key, b.key))

    def test_each_bay_has_equal_clear_gaps_and_no_post_bar_collision(self):
        for preset in PRESETS[:18]:
            built = MODULE.build_layout(values(**preset["parameters"]))
            for bay in built.bays:
                with self.subTest(preset=preset["id"], bay=bay.key):
                    self.assertLessEqual(max(bay.clear_gaps), DEFAULTS["maximumVerticalClearGap"] + 1e-6)
                    self.assertAlmostEqual(min(bay.clear_gaps), max(bay.clear_gaps))
                    bars = [part for part in built.tubes if part.group == bay.key and part.category == "guardrail.vertical_bar"]
                    self.assertAlmostEqual(sum(bay.clear_gaps) + sum(part.profile.width for part in bars), bay.clear)

    def test_shared_corner_is_one_post_double_corner_is_two_nonoverlapping_posts(self):
        shared = MODULE.build_layout(values(layout="left_l"))
        double = MODULE.build_layout(values(layout="left_l", cornerPostMode="double"))
        self.assertEqual(len(double.posts), len(shared.posts) + 1)
        a, b = double.posts[3:5]
        gap_x = max(0, abs(a.point[0] - b.point[0]) - (a.profile.width + b.profile.width) / 2)
        gap_y = max(0, abs(a.point[1] - b.point[1]) - (a.profile.depth + b.profile.depth) / 2)
        self.assertAlmostEqual(math.hypot(gap_x, gap_y), DEFAULTS["doublePostClearGap"])

    def test_dimension_modes_are_explicit(self):
        outside = MODULE.build_layout(values(dimensionMode="outside_to_outside"))
        center = MODULE.build_layout(values(dimensionMode="center_to_center"))
        self.assertAlmostEqual(outside.posts[-1].point[0] - outside.posts[0].point[0], 2960)
        self.assertAlmostEqual(center.posts[-1].point[0] - center.posts[0].point[0], 3000)

    def test_guard_height_is_top_surface_not_centerline(self):
        built = MODULE.build_layout(values())
        self.assertAlmostEqual(max(bounds(part)[1][2] for part in built.tubes), DEFAULTS["guardHeight"])
        bottom = next(part for part in built.tubes if part.key.endswith(".bottom"))
        self.assertAlmostEqual(bounds(bottom)[0][2], DEFAULTS["bottomClearance"])

    def test_large_posts_split_caps_without_intersection(self):
        for mode in ("middle", "ends"):
            built = MODULE.build_layout(values(layout="u", largePostMode=mode))
            self.assertTrue(any(post.large for post in built.posts))
            for a, b in combinations(built.tubes, 2):
                self.assertFalse(overlap(a, b), (mode, a.key, b.key))

    def test_non_square_profiles_use_actual_axis_dimensions(self):
        built = MODULE.build_layout(values(layout="u", postWidth=60, postDepth=40,
                                            infillWidth=24, infillDepth=16, railWidth=25, railDepth=35))
        for bay in built.bays:
            self.assertLessEqual(max(bay.clear_gaps), 105 + 1e-6)
        for a, b in combinations(built.tubes, 2):
            self.assertFalse(overlap(a, b), (a.key, b.key))

    def test_fixed_center_keeps_symmetric_edges_and_max_gap(self):
        for clear in (60, 106, 230, 444, 1180, 5000):
            centers, gaps = SUBJECT.distribute_bars(clear, 20, 105, "fixed_center")
            self.assertAlmostEqual(gaps[0], gaps[-1])
            self.assertLessEqual(max(gaps), 105 + 1e-6)
            self.assertGreaterEqual(min(gaps), 0)
            self.assertAlmostEqual(sum(gaps) + len(centers) * 20, clear)

    def test_round_presets_emit_real_outer_envelope_copes_not_intersecting_tubes(self):
        preset = next(preset for preset in PRESETS if preset["id"] == "round-left-l")
        params = values(**preset["parameters"])
        built = MODULE.build_layout(params)
        self.assertTrue(any(part.clips for part in built.tubes))
        document = MODULE.generate(params, {})
        nodes = {node["key"]: node for node in document["geometry"]}
        for part in built.tubes:
            if not part.clips:
                continue
            node = nodes[part.key + ".finished"]
            self.assertEqual(node["operator"], "boolean")
            self.assertEqual(node["arguments"]["operation"], "subtract")
            for cutter in node["arguments"]["tools"]:
                transform = nodes[cutter]
                extrusion = nodes[transform["inputs"][0]]
                profile = nodes[extrusion["inputs"][0]]
                self.assertEqual(len(profile["arguments"]["contours"]), 1)

    def test_real_plates_have_no_fake_tube_and_do_not_overlap_bar_zone(self):
        for kind in ("plate", "lower_plate"):
            params = values(infillType=kind, installation="base_plate", layout="u", cornerPostMode="double")
            built = MODULE.build_layout(params)
            panels = [plate for plate in built.plates if plate.category == "plate.infill"]
            self.assertEqual(len(panels), len(built.bays))
            for panel in panels:
                for tube in built.tubes:
                    if tube.group == panel.group and tube.category == "guardrail.vertical_bar":
                        self.assertLess(panel.center[2] + panel.height / 2, tube.start[2])
            bases = [plate for plate in built.plates if plate.category == "plate.base"]
            self.assertEqual(len(bases), len(built.posts) - 2)
            self.assertTrue(all(len(plate.holes) == 4 for plate in bases))
            self.assertTrue(all(tube.start[2] == DEFAULTS["basePlateThickness"] for tube in built.tubes if tube.category == "guardrail.post"))

    def test_single_purpose_results_are_closed(self):
        for purpose in ("display", "manufacturing"):
            document = MODULE.generate(values(), {"geometryPurpose": purpose})
            self.assertEqual(document["extensions"]["tubeDesigner.geometryPurpose"], purpose)
            self.assertEqual(document["outputs"][0]["purpose"], "result")
            self.assertTrue(all(set(item["representations"]) == {"result"} for item in document["items"]))

    def test_invalid_dimensions_fail_before_geometry_is_emitted(self):
        for change in ({"guardHeight": float("nan")}, {"maximumVerticalClearGap": 0},
                       {"sideLength1": 100}, {"upperRailDrop": 10}, {"railWidth": 70},
                       {"infillType": "lower_plate", "lowerPanelHeight": 1400},
                       {"cornerPostMode": "double", "layout": "u", "sideLength2": 100}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                MODULE.build_layout(values(**change))

    def test_u_turn_rejects_wide_overlapping_caps_and_accepts_clear_separation(self):
        for side_length in (120, 240):
            with self.subTest(side_length=side_length), self.assertRaisesRegex(ValueError, "U型.*中心距"):
                MODULE.build_layout(values(layout="u", sideLength2=side_length, handrailWidth=200))
        # Centerline distance is 241 - 40 = 201; 200 mm wide caps have 1 mm clear.
        built = MODULE.build_layout(values(layout="u", sideLength2=241, handrailWidth=200))
        for first, second in combinations(built.tubes, 2):
            self.assertFalse(overlap(first, second), (first.key, second.key))

    def test_u_turn_allowance_includes_large_posts_and_real_baseplates(self):
        for parameters in (values(layout="u", sideLength2=120, largePostMode="ends"),
                           values(layout="u", sideLength2=140, installation="base_plate")):
            with self.subTest(parameters=parameters), self.assertRaisesRegex(ValueError, "U型.*中心距"):
                MODULE.build_layout(parameters)
        MODULE.build_layout(values(layout="u", sideLength2=121, largePostMode="ends"))
        MODULE.build_layout(values(layout="u", sideLength2=141, installation="base_plate"))

    def test_huge_dense_layout_is_stopped_before_any_geometry_graph_is_built(self):
        parameters = values(layout="u", sideLength1=30000, sideLength2=30000, sideLength3=30000,
                            maximumPostSpacing=1000, maximumVerticalClearGap=1,
                            infillWidth=3, infillDepth=3, infillWallThickness=0.5, infillCornerRadius=0)
        with patch.object(SUBJECT, "NeutralModel", side_effect=AssertionError("geometry must not start")):
            with self.assertRaisesRegex(ValueError, "超过2000件.*分段建模"):
                MODULE.generate(parameters, {})

    def test_total_part_budget_counts_panels_and_baseplates_and_allows_exact_limit(self):
        parameters = values(infillType="plate", installation="base_plate")
        built = MODULE.build_layout(parameters)
        total = len(built.tubes) + len(built.plates)
        self.assertGreater(len(built.plates), 0)
        with patch.object(SUBJECT, "MAX_PART_COUNT", total):
            MODULE.build_layout(parameters)
        with patch.object(SUBJECT, "MAX_PART_COUNT", total - 1):
            with self.assertRaisesRegex(ValueError, "管材和板件合计"):
                MODULE.build_layout(parameters)

    def test_all_twenty_default_presets_stay_below_budget_without_geometry_regressions(self):
        for preset in LEGACY_PRESETS:
            with self.subTest(preset=preset["id"]):
                built = MODULE.build_layout(values(**preset["parameters"]))
                self.assertLessEqual(len(built.tubes) + len(built.plates), SUBJECT.MAX_PART_COUNT)
                self.assertGreater(len(built.tubes), 0)

    def test_decorative_patterns_are_independent_cut_tubes_not_unprocessed_overlaps(self):
        for kind, expected_per_bay in (("cross", 3), ("diamond", 4)):
            parameters = values(infillType=kind, railCount=2)
            built = MODULE.build_layout(parameters)
            decorative = [tube for tube in built.tubes if tube.category == "guardrail.decorative_bar"]
            self.assertEqual(len(decorative), len(built.bays) * expected_per_bay)
            self.assertTrue(all(tube.keep_volume and tube.diagonal and tube.clips for tube in decorative))
            document = MODULE.generate(parameters, {})
            nodes = {node["key"]: node for node in document["geometry"]}
            for tube in decorative:
                self.assertEqual(nodes[tube.key + ".bounded"]["arguments"]["operation"], "intersect")
                self.assertEqual(nodes[tube.key + ".finished"]["arguments"]["operation"], "subtract")
                item = next(item for item in document["items"] if item["key"] == tube.key)
                self.assertEqual(item["properties"]["manufacturing.sourcing"], "made")
                self.assertEqual(item["properties"]["tubeDesigner.endProcess"]["startCut"], "miter")
                self.assertIn("tubeDesigner.profile", item["properties"])

    def test_x_second_diagonal_is_two_parts_with_matching_main_bar_cutters(self):
        built = MODULE.build_layout(values(infillType="cross"))
        for bay in built.bays:
            main = next(tube for tube in built.tubes if tube.key == bay.key + ".pattern.cross.1")
            halves = [tube for tube in built.tubes if tube.group == bay.key and tube.key.endswith((".cross.2", ".cross.3"))]
            self.assertEqual(len(halves), 2)
            self.assertTrue(all(main.key in tube.clips for tube in halves))

    def test_diamond_keep_quadrants_are_nonoverlapping_and_cover_frame(self):
        built = MODULE.build_layout(values(infillType="diamond"))
        for bay in built.bays:
            volumes = [volume for volume in built.volumes if volume.key.startswith(bay.key + ".")]
            self.assertEqual(len(volumes), 4)
            for first, second in combinations(volumes, 2):
                along_delta = abs(sum((first.center[i] - second.center[i]) * bay.direction[i] for i in range(3)))
                z_delta = abs(first.center[2] - second.center[2])
                self.assertTrue(along_delta >= (first.width + second.width) / 2 - 1e-7
                                or z_delta >= (first.height + second.height) / 2 - 1e-7)

    def test_wall_pickets_have_real_manufacturing_holes_but_preview_skips_internal_holes(self):
        parameters = values(guardrailUse="wall", railCount=3)
        built = MODULE.build_layout(parameters)
        bars = [tube for tube in built.tubes if tube.category == "guardrail.vertical_bar"]
        self.assertTrue(all(tube.end[2] == 1320 for tube in bars))
        caps = [tube for tube in built.tubes if tube.category == "guardrail.handrail"]
        self.assertEqual(sum(len(tube.hole_tools) for tube in caps), len(bars))
        preview = MODULE.generate(parameters, {"geometryPurpose": "display"})
        manufacturing = MODULE.generate(parameters, {"geometryPurpose": "manufacturing"})
        preview_nodes = {node["key"]: node for node in preview["geometry"]}
        manufacturing_nodes = {node["key"]: node for node in manufacturing["geometry"]}
        for cap in caps:
            self.assertNotIn(cap.key + ".finished", preview_nodes)
            self.assertEqual(len(manufacturing_nodes[cap.key + ".finished"]["arguments"]["tools"]), len(cap.hole_tools))
        first_tool = manufacturing_nodes[bars[0].key + ".envelope.solid"]
        extrusion = manufacturing_nodes[first_tool["inputs"][0]]
        profile = manufacturing_nodes[extrusion["inputs"][0]]
        self.assertAlmostEqual(profile["arguments"]["contours"][0]["width"], bars[0].profile.width + 0.6)

    def test_wall_spear_is_a_resource_accessory_per_picket(self):
        parameters = values(guardrailUse="wall", spearTipEnabled=True, railCount=2)
        built = MODULE.build_layout(parameters)
        bars = [tube for tube in built.tubes if tube.category == "guardrail.vertical_bar"]
        self.assertEqual(len(built.components), len(bars))
        self.assertTrue(all(component.origin[2] == 1320 for component in built.components))
        document = MODULE.generate(parameters, {})
        resources = [node for node in document["geometry"] if node["operator"] == "resource"]
        self.assertEqual(len(resources), 1)
        self.assertEqual(resources[0]["arguments"]["reference"], "system:spear-tip")
        accessories = [item for item in document["items"] if item["properties"].get("manufacturing.partKind") == "accessory"]
        self.assertEqual(len(accessories), len(bars))
        self.assertTrue(all(item["properties"]["manufacturing.sourcing"] == "purchased" for item in accessories))

    def test_glass_is_purchased_not_metal_plate_or_tube_and_has_four_clamps_per_bay(self):
        parameters = values(infillType="glass", railCount=2, panelEdgeClearance=5, glassClipEnabled=True)
        built = MODULE.build_layout(parameters)
        self.assertEqual(len(built.components), len(built.bays) * 4)
        self.assertTrue(all(plate.part_kind == "glass" for plate in built.plates))
        document = MODULE.generate(parameters, {})
        glass = [item for item in document["items"] if item["properties"].get("manufacturing.partKind") == "glass"]
        self.assertEqual(len(glass), len(built.bays))
        for item in glass:
            props = item["properties"]
            self.assertEqual(props["manufacturing.sourcing"], "purchased")
            self.assertEqual(props["manufacturing.materialCategory"], "glass")
            self.assertEqual(props["manufacturing.plate"]["thickness"], 8)
            self.assertNotIn("tubeDesigner.profile", props)
        self.assertTrue(any(diagnostic["code"] == "guardrail.glass-specification" for diagnostic in document["diagnostics"]))

    def test_known_component_interface_mismatches_fail_instead_of_scaling(self):
        for change in ({"postCapEnabled": True},
                       {"postCapEnabled": True, "largePostMode": "middle", "largePostSize": 100},
                       {"infillType": "glass", "glassClipEnabled": True, "glassThickness": 10, "panelEdgeClearance": 5},
                       {"infillType": "glass", "glassClipEnabled": True, "panelEdgeClearance": 2},
                       {"guardrailUse": "wall", "infillType": "diamond"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                MODULE.build_layout(values(**change))

    def test_cap_mounts_above_large_post_and_template_reference_is_preserved(self):
        parameters = values(largePostMode="middle", postCapEnabled=True, postCapModelReference="template:post-cap")
        built = MODULE.build_layout(parameters)
        self.assertEqual(len(built.components), 1)
        self.assertEqual(built.components[0].origin[2], 1200)
        document = MODULE.generate(parameters, {})
        self.assertTrue(any(node["operator"] == "resource" and node["arguments"]["reference"] == "template:post-cap"
                            for node in document["geometry"]))

    def test_component_models_are_real_closed_documents_and_template_resource_exists(self):
        models = ROOT / "src/apps/tube-designer/templates/accessory"
        for model_id in ("post-cap", "post-cap-40", "spear-tip", "glass-clamp", "connector-block"):
            descriptor = json.loads((models / model_id / "model.json").read_text(encoding="utf-8"))
            self.assertEqual(descriptor["schema"], "icax.component-model")
            model = json.loads((models / model_id / descriptor["geometryFile"]).read_text(encoding="utf-8"))
            self.assertEqual(model["schema"], "icax.neutral-model")
            self.assertEqual(model["lengthUnit"], "mm")
            keys = {node["key"] for node in model["geometry"]}
            for node in model["geometry"]:
                self.assertIn(node["operator"], {"profile2d", "extrude", "boolean"})
                self.assertTrue(set(node["inputs"]).issubset(keys))
            for item in model["items"]:
                self.assertTrue(set(item["representations"].values()).issubset(keys))
        resource = DESCRIPTOR["extensions"]["modelResources"]["post-cap"]
        self.assertTrue((DIRECTORY / resource["path"]).is_file())

    def test_component_reference_safety_and_rigid_placement(self):
        from icax_template_sdk import NeutralModel
        model = NeutralModel(template_id="test", template_version="1", package_digest="", parameters={})
        geometry = SUBJECT._components.ComponentModelGeometry(model)
        for reference in ("../../cap.step", "system:../cap", "https://example.com/cap.step"):
            with self.assertRaises(ValueError):
                geometry.emit("invalid", reference)
        with self.assertRaisesRegex(ValueError, "刚体"):
            geometry.emit("scaled", "system:post-cap", x_axis=(2, 0, 0))
        geometry.emit("a", "library:abc-123", origin=(0, 0, 0))
        geometry.emit("b", "library:abc-123", origin=(100, 0, 0))
        self.assertEqual(len([node for node in model.build()["geometry"] if node["operator"] == "resource"]), 1)

    def test_wall_presets_have_separate_catalog_category_without_template_duplication(self):
        fence = [preset for preset in PRESETS if preset["id"].startswith("wall-")]
        self.assertEqual(len(fence), 4)
        self.assertTrue(all(preset["categoryPath"] == ["护栏", "围墙栏杆"] for preset in fence))

    def test_accessories_are_included_in_the_total_part_budget(self):
        parameters = values(guardrailUse="wall", spearTipEnabled=True)
        built = MODULE.build_layout(parameters)
        count = len(built.tubes) + len(built.plates) + len(built.components)
        with patch.object(SUBJECT, "MAX_PART_COUNT", count - 1):
            with self.assertRaisesRegex(ValueError, "另含配件"):
                MODULE.build_layout(parameters)

    def test_wall_hole_clearance_cannot_merge_holes_and_tips_cannot_collide(self):
        with self.assertRaisesRegex(ValueError, "型孔连通"):
            MODULE.build_layout(values(guardrailUse="wall", maximumVerticalClearGap=1, picketHoleClearance=3))
        with self.assertRaisesRegex(ValueError, "枪尖相碰"):
            MODULE.build_layout(values(guardrailUse="wall", maximumVerticalClearGap=1,
                                       picketHoleClearance=0.1, spearTipEnabled=True))

    def test_decorative_tiny_or_off_center_opening_is_rejected_before_geometry(self):
        for kind in ("cross", "diamond"):
            for change in ({"guardHeight": 300, "bottomClearance": 228},
                           {"guardHeight": 400, "handrailDepth": 200, "bottomClearance": 80},
                           {"sideLength1": 650, "largePostMode": "middle", "largePostSize": 300}):
                parameters = values(infillType=kind, railCount=2, **change)
                with self.subTest(kind=kind, change=change), patch.object(
                        SUBJECT, "NeutralModel", side_effect=AssertionError("geometry must not start")):
                    with self.assertRaisesRegex(ValueError, "花格有效净空"):
                        MODULE.generate(parameters, {})

    def test_decorative_minimum_clearance_allows_nonempty_branch_layout(self):
        for kind in ("cross", "diamond"):
            # 300 - 40 - 185 - 30 = 45 mm clear. Required height is two
            # 20-mm bar widths plus the 5-mm top/bottom half-depth mismatch.
            built = MODULE.build_layout(values(infillType=kind, railCount=2, guardHeight=300, bottomClearance=185))
            decorative = [tube for tube in built.tubes if tube.category == "guardrail.decorative_bar"]
            self.assertEqual(len(decorative), len(built.bays) * (3 if kind == "cross" else 4))
            self.assertTrue(all(tube.length > 0 for tube in decorative))

    def test_corner_x_diagonal_is_trimmed_against_the_overhanging_neighbor_cap(self):
        # Native regression: this pair previously overlapped by 187.927 mm3.
        # The first cap occupies the corner before the second cap begins.
        for layout in ("left_l", "right_l", "u"):
            with self.subTest(layout=layout):
                parameters = values(layout=layout, infillType="cross", railCount=2)
                built = MODULE.build_layout(parameters)
                target = next(tube for tube in built.tubes if tube.key == "segment.2.bay.1.pattern.cross.2")
                self.assertIn("segment.1.cap.1", target.clips)
                self.assertIn("segment.2.cap.1", target.clips)
                document = MODULE.generate(parameters, {})
                nodes = {node["key"]: node for node in document["geometry"]}
                tools = nodes[target.key + ".finished"]["arguments"]["tools"]
                self.assertIn("segment.1.cap.1.envelope.solid", tools)
                if layout == "u":
                    self.assertIn("segment.3.cap.1", target.clips)


if __name__ == "__main__":
    unittest.main()
