import json
import math
import unittest
import sys
from ModularGuardrailTests import MODULE as PACKAGE, values as package_values

# This suite exercises the shared legacy spacing kernel, not the new fixed
# family wrapper and explicit per-side bay-count contract.
MODULE = sys.modules[PACKAGE._name]
def values(**changes):
    p = package_values(**changes)
    for key in ("sideBayCount1", "sideBayCount2", "sideBayCount3"):
        p.pop(key, None)
    return p


class ElevationTests(unittest.TestCase):
    def test_continuous_keeps_sections_normal_and_bars_vertical(self):
        for angle in (-45, -20, 20, 45):
            for count in (2, 3):
                p = values(pathMode="continuous", slopeAngle=angle, railCount=count)
                built = MODULE.build_layout(p)
                for t in built.tubes:
                    axis = [(b-a)/t.length for a,b in zip(t.start,t.end)]
                    self.assertAlmostEqual(sum(a*b for a,b in zip(axis,t.x_axis)), 0)
                    self.assertAlmostEqual(sum(a*b for a,b in zip(axis,t.y_axis)), 0)
                    if t.category in ("guardrail.post", "guardrail.vertical_bar"):
                        self.assertEqual(t.start[:2], t.end[:2])
                        if t.category == "guardrail.vertical_bar":
                            self.assertTrue(t.keep_volume)
                            self.assertEqual(len(t.clips), 2)
                    else:
                        self.assertAlmostEqual((t.end[2]-t.start[2])/(t.end[0]-t.start[0]),math.tan(math.radians(angle)))
                for purpose in ("display", "manufacturing", None):
                    document = MODULE.generate(p, {} if purpose is None else {"geometryPurpose":purpose})
                    json.dumps(document, allow_nan=False)
                    nodes = {node["key"]:node for node in document["geometry"]}
                    for item in document["items"]:
                        if item["key"] in {t.key for t in built.tubes if t.keep_volume}:
                            for ref in item["representations"].values():
                                self.assertEqual(nodes[ref]["arguments"]["operation"], "subtract")
                                bounded = nodes[item["key"] + ".bounded"]
                                self.assertEqual(bounded["arguments"]["operation"], "intersect")

    def test_steps_have_separate_horizontal_caps_and_vertical_posts(self):
        for angle in (-30,30):
            built = MODULE.build_layout(values(pathMode="stepped", slopeAngle=angle))
            caps = [t for t in built.tubes if t.category=="guardrail.handrail"]
            self.assertEqual(len(caps),len(built.bays))
            self.assertGreater(len({t.start[2] for t in caps}),1)
            for t in built.tubes:
                if t.category in ("guardrail.handrail", "guardrail.cross_rail"):
                    self.assertEqual(t.start[2],t.end[2])
            json.dumps(MODULE.generate(values(pathMode="stepped",slopeAngle=angle),{}),allow_nan=False)

    def test_zero_slope_keeps_the_declared_post_connected_construction(self):
        for mode in ("continuous", "stepped"):
            built = MODULE.build_layout(values(pathMode=mode,slopeAngle=0))
            caps=[t for t in built.tubes if t.category=="guardrail.handrail"]
            self.assertEqual(len(caps),len(built.bays))
            for tube in caps:
                self.assertEqual(tube.start[2],tube.end[2])
                self.assertTrue(tube.clips)

    def test_invalid_combinations_fail_explicitly(self):
        for change in ({"slopeAngle":float("nan")},{"slopeAngle":61},{"infillType":"lower_plate"},{"layout":"left_l","cornerPostMode":"double"}):
            with self.assertRaises(ValueError):
                MODULE.build_layout(values(pathMode="continuous",**change))

    def test_piecewise_paths_round_receivers_and_large_caps(self):
        for layout in ("left_l", "right_l", "u"):
            for kind in ("rect", "round"):
                p=values(pathMode="continuous",layout=layout,slopeAngle=30,slopeAngle2=0,slopeAngle3=-20,
                    handrailProfileType=kind,postProfileType=kind,railProfileType=kind)
                built=MODULE.build_layout(p)
                self.assertEqual(len([t for t in built.tubes if t.category=="guardrail.handrail"]),len(built.bays))
                for tube in built.tubes:
                    if tube.category=="guardrail.post":
                        self.assertEqual(tube.start[:2],tube.end[:2])
                        continue
                    self.assertTrue(tube.clips)
                    self.assertTrue(tube.keep_volume)
                json.dumps(MODULE.generate(p,{"geometryPurpose":"manufacturing"}),allow_nan=False)
        built=MODULE.build_layout(values(pathMode="continuous",largePostMode="middle",postCapEnabled=True))
        self.assertTrue(built.components)
        for cap in built.components:
            post=next(t for t in built.tubes if t.key==cap.key.removesuffix(".cap"))
            self.assertEqual(cap.origin[2],post.end[2])
            self.assertTrue(any(cap.key in t.component_clips for t in built.tubes if t.category=="guardrail.handrail"))
        for purpose in ("display", "manufacturing"):
            document=MODULE.generate(values(pathMode="continuous",largePostMode="middle",postCapEnabled=True),{"geometryPurpose":purpose})
            nodes={node["key"]:node for node in document["geometry"]}
            for item in document["items"]:
                for cap_key in item["properties"].get("manufacturing.clearanceComponents",[]):
                    node=nodes[item["representations"]["result"]]
                    self.assertIn(cap_key+".placed",node["arguments"]["tools"])

    def test_actual_tread_centres_and_maximum_post_spacing(self):
        for mode in ("continuous", "stepped"):
            for rise in (-175,175):
                p=values(pathMode=mode,elevationSource="treads",treadGoing=280,treadRise=rise,
                    treadCount1=10,treadCount2=0,treadCount3=4,maximumPostSpacing=850,layout="u",installation="base_plate")
                built=MODULE.build_layout(p)
                for post in built.posts:
                    if abs(post.point[1])<1e-7:
                        self.assertAlmostEqual(post.point[0]/280,round(post.point[0]/280))
                        self.assertAlmostEqual(post.point[2],round(post.point[0]/280)*rise)
                for bay in built.bays:
                    self.assertLessEqual(math.dist(bay.left.point[:2],bay.right.point[:2]),850+1e-6)
                self.assertAlmostEqual(built.path_vertices[1][0],2800)
                for plate in built.plates:
                    self.assertEqual(plate.x_axis,(1.,0.,0.))
                    self.assertEqual(plate.y_axis,(0.,1.,0.))
        for change in ({"treadGoing":900},{"treadCount1":1.5},{"treadRise":500,"treadGoing":100},{"basePlateSize":300}):
            with self.assertRaises(ValueError):
                MODULE.build_layout(values(pathMode="continuous",elevationSource="treads",maximumPostSpacing=850,installation="base_plate",**change))

    def test_horizontal_infill_follows_each_segment(self):
        p=values(pathMode="continuous",layout="left_l",infillType="horizontal",slopeAngle=30,slopeAngle2=-20)
        built=MODULE.build_layout(p)
        bars=[t for t in built.tubes if t.category=="guardrail.horizontal_bar"]
        self.assertTrue(bars)
        for t in bars:
            expected=30 if t.group.startswith("segment.1.") else -20
            self.assertAlmostEqual((t.end[2]-t.start[2])/math.dist(t.start[:2],t.end[:2]),math.tan(math.radians(expected)))
            axis=[(b-a)/t.length for a,b in zip(t.start,t.end)]
            self.assertAlmostEqual(sum(a*b for a,b in zip(axis,t.x_axis)),0)
            self.assertAlmostEqual(sum(a*b for a,b in zip(axis,t.y_axis)),0)

    def test_shared_corner_keeps_are_complementary_halfspaces(self):
        for layout in ("left_l", "right_l", "u"):
            built=MODULE.build_layout(values(pathMode="continuous",layout=layout))
            volumes={v.key:v for v in built.volumes}
            corner_volumes=[v for v in built.volumes if v.key.startswith("corner.keep.")]
            self.assertTrue(corner_volumes)
            for v in corner_volumes:
                opposite=[other for other in corner_volumes if other is not v and
                    sum(a*b for a,b in zip(v.x_axis,other.x_axis)) < -.999999 and
                    math.dist([v.center[i]-v.x_axis[i]*v.width/2 for i in range(3)],
                              [other.center[i]-other.x_axis[i]*other.width/2 for i in range(3)]) < 1e-6]
                self.assertEqual(len(opposite),1)
            for t in built.tubes:
                for key in t.extra_keep_volumes:
                    self.assertIn(key,volumes)
                if t.category=="guardrail.handrail":
                    self.assertTrue(all(key.startswith("post.") for key in t.clips))


if __name__ == "__main__":
    unittest.main()
