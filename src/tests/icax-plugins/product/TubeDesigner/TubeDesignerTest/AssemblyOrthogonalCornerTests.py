import copy
import importlib.util
import itertools
from pathlib import Path
import unittest
from unittest.mock import patch


TEMPLATES = Path(__file__).resolve().parents[5] / "apps/tube-designer/templates"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section(width, depth, wall, x_world, y_world):
    def rectangle(w, d, reverse=False):
        points = [[-w/2,-d/2],[w/2,-d/2],[w/2,d/2],[-w/2,d/2]]
        if reverse:
            points.reverse()
        return {"kind":"path","closed":True,"segments":[{"kind":"line","start":p,
            "end":points[(index+1)%4]} for index,p in enumerate(points)]}
    return {"schema":"icax.tube-profile","schemaVersion":1,"id":"rect","kind":"rect",
        "width":width,"depth":depth,"parameters":{"width":width,"depth":depth,
        "wallThickness":wall,"cornerRadius":0,"innerRadius":0},
        "contours":[rectangle(width,depth),rectangle(width-2*wall,depth-2*wall,True)],
        "sectionFrame":{"verification":"committed-source-brep-replay","xAxisWorld":x_world,
                        "yAxisWorld":y_world,"centerlineUV":[0,0],"outerContourIndex":0}}


def participants():
    frames = {
        "memberA": ([1,0,0,120, 0,1,0,0, 0,0,1,0, 0,0,0,1],
                    [1,0,0,-120, 0,1,0,0, 0,0,1,0, 0,0,0,1], [120,0,0], "end", 280,20,
                    [0,1,0],[0,0,1],80,40,3),
        "memberB": ([0,0,1,-120, 0,1,0,0, -1,0,0,0, 0,0,0,1],
                    [0,0,-1,0, 0,1,0,0, 1,0,0,120, 0,0,0,1], [-120,0,0], "start",280,20,
                    [0,1,0],[-1,0,0],80,40,3),
        "memberC": ([0,1,0,-110, 0,0,-1,0, -1,0,0,0, 0,0,0,1],
                    [0,0,-1,0, 1,0,0,110, 0,-1,0,0, 0,0,0,1], [-110,0,0],"start",220,0,
                    [0,0,-1],[-1,0,0],20,20,2),
    }
    away = {"memberA":[-1,0,0],"memberB":[0,0,1],"memberC":[0,1,0]}
    def vector(matrix, value): return [sum(matrix[4*r+c]*value[c] for c in range(3)) for r in range(3)]
    return [{"role":role,"memberEntityId":role+"-entity","itemKey":role+"-key","length":length,
        "anchor":{"kind":"end","end":end,"trim":0,"rotation":0,"stockAllowance":allowance},
        "section":section(width,depth,wall,x_world,y_world),
        "nodeFrame":{"selfAwayPart":vector(matrix,away[role]),"selfNodePart":node,
            "awayPartsByRole":{key:vector(matrix,value) for key,value in away.items()},
            "sourceToPart":matrix,"partToSource":inverse}}
        for role,(matrix,inverse,node,end,length,allowance,x_world,y_world,width,depth,wall) in frames.items()]


class OrthogonalCornerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load(TEMPLATES / "_shared/assembly_template_runtime.py", "corner_test_runtime")

    def product(self):
        return self.runtime.get_example_product("orthogonal-corner")["finishedProduct"]

    def bound(self, a="miter", c="weld", actual=None, **values):
        return self.runtime.resolve_bound_plan("orthogonal-corner", {"abJoint":a,"cJoint":c,**values},
            participants=participants() if actual is None else actual, product_topology="orthogonal-corner")

    def test_six_modes_reuse_product_and_exact_parameter_echo(self):
        product = self.product()
        before = copy.deepcopy(product)
        plans = []
        for ab,c in itertools.product(("miter","wrap"),("weld","insert","tabs")):
            values = {"abJoint":ab,"cJoint":c}
            with self.subTest(ab=ab,c=c):
                self.assertTrue(self.runtime.check_applicability("orthogonal-corner",product,values)["applicable"])
                plan = self.runtime.preview_plan("orthogonal-corner",values,finished_product=product,manufacturing_only=True)
                plans.append(plan)
                self.assertEqual(plan["parameters"], self.runtime._validated_values(self.runtime._template_by_id("orthogonal-corner"),values))
                self.assertEqual(plan["finishedProduct"],product)
                self.assertEqual(len(plan["manufacturingParts"]),3)
                blanks = {part["sourceRole"]:part for part in plan["manufacturingParts"]}
                self.assertEqual([blanks[role]["request"]["length"] for role in ("memberA","memberB","memberC")], [280,280,220])
                self.assertEqual([len(blanks[role]["request"]["features"]) for role in ("memberA","memberB")],
                                 [int(c!="weld"),int(c!="weld" and ab=="miter")])
                self.assertEqual(blanks["memberC"]["request"]["ends"]["start"]["trim"],40 if c=="weld" else 28)
                for part in blanks.values():
                    for feature in part["request"]["features"]:
                        if feature["toolRef"]["id"]=="branch-profile":
                            self.assertEqual(feature["operationParameterValues"]["azimuth"],90)
                            self.assertEqual(feature["operationParameterValues"]["roll"],feature["roll"])
        self.assertEqual(product,before)
        self.assertTrue(all(plan["designParts"]==plans[0]["designParts"] for plan in plans))

    def test_applicability_example_are_pure_and_reject_actual_geometric_limits(self):
        with patch.object(self.runtime,"preview_plan",side_effect=AssertionError("generation forbidden")), \
                patch.object(self.runtime,"_load_assembly_script",side_effect=AssertionError("plan forbidden")):
            for ab,c in itertools.product(("miter","wrap"),("weld","insert","tabs")):
                values={"abJoint":ab,"cJoint":c}
                product=self.runtime.get_example_product("orthogonal-corner",values)["finishedProduct"]
                self.assertTrue(self.runtime.check_applicability("orthogonal-corner",product,values)["applicable"])
            invalid=self.product()
            invalid["spans"]["armB"]["parameters"]["depth"] = 42
            self.assertFalse(self.runtime.check_applicability("orthogonal-corner",invalid)["applicable"])
            invalid=self.product()
            invalid["spans"]["armC"]["parameters"].update(width=40,depth=40)
            self.assertFalse(self.runtime.check_applicability("orthogonal-corner",invalid,{"cJoint":"insert"})["applicable"])
            for key,value,joint in (("insertDepth",3,"insert"),("insertDepth",40,"insert"),
                                    ("tabLength",40,"tabs"),("tabWidth",15,"tabs")):
                result=self.runtime.check_applicability("orthogonal-corner",self.product(),{"cJoint":joint,key:value})
                self.assertFalse(result["applicable"])
                self.assertTrue(result["reason"])

    def test_hidden_invalid_drafts_do_not_change_weld_geometry_or_echo(self):
        drafts={"cJoint":"weld","insertDepth":-1,"fitGap":-2,"tabWidth":-3,"tabLength":-4,"sideClearance":-5}
        plain=self.runtime.preview_plan("orthogonal-corner",finished_product=self.product())
        dirty=self.runtime.preview_plan("orthogonal-corner",drafts,finished_product=self.product())
        self.assertEqual(plain["manufacturingParts"],dirty["manufacturingParts"])
        self.assertTrue(all(dirty["parameters"][key]==value for key,value in drafts.items()))
        self.assertEqual(set(dirty["parameters"]),{"abJoint","cJoint","insertDepth","fitGap","tabWidth","tabLength","sideClearance"})

    def test_feasible_parameter_edges_have_direct_examples(self):
        for values in ({"cJoint":"insert","insertDepth":0.1,"fitGap":0},
                       {"cJoint":"insert","insertDepth":100,"fitGap":3},
                       {"cJoint":"tabs","tabWidth":1,"tabLength":1,"sideClearance":0},
                       {"cJoint":"tabs","tabWidth":30,"tabLength":60,"sideClearance":3}):
            with self.subTest(values=values):
                product=self.runtime.get_example_product("orthogonal-corner",values)["finishedProduct"]
                plan=self.runtime.preview_plan("orthogonal-corner",values,finished_product=product)
                self.assertEqual(len(plan["manufacturingParts"]),3)
        with self.assertRaisesRegex(ValueError,"半宽"):
            self.runtime.get_example_product("orthogonal-corner",{"cJoint":"tabs","tabWidth":30,"tabLength":1})

    def test_bound_six_modes_keep_sources_and_true_three_end_anchors(self):
        actual=participants()
        before=copy.deepcopy(actual)
        for ab,c in itertools.product(("miter","wrap"),("weld","insert","tabs")):
            with self.subTest(ab=ab,c=c):
                plan=self.bound(ab,c,actual)
                self.assertTrue(plan["supported"])
                self.assertEqual(plan["parameters"]["abJoint"],ab)
                self.assertEqual(len(plan["workflow"]["blankParts"]),3)
                self.assertEqual(len(plan["nodeContacts"]),3 if ab=="miter" else 2)
                cpart=next(part for part in plan["parts"] if part["role"]=="memberC")
                self.assertEqual(set(cpart["ends"]),{"start"})
                self.assertEqual(cpart["ends"]["start"]["trim"],40 if c=="weld" else 28)
                if c!="weld":
                    receivers=[part for part in plan["parts"] if part["features"]]
                    self.assertEqual([part["role"] for part in receivers],["memberA","memberB"] if ab=="miter" else ["memberA"])
                    for receiver in receivers:
                        frame=receiver["features"][0]["section"]["profile"]["sectionFrame"]
                        self.assertEqual(frame["verification"],"committed-source-brep-replay")
                        self.assertIn("branchManufacturingZHostToolPart",frame)
        self.assertEqual(actual,before)

    def test_bound_uses_actual_quarter_turned_manufacturing_tab_walls(self):
        actual=participants()
        frame=actual[2]["nodeFrame"]
        frame["sourceToPart"]=[0,1,0,-110, -1,0,0,0, 0,0,1,0, 0,0,0,1]
        frame["partToSource"]=[0,-1,0,0, 1,0,0,110, 0,0,1,0, 0,0,0,1]
        frame["awayPartsByRole"]={"memberA":[0,1,0],"memberB":[0,0,1],"memberC":[1,0,0]}
        for c in ("weld","insert","tabs"):
            with self.subTest(c=c):
                plan=self.bound(c=c,actual=actual)
                self.assertTrue(plan["supported"])
                if c=="tabs":
                    host=next(part for part in plan["parts"] if part["role"]=="memberA")
                    self.assertEqual(host["features"][0]["section"]["profile"]["sectionFrame"]
                        ["branchManufacturingZHostToolPart"],[0,0,1])

    def test_bound_refuses_nonorthogonal_noncoincident_and_wrong_sections(self):
        invalid=participants()
        for part in invalid:
            part["nodeFrame"]["awayPartsByRole"]["memberC"] = part["nodeFrame"]["awayPartsByRole"]["memberB"]
        with self.assertRaises(ValueError): self.bound(actual=invalid)
        invalid=participants()
        invalid[2]["nodeFrame"]["partToSource"][3] += 2
        invalid[2]["nodeFrame"]["sourceToPart"][11] += 2
        with self.assertRaisesRegex(ValueError,"同一端点"):
            self.bound(actual=invalid)
        invalid=participants()
        invalid[1]["section"]=section(80,42,3,[0,1,0],[-1,0,0])
        with self.assertRaisesRegex(ValueError,"相同"):
            self.bound(actual=invalid)
        invalid=participants()
        invalid[2]["section"]=section(40,40,2,[0,0,-1],[-1,0,0])
        with self.assertRaisesRegex(ValueError,"端口"):
            self.bound(c="insert",actual=invalid)
        with self.assertRaisesRegex(ValueError,"半孔"):
            self.bound(c="insert",insertDepth=40)
        with self.assertRaisesRegex(ValueError,"斜接缝"):
            self.bound(c="tabs",tabWidth=12,sideClearance=1)

    def test_script_result_cannot_drop_processes_change_echo_or_mutate_inputs(self):
        original=self.runtime._load_assembly_script("orthogonal-corner")
        builder=original.build_bound_operations
        def missing(context):
            result=builder(context)
            result["operations"].pop()
            return result
        def altered(context):
            result=builder(context)
            result["parameters"]["internalRadius"]=123
            return result
        def mutated(context):
            result=builder(context)
            context["participants"]["memberA"]["length"] += 1
            return result
        for callback in (missing,altered,mutated):
            original.build_bound_operations=callback
            with self.subTest(callback=callback.__name__), patch.object(self.runtime,"_load_assembly_script",return_value=original):
                with self.assertRaises(ValueError): self.bound()


if __name__=="__main__": unittest.main()
