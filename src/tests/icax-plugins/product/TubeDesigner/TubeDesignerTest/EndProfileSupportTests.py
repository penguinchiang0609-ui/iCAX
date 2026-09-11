"""Independent end-profile support/placement math; no OCC or project writes.

Analytic production extrema are cross-checked against dense parameterized
boundaries generated here. Sampling is a test oracle, never production geometry.
"""
import copy
import importlib.util
import math
from pathlib import Path
import unittest


ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / "src/apps/tube-designer/templates").is_dir())
SPEC = importlib.util.spec_from_file_location("end_profile_support_subject",
    ROOT / "src/apps/tube-designer/templates/mold/end-profile/tool.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)
SAMPLES = 16384


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def plus(a, b):
    return [x+y for x, y in zip(a, b)]


def times(a, scale):
    return [x*scale for x in a]


def curve(center, a, b, start=0, sweep=math.tau, count=SAMPLES):
    return [[center[k]+a[k]*math.cos(start+sweep*i/count)
             +b[k]*math.sin(start+sweep*i/count) for k in range(2)]
            for i in range(count+1)]


def circular_arc(center, radius, start, sweep):
    points = curve(center, [radius, 0], [0, radius], start, sweep)
    return {"kind": "arc", "start": points[0], "middle": points[SAMPLES//2], "end": points[-1]}, points


def ellipse_arc(center, major, minor, rotation, start, sweep):
    a = [major*math.cos(rotation), major*math.sin(rotation)]
    b = [-minor*math.sin(rotation), minor*math.cos(rotation)]
    return {"kind": "ellipseArc", "center": center, "majorRadius": major,
            "minorRadius": minor, "rotation": rotation, "startAngle": start,
            "endAngle": start+sweep}, curve(center, a, b, start, sweep)


def rounded_points(width, height, radius):
    hx, hy = width/2, height/2
    if not radius:
        return [[-hx, -hy], [hx, -hy], [hx, hy], [-hx, hy]]
    result = []
    for sx, sy, angle in ((1, 1, 0), (-1, 1, math.pi/2),
                          (-1, -1, math.pi), (1, -1, math.pi*1.5)):
        result += curve([sx*(hx-radius), sy*(hy-radius)], [radius, 0], [0, radius], angle, math.pi/2, SAMPLES//4)
    return result


def rational_quarter(center, major, minor, rotation, quadrant):
    a = [major*math.cos(rotation), major*math.sin(rotation)]
    b = [-minor*math.sin(rotation), minor*math.cos(rotation)]
    start, end = quadrant*math.pi/2, (quadrant+1)*math.pi/2
    middle, weight = (start+end)/2, math.sqrt(.5)
    points = [[center[k]+a[k]*math.cos(t)/w+b[k]*math.sin(t)/w for k in range(2)]
              for t, w in ((start, 1), (middle, weight), (end, 1))]
    return {"kind": "nurbs", "degree": 2, "controlPoints": points,
            "weights": [1, weight, 1], "knots": [0, 0, 0, 1, 1, 1]}, curve(center, a, b, start, math.pi/2)


def sampled_rational(segment, start=0, end=1):
    degree, points = segment["degree"], segment["controlPoints"]
    weights = segment.get("weights", [1]*(degree+1))
    samples = []
    for i in range(SAMPLES+1):
        t = start+(end-start)*i/SAMPLES
        factors = [math.comb(degree, k)*t**k*(1-t)**(degree-k)*weights[k] for k in range(degree+1)]
        samples.append([sum(p[axis]*w for p, w in zip(points, factors))/sum(factors) for axis in range(2)])
    return samples


class EndProfileSupport(unittest.TestCase):
    def assert_dense(self, contour, points, directions=None, tolerance=2e-6):
        directions = directions or [(math.cos(r), -math.sin(r)) for r in
                                     (0, .13, .4, .77, math.pi/2, 2.1, math.pi, 4.7, 5.92)]
        for d in directions:
            with self.subTest(kind=contour["kind"], direction=d):
                low, high = tool._support(contour, d)
                samples = [dot(point, d) for point in points]
                sampled_low, sampled_high = min(samples), max(samples)
                # Dense samples lie inside the exact interval; the small
                # discretization gap is quadratic in the parameter spacing.
                self.assertLessEqual(low, sampled_low+tolerance)
                self.assertGreaterEqual(high, sampled_high-tolerance)
                self.assertLessEqual(sampled_low-low, tolerance)
                self.assertLessEqual(high-sampled_high, tolerance)
                reverse = tool._support(contour, [-d[0], -d[1]])
                self.assertAlmostEqual(reverse[0], -high, delta=tolerance)
                self.assertAlmostEqual(reverse[1], -low, delta=tolerance)

    def test_primitive_roll_support_matches_independent_boundary_samples(self):
        cases = [({"kind": "circle", "radius": 20}, curve([0, 0], [20, 0], [0, 20])),
                 ({"kind": "ellipse", "width": 70, "height": 14}, curve([0, 0], [35, 0], [0, 7])),
                 ({"kind": "ellipse", "width": 14, "height": 70}, curve([0, 0], [7, 0], [0, 35]))]
        for kind, width, height, radius in (("roundedRectangle", 70, 20, 0),
                                          ("roundedRectangle", 70, 20, 4),
                                          ("roundedRectangle", 20, 70, 8),
                                          ("capsule", 70, 20, 10),
                                          ("capsule", 20, 70, 10),
                                          ("capsule", 20, 20, 10)):
            cases.append(({"kind": kind, "width": width, "height": height, "radius": radius},
                          rounded_points(width, height, radius)))
        for contour, points in cases:
            self.assert_dense(contour, points)

    def test_asymmetric_and_concave_polygon_support_uses_real_origin(self):
        for points in ([[3, 5], [42, 5], [30, 25], [-7, 17]],
                       [[-20, -15], [20, -15], [20, 15], [5, 15], [5, 0], [-5, 0], [-5, 15], [-20, 15]],
                       [[100000005, -99999995], [100000040, -99999990], [100000030, -99999960]]):
            self.assert_dense({"kind": "polygon", "points": points}, points, tolerance=4e-7)

    def test_circle_arcs_cross_zero_clockwise_counterclockwise_and_major_sweeps(self):
        for start, sweep in ((math.radians(350), math.radians(40)),
                             (math.radians(20), -math.radians(80)),
                             (-2.8, 5.4), (2.8, -5.4),
                             (-.001, .002), (.001, -.002),
                             (.4, math.tau-.01), (.4, -math.tau+.01)):
            segment, points = circular_arc([13, -17], 37, start, sweep)
            self.assert_dense({"kind": "path", "segments": [segment]}, points, tolerance=4e-6)

    def test_large_coordinate_arcs_do_not_use_unstable_absolute_square_subtraction(self):
        for center in ([1e8, -2e8], [-1e9, 1e9]):
            for sweep in (1.7, -4.1):
                segment, points = circular_arc(center, 120, 5.7, sweep)
                self.assert_dense({"kind": "path", "segments": [segment]}, points, tolerance=1e-5)

    def test_rotated_ellipse_arcs_use_radians_and_increasing_wrapped_parameter_ranges(self):
        # Native ellipseArc ranges increase and may cross tau; a raw end<start
        # is not a native-valid alternative encoding for clockwise motion.
        for rotation, start, sweep in ((.43, 5.8, 1.2), (2.1, -.8, 2.9),
                                       (-1.2, -math.tau*3+.2, math.tau),
                                       (math.pi/2, .4, .9), (.0, .6, 5.4)):
            segment, points = ellipse_arc([21, -9], 45, 8, rotation, start, sweep)
            self.assert_dense({"kind": "path", "segments": [segment]}, points, tolerance=4e-6)

    def test_path_support_unions_line_arc_and_ellipse_extrema(self):
        arc, a_points = circular_arc([0, 0], 30, 0, math.pi)
        ellipse, e_points = ellipse_arc([0, 0], 30, 12, 0, math.pi, math.pi)
        path = {"kind": "path", "segments": [arc, ellipse]}
        self.assert_dense(path, a_points+e_points)
        line = {"kind": "line", "start": [-30, 0], "end": [70, 15]}
        self.assert_dense({"kind": "path", "segments": [arc, line]}, a_points+[line["start"], line["end"]])

    def test_imported_rational_ellipse_quadrants_match_exact_ellipse_for_every_roll(self):
        for rotation in (0, .43, 2.1):
            segments = []
            for quadrant in range(4):
                segment, points = rational_quarter([13, -7], 47, 11, rotation, quadrant)
                self.assert_dense({"kind": "path", "segments": [segment]}, points)
                # Reversing the curve must preserve the same spatial support.
                reversed_segment = {**segment, "controlPoints": list(reversed(segment["controlPoints"]))}
                self.assert_dense({"kind": "path", "segments": [reversed_segment]}, points)
                segments.append(segment)
            for roll in range(0, 360, 7):
                d = [math.cos(math.radians(roll)), -math.sin(math.radians(roll))]
                a = [47*math.cos(rotation), 47*math.sin(rotation)]
                b = [-11*math.sin(rotation), 11*math.cos(rotation)]
                offset, radius = dot([13, -7], d), math.hypot(dot(a, d), dot(b, d))
                actual = tool._support({"kind": "path", "segments": segments}, d)
                self.assertAlmostEqual(actual[0], offset-radius, delta=2e-11)
                self.assertAlmostEqual(actual[1], offset+radius, delta=2e-11)

    def test_rational_ellipse_large_translation_does_not_shift_extrema_roots(self):
        segments = [rational_quarter([1e9, -2e9], 47, 11, .63, q)[0] for q in range(4)]
        for roll in range(0, 360, 11):
            d = [math.cos(math.radians(roll)), -math.sin(math.radians(roll))]
            a, b = [47*math.cos(.63), 47*math.sin(.63)], [-11*math.sin(.63), 11*math.cos(.63)]
            offset, radius = dot([1e9, -2e9], d), math.hypot(dot(a, d), dot(b, d))
            actual = tool._support({"kind": "path", "segments": segments}, d)
            self.assertAlmostEqual(actual[0], offset-radius, delta=8e-7)
            self.assertAlmostEqual(actual[1], offset+radius, delta=8e-7)

    def test_rational_clamped_nonunit_knots_and_trimmed_span_match_dense_oracle(self):
        segment, _ = rational_quarter([13, -7], 47, 11, .27, 2)
        segment.update(knots=[2, 2, 2, 5, 5, 5], startParameter=2.6, endParameter=4.4)
        self.assert_dense({"kind": "path", "segments": [segment]}, sampled_rational(segment, .2, .8))
        compressed = {**segment, "knots": [2, 5], "multiplicities": [3, 3]}
        for d in [(1, 0), (0, 1), (.37, -.91)]:
            self.assertEqual(tool._spline_support(segment, d), tool._spline_support(compressed, d))

    def test_cubic_rational_and_polynomial_spans_find_all_interior_extrema(self):
        for weights in (None, [1, .3, 2, 1]):
            segment = {"kind": "nurbs" if weights else "bspline", "degree": 3,
                       "controlPoints": [[-10, 0], [30, 50], [-20, -40], [40, 0]],
                       "knots": [0, 0, 0, 0, 1, 1, 1, 1]}
            if weights: segment["weights"] = weights
            self.assert_dense({"kind": "path", "segments": [segment]}, sampled_rational(segment))
        # y'(t)=3*(2t-1)^2 has a repeated interior stationary root; constant
        # projection and clamped endpoint extrema are valid too.
        segment = {"kind": "bspline", "degree": 3,
                   "controlPoints": [[0, 0], [0, 1], [0, 0], [0, 1]],
                   "knots": [0, 0, 0, 0, 1, 1, 1, 1]}
        self.assertEqual(tool._spline_support(segment, (1, 0)), (0, 0))
        self.assertEqual(tool._spline_support(segment, (0, 1)), (0, 1))

    def test_multispan_periodic_invalid_weights_and_outside_trim_are_not_guessed(self):
        segment, _ = rational_quarter([0, 0], 30, 15, 0, 0)
        for change in ({"periodic": True}, {"degree": 4},
                       {"knots": [0, 0, 0, .5, 1, 1, 1]},
                       {"multiplicities": [2, 3], "knots": [0, 1]},
                       {"weights": [1, 0, 1]}, {"weights": [1, -1, 1]},
                       {"startParameter": -.1, "endParameter": .8},
                       {"startParameter": .1}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                tool._spline_support({**segment, **change}, (1, 0))

    def test_unsupported_spline_and_degenerate_arc_fail_explicitly(self):
        with self.assertRaisesRegex(ValueError, "暂不支持"):
            tool._support({"kind": "path", "segments": [{"kind": "bspline"}]}, (1, 0))
        with self.assertRaisesRegex(ValueError, "退化圆弧"):
            tool._support({"kind": "path", "segments": [{"kind": "arc", "start": [0, 0], "middle": [1, 1], "end": [2, 2]}]}, (1, 0))
        with self.assertRaisesRegex(ValueError, "非空"):
            tool._support({"kind": "path", "segments": []}, (1, 0))


class EndProfileConvexPlacement(unittest.TestCase):
    def generate(self, contour=None, *, end="start", trim=12, **overrides):
        p = {"angle": 90, "azimuth": 0, "roll": 0, "axialOffset": 0,
             "offsetY": 0, "offsetZ": 0, "cutMode": "convex", **overrides}
        context = {"end": end, "bounds": {"min": [100, -40, -25], "max": [1100, 40, 25]},
                   "placement": {"datum": "long", "trim": trim, "rotation": 37},
                   "section": {"profile": {"contours": [contour or {"kind": "circle", "radius": 20}, {"kind": "circle", "radius": 18}]}}}
        before = copy.deepcopy(context)
        value = tool.generate(p, context)
        self.assertEqual(context, before, "Generating the filled contour must not mutate its saved source section")
        nodes = {n["key"]: n for n in value["model"]["geometry"]}
        self.assertEqual(len(nodes["section"]["arguments"]["contours"]), 1)
        self.assertEqual(nodes["section"]["arguments"]["contours"][0], context["section"]["profile"]["contours"][0])
        return value, nodes

    def test_default_circle_nominal_tip_is_at_anchor_and_original_trim_slab_remains(self):
        value, nodes = self.generate()
        placement = nodes["section"]["arguments"]["placement"]
        center = plus(placement["origin"], times(nodes["section-tool"]["arguments"]["vector"], .5))
        for x, expected in zip(center, [20, 0, 0]): self.assertAlmostEqual(x, expected)
        self.assertEqual(value["coordinateSpace"], "end-local")
        self.assertIsNone(value["datumCenter"])
        self.assertTrue(value["requireEndContact"])
        self.assertFalse(value.get("requireRetainedEndMaterial", False))
        self.assertEqual(nodes["tool"]["inputs"], ["slab", "convex-cut"])
        self.assertEqual(nodes["tool"]["arguments"]["operation"], "union")
        self.assertEqual(nodes["convex-cut"]["inputs"], ["forming-region", "section-tool"])
        self.assertEqual(nodes["convex-cut"]["arguments"]["operation"], "subtract")
        slab = nodes["slab-section"]["arguments"]
        self.assertEqual(slab["placement"]["origin"][0]+slab["contours"][0]["width"]/2, 0)

    def test_oblique_halfspace_tracks_section_middle_and_parallel_extrusion_axis(self):
        contour = {"kind": "polygon", "points": [[3, -7], [31, -7], [27, 12], [-9, 17]]}
        for angle, azimuth, roll in ((90, 0, 0), (35, 70, 23), (145, -120, -31), (.001, 22, 46)):
            with self.subTest(angle=angle, azimuth=azimuth, roll=roll):
                _, nodes = self.generate(contour, angle=angle, azimuth=azimuth, roll=roll,
                                         axialOffset=7, offsetY=13, offsetZ=-4)
                a, b, r = map(math.radians, (angle, azimuth, roll))
                normal = [math.sin(a), -math.cos(a)*math.sin(b), -math.cos(a)*math.cos(b)]
                axis = [math.cos(a), math.sin(a)*math.sin(b), math.sin(a)*math.cos(b)]
                section = nodes["section"]["arguments"]["placement"]
                vector = nodes["section-tool"]["arguments"]["vector"]
                center = plus(section["origin"], times(vector, .5))
                low, high = tool._support(contour, (math.cos(r), -math.sin(r)))
                self.assertAlmostEqual(center[0]+low/math.sin(a), 7, delta=1e-7)
                split = nodes["forming-section"]["arguments"]["placement"]
                self.assertAlmostEqual(dot(plus(split["origin"], times(center, -1)), normal), (low+high)/2, delta=1e-7)
                self.assertAlmostEqual(dot(split["xAxis"], normal), 0, delta=1e-12)
                self.assertAlmostEqual(dot(split["yAxis"], normal), 0, delta=1e-12)
                for x, expected in zip(split["xAxis"], axis): self.assertAlmostEqual(x, expected)
                cut_vector = nodes["forming-region"]["arguments"]["vector"]
                self.assertLess(dot(cut_vector, normal), 0, "E extends toward the selected end, not into the unshaped far stock")
                length = math.sqrt(dot(vector, vector))
                self.assertGreater(length/2, abs(center[0]), "Shallow poses include their derived axial shift in the finite extrusion reach")
                # A near point is inside E, while a far point is outside E;
                # translating along the tool axis cannot change membership.
                for signed in (-5, 5):
                    point = plus(split["origin"], plus(times(normal, signed), times(axis, 123)))
                    self.assertAlmostEqual(dot(plus(point, times(split["origin"], -1)), normal), signed, delta=1e-7)

    def test_left_right_trim_and_part_rotation_are_not_applied_twice_by_template(self):
        start, _ = self.generate(end="start", trim=0)
        end, _ = self.generate(end="end", trim=200)
        self.assertEqual(start, end, "Native end-local mapping alone owns side, trim and around-stock rotation")

    def test_parallel_convex_is_rejected_but_concave_keeps_its_existing_pose_range(self):
        for angle in (0, 180):
            with self.assertRaisesRegex(ValueError, "不能与主管轴平行"):
                self.generate(angle=angle)
            _, nodes = self.generate(angle=angle, cutMode="concave")
            self.assertNotIn("forming-region", nodes)
            self.assertEqual(nodes["tool"]["inputs"], ["slab", "section-tool"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
