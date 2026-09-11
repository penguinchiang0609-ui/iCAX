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
        moved={**base,"station":780,"reference":"end","rotation":37,
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
        moved={**base,"station":880,"reference":"end","rotation":37,
               "offsetY":12,"offsetZ":-4}
        second=self.prepare([moved])["features"][0]["toolSnapshot"]["geometry"]
        self.assertEqual(first,second)

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
        for key in ("v-notch-sharp", "edge-arc-groove"):
            self.assertEqual("槽口", items[key]["category"])
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
