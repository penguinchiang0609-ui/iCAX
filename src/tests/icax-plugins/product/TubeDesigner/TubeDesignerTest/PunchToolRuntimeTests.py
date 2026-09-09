import copy
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[6]
SPEC = importlib.util.spec_from_file_location("punch_runtime", ROOT / "src/apps/tube-designer/templates/_shared/punch_tool_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


class PunchTools(unittest.TestCase):
    def branch_geometry(self, bounds, **parameters):
        feature={"toolTarget":"part","station":(bounds["max"][0]-bounds["min"][0])/2,
                 "reference":"start","section":{"source":"dxf","profile":{"contours":[{"kind":"circle","radius":3}]}},
                 "toolRef":{"id":"branch-profile"},"toolParameters":parameters}
        return runtime.prepare({"features":[feature],"bounds":bounds})["features"][0]["toolSnapshot"]["geometry"]

    def test_branch_auto_through_extends_half_stock_span_at_each_end(self):
        bounds={"min":[0,-20,-20],"max":[1000,20,20]}
        geometry=self.branch_geometry(bounds)
        nodes={node["key"]:node for node in geometry["model"]["geometry"]}
        origin=nodes["section"]["arguments"]["placement"]["origin"]
        vector=nodes["tool"]["arguments"]["vector"]
        for actual,expected in zip(origin,[500,0,-40]):self.assertAlmostEqual(expected,actual)
        for actual,expected in zip(vector,[0,0,80]):self.assertAlmostEqual(expected,actual)

    def test_branch_auto_extension_tracks_axis_projection_not_fixed_world_dimension(self):
        bounds={"min":[10,-30,-15],"max":[1010,30,15]}
        for angle,azimuth in ((90,0),(90,90),(90,45),(35,20),(145,200),(0,0),(180,0)):
            with self.subTest(angle=angle,azimuth=azimuth):
                nodes={node["key"]:node for node in self.branch_geometry(bounds,angle=angle,azimuth=azimuth,
                    offsetY=70,offsetZ=-12)["model"]["geometry"]}
                origin=nodes["section"]["arguments"]["placement"]["origin"]
                vector=nodes["tool"]["arguments"]["vector"]
                length=math.sqrt(sum(component*component for component in vector))
                axis=[component/length for component in vector]
                low=sum(min(bounds["min"][i]*axis[i],bounds["max"][i]*axis[i]) for i in range(3))
                high=sum(max(bounds["min"][i]*axis[i],bounds["max"][i]*axis[i]) for i in range(3))
                span=high-low
                start=sum(origin[i]*axis[i] for i in range(3))
                end=start+length
                self.assertAlmostEqual(span/2,low-start)
                self.assertAlmostEqual(span/2,end-high)
                self.assertAlmostEqual(2*span,length)

    def test_branch_fixed_length_directions_are_not_extended(self):
        bounds={"min":[0,-20,-20],"max":[1000,20,20]}
        for direction,start in (("positive",0),("negative",-120),("symmetric",-60)):
            with self.subTest(direction=direction):
                nodes={node["key"]:node for node in self.branch_geometry(bounds,direction=direction,length=120)["model"]["geometry"]}
                origin=nodes["section"]["arguments"]["placement"]["origin"]
                vector=nodes["tool"]["arguments"]["vector"]
                self.assertAlmostEqual(start,origin[2])
                self.assertAlmostEqual(120,vector[2])

    def test_part_templates_and_section_snapshot(self):
        source={"id":"branch-1","toolTarget":"part","station":200,"reference":"start",
                "section":{"source":"dxf","profile":{"contours":[{"kind":"circle","radius":6}]}},
                "toolRef":{"id":"branch-profile"},"toolParameters":{}}
        result=self.prepare([source]);item=result["features"][0]
        self.assertEqual(source["section"],item["section"])
        self.assertEqual("part",item["toolSnapshot"]["geometry"]["coordinateSpace"])
        saved=result["recipe"]["features"][0];previous=runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as folder:
                runtime.ROOT=Path(folder)
                self.assertEqual("frozen",self.prepare([saved])["features"][0]["toolSnapshot"]["resolution"])
                for change in ({"station":220},{"section":{}},{"toolParameters":{"angle":45}}):
                    with self.assertRaisesRegex(ValueError,"只读"):
                        self.prepare([{**saved,**change}])
                with self.assertRaisesRegex(ValueError,"主管"):
                    runtime.prepare({"features":[saved],"rebased":True,"bounds":{"min":[0,-20,-10],"max":[500,20,10]}})
        finally:runtime.ROOT=previous

    def test_v_root_is_exact_three_point_arc(self):
        source={"toolTarget":"part","toolRef":{"id":"v-notch"},"toolParameters":{"rootRadius":2},"station":500}
        geometry=self.prepare([source])["features"][0]["toolSnapshot"]["geometry"]
        arc=geometry["model"]["geometry"][0]["arguments"]["contours"][0]["segments"][1]
        self.assertEqual("arc",arc["kind"]);self.assertEqual([0,-9],arc["middle"])
        self.assertEqual(4,len(geometry["model"]["geometry"][0]["arguments"]["contours"][0]["segments"]))
        with self.assertRaisesRegex(ValueError,"底部保留"):
            self.prepare([{**source,"toolParameters":{"bridge":20}}])

    def v_geometry(self, **parameters):
        return self.prepare([{"toolTarget":"part","toolRef":{"id":"v-notch"},
            "toolParameters":parameters,"station":500}])["features"][0]["toolSnapshot"]["geometry"]

    def test_v_styles_have_distinct_actual_geometry_and_sensible_defaults(self):
        descriptor=next(tool for tool in runtime.catalogue()["tools"] if tool["id"]=="v-notch")
        styles=next(p for p in descriptor["parameters"] if p["key"]=="style")["options"]
        self.assertEqual({"sharp_v","asymmetric_v","rounded_v","left_arc","right_arc","flat_v","relief_v"},
                         {item["value"] for item in styles})
        # Compare only the neutral geometry, not the style name or metadata.
        shapes={item["value"]:self.v_geometry(style=item["value"])["model"]["geometry"] for item in styles}
        self.assertEqual(7,len({json.dumps(shape,sort_keys=True) for shape in shapes.values()}))
        for style in ("rounded_v","left_arc","right_arc"):
            segments=shapes[style][0]["arguments"]["contours"][0]["segments"]
            self.assertEqual(["line","arc","line","line"],[s["kind"] for s in segments])
        flat=shapes["flat_v"][0]["arguments"]["contours"][0]["segments"]
        self.assertEqual([-1,-9],flat[1]["start"])
        self.assertEqual([1,-9],flat[1]["end"])
        relief=next(n for n in shapes["relief_v"] if n["key"]=="relief-profile")
        self.assertEqual({"kind":"path","segments":[
            {"kind":"arc","start":[-2,0],"middle":[0,-2],"end":[2,0]},
            {"kind":"arc","start":[2,0],"middle":[0,2],"end":[-2,0]}]},relief["arguments"]["contours"][0])
        self.assertEqual([500,21,-7],relief["arguments"]["placement"]["origin"])

    def test_v_relief_uses_placement_for_the_circle_center_at_any_rotation(self):
        for rotation in (0,37,90,180,270):
            for parameters in ({"style":"relief_v","holeDiameter":4,"holeLift":2},
                               {"style":"sharp_v","reliefDiameter":4,"reliefLift":2}):
                with self.subTest(rotation=rotation,parameters=parameters):
                    nodes={n["key"]:n for n in self.v_geometry(rotation=rotation,**parameters)["model"]["geometry"]}
                    profile=nodes["relief-profile"]["arguments"]
                    self.assertNotIn("center",profile["contours"][0],"Neutral circles use placement, not the imported-DXF center field")
                    contour=profile["contours"][0]
                    self.assertEqual("path",contour["kind"])
                    self.assertEqual(2,len(contour["segments"]))
                    for index,arc in enumerate(contour["segments"]):
                        self.assertEqual("arc",arc["kind"])
                        self.assertEqual(arc["end"],contour["segments"][(index+1)%2]["start"])
                        for key in ("start","middle","end"):
                            self.assertAlmostEqual(2,math.hypot(*arc[key]))
                    origin=profile["placement"]["origin"]
                    vector=nodes["relief"]["arguments"]["vector"]
                    middle=[origin[i]+vector[i]/2 for i in range(3)]
                    sr,cr=math.sin(math.radians(rotation)),math.cos(math.radians(rotation))
                    circle_height=-(20*abs(sr)+10*abs(cr))+1+2
                    for actual,expected in zip(middle,[500,sr*circle_height,cr*circle_height]):
                        self.assertAlmostEqual(expected,actual)

    def test_v_one_sided_arcs_are_exact_mirrors_and_radius_is_editable(self):
        arcs={style:self.v_geometry(style=style,curveRadius=4)["model"]["geometry"][0]
                    ["arguments"]["contours"][0]["segments"][1] for style in ("left_arc","right_arc")}
        for point in (arcs["left_arc"][key] for key in ("start","middle","end")):
            self.assertAlmostEqual(4,math.hypot(point[0],point[1]-(-9+4)))
        for left_key,right_key in (("start","end"),("middle","middle"),("end","start")):
            a,b=arcs["left_arc"][left_key],arcs["right_arc"][right_key]
            self.assertAlmostEqual(a[0],-b[0]);self.assertAlmostEqual(a[1],b[1])
        for style in arcs:
            smaller=self.v_geometry(style=style,curveRadius=2)["model"]["geometry"][0]["arguments"]["contours"][0]["segments"][1]
            self.assertNotEqual(smaller,arcs[style])

    def test_v_asymmetric_angles_independently_control_the_two_walls(self):
        def contour(left,right):
            return self.v_geometry(style="asymmetric_v",leftAngle=left,rightAngle=right)["model"]["geometry"][0]["arguments"]["contours"][0]["segments"]
        a,b,c=contour(30,60),contour(20,60),contour(30,50)
        self.assertNotEqual(a[0]["start"],b[0]["start"])
        self.assertEqual(a[1]["end"],b[1]["end"])
        self.assertEqual(a[0]["start"],c[0]["start"])
        self.assertNotEqual(a[1]["end"],c[1]["end"])

    def test_v_bottom_cut_and_relief_can_intentionally_cross_the_retained_edge(self):
        # Tool creation must not forbid a temporary or intentional split.
        geometry=self.v_geometry(style="relief_v",holeDiameter=8,holeLift=0,bottomCut=True,bottomCutWidth=6)
        nodes={node["key"]:node for node in geometry["model"]["geometry"]}
        self.assertEqual(["notch","relief","bottom-cut"],nodes["tool"]["inputs"])
        self.assertEqual("union",nodes["tool"]["arguments"]["operation"])
        points=nodes["bottom-cut-profile"]["arguments"]["contours"][0]["segments"]
        self.assertEqual([-3,-11],points[0]["start"])
        self.assertEqual([3,-8],points[1]["end"])
        # The old explicit release-hole fields remain supported too.
        self.assertEqual("tool",self.v_geometry(reliefDiameter=8,reliefLift=0)["outputKey"])

    def test_v_variant_parameter_bounds_and_recipe_roundtrip(self):
        for params in ({"style":"unknown"},{"style":"rounded_v","curveRadius":0},
                       {"style":"rounded_v","curveRadius":1000},
                       {"style":"flat_v","flatWidth":0},{"style":"relief_v","holeDiameter":0},
                       {"style":"asymmetric_v","leftAngle":0},{"style":"asymmetric_v","rightAngle":90},
                       {"bottomCut":True,"bottomCutWidth":0},{"curveRadius":float("inf")}):
            with self.subTest(params=params),self.assertRaises(ValueError):self.v_geometry(**params)
        source={"id":"v-array","toolTarget":"part","station":500,"arrayCount":3,"arrayPitch":100,
                "toolRef":{"id":"v-notch"},"toolParameters":{"style":"left_arc","curveRadius":4,"bottomCut":True}}
        saved=json.loads(json.dumps(self.prepare([source])["recipe"]["features"][0]))
        self.assertEqual("left_arc",saved["toolParameters"]["style"])
        self.assertEqual(saved,self.prepare([saved])["recipe"]["features"][0])
        changed=copy.deepcopy(saved);changed["toolParameters"]["curveRadius"]=6
        edited=self.prepare([changed])["recipe"]["features"][0]
        self.assertNotEqual(saved["frozenTool"]["geometryDigest"],edited["frozenTool"]["geometryDigest"])
        previous=runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as folder:
                runtime.ROOT=Path(folder)
                self.assertEqual(saved,self.prepare([saved])["recipe"]["features"][0])
        finally:runtime.ROOT=previous

    def test_signed_multirow_array_survives_save_and_frozen_fallback(self):
        source={"id":"branch-array","toolTarget":"part","station":200,"reference":"end",
                "face":"left","arrayCount":3,"arrayPitch":-100,"rowCount":2,"rowPitch":-8,
                "section":{"profile":{"contours":[{"kind":"circle","radius":3}]}},
                "toolRef":{"id":"branch-profile"},"toolParameters":{"azimuth":90}}
        saved=json.loads(json.dumps(self.prepare([source])["recipe"]["features"][0]))
        for key in ("face","reference","arrayCount","arrayPitch","rowCount","rowPitch"):
            self.assertEqual(source[key],saved[key])
        self.assertEqual(saved["frozenTool"],self.prepare([saved])["recipe"]["features"][0]["frozenTool"])
        previous=runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as folder:
                runtime.ROOT=Path(folder)
                replayed=self.prepare([saved])["features"][0]
                self.assertEqual("frozen",replayed["toolSnapshot"]["resolution"])
                self.assertEqual(-8,replayed["rowPitch"])
                for change in ({"face":"top"},{"arrayPitch":100},{"rowPitch":8},{"rowCount":3}):
                    with self.assertRaisesRegex(ValueError,"只读"):
                        self.prepare([{**saved,**change}])
        finally:runtime.ROOT=previous

    def prepare(self, features=None, ends=None):
        return runtime.generate({"action": "prepare", "features": features or [], "ends": ends or {},
            "bounds": {"min": [0, -20, -10], "max": [1000, 20, 10]}}, {})

    def test_catalogue_has_programmatic_fixed_and_both_end_shapes(self):
        catalogue = runtime.catalogue()
        self.assertEqual([], catalogue["errors"])
        items = {t["id"]: t for t in catalogue["tools"]}
        self.assertEqual("fixed", items["diamond-12"]["kind"])
        self.assertEqual("end", items["end-convex"]["target"])
        self.assertEqual("end", items["end-cope"]["target"])
        for key in ("end-key-joint", "end-step-z", "end-profile"):
            self.assertEqual("end", items[key]["target"])
        self.assertTrue(items["end-profile"]["requiresSection"])

    def test_system_standard_moulds_are_categorized_and_generate_profiles(self):
        items = {tool["id"]: tool for tool in runtime.catalogue()["tools"]}
        expected_hole_types = ("circle", "square", "rectangle", "ellipse",
                               "diamond-12", "hexagon", "triangle", "single-d", "double-d")
        for tool_id in expected_hole_types:
            with self.subTest(tool_id=tool_id):
                self.assertEqual("孔型", items[tool_id]["category"])
                prepared = self.prepare([{"toolTarget": "side", "station": 500,
                                           "toolRef": {"id": tool_id}, "toolParameters": {}}])
                geometry = prepared["features"][0]["toolSnapshot"]["geometry"]
                self.assertEqual("profile", geometry["mode"])
                self.assertTrue(geometry["contours"])
        slot = self.prepare([{"toolTarget": "side", "station": 500,
                              "toolRef": {"id": "slot"}, "toolParameters": {}}])
        self.assertEqual("槽口", items["slot"]["category"])
        self.assertTrue(slot["features"][0]["toolSnapshot"]["geometry"]["contours"])
        self.assertEqual("槽口", items["v-notch"]["category"])
        self.assertEqual("支管", items["branch-profile"]["category"])
        self.assertEqual("端面", items["end-profile"]["category"])

    def test_explicit_joint_and_step_contracts(self):
        for key in ("end-key-joint", "end-step-z"):
            for end in ("start", "end"):
                item={"type":"template","toolRef":{"id":key},"toolParameters":{}}
                result=self.prepare(ends={end:item})["ends"][end]
                self.assertEqual("end-local",result["toolSnapshot"]["geometry"]["coordinateSpace"])
                for datum in ("center","short"):
                    with self.assertRaisesRegex(ValueError,"基准|定位"):
                        self.prepare(ends={end:{**item,"datum":datum}})
        for key, parameters in (("end-key-joint",{"width":40}),("end-key-joint",{"depth":1000}),
                                ("end-step-z",{"splitOffset":20}),("end-step-z",{"depth":1000})):
            with self.assertRaises(ValueError):
                self.prepare(ends={"start":{"type":"template","toolRef":{"id":key},"toolParameters":parameters}})

    def test_end_profile_section_survives_recipe_and_frozen_replay(self):
        section={"source":"dxf","profile":{"contours":[{"kind":"circle","radius":10}]}}
        item={"type":"template","toolRef":{"id":"end-profile"},"toolParameters":{},"section":section}
        prepared=self.prepare(ends={"end":item})
        saved=prepared["recipe"]["ends"]["end"]
        self.assertEqual(section,saved["section"])
        self.assertEqual(section,saved["frozenTool"]["context"]["section"])
        self.assertTrue(saved["frozenTool"]["geometry"]["requireEndContact"])
        with self.assertRaisesRegex(ValueError,"截面"):
            self.prepare(ends={"start":{k:v for k,v in item.items() if k!="section"}})
        previous=runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                runtime.ROOT=Path(directory)
                replay=self.prepare(ends={"end":saved})["ends"]["end"]
                self.assertEqual("frozen",replay["toolSnapshot"]["resolution"])
                self.assertEqual(saved,runtime.recipe_item(replay))
                changed=copy.deepcopy(saved);changed["section"]["profile"]["contours"][0]["radius"]=9
                with self.assertRaisesRegex(ValueError,"只读"):
                    self.prepare(ends={"end":changed})
        finally:runtime.ROOT=previous

    def test_end_profile_modes_always_use_filled_outer_contour(self):
        for source in ("library", "dxf"):
            section={"source":source,"profile":{"contours":[{"kind":"circle","radius":25},{"kind":"circle","radius":23}]}}
            for end in ("start", "end"):
                for mode in ("concave", "convex"):
                    with self.subTest(source=source,end=end,mode=mode):
                        item={"type":"template","toolRef":{"id":"end-profile"},"section":section,"toolParameters":{"cutMode":mode}}
                        result=self.prepare(ends={end:item})
                        prepared=result["ends"][end]
                        nodes={node["key"]:node for node in prepared["toolSnapshot"]["geometry"]["model"]["geometry"]}
                        self.assertEqual(section["profile"]["contours"][:1],nodes["section"]["arguments"]["contours"])
                        self.assertNotIn("cutRegion",prepared["toolParameters"])
                        self.assertFalse(any(p["key"]=="cutRegion" for p in prepared["toolSnapshot"]["descriptor"]["parameters"]))
                        if mode=="convex":
                            self.assertEqual("subtract",nodes["convex-cut"]["arguments"]["operation"])
                            self.assertEqual(["forming-region","section-tool"],nodes["convex-cut"]["inputs"])
                            self.assertEqual(["slab","convex-cut"],nodes["tool"]["inputs"])
                            self.assertAlmostEqual(25,nodes["forming-section"]["arguments"]["placement"]["origin"][0])
                        else:
                            self.assertNotIn("forming-region",nodes)
                        saved=result["recipe"]["ends"][end]
                        self.assertEqual(mode,saved["toolParameters"]["cutMode"])
                        self.assertEqual(saved,self.prepare(ends={end:json.loads(json.dumps(saved))})["recipe"]["ends"][end])

    def test_end_profile_old_switch_normalizes_only_current_editable_recipe(self):
        section={"source":"library","profile":{"contours":[{"kind":"circle","radius":25},{"kind":"circle","radius":23}]}}
        item={"type":"template","toolRef":{"id":"end-profile"},"section":section,"toolParameters":{}}
        expected=self.prepare(ends={"start":item})["ends"]["start"]["toolSnapshot"]["geometry"]
        for old in ("outer","material"):
            supplied=copy.deepcopy(item);supplied["toolParameters"]["cutRegion"]=old
            actual=self.prepare(ends={"start":supplied})["ends"]["start"]
            self.assertEqual("concave",actual["toolParameters"]["cutMode"])
            self.assertEqual(expected,actual["toolSnapshot"]["geometry"])
            self.assertNotIn("cutRegion",actual["toolParameters"])

        # An exact old drawing remains immutable, including a hollow cutter.
        saved=self.prepare(ends={"start":item})["recipe"]["ends"]["start"]
        saved["toolRef"]["digest"]="f"*64
        saved["toolParameters"].pop("cutMode");saved["toolParameters"]["cutRegion"]="material"
        frozen=saved["frozenTool"]
        frozen["ref"]=copy.deepcopy(saved["toolRef"]);frozen["parameters"]=copy.deepcopy(saved["toolParameters"])
        frozen["geometry"]["model"]["geometry"][0]["arguments"]["contours"]=copy.deepcopy(section["profile"]["contours"])
        frozen["geometryDigest"]=runtime.hashlib.sha256(runtime._json_bytes(frozen["geometry"])).hexdigest()
        frozen["instance"]=runtime._instance(saved)
        restored=self.prepare(ends={"start":saved})["ends"]["start"]
        self.assertEqual("frozen",restored["toolSnapshot"]["resolution"])
        self.assertEqual(frozen["geometry"],restored["toolSnapshot"]["geometry"])
        self.assertEqual(saved["toolRef"],restored["toolRef"])

    def test_end_profile_convex_rejects_parallel_axis_without_restricting_concave(self):
        item={"type":"template","toolRef":{"id":"end-profile"},"section":{"profile":{"contours":[{"kind":"circle","radius":25}]}},"toolParameters":{}}
        for angle in (0,180):
            item["toolParameters"]={"cutMode":"convex","angle":angle}
            with self.assertRaisesRegex(ValueError,"不能与主管轴平行"):
                self.prepare(ends={"start":item})
            item["toolParameters"]["cutMode"]="concave"
            self.prepare(ends={"start":item})

    def test_end_profile_convex_uses_actual_dxf_importers_rational_ellipse(self):
        spec=importlib.util.spec_from_file_location("end_cut_dxf_fixture",ROOT / "src/apps/tube-designer/templates/_shared/dxf_profile_importer.py")
        importer=importlib.util.module_from_spec(spec);sys.modules[spec.name]=importer
        try:
            spec.loader.exec_module(importer)
            path=ROOT / "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/PartDrawingSection.dxf"
            profile=importer.generate({"sourcePath":str(path)},{})["profile"]
        finally:
            sys.modules.pop(spec.name,None)
        outer=profile["contours"][0]
        self.assertEqual("path",outer["kind"])
        self.assertEqual({"nurbs"},{segment["kind"] for segment in outer["segments"]})
        self.assertEqual(2,len(profile["contours"]))
        for end in ("start","end"):
            item={"type":"template","toolRef":{"id":"end-profile"},"section":{"source":"dxf","profile":profile},"toolParameters":{"cutMode":"convex"}}
            prepared=self.prepare(ends={end:item})["ends"][end]
            nodes={node["key"]:node for node in prepared["toolSnapshot"]["geometry"]["model"]["geometry"]}
            self.assertEqual([outer],nodes["section"]["arguments"]["contours"],"Use the original exact imported outer curves, not a sampled polygon")
            self.assertAlmostEqual(12,nodes["forming-section"]["arguments"]["placement"]["origin"][0])

    def test_legacy_dimensions_migrate_and_pin(self):
        item = self.prepare([{"type": "circle", "diameter": 12}])["features"][0]
        self.assertEqual(12, item["toolParameters"]["diameter"])
        self.assertEqual(6, item["toolSnapshot"]["geometry"]["contours"][0]["radius"])
        self.assertEqual(64, len(item["toolRef"]["digest"]))

    def test_recipe_freezes_geometry_and_missing_tool_is_delete_only(self):
        evaluated = self.prepare([{"type": "circle", "diameter": 12}])
        saved = evaluated["recipe"]["features"][0]
        for forbidden in ("toolSnapshot", "descriptor", "scriptSource"):
            self.assertNotIn(forbidden, saved)
        self.assertEqual(6, saved["frozenTool"]["geometry"]["contours"][0]["radius"])
        self.assertEqual("subtract", evaluated["recipe"]["operations"][0]["operator"])
        previous = runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                runtime.ROOT = Path(directory)
                restored=self.prepare([json.loads(json.dumps(saved))])["features"][0]
                self.assertEqual("frozen",restored["toolSnapshot"]["resolution"])
                self.assertEqual(saved, runtime.recipe_item(restored))
                for key, value in (("enabled",False),("station",99),("arrayCount",2),("id","copied"),
                                   ("toolParameters",{"diameter":99})):
                    with self.subTest(key=key), self.assertRaisesRegex(ValueError,"只读"):
                        self.prepare([{**saved,key:value}])
                without_geometry=copy.deepcopy(saved)
                del without_geometry["frozenTool"]
                with self.assertRaises(ValueError):
                    self.prepare([without_geometry])
                self.assertEqual([], self.prepare([])["recipe"]["features"])
        finally:
            runtime.ROOT = previous

    def test_new_templates_require_no_dispatch_changes(self):
        previous = runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                runtime.ROOT = Path(directory)
                for kind in ("programmatic", "fixed"):
                    folder = runtime.ROOT / ("new-" + kind)
                    folder.mkdir()
                    descriptor = {"schema": "icax.punch-tool", "schemaVersion": 1, "id": folder.name,
                        "version": "1.0.0", "displayName": "新增刀具", "kind": kind, "target": "side", "parameters": []}
                    (folder / "tool.json").write_text(json.dumps(descriptor), encoding="utf-8")
                    geometry = {"mode": "profile", "contours": [{"kind": "circle", "radius": 3}]}
                    if kind == "fixed":
                        (folder / "geometry.json").write_text(json.dumps(geometry), encoding="utf-8")
                    else:
                        (folder / "tool.py").write_text("def generate(parameters, context):\n    return " + repr(geometry), encoding="utf-8")
                    prepared = self.prepare([{"toolRef": {"id": folder.name}, "toolParameters": {}}])
                    self.assertEqual(geometry, prepared["features"][0]["toolSnapshot"]["geometry"])
                self.assertEqual(2, len(runtime.catalogue()["tools"]))
        finally:
            runtime.ROOT = previous

    def test_fixed_dimensions_reject_changes(self):
        with self.assertRaisesRegex(ValueError, "未声明"):
            self.prepare([{"toolRef": {"id": "diamond-12"}, "toolParameters": {"diameter": 99}}])

    def test_reject_nonfinite_unknown_parameters_and_version_drift(self):
        for params in ({"diameter": float("nan")}, {"diameter": -1}, {"evil": 10}):
            with self.assertRaises(ValueError):
                self.prepare([{"toolRef": {"id": "circle"}, "toolParameters": params}])
        with self.assertRaises(ValueError):
            self.prepare([{"toolRef": {"id": "circle", "digest": "wrong"}, "toolParameters": {}}])

    def test_version_drift_preserves_frozen_geometry_and_matching_tool_can_edit(self):
        saved=self.prepare([{"type":"circle","diameter":12}])["recipe"]["features"][0]
        modified=copy.deepcopy(saved)
        modified["toolParameters"]["diameter"]=14
        self.assertEqual(7,self.prepare([modified])["features"][0]["frozenTool"]["geometry"]["contours"][0]["radius"])
        # A mismatched installed tool is not a substitute for the original one.
        saved["toolRef"]["digest"]="old-version"
        saved["frozenTool"]["ref"]["digest"]="old-version"
        saved["frozenTool"]["instance"]["toolRef"]["digest"]="old-version"
        self.assertEqual("frozen",self.prepare([saved])["features"][0]["toolSnapshot"]["resolution"])

    def test_missing_end_tool_is_frozen_and_read_only(self):
        result=self.prepare(ends={"end":{"type":"template","toolRef":{"id":"end-convex"},"toolParameters":{"diameter":20}}})
        saved=result["recipe"]["ends"]["end"]
        previous=runtime.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                runtime.ROOT=Path(directory)
                replayed=self.prepare(ends={"end":saved})
                self.assertEqual(saved["frozenTool"]["geometry"],replayed["ends"]["end"]["toolSnapshot"]["geometry"])
                with self.assertRaisesRegex(ValueError,"只读"):
                    self.prepare(ends={"end":{**saved,"trim":10}})
                self.assertEqual("keep",self.prepare(ends={"end":{"type":"keep"}})["ends"]["end"]["type"])
                changed=copy.deepcopy(saved);changed["frozenTool"]["geometry"]["outputKey"]="tampered"
                with self.assertRaisesRegex(ValueError,"校验"):
                    self.prepare(ends={"end":changed})
        finally:
            runtime.ROOT=previous

    def test_persisted_baseline_rejects_replacement_and_forged_read_only_fields(self):
        saved=self.prepare([{"id":"one","type":"circle"}])["recipe"]
        saved["features"][0]["toolRef"]["digest"]="missing"
        replacement={"id":"one","toolRef":{"id":"rectangle"},"toolParameters":{}}
        with self.assertRaisesRegex(ValueError,"只读"):
            runtime.prepare({"features":[replacement],"bounds":{},"original":saved})
        self.assertEqual([],runtime.prepare({"features":[],"bounds":{},"original":saved})["features"])

    def test_no_traversal_or_resource_loading(self):
        for key in ("../circle", "/circle", "../../punch_tool_runtime.py"):
            with self.assertRaises(ValueError):
                self.prepare([{"toolRef": {"id": key}, "toolParameters": {}}])
        saved = self.prepare([{"type": "circle"}])["features"][0]
        saved["toolSnapshot"]["geometry"] = {"mode": "solid", "outputKey": "steal",
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                "geometry": [{"key": "steal", "operator": "resource", "arguments": {"path": "secret.step"}}]}}
        # Project-supplied definitions are ignored; only the installed circle runs.
        regenerated = self.prepare([saved])["features"][0]["toolSnapshot"]["geometry"]
        self.assertEqual("circle", regenerated["contours"][0]["kind"])
        with self.assertRaisesRegex(ValueError, "不允许资源"):
            runtime._validate_geometry(saved["toolSnapshot"]["geometry"], "side")
        saved["frozenTool"]["geometry"]=saved["toolSnapshot"]["geometry"]
        with self.assertRaisesRegex(ValueError,"不允许资源"):
            runtime._restore_frozen(saved["frozenTool"],saved["toolRef"],saved["toolParameters"],{"target":"side","lengthUnit":"mm"})

    def test_end_tools_produce_different_boolean_recipes(self):
        operations = {}
        for kind in ("cope", "convex"):
            result = self.prepare(ends={"start": {"toolRef": {"id": "end-" + kind}, "type": "template",
                "toolParameters": {"diameter": 20}}})
            geometry = result["ends"]["start"]["toolSnapshot"]["geometry"]
            operations[kind] = geometry["model"]["geometry"][-1]["arguments"]["operation"]
        self.assertEqual({"cope": "union", "convex": "subtract"}, operations)

if __name__ == "__main__":
    unittest.main()
