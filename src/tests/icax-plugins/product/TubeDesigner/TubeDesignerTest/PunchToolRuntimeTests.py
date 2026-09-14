import copy
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[6]
def rectangle(x, y, inner=False):
    points = [[-x,-y],[x,-y],[x,y],[-x,y]]
    return {"inner":inner,"closed":True,"edges":[
        {"kind":"line","start":p,"end":points[(i+1)%4]} for i,p in enumerate(points)]}
TARGET = {"schema":"icax.mold-section","schemaVersion":1,"status":"available","tolerance":0.001,
          "contours":[rectangle(20,10),rectangle(18,8,True)]}
SPEC = importlib.util.spec_from_file_location("punch_runtime", ROOT / "src/apps/tube-designer/templates/_shared/punch_tool_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


class PunchTools(unittest.TestCase):
    def test_embedded_arc_male_female_keeps_circle_and_mirrors_step(self):
        plain=self.v_geometry("embedded-arc-notch")
        joint=self.v_geometry("embedded-arc-notch",maleFemale=True)
        explicit=self.v_geometry("embedded-arc-notch",maleFemale=True,maleFemaleSize=2)
        self.assertEqual(joint,explicit)
        self.assertEqual(plain['model']['geometry'][2:4],joint['model']['geometry'][2:4])
        edges=joint['model']['geometry'][0]['arguments']['contours'][0]['segments']
        self.assertEqual(6,len(edges))
        self.assertAlmostEqual(8,edges[0]['start'][1])
        mirrored=self.v_geometry("embedded-arc-notch",maleFemale=True,rightArc=False)
        other=mirrored['model']['geometry'][0]['arguments']['contours'][0]['segments']
        for a,b in zip(edges,other):
            for key in ('start','end'):self.assertEqual([-a[key][0],a[key][1]],b[key])
        with self.assertRaisesRegex(ValueError,'壁厚'):
            self.v_geometry("embedded-arc-notch",maleFemale=True,maleFemaleSize=1)
        with self.assertRaisesRegex(ValueError,'重叠'):
            self.v_geometry("embedded-arc-notch",maleFemale=True,maleFemaleSize=8)
        disabled=self.v_geometry("embedded-arc-notch",maleFemale=False,maleFemaleSize=100)
        self.assertEqual(plain,disabled)

    def test_new_notches_are_discoverable_independent_packages(self):
        tools = {t["id"]: t for t in runtime.catalogue()["tools"]}
        for key in ("embedded-arc-notch", "segmented-bend"):
            with self.subTest(key=key):
                self.assertEqual("槽口", tools[key]["category"])
                self.assertTrue(tools[key]["illustration"]["paths"])
                self.assertTrue(tools[key]["parameterDiagram"]["labels"])
                g = self.v_geometry(key)
                self.assertEqual(key, g["model"]["template"]["id"])
                self.assertEqual("not-performed", g["calculation"]["formingValidation"])
                for node in g["model"]["geometry"]:
                    if node["operator"] != "profile2d": continue
                    edges = node["arguments"]["contours"][0]["segments"]
                    for i,e in enumerate(edges):
                        self.assertEqual(e["end"], edges[(i+1)%len(edges)]["start"])
                        self.assertGreater(math.dist(e["start"],e["end"]), 1e-9)

    def test_embedded_circle_intersection_mirror_and_retained_material(self):
        for angle in (30, 60, 90, 120, 150):
            a = self.v_geometry("embedded-arc-notch", angle=angle, bendRadius=10)
            b = self.v_geometry("embedded-arc-notch", angle=angle, bendRadius=10, rightArc=False)
            calc = a["calculation"]
            center, intersection = calc["circleCenter"], calc["flankIntersection"]
            self.assertAlmostEqual(10, math.dist(center, intersection))
            self.assertAlmostEqual(intersection[0], (intersection[1]-center[1])*math.tan(math.radians(angle/2)))
            self.assertEqual([-center[0],center[1]], b["calculation"]["circleCenter"])
            nodes = a["model"]["geometry"]
            self.assertEqual("subtract", nodes[-1]["arguments"]["operation"])
            self.assertEqual(["v-base","retained-circle"], nodes[-1]["inputs"])
            # At 90 degrees the circle retains the would-be V waste at (4,5),
            # while (0,5) is still cut: this is not a release-hole union.
            if angle == 90:
                self.assertLess((4-10)**2+5**2, 10**2)
                self.assertGreater((0-10)**2+5**2, 10**2)
            for n,m in zip(nodes,b["model"]["geometry"]):
                if n["operator"] != "profile2d": continue
                for e,f in zip(n["arguments"]["contours"][0]["segments"],m["arguments"]["contours"][0]["segments"]):
                    for name in ("start","middle","end"):
                        if name in e:self.assertEqual([-e[name][0],e[name][1]], f[name])
        with self.assertRaisesRegex(ValueError, "超出"):
            self.v_geometry("embedded-arc-notch", bendRadius=20)
        with self.assertRaises(ValueError):
            self.v_geometry("embedded-arc-notch", rootClearance=30)

    def test_standalone_segments_radius_datums_and_auto_count(self):
        a = self.v_geometry("segmented-bend", bendRadius=40)
        b = self.v_geometry("segmented-bend", bendRadius=50, radiusDatum="centerline")
        self.assertEqual(a["model"], b["model"])
        c = a["calculation"]
        self.assertEqual(6, c["segmentCount"])
        self.assertAlmostEqual(13.052619222, c["chordPitch"], places=8)
        self.assertAlmostEqual(90, c["singleNotchAngle"]*6)
        positions = [n["arguments"]["placement"]["origin"][0] for n in a["model"]["geometry"] if n["operator"]=="profile2d"]
        self.assertAlmostEqual(0, sum(positions)/len(positions))
        for x,y in zip(positions,positions[1:]): self.assertAlmostEqual(c["chordPitch"],y-x)
        auto = self.v_geometry("segmented-bend",countMode="tolerance",maximumChordError=0.1)["calculation"]
        self.assertLessEqual(auto["chordError"],0.1)
        self.assertGreater(auto["segmentCount"],6)
        self.assertGreater(50*(1-math.cos(math.radians(90/(auto["segmentCount"]-1))/2)),0.1)
        with self.assertRaisesRegex(ValueError,"64"):
            self.v_geometry("segmented-bend",countMode="tolerance",maximumChordError=0.000001)
        with self.assertRaises(ValueError):self.v_geometry("segmented-bend",segmentCount=2.5)

    def test_segments_compensation_is_checked_for_actual_opening(self):
        a=self.v_geometry("segmented-bend",bendCompensation=False)["calculation"]
        b=self.v_geometry("segmented-bend",bendCompensation=True,kFactor=0.62)["calculation"]
        self.assertAlmostEqual(0.62*2*math.radians(15),b["singleNotchAllowance"])
        self.assertAlmostEqual(a["singleNotchOpening"]+b["singleNotchAllowance"],b["singleNotchOpening"])
        with self.assertRaisesRegex(ValueError,"间隔"):
            self.v_geometry("segmented-bend",minimumLand=(a["remainingLand"]+b["remainingLand"])/2,bendCompensation=True)

    def test_new_notches_round_shell_applicability_and_snapshot_roundtrip(self):
        def circle(r,inner):
            return {"inner":inner,"closed":True,"edges":[{"kind":"circleArc","center":[0,0],
                "xAxis":[1,0],"yAxis":[0,1],"radius":r,"first":0,"last":math.tau,
                "start":[r,0],"end":[r,0]}]}
        section={**TARGET,"contours":[circle(10,False),circle(8,True)]}
        for key in ("embedded-arc-notch","segmented-bend"):
            params={"bounds":{"min":[0,-10,-10],"max":[1000,10,10]},"targetSection":section,
                "features":[{"toolTarget":"part","toolRef":{"id":key},"station":500}]}
            if key=="embedded-arc-notch":
                with self.assertRaisesRegex(ValueError,"正交"):runtime.prepare(params)
            else:
                result=runtime.prepare(params)
                params["features"]=json.loads(json.dumps(result["recipe"]["features"]))
                again=runtime.prepare(params)
                self.assertEqual(result["features"][0]["toolSnapshot"]["geometry"],again["features"][0]["toolSnapshot"]["geometry"])
                self.assertEqual(2,again["features"][0]["toolSnapshot"]["parameters"]["wallThickness"])

    def test_removed_developed_strategy_is_not_offered_or_silently_remapped(self):
        descriptor=next(t for t in runtime.catalogue()["tools"] if t["id"]=="v-notch-sharp")
        parameters={p["key"]:p for p in descriptor["parameters"]}
        self.assertNotIn("rightRoundRadius",parameters)
        self.assertNotIn("developed",[o["value"] for o in parameters["bottomStrategy"]["options"]])
        self.assertNotIn("developed",json.dumps(descriptor.get("parameterDiagram",{})))
        with self.assertRaises(ValueError):
            self.v_geometry(bottomStrategy="developed")

    def test_side90_root_and_top_width_increase_and_mirror(self):
        geometries=[self.v_geometry('edge-arc-groove',arcDefinition='side90',leftArc=left,
            bendCompensation=True) for left in (True,False)]
        edges=[g['model']['geometry'][0]['arguments']['contours'][0]['segments'] for g in geometries]
        for a,b in zip(*edges):
            for key in ('start','middle','end'):
                if key in a:self.assertEqual([-a[key][0],a[key][1]],b[key])
        root=edges[0][2];radius=19;length=math.pi/2*(radius+0.62)
        self.assertAlmostEqual(length,math.dist(root['start'],root['end']))
        self.assertAlmostEqual(length+radius,edges[0][0]['end'][0]-edges[0][0]['start'][0])
        with self.assertRaisesRegex(ValueError,'固定'):
            self.v_geometry('edge-arc-groove',arcDefinition='side90',angle=60)

    def test_root_pattern_has_three_transverse_slots_and_checks_bridges(self):
        geometry=self.v_geometry(rootSlotPattern=True)
        nodes={n['key']:n for n in geometry['model']['geometry']}
        self.assertEqual(['notch','root-center','root-left','root-right'],nodes['tool']['inputs'])
        self.assertEqual(-6,nodes['root-center']['arguments']['vector'][1])
        self.assertEqual(-3,nodes['root-left']['arguments']['vector'][1])
        with self.assertRaisesRegex(ValueError,'桥宽'):
            self.v_geometry(rootSlotPattern=True,centerSlotLength=32,sideSlotLength=3)

    def test_relief_shapes_are_closed_without_zero_length_edges(self):
        for kind in ('circle','capsule','roundedRectangle'):
            geometry=self.v_geometry(bottomStrategy='relief',reliefShape=kind,reliefLength=3,reliefHeight=1)
            node=next(n for n in geometry['model']['geometry'] if n['key']=='relief-profile')
            edges=node['arguments']['contours'][0]['segments']
            for i,edge in enumerate(edges):
                self.assertGreater(math.dist(edge['start'],edge['end']),1e-9)
                self.assertEqual(edge['end'],edges[(i+1)%len(edges)]['start'])

    def test_segmented_bend_chord_spacing_and_overlap(self):
        geometry=self.v_geometry(segmentedBend=True,segmentCount=6,centerlineRadius=50,angle=90)
        metrics=geometry['calculation']
        self.assertAlmostEqual(13.052619222,metrics['chordPitch'],places=8)
        self.assertAlmostEqual(0.427756931,metrics['chordError'],places=8)
        self.assertEqual(15,metrics['singleNotchAngle'])
        self.assertEqual(90,metrics['finalIncludedAngle'])
        automatic=self.v_geometry(segmentedBend=True,centerlineRadius=50,maximumChordError=0.1)['calculation']
        self.assertLessEqual(automatic['chordError'],0.1)
        self.assertGreater(automatic['segmentCount'],6)
        with self.assertRaisesRegex(ValueError,'重叠'):
            self.v_geometry(segmentedBend=True,centerlineRadius=5)

    def test_fitter_dependency_paths_are_scoped_and_validated(self):
        spec=importlib.util.spec_from_file_location("fitter_worker_test",ROOT/"src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py")
        worker=importlib.util.module_from_spec(spec);spec.loader.exec_module(worker)
        previous=list(sys.path)
        try:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);dependency=root/"dependencies";dependency.mkdir()
                (dependency/"icax_fitter_dependency_probe.py").write_text("VALUE = 42\n",encoding="utf8")
                script=root/"fitter.py"
                script.write_text('def fitter(contours, context):\n    from icax_fitter_dependency_probe import VALUE\n    if context.get("fail"): raise ValueError("probe failure")\n    return {"matched": VALUE == 42}\n',encoding="utf8")
                request={"fitterPath":str(script),"contours":[],"moduleSearchPaths":[str(dependency)]}
                self.assertEqual({"matched":True},worker._fit(request))
                self.assertEqual(previous,sys.path)
                with self.assertRaisesRegex(ValueError,"probe failure"):
                    worker._fit({**request,"context":{"fail":True}})
                self.assertEqual(previous,sys.path)
                with self.assertRaises(ValueError):
                    worker._fit({**request,"moduleSearchPaths":["relative"]})
        finally:
            sys.modules.pop("icax_fitter_dependency_probe",None)

    def test_analysis_uses_contours_not_profile_tags(self):
        feature = {"toolTarget":"part","toolRef":{"id":"v-notch-sharp"},"toolParameters":{"wallThickness":9}}
        saved = self.prepare([feature])["features"][0]["toolSnapshot"]
        self.assertEqual(2, saved["parameters"]["wallThickness"])
        self.assertEqual(2, saved["context"]["analysis"]["wallThickness"])

    def test_analysis_failure_blocks_generate(self):
        descriptor = {"kind":"programmatic","parameters":[],"sectionAnalysis":{"schemaVersion":1}}
        with self.assertRaisesRegex(ValueError,"rejected"):
            runtime._analyze_mould({"analyze":lambda p,s,c:{"applicable":False,"reason":"rejected"}}, descriptor,{},{})

    def test_analysis_cannot_overwrite_ordinary_parameters(self):
        with self.assertRaisesRegex(ValueError,"派生"):
            runtime._analyze_mould({"analyze":lambda p,s,c:{"applicable":True,"data":{},"derivedParameters":{"angle":1}}},
                                  {"parameters":[]},{},{})

    def test_plain_hole_needs_no_section(self):
        self.assertEqual(({},None),runtime._analyze_mould({},{"parameters":[]},{},{}))

    def test_oblique_direction_rejected_by_mould(self):
        with self.assertRaisesRegex(ValueError,"平直"):
            self.prepare([{"toolTarget":"part","toolRef":{"id":"v-notch-sharp"},"rotation":37}])

    def test_rotation_changes_measured_section(self):
        item=self.prepare([{"toolTarget":"part","toolRef":{"id":"v-notch-sharp"},"rotation":90}])["features"][0]
        bounds=item["toolSnapshot"]["context"]["analysis"]["bounds"]
        self.assertAlmostEqual(40,bounds["max"][2]-bounds["min"][2])

    def test_custom_mould_owns_analysis_and_receives_exact_geometry(self):
        descriptor={"id":"custom-analysis","version":"1","kind":"programmatic","target":"part",
                    "displayName":"Custom","sectionAnalysis":{"schemaVersion":1},"parameters":[]}
        source='def analyze(p,s,c):\n    return {"applicable":False,"reason":"exact="+str(s["probe"]),"data":{}}\ndef generate(p,c):\n    raise RuntimeError("must-not-generate")\n'
        tools=[{"id":descriptor["id"],"version":"1","descriptor":descriptor,"scriptSource":source}]
        with self.assertRaisesRegex(ValueError,"exact=0.123456789123"):
            runtime._evaluate({"id":descriptor["id"]},{},{"target":"part","targetSection":{"probe":0.123456789123}},user_tools=tools)

    def test_section_helper_preserves_splines_and_rejects_invalid_curve_data(self):
        section=copy.deepcopy(TARGET)
        section["contours"][0]["edges"][0].update(kind="nurbs",degree=3,poles=[[1,2],[3,4]],
            knots=[0,1],multiplicities=[4,4],weights=[1,0.8])
        queries=runtime._section_queries()
        local=queries.local_section(section)
        self.assertEqual(section,local)
        with self.assertRaisesRegex(ValueError,"无效"):
            queries.bounds(local["contours"][0])

    def test_section_helper_rejects_false_closed_flag(self):
        section=copy.deepcopy(TARGET)
        section["contours"][0]["edges"][0]["end"]=[21,-10]
        with self.assertRaisesRegex(ValueError,"连接"):
            runtime._section_queries().local_section(section)

    def test_exact_circle_extrema_and_mould_rejection(self):
        edge={"kind":"circleArc","center":[0,0],"xAxis":[1,0],"yAxis":[0,1],
              "radius":10,"first":0,"last":math.tau,"start":[10,0],"end":[10,0]}
        outer={"inner":False,"closed":True,"edges":[edge]}
        queries=runtime._section_queries()
        self.assertEqual({"min":[-10,-10],"max":[10,10]},queries.bounds(outer))
        inner=copy.deepcopy(outer);inner["inner"]=True
        inner["edges"][0].update(radius=8,start=[8,0],end=[8,0])
        result=runtime.prepare({"features":[{"toolTarget":"part","toolRef":{"id":"v-notch-sharp"},
            "toolParameters":{"segmentedBend":True,"centerlineRadius":50}}],
            "bounds":{"min":[0,-10,-10],"max":[1000,10,10]},
            "targetSection":{**TARGET,"contours":[outer,inner]}})
        self.assertEqual(2,result['features'][0]['toolSnapshot']['parameters']['wallThickness'])
        for tool in ("edge-arc-groove",):
            with self.assertRaisesRegex(ValueError,"平直"):
                runtime.prepare({"features":[{"toolTarget":"part","toolRef":{"id":tool}}],
                    "bounds":{"min":[0,-10,-10],"max":[1000,10,10]},
                    "targetSection":{**TARGET,"contours":[outer,inner]}})

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

    def v_geometry(self, tool_id="v-notch-sharp", **parameters):
        return self.prepare([{"toolTarget":"part","toolRef":{"id":tool_id},
            "toolParameters":parameters,"station":500}])["features"][0]["toolSnapshot"]["geometry"]

    def test_v_tools_are_independent_packages_with_geometry_only_parameters(self):
        tools={item["id"]:item for item in runtime.catalogue()["tools"]
               if item["id"].startswith("v-notch-") or item["id"]=="edge-arc-groove"}
        self.assertEqual({"v-notch-sharp","edge-arc-groove"},set(tools))
        self.assertTrue(all(not any(p.get("placement") for p in item["parameters"]) for item in tools.values()))
        self.assertNotIn("maleFemale", {p["key"] for p in tools["edge-arc-groove"]["parameters"]})
        geometries={key:json.dumps(self.v_geometry(key),sort_keys=True) for key in tools}
        self.assertEqual(len(geometries),len(set(geometries.values())))

    def test_sharp_v_is_a_local_mould_and_ignores_station_placement(self):
        base={"toolTarget":"part","toolRef":{"id":"v-notch-sharp"},
              "toolParameters":{"angle":90,"bridge":1},"station":120,"reference":"start"}
        first=self.prepare([base])["features"][0]["toolSnapshot"]["geometry"]
        moved={**base,"station":780,"reference":"end","rotation":0,
               "offsetY":12,"offsetZ":-4}
        second=self.prepare([moved])["features"][0]["toolSnapshot"]["geometry"]
        self.assertEqual("part-local",first["coordinateSpace"])
        self.assertEqual(first,second)

    def test_edge_arc_groove_is_a_box_minus_a_cylinder_with_one_arc_side(self):
        parameters={"angle":90,"bridge":1,"reliefDiameter":0,"reliefLift":0,
                    "bottomCut":False,"bottomCutWidth":2,"leftArc":True}
        left_geometry=self.v_geometry("edge-arc-groove",**parameters)
        right_geometry=self.v_geometry("edge-arc-groove",**{**parameters,"leftArc":False})
        geometries={"left":left_geometry,"right":right_geometry}
        for geometry in geometries.values():
            nodes={node["key"]:node for node in geometry["model"]["geometry"]}
            self.assertEqual("subtract",nodes["notch"]["arguments"]["operation"])
            self.assertEqual(["notch-base","arc-cylinder"],nodes["notch"]["inputs"])
            base=next(node for node in geometry["model"]["geometry"]
                      if node["key"]=="notch-base-profile")["arguments"]["contours"][0]["segments"]
            cylinder=next(node for node in geometry["model"]["geometry"]
                          if node["key"]=="arc-cylinder-profile")["arguments"]["contours"][0]["segments"]
            self.assertEqual(4,len(base))
            self.assertTrue(all(segment["kind"]=="line" for segment in base))
            self.assertEqual(2,len(cylinder))
            self.assertTrue(all(segment["kind"]=="arc" for segment in cylinder))
        self.assertEqual("part-local",left_geometry["coordinateSpace"])
        self.assertEqual("part-local",right_geometry["coordinateSpace"])

    def test_edge_arc_groove_ignores_external_feature_placement(self):
        base={"toolTarget":"part","toolRef":{"id":"edge-arc-groove"},
              "toolParameters":{"angle":90,"leftArc":True,"bridge":1},"station":120,
              "reference":"start","rotation":0,"offsetY":0,"offsetZ":0}
        first=self.prepare([base])["features"][0]["toolSnapshot"]["geometry"]
        moved={**base,"station":880,"reference":"end","rotation":0,
               "offsetY":12,"offsetZ":-4}
        second=self.prepare([moved])["features"][0]["toolSnapshot"]["geometry"]
        self.assertEqual(first,second)

    def slot_nodes(self, tool="v-notch-sharp", **parameters):
        return {n["key"]: n for n in self.v_geometry(tool, **parameters)["model"]["geometry"]}

    def test_slot_inner_reference_adds_only_explicit_wall(self):
        for tool, key, profile in (("v-notch-sharp", "leaveBottom", "notch-profile"),
                                   ("edge-arc-groove", "bridge", "notch-base-profile")):
            with self.subTest(tool=tool):
                inner = self.slot_nodes(tool, bottomReference="inner", wallThickness=2, **{key: 1})
                outer = self.slot_nodes(tool, wallThickness=2, **{key: 3})
                self.assertEqual(inner, outer)
                points = inner[profile]["arguments"]["contours"][0]["segments"]
                self.assertAlmostEqual(-7, min(s["start"][1] for s in points))
                self.slot_nodes(tool, bottomReference="inner")  # bound from the host, not guessed
                for values, message in (
                                        ({"bottomReference":"inner", "wallThickness":2, key:19}, "截面高度"),):
                    with self.assertRaisesRegex(ValueError, message):
                        self.slot_nodes(tool, **values)

    def test_slot_release_depth_is_measured_from_selected_outer_wall(self):
        for tool, settings in (("v-notch-sharp", {"bottomStrategy":"relief"}),
                               ("edge-arc-groove", {"reliefDiameter":1})):
            for side, sign in (("positive",1), ("negative",-1)):
                with self.subTest(tool=tool, side=side):
                    nodes = self.slot_nodes(tool, **settings, reliefDepth=2, reliefSide=side)
                    start = nodes["relief-profile"]["arguments"]["placement"]["origin"][1]
                    vector = nodes["relief"]["arguments"]["vector"][1]
                    self.assertEqual(sign * 21, start)
                    self.assertEqual(sign * 18, start + vector)
                    self.assertEqual(-42, nodes["notch" if tool=="v-notch-sharp" else "notch-base"]["arguments"]["vector"][1])
            through = self.slot_nodes(tool, **settings)
            self.assertEqual(-42, through["relief"]["arguments"]["vector"][1])
            with self.assertRaisesRegex(ValueError, "切深"):
                self.slot_nodes(tool, **settings, reliefDepth=41)

    def test_slot_flat_reference_preserves_legacy_and_offers_actual_floor(self):
        for mode, expected in (("apex", -8), ("actual", -9)):
            nodes = self.slot_nodes(bottomStrategy="flat", flatWidth=2, flatReference=mode)
            segments = nodes["notch-profile"]["arguments"]["contours"][0]["segments"]
            self.assertAlmostEqual(expected, min(s["start"][1] for s in segments))
        with self.assertRaisesRegex(ValueError, "平底宽度"):
            self.slot_nodes(bottomStrategy="flat", flatWidth=38)

    def test_slot_male_female_uses_physical_top_and_keeps_round_arcs(self):
        for strategy in ("sharp", "flat", "rounded", "relief"):
            nodes = self.slot_nodes(bottomStrategy=strategy, maleFemale=True, maleFemaleSize=0, wallThickness=2)
            segments = nodes["notch-profile"]["arguments"]["contours"][0]["segments"]
            self.assertAlmostEqual(8, segments[0]["start"][1])
            if strategy == "rounded":
                self.assertEqual(2, sum(s["kind"] == "arc" for s in segments))
            with self.assertRaisesRegex(ValueError, "公母尺寸"):
                self.slot_nodes(bottomStrategy=strategy, maleFemale=True, maleFemaleSize=19)
        self.slot_nodes(maleFemale=True, maleFemaleSize=0)
        with self.assertRaisesRegex(ValueError, "不能小于"):
            self.slot_nodes(maleFemale=True, maleFemaleSize=1, wallThickness=2)

    def test_slot_release_maximum_radius_has_no_zero_length_edges(self):
        for length, height in ((2,2), (4,2), (2,4)):
            nodes = self.slot_nodes(bottomStrategy="relief", reliefLength=length, reliefHeight=height, reliefRadius=1)
            segments = nodes["relief-profile"]["arguments"]["contours"][0]["segments"]
            for index, segment in enumerate(segments):
                self.assertGreater(math.dist(segment["start"], segment["end"]), 1e-9)
                self.assertEqual(segment["end"], segments[(index+1) % len(segments)]["start"])
        with self.assertRaisesRegex(ValueError, "短边"):
            self.slot_nodes(bottomStrategy="relief", reliefRadius=1)

    def test_edge_release_does_not_silently_cut_bottom(self):
        with self.assertRaisesRegex(ValueError, "切穿底面"):
            self.slot_nodes("edge-arc-groove", reliefDiameter=4)
        self.slot_nodes("edge-arc-groove", reliefDiameter=4, reliefLift=2)
        self.slot_nodes("edge-arc-groove", reliefDiameter=4, bottomCut=True)

    def test_slot_legacy_radius_really_activates_rounded_strategy(self):
        self.assertEqual(self.v_geometry(rootRadius=2), self.v_geometry(bottomStrategy="rounded", roundRadius=2))

    def test_slot_contours_remain_connected_across_angle_and_strategy_matrix(self):
        cases = [("edge-arc-groove", {"angle":angle, "leftArc":left})
                 for angle in (1,45,90,120,130) for left in (True,False)]
        cases += [("v-notch-sharp", {"angle":angle, "bottomStrategy":strategy, "maleFemale":male})
                  for angle in (1,45,90,135) for strategy in ("sharp","flat","rounded","relief")
                  for male in (True,False)]
        cases.append(("v-notch-sharp", {"asymmetric":True, "leftAngle":25, "rightAngle":65,
                                         "bottomStrategy":"rounded", "maleFemale":True}))
        for tool, parameters in cases:
            with self.subTest(tool=tool, parameters=parameters):
                for node in self.slot_nodes(tool, **parameters).values():
                    if node["operator"] != "profile2d":
                        continue
                    segments = node["arguments"]["contours"][0]["segments"]
                    for index, segment in enumerate(segments):
                        self.assertGreater(math.dist(segment["start"], segment["end"]), 1e-9)
                        self.assertEqual(segment["end"], segments[(index+1) % len(segments)]["start"])
                        self.assertTrue(all(math.isfinite(v) for point in (segment["start"],segment["end"]) for v in point))

    def test_slot_new_parameters_survive_recipe_and_frozen_replay(self):
        for tool, extra in (("v-notch-sharp", {"bottomStrategy":"relief", "flatReference":"actual"}),
                            ("edge-arc-groove", {"reliefDiameter":1})):
            parameters = dict(bottomReference="inner", wallThickness=2, reliefDepth=2, reliefSide="negative", **extra)
            feature = {"toolTarget":"part", "toolRef":{"id":tool}, "toolParameters":parameters, "station":500}
            saved = self.prepare([feature])["recipe"]["features"][0]
            for key, value in parameters.items():
                self.assertEqual(value, saved["toolParameters"][key])
            self.assertEqual(saved["frozenTool"], self.prepare([saved])["recipe"]["features"][0]["frozenTool"])

    def test_k_factor_changes_real_contour_by_documented_allowance(self):
        for tool, extra, profile in (("v-notch-sharp", {"bottomStrategy":"rounded"}, "notch-profile"),
                                     ("edge-arc-groove", {}, "notch-base-profile")):
            for angle in (45,90,120):
                for k in (0,0.62,1):
                    with self.subTest(tool=tool, angle=angle, k=k):
                        base = self.slot_nodes(tool, angle=angle, wallThickness=2, **extra)
                        compensated = self.slot_nodes(tool, angle=angle, wallThickness=2, **extra,
                            bendCompensation=True, useDefaultKFactor=False, kFactor=k)
                        a = base[profile]["arguments"]["contours"][0]["segments"]
                        b = compensated[profile]["arguments"]["contours"][0]["segments"]
                        delta = 2 * math.pi * k * 2 * angle / 360
                        width = lambda segments: max(s["start"][0] for s in segments)-min(s["start"][0] for s in segments)
                        self.assertAlmostEqual(delta, width(b)-width(a))
                        self.assertEqual([s["start"][1] for s in a], [s["start"][1] for s in b])
                        for index, segment in enumerate(b):
                            self.assertEqual(segment["end"], b[(index+1)%len(b)]["start"])

    def test_rounded_v_preserves_original_root_arc_construction(self):
        for left, right in ((0.5,0.5),(22.5,22.5),(45,45),(25,65),(75,10)):
            for k in (0,0.62,1):
                nodes = self.slot_nodes(bottomStrategy="rounded", roundRadius=2,
                    asymmetric=True, leftAngle=left, rightAngle=right,
                    bendCompensation=True, useDefaultKFactor=False, kFactor=k)
                edges = nodes["notch-profile"]["arguments"]["contours"][0]["segments"]
                arcs = [e for e in edges if e["kind"]=="arc"]
                self.assertEqual(2,len(arcs))
                for arc, angle, is_left in ((arcs[0],right,False),(arcs[1],left,True)):
                    # A root point is at the circle's bottom; derive its center
                    # independently and verify all three arc points.
                    root = arc["start"] if is_left else arc["end"]
                    slope = arc["end"] if is_left else arc["start"]
                    center = [root[0],root[1]+2]
                    for key in ("start","middle","end"):
                        self.assertAlmostEqual(2,math.dist(center,arc[key]),places=9)
                    a=math.radians(angle)
                    self.assertAlmostEqual(center[0]+(1 if is_left else -1)*2*math.sin(a),slope[0])
                    self.assertAlmostEqual(center[1]-2*math.cos(a),slope[1])
                    self.assertAlmostEqual(-9,root[1])

    def test_asymmetric_flat_preserves_virtual_apex_rays(self):
        left, right = math.tan(math.radians(25)), math.tan(math.radians(65))
        nodes=self.slot_nodes(bottomStrategy="flat",flatWidth=4,flatReference="apex",
                              asymmetric=True,leftAngle=25,rightAngle=65)
        edges=nodes["notch-profile"]["arguments"]["contours"][0]["segments"]
        floor=-9+4/(left+right)
        self.assertAlmostEqual(floor,edges[0]["end"][1])
        self.assertAlmostEqual(-(floor+9)*left,edges[0]["end"][0])
        self.assertAlmostEqual((floor+9)*right,edges[1]["end"][0])
        self.assertAlmostEqual(-20*left,edges[0]["start"][0])
        self.assertAlmostEqual(20*right,edges[2]["end"][0])

    def test_edge_arc_gap_formula_and_crossing_rejection(self):
        for angle in (1,45,90,120,130):
            theta=math.radians(angle)
            nodes=self.slot_nodes("edge-arc-groove",angle=angle)
            radius=19/(2*math.sin(theta/2)**2)
            center=nodes["arc-cylinder-profile"]["arguments"]["placement"]["origin"]
            self.assertAlmostEqual(-radius*math.sin(theta),center[0])
            self.assertAlmostEqual(-9+radius,center[2])
            # Check the actual gap throughout the cut height, not just
            # continuity of the input rectangle before cylinder subtraction.
            expected=radius*(theta-math.tan(theta/2))
            self.assertGreater(expected,0)
            for i in range(101):
                z=19*i/100
                gap=radius*theta+z/math.tan(theta)-math.sqrt(max(0,2*radius*z-z*z))
                self.assertGreaterEqual(gap+1e-7,expected)
        for angle in (135,150,170):
            for side in (True,False):
                with self.assertRaisesRegex(ValueError,"顶部槽宽"):
                    self.slot_nodes("edge-arc-groove",angle=angle,leftArc=side)

    def test_default_k_factor_and_asymmetric_angle(self):
        extra = dict(bottomStrategy="rounded", wallThickness=2, bendCompensation=True)
        self.assertEqual(self.slot_nodes(**extra, kFactor=1),
                         self.slot_nodes(**extra, useDefaultKFactor=False, kFactor=0.62))
        for side in (True,False):
            nodes = self.slot_nodes("edge-arc-groove", leftArc=side, wallThickness=2, bendCompensation=True)
            base = self.slot_nodes("edge-arc-groove", leftArc=side, wallThickness=2)
            x = nodes["arc-cylinder-profile"]["arguments"]["placement"]["origin"][0]
            old_x = base["arc-cylinder-profile"]["arguments"]["placement"]["origin"][0]
            self.assertAlmostEqual((-1 if side else 1)*0.62*math.pi/2, x-old_x)
        base = self.slot_nodes(**extra, asymmetric=True, leftAngle=25, rightAngle=65, useDefaultKFactor=False, kFactor=0)
        nodes = self.slot_nodes(**extra, asymmetric=True, leftAngle=25, rightAngle=65)
        a = base["notch-profile"]["arguments"]["contours"][0]["segments"]
        b = nodes["notch-profile"]["arguments"]["contours"][0]["segments"]
        self.assertAlmostEqual(0.62*math.pi/2, a[0]["start"][0]-b[0]["start"][0])

    def test_k_factor_validation_and_persistence(self):
        for tool, extra in (("v-notch-sharp", {"bottomStrategy":"rounded"}), ("edge-arc-groove", {})):
            self.slot_nodes(tool, **extra, bendCompensation=True)  # uses target wall thickness
            for k in (-0.1,1.1,float("nan"),True):
                with self.assertRaises(ValueError):
                    self.slot_nodes(tool, **extra, wallThickness=2, bendCompensation=True, useDefaultKFactor=False, kFactor=k)
            parameters = dict(extra, wallThickness=2, bendCompensation=True, useDefaultKFactor=False, kFactor=0.8)
            saved = self.prepare([{"toolTarget":"part", "toolRef":{"id":tool}, "toolParameters":parameters}])["recipe"]["features"][0]
            self.assertEqual(0.8, saved["toolParameters"]["kFactor"])
            self.assertEqual(saved["frozenTool"],self.prepare([saved])["recipe"]["features"][0]["frozenTool"])
        self.assertEqual(self.slot_nodes(wallThickness=2),
                         self.slot_nodes(wallThickness=2, bendCompensation=True),
                         "Switching away from rounded ignores the retained compensation switch")

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
            "bounds": {"min": [0, -20, -10], "max": [1000, 20, 10]}, "targetSection":TARGET}, {})

    def test_catalogue_has_programmatic_and_both_end_shapes(self):
        catalogue = runtime.catalogue()
        self.assertEqual([], catalogue["errors"])
        items = {t["id"]: t for t in catalogue["tools"]}
        self.assertNotIn("diamond-12", items)
        self.assertEqual("end", items["end-convex"]["target"])
        self.assertEqual("end", items["end-cope"]["target"])
        for key in ("end-key-joint", "end-step-z", "end-profile"):
            self.assertEqual("end", items[key]["target"])
        self.assertTrue(items["end-profile"]["requiresSection"])

    def test_system_standard_moulds_are_categorized_and_generate_profiles(self):
        items = {tool["id"]: tool for tool in runtime.catalogue()["tools"]}
        expected_hole_types = ("circle", "square", "rectangle", "ellipse",
                               "hexagon", "triangle", "single-d", "double-d", "keyhole")
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
        self.assertEqual("孔型", items["slot"]["category"])
        self.assertTrue(slot["features"][0]["toolSnapshot"]["geometry"]["contours"])
        for key in ("v-notch-sharp", "edge-arc-groove"):
            self.assertEqual("槽口", items[key]["category"])
        self.assertEqual("支管", items["branch-profile"]["category"])
        self.assertEqual("端面", items["end-profile"]["category"])

    def test_d_holes_use_one_reference_circle(self):
        for tool_id, parameters in (
            ("single-d", {"diameter": 20, "flatOffset": 6}),
            ("single-d", {"diameter": 20, "flatOffset": 0}),
            ("double-d", {"diameter": 20, "flatSpacing": 12}),
        ):
            with self.subTest(tool_id=tool_id, parameters=parameters):
                result = self.prepare([{"toolTarget": "side", "station": 500,
                    "toolRef": {"id": tool_id}, "toolParameters": parameters}])
                segments = result["features"][0]["toolSnapshot"]["geometry"]["contours"][0]["segments"]
                for i, segment in enumerate(segments):
                    self.assertEqual(segment["end"], segments[(i+1) % len(segments)]["start"])
                    if segment["kind"] == "arc":
                        for key in ("start", "middle", "end"):
                            self.assertAlmostEqual(100, sum(v*v for v in segment[key]))
                if tool_id == "double-d":
                    self.assertEqual(2, sum(s["kind"] == "line" for s in segments))
                    self.assertEqual([-8, -6], segments[0]["start"])
                    self.assertEqual([8, -6], segments[0]["end"])

    def test_keyhole_has_connected_head_neck_and_round_end(self):
        result = self.prepare([{"toolTarget": "side", "station": 500,
            "toolRef": {"id": "keyhole"}, "toolParameters": {}}])
        segments = result["features"][0]["toolSnapshot"]["geometry"]["contours"][0]["segments"]
        for i, segment in enumerate(segments):
            self.assertEqual(segment["end"], segments[(i+1) % 4]["start"])
        self.assertEqual([22, 0], segments[1]["middle"])
        self.assertEqual([-10, 0], segments[3]["middle"])
        for point in ("start", "middle", "end"):
            self.assertAlmostEqual(100, sum(v*v for v in segments[3][point]))
            x, y = segments[1][point]
            self.assertAlmostEqual(16, (x-18)**2+y*y)

    def test_functional_holes_reject_degenerate_and_legacy_dimensions(self):
        for tool_id, parameters in (
            ("single-d", {"flatOffset": 10}),
            ("double-d", {"flatSpacing": 20}),
            ("keyhole", {"neckWidth": 20}),
            ("keyhole", {"centerDistance": 5}),
            ("single-d", {"spanAlong": 20, "spanAcross": 12}),
            ("double-d", {"spanAlong": 20, "spanAcross": 12}),
        ):
            with self.subTest(tool_id=tool_id, parameters=parameters):
                with self.assertRaises(ValueError):
                    self.prepare([{"toolTarget": "side", "station": 500,
                        "toolRef": {"id": tool_id}, "toolParameters": parameters}])

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
                    if kind == "fixed":
                        with self.assertRaisesRegex(ValueError, "未声明"):
                            self.prepare([{"toolRef": {"id": folder.name}, "toolParameters": {"diameter": 99}}])
                self.assertEqual(2, len(runtime.catalogue()["tools"]))
        finally:
            runtime.ROOT = previous

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
