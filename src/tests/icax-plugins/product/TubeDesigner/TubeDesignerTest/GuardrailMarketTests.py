import json
import unittest
from ModularGuardrailTests import MODULE, SUBJECT, values, overlap


class MarketTests(unittest.TestCase):
    def test_disabling_large_posts_ignores_retained_cap_selection(self):
        built = MODULE.build_layout(values(largePostMode="none", postCapEnabled=True,
                                           postCapModelReference="missing-inactive-cap"))
        self.assertFalse(built.components)

    def test_horizontal_infill_geometry(self):
        for layout in ("straight", "left_l", "right_l", "u"):
            built = MODULE.build_layout(values(layout=layout,infillType="horizontal"))
            bars = [t for t in built.tubes if t.category=="guardrail.horizontal_bar"]
            self.assertTrue(bars)
            self.assertFalse(any(t.category=="guardrail.vertical_bar" for t in built.tubes))
            for t in bars:
                self.assertEqual(t.start[2],t.end[2])
                for other in built.tubes:
                    if other is not t:
                        self.assertFalse(overlap(t,other),(t.key,other.key))
            json.dumps(MODULE.generate(values(layout=layout,infillType="horizontal"),{}),allow_nan=False)

    def test_fixed_count_and_half_edges(self):
        for count in (0,1,8):
            centers,gaps=SUBJECT.distribute_bars(1100,20,110,"manual_count",count)
            self.assertEqual(len(centers),count)
            self.assertAlmostEqual(sum(gaps)+count*20,1100)
        centers,gaps=SUBJECT.distribute_bars(1100,20,110,"half_edge")
        self.assertAlmostEqual(gaps[0]*2,gaps[1])
        self.assertAlmostEqual(gaps[-1]*2,gaps[1])
        self.assertLessEqual(max(gaps),110)
        self.assertAlmostEqual(sum(gaps)+len(centers)*20,1100)
        for count in (-1,1.5,True,100):
            with self.assertRaises(ValueError):
                SUBJECT.distribute_bars(1100,20,110,"manual_count",count)

    def test_side_mount_has_real_vertical_drilled_plates(self):
        p=values(installation="side_plate")
        built=MODULE.build_layout(p)
        self.assertEqual(len(built.plates),len(built.posts))
        for plate in built.plates:
            self.assertEqual(plate.y_axis,(0.,0.,1.))
            self.assertEqual(len(plate.holes),4)
            self.assertLess(plate.center[2]+plate.height/2,0)
        for post in (t for t in built.tubes if t.category=="guardrail.post"):
            self.assertAlmostEqual(post.start[2],-220)
        json.dumps(MODULE.generate(p,{}),allow_nan=False)
        self.assertTrue(MODULE.build_layout({**p,"layout":"left_l"}).plates)
        for change in ({"sidePlateDrop":50},{"sidePlateHeight":20}):
            with self.assertRaises(ValueError):
                MODULE.build_layout({**p,**change})


if __name__=="__main__":
    unittest.main()
