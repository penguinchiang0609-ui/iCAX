"""Report-only PK area/roll-length measurements from immutable native poses."""
from copy import deepcopy
import json
import math
from pathlib import Path
import tempfile
import unittest

from plane_materials import native_benchmark, report_result
from plane_materials_metrics import calculate_metrics, geometry_bounds, source_quantity_rows


def rectangle(width=2, height=3, holes=None):
    return dict(radius=0, center=[0, 0], outer=[[0, 0], [width, 0], [width, height], [0, height]], holes=holes or [])


def fixture(geometry=None, *, axis="y", x=4, y=10, angle=0):
    task = dict(sourceStock=dict(kind="roll", width=100 if axis == "y" else -1, height=-1 if axis == "y" else 100),
                rules=dict(edgeMarginMm=.5))
    benchmark = dict(status="finished", valid=True, complete=True, placed=1, required=1,
                     problem=dict(parts=[dict(id="p", quantity=1, geometry=geometry or rectangle())]),
                     layouts=[dict(stock="stock", instance=0, width=100 if axis == "y" else 1000000,
                                   height=1000000 if axis == "y" else 100,
                                   placements=[dict(part="p", instance=0, x=x, y=y, angle=angle)])])
    return task, benchmark


class MaterialsMetricsTests(unittest.TestCase):
    def test_one_source_dxf_copy_does_not_become_one_copy_per_component(self):
        task=dict(status="ready",sourceParts=[dict(id="source",assetId="same",quantity=3)],parts=[
            dict(id="P0000_C000",assetId="same",quantity=3),dict(id="P0000_C001",assetId="same",quantity=3)])
        before=deepcopy(task);row=source_quantity_rows(task)[0]
        self.assertEqual((row["quantity"],row["materialComponentCount"],row["nativePieceQuantity"]),(3,2,6))
        self.assertEqual(row["nativePartIds"],["P0000_C000","P0000_C001"])
        self.assertEqual(task,before)

    def test_original_source_groups_stay_separate_when_they_share_one_dxf(self):
        task=dict(status="ready",sourceParts=[dict(id="a",assetId="same",quantity=2),dict(id="b",assetId="same",quantity=5)],
            parts=[dict(id="P0000_C000",quantity=2),dict(id="P0000_C001",quantity=2),dict(id="P0001",quantity=5)])
        rows=source_quantity_rows(task)
        self.assertEqual([(r["quantity"],r["nativePieceQuantity"]) for r in rows],[(2,4),(5,5)])

    def test_source_contours_keep_their_quantities_and_unparsed_material_is_unknown(self):
        task=dict(status="ready",parts=[dict(id="P123",quantity=20,geometrySource=dict(kind="source-contour"))])
        self.assertEqual(source_quantity_rows(task)[0]["nativePieceQuantity"],20)
        task["status"]="unsupported-geometry"
        self.assertIsNone(source_quantity_rows(task)[0]["materialComponentCount"])
        self.assertIsNone(source_quantity_rows(task)[0]["nativePieceQuantity"])

    def test_nonzero_start_keeps_front_blank_and_full_fixed_width(self):
        task, benchmark = fixture()
        before = deepcopy((task, benchmark))
        metrics = calculate_metrics(task, benchmark)
        usage = metrics["rollUsage"]
        self.assertEqual(usage["axis"], "y")
        self.assertEqual(usage["lengths"], [13.5])  # Not bounding-box span 3 + .5.
        self.assertEqual(usage["usedArea"], 1350)
        self.assertEqual(metrics["materialArea"], 6)
        self.assertEqual(metrics["sheetArea"], 100000000)
        self.assertAlmostEqual(usage["utilization"], 6 / 1350)
        self.assertEqual((task, benchmark), before)

    def test_quarter_and_nonorthogonal_rotations_measure_original_vertices(self):
        for angle in (90, 37):
            with self.subTest(angle=angle):
                task, benchmark = fixture(x=10, y=20, angle=angle)
                radians = angle * math.pi / 180
                expected = 20 + 2 * math.sin(radians) + 3 * math.cos(radians) + .5
                self.assertAlmostEqual(calculate_metrics(task, benchmark)["rollUsage"]["totalLength"], expected)
        task, benchmark = fixture(x=10, y=20, angle=90)
        self.assertEqual(calculate_metrics(task, benchmark)["rollUsage"]["totalLength"], 22.5)

    def test_circle_uses_analytic_rotated_center_and_net_material_area(self):
        circle = dict(radius=2, center=[5, 1], outer=[], holes=[])
        task, benchmark = fixture(circle, x=3, y=7, angle=90)
        metrics = calculate_metrics(task, benchmark)
        self.assertAlmostEqual(metrics["materialArea"], 4 * math.pi)
        self.assertEqual(metrics["rollUsage"]["totalLength"], 14.5)

    def test_asymmetric_mirror_reflects_local_y_before_rotation_in_each_pose_form(self):
        geometry = dict(radius=0, outer=[[1, 2], [7, 3], [2, 9]], holes=[])
        for angle, expected in ((0, (11, 11, 17, 18)), (90, (12, 21, 19, 27))):
            for wrapper in (None, "transform", "pose"):
                with self.subTest(angle=angle, wrapper=wrapper):
                    pose = dict(x=10, y=20, angleDegrees=angle, mirrored=True)
                    placement = {wrapper: pose} if wrapper else pose
                    before = deepcopy((geometry, placement))
                    self.assertEqual(geometry_bounds(geometry, placement), expected)
                    self.assertEqual((geometry, placement), before)

    def test_mirrored_off_center_circle_keeps_radius_at_an_arbitrary_angle(self):
        circle = dict(radius=2, center=[5, 3], outer=[], holes=[])
        c, s = math.cos(math.radians(37)), math.sin(math.radians(37))
        cx, cy = 10 + 5*c + 3*s, 20 + 5*s - 3*c
        for wrapper in (None, "transform", "pose"):
            with self.subTest(wrapper=wrapper):
                pose = dict(x=10, y=20, angle=37, mirrored=True)
                placement = {wrapper: pose} if wrapper else pose
                actual = geometry_bounds(circle, placement)
                for value, expected in zip(actual, (cx-2, cy-2, cx+2, cy+2)):
                    self.assertAlmostEqual(value, expected)

    def test_mirror_state_is_strict_boolean_and_legacy_absence_is_original(self):
        original = dict(x=10, y=20, angle=37)
        self.assertEqual(geometry_bounds(rectangle(), original),
                         geometry_bounds(rectangle(), dict(original, mirrored=False)))
        for wrapper in (None, "transform", "pose"):
            for invalid in (0, 1, None, "false", "true", [], {}):
                with self.subTest(wrapper=wrapper, invalid=invalid):
                    pose = dict(original, mirrored=invalid)
                    placement = {wrapper: pose} if wrapper else pose
                    with self.assertRaisesRegex(ValueError, "布尔值"):
                        geometry_bounds(rectangle(), placement)
        for placement in (None, [], {"transform": None}, {"pose": []}):
            with self.subTest(placement=placement), self.assertRaises(ValueError):
                geometry_bounds(rectangle(), placement)

    def test_roll_end_uses_mirrored_material_extrema_and_trailing_margin_on_both_axes(self):
        geometry = dict(radius=0, outer=[[0, 0], [8, 1], [2, 9]], holes=[])
        c, s = math.cos(math.radians(37)), math.sin(math.radians(37))
        for axis, endpoint in (("x", 10 + 2*c + 9*s), ("y", 20 + 8*s - c)):
            for wrapper in (None, "transform", "pose"):
                with self.subTest(axis=axis, wrapper=wrapper):
                    task, benchmark = fixture(geometry, axis=axis, x=10, y=20, angle=37)
                    placement = benchmark["layouts"][0]["placements"][0]
                    pose = {key: placement.pop(key) for key in ("x", "y", "angle")}
                    pose["mirrored"] = True
                    placement.update({wrapper: pose} if wrapper else pose)
                    before = deepcopy((task, benchmark))
                    metrics = calculate_metrics(task, benchmark)
                    usage = metrics["rollUsage"]
                    self.assertFalse(metrics["metricsIssues"])
                    self.assertAlmostEqual(usage["totalLength"], endpoint + .5)
                    self.assertAlmostEqual(usage["usedArea"], 100 * (endpoint + .5))
                    self.assertAlmostEqual(usage["utilization"], metrics["materialArea"] / usage["usedArea"])
                    self.assertEqual((task, benchmark), before)

    def test_invalid_mirror_state_reports_unknown_roll_usage_without_changing_completion(self):
        task, benchmark = fixture()
        benchmark["layouts"][0]["placements"][0]["mirrored"] = "false"
        before = deepcopy(benchmark)
        metrics = calculate_metrics(task, benchmark)
        self.assertIsNone(metrics["rollUsage"])
        self.assertEqual(metrics["metricsIssues"], ["原生零件镜像状态必须是布尔值。"])
        self.assertEqual(benchmark, before)

    def test_holes_reduce_area_and_do_not_shorten_outer_extent(self):
        hole = [[1, 1], [3, 1], [3, 3], [1, 3]]
        for ring in (hole, list(reversed(hole))):
            task, benchmark = fixture(rectangle(10, 10, [ring]), x=1, y=1, angle=37)
            metrics = calculate_metrics(task, benchmark)
            self.assertEqual(metrics["materialArea"], 96)
            self.assertAlmostEqual(metrics["rollUsage"]["totalLength"], 1 + 10 * math.sin(math.radians(37)) + 10 * math.cos(math.radians(37)) + .5)

    def test_unlimited_x_and_missing_roll_axis_use_actual_fixed_layout_height(self):
        for missing in (False, True):
            task, benchmark = fixture(axis="x", x=50, y=30, angle=37)
            if missing:
                task["sourceStock"].pop("width")
            benchmark["layouts"][0]["height"] = 80
            metrics = calculate_metrics(task, benchmark)
            usage = metrics["rollUsage"]
            expected = 50 + 2 * math.cos(math.radians(37)) + .5
            self.assertEqual(usage["axis"], "x")
            self.assertAlmostEqual(usage["totalLength"], expected)
            self.assertAlmostEqual(usage["usedArea"], 80 * expected)

    def test_multiple_boards_sum_origin_based_lengths_and_fixed_width_areas(self):
        task, benchmark = fixture()
        second = deepcopy(benchmark["layouts"][0])
        second["instance"] = 1
        second["placements"][0]["y"] = 20
        benchmark["layouts"].append(second)
        metrics = calculate_metrics(task, benchmark)
        self.assertEqual(metrics["rollUsage"]["lengths"], [13.5, 23.5])
        self.assertEqual(metrics["rollUsage"]["totalLength"], 37)
        self.assertEqual(metrics["rollUsage"]["usedArea"], 3700)
        self.assertEqual(metrics["materialArea"], 12)
        self.assertAlmostEqual(metrics["rollUsage"]["utilization"], 12 / 3700)

    def test_finite_sheet_and_cap_alone_never_create_roll_metrics(self):
        for source in (None, dict(kind="sheet", width=100, height=1000000),
                       dict(kind="roll", width=100, height=1000000), dict(kind="roll", width=-1, height=-1),
                       dict(kind="roll", width=100, height=0), dict(kind="roll", width=0, height=100)):
            task, benchmark = fixture()
            task["sourceStock"] = source
            self.assertIsNone(calculate_metrics(task, benchmark)["rollUsage"])

    def test_native_area_priority_and_legacy_ratio_compatibility(self):
        task, benchmark = fixture()
        benchmark.update(utilization=.25, materialArea=8, validation=dict(materialArea=7))
        metrics = calculate_metrics(task, benchmark, material_area=9)
        self.assertEqual(metrics["materialArea"], 9)
        self.assertEqual(metrics["materialAreaSource"], "native-validation")
        self.assertEqual(calculate_metrics(task, benchmark)["materialArea"], 7)
        benchmark.pop("materialArea");benchmark.pop("validation")
        metrics = calculate_metrics(task, benchmark)
        self.assertEqual(metrics["materialArea"], .25 * metrics["sheetArea"])
        self.assertEqual(metrics["materialAreaSource"], "legacy-sheet-utilization")

    def test_partial_and_invalid_measurements_never_promote_completion(self):
        for valid, complete in ((True, False), (False, True), (False, False)):
            task, benchmark = fixture()
            benchmark.update(valid=valid, complete=complete)
            before = deepcopy(benchmark)
            metrics = calculate_metrics(task, benchmark)
            self.assertEqual(metrics["rollUsage"]["totalLength"], 13.5)
            self.assertNotIn("complete", metrics)
            self.assertNotIn("valid", metrics)
            self.assertEqual(benchmark, before)

    def test_missing_information_stays_none_and_missing_geometry_is_not_skipped(self):
        self.assertEqual(calculate_metrics({}, {})["materialArea"], None)
        self.assertEqual(calculate_metrics({}, {})["sheetArea"], None)
        self.assertEqual(calculate_metrics({}, {})["rollUsage"], None)
        task, benchmark = fixture()
        benchmark["layouts"][0]["placements"].append(dict(part="missing", x=0, y=100, angle=0))
        metrics = calculate_metrics(task, benchmark)
        self.assertIsNone(metrics["rollUsage"])
        self.assertTrue(metrics["metricsIssues"])


class NativePublicationTests(unittest.TestCase):
    def setUp(self):
        base=Path(__file__).resolve().parents[3]/"output/work/sheet-nesting/report-publication-tests"
        base.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        self.raw=self.root/"catalog/raw/task.json";self.raw.parent.mkdir(parents=True)
        self.task=dict(required=1,inputSha256="original-input",rules=dict(edgeMarginMm=.5))
        self.final=dict(format="ICAX_NESTING_TASK_RESULT_1",strategy="plane.initial",
            wholeJobWallSeconds=30,firstCompleteWallSeconds=2,settings={},
            problem=dict(parts=[dict(id="p",quantity=1,freeRotation=False,allowMirror=True,
                allowedAngles=[0],geometry=rectangle())],stocks=[dict(id="stock",width=100,height=100,quantity=1)]),
            result=dict(valid=True,complete=True,boards=1,placed=1,remainingCount=0,remaining=[],
                layouts=[dict(stock="stock",instance=0,width=100,height=100,materialArea=6,
                    placements=[dict(part="p",instance=0,x=10,y=20,angle=0,mirrored=False,group=17)])],
                diagnostics=[],stats={}),
            validation=dict(valid=True,complete=True,placed=1,materialArea=6,usedEnvelopeArea=6,errors=[]))
        self.raw.write_text(json.dumps(self.final),encoding="utf-8")
        self.snapshot=deepcopy(self.final);self.snapshot.update(live=True,wholeJobWallSeconds=12,settings={"live":True})
        self.snapshot["result"].update(diagnostics=["publication metadata differs"],stats={"elapsedSeconds":12})

    def publish(self,snapshot):
        self.raw.with_suffix(".incumbent.json").write_text(json.dumps(snapshot),encoding="utf-8")
        return self.read()

    def read(self):
        return native_benchmark(self.task,self.raw,self.root,"native-program",1)

    def test_matching_final_layout_retains_three_distinct_times_and_csv(self):
        before=self.raw.read_bytes();benchmark=self.publish(self.snapshot)
        self.assertEqual((benchmark["firstCompleteWallSeconds"],benchmark["bestFoundWallSeconds"],benchmark["nativeWallSeconds"]),(2,12,30))
        row={};report_result(row,self.task,benchmark)
        self.assertEqual(row["icax_best_found_seconds"],12)
        self.assertEqual(self.raw.read_bytes(),before)
        self.assertEqual(json.loads(self.raw.with_suffix(".incumbent.json").read_text(encoding="utf-8")),self.snapshot)

    def test_changed_layout_input_or_native_conclusion_does_not_claim_the_best_time(self):
        changes=(
            ("coordinate",lambda v:v["result"]["layouts"][0]["placements"][0].__setitem__("x",10+1e-10)),
            ("reflection",lambda v:v["result"]["layouts"][0]["placements"][0].__setitem__("mirrored",True)),
            ("reflection-number",lambda v:v["result"]["layouts"][0]["placements"][0].__setitem__("mirrored",0)),
            ("stock-instance",lambda v:v["result"]["layouts"][0].__setitem__("instance",1)),
            ("inventory",lambda v:v["problem"]["stocks"][0].__setitem__("quantity",2)),
            ("format",lambda v:v.__setitem__("format","other")),
            ("strategy",lambda v:v.__setitem__("strategy","plane.nfp-array")),
            ("partial",lambda v:v["result"].__setitem__("complete",False)),
            ("invalid",lambda v:v["validation"].__setitem__("valid",False)),
            ("missing-piece",lambda v:v["validation"].__setitem__("placed",0)))
        for name,change in changes:
            with self.subTest(name=name):
                candidate=deepcopy(self.snapshot);change(candidate);benchmark=self.publish(candidate)
                self.assertNotIn("bestFoundWallSeconds",benchmark)
                self.assertEqual((benchmark["status"],benchmark["nativeWallSeconds"]),("finished",30))
        # Even matching declarations cannot supply a timing for absent pieces.
        for payload in (self.final,self.snapshot):payload["result"].update(layouts=[],boards=0)
        self.raw.write_text(json.dumps(self.final),encoding="utf-8")
        self.assertNotIn("bestFoundWallSeconds",self.publish(self.snapshot))

    def test_publication_time_has_finite_numeric_bounds(self):
        for invalid in (None,"12",True,-1,31,float("nan"),float("inf"),float("-inf")):
            with self.subTest(time=invalid):
                candidate=deepcopy(self.snapshot);candidate["wholeJobWallSeconds"]=invalid
                self.assertNotIn("bestFoundWallSeconds",self.publish(candidate))
        for boundary in (0,30):
            with self.subTest(boundary=boundary):
                candidate=deepcopy(self.snapshot);candidate["wholeJobWallSeconds"]=boundary
                self.assertEqual(self.publish(candidate)["bestFoundWallSeconds"],boundary)

    def test_absent_misnamed_or_broken_snapshot_keeps_best_time_unknown(self):
        self.raw.with_name("other.incumbent.json").write_text(json.dumps(self.snapshot),encoding="utf-8")
        self.assertNotIn("bestFoundWallSeconds",self.read())
        self.raw.with_suffix(".incumbent.json").write_text('{"live":',encoding="utf-8")
        benchmark=self.read();self.assertNotIn("bestFoundWallSeconds",benchmark)
        row={};report_result(row,self.task,benchmark)
        self.assertEqual(row["icax_best_found_seconds"],"")
        self.assertEqual((benchmark["firstCompleteWallSeconds"],benchmark["nativeWallSeconds"]),(2,30))


if __name__ == "__main__":
    unittest.main()
