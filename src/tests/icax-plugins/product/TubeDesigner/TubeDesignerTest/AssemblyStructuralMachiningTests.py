"""Local machining reuse, actual placement and atomic repeated application."""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=next(parent for parent in Path(__file__).resolve().parents if (parent/"src/apps/tube-designer/templates").is_dir())
sys.path.insert(0,str(ROOT/"src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_sdk import NeutralModel

def load(name):
    path=ROOT/"src/apps/tube-designer/templates/_shared"/(name+".py")
    spec=importlib.util.spec_from_file_location("structural_test_"+name,path)
    module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
    return module

RUNTIME=load("assembly_geometry_process_runtime")
RULES=load("assembly_structural_machining")

def process(geometry):
    return {"schema":"icax.assembly-process-input","schemaVersion":1,
            "parts":{"stock":{"id":"arbitrary-stock","length":300,"section":{"width":20,"depth":20,"wall":1}}},
            "geometry":deepcopy(geometry)}

def region(x=0):
    return {"placement":{"origin":[x,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "polygon":[[-10,-10],[10,-10],[10,10],[-10,10]],"vector":[0,0,300]}

class AssemblyStructuralMachiningTests(unittest.TestCase):
    def test_profile_dependency_revision_invalidates_repeated_stock_execution(self):
        data=process({"receivers":[{"placement":region()["placement"],
            "contours":[{"kind":"circle","center":[0,0],"radius":5}],
            "vector":[0,0,300],"clearance":.3}]})
        saved=deepcopy(data)
        baseline=RUNTIME.evaluate("structural-stock-fit",data,{})
        self.assertTrue(baseline["applicable"],baseline["reason"])
        original_read=Path.read_bytes
        for dependency in ("tube_profile_catalog.py","profile_package_runtime.py"):
            with self.subTest(dependency=dependency):
                model=NeutralModel(template_id="dependency-revision",template_version="1",package_digest="",parameters={})
                profile=model.geometry("stock.profile","profile2d",arguments={
                    "placement":region()["placement"],"contours":[RULES.path(region()["polygon"])]})
                raw=model.geometry("stock.solid","extrude",inputs=[profile],arguments={"vector":[0,0,300]})
                first=RUNTIME.invoke(model,raw,"structural-stock-fit",data,{},"before","same-stock")
                before=len(model._document["geometry"])
                dependency_path=Path(RULES.__file__).with_name(dependency)
                def revised_bytes(path):
                    value=original_read(path)
                    return value+b"\n# dependency revision\n" if path==dependency_path else value
                with patch.object(Path,"read_bytes",revised_bytes):
                    revised=RUNTIME.evaluate("structural-stock-fit",data,{})
                    second=RUNTIME.invoke(model,raw,"structural-stock-fit",data,{},"after","same-stock")
                self.assertTrue(revised["applicable"],revised["reason"])
                self.assertEqual(revised["geometry"],baseline["geometry"])
                self.assertEqual(revised["parameters"],baseline["parameters"])
                self.assertNotEqual(revised["functionDigest"],baseline["functionDigest"])
                self.assertNotEqual(first,second,"A dependency revision must not reuse the previous processed stock")
                self.assertGreater(len(model._document["geometry"]),before)
                instances=model.build()["extensions"][RUNTIME.EXTENSION]["instances"]
                self.assertNotEqual(instances[0]["executionSignature"],instances[1]["executionSignature"])
                self.assertEqual(data,saved)

    def test_ellipse_observation_uses_exact_conic_without_changing_nominal_section(self):
        center=[12,-7];rotation=.37
        source=[{"kind":"path","closed":True,"segments":[{"kind":"ellipseArc","center":center,
            "majorRadius":50,"minorRadius":10,"rotation":rotation,"startAngle":0,"endAngle":2*math.pi}]}]
        before=deepcopy(source);contours=RULES.exact_section_contours(source)
        self.assertEqual(source,before);self.assertEqual(len(contours[0]["segments"]),4)
        for curve in contours[0]["segments"]:
            self.assertEqual(curve["kind"],"nurbs");self.assertEqual(curve["degree"],2)
            for t in (0,.1,.3,.5,.8,1):
                basis=[(1-t)**2,2*t*(1-t),t*t]
                weights=[basis[i]*curve["weights"][i] for i in range(3)]
                point=[sum(weights[i]*curve["controlPoints"][i][j] for i in range(3))/sum(weights) for j in range(2)]
                x=(point[0]-center[0])*math.cos(rotation)+(point[1]-center[1])*math.sin(rotation)
                y=-(point[0]-center[0])*math.sin(rotation)+(point[1]-center[1])*math.cos(rotation)
                self.assertAlmostEqual((x/50)**2+(y/10)**2,1,places=13)
        reversed_input=deepcopy(source);reversed_input[0]["segments"][0]["reversed"]=True
        reverse=RULES.exact_section_contours(reversed_input)[0]["segments"]
        self.assertTrue(all(curve["reversed"] for curve in reverse))
        self.assertEqual([curve["controlPoints"] for curve in reverse],
                         [curve["controlPoints"] for curve in contours[0]["segments"]][::-1])

    def test_clearance_is_applied_by_function_once_to_actual_outer_section(self):
        catalog=load("tube_profile_catalog")
        profile=catalog.load_profile({"memberProfileType":"rect","memberWidth":20,"memberDepth":30,
            "memberWallThickness":1.2,"memberCornerRadius":1,"memberInnerCornerRadius":1},"member")
        nominal=profile.contours()[:1]
        receiver={"geometryKind":"stock-section","placement":region()["placement"],"contours":nominal,
                  "vector":[0,0,300],"clearance":.3,"profileEnvelope":{"kind":"system","profileId":profile.profile_id,
                  "profileData":deepcopy(profile._profile_data)}}
        data=process({"receivers":[receiver]});before=deepcopy(data)
        answer=RUNTIME.evaluate("structural-stock-fit",data,{})
        self.assertTrue(answer["applicable"],answer["reason"])
        section=next(node for node in answer["geometry"] if node["key"]=="receiver.0.profile")
        self.assertEqual(section["arguments"]["contours"],profile.contours(clearance=.3)[:1])
        self.assertEqual(len(section["arguments"]["contours"]),1)
        self.assertEqual(data,before);self.assertEqual(receiver["contours"],nominal)

    def test_observed_diagonal_stock_keeps_exact_local_section_and_complete_vector(self):
        q=2**-.5;world=[5,7,11]
        contour=RULES.path([[-8,-3],[12,-3],[12,7],[-8,7]])
        section={"geometryKind":"stock-section","sourceMemberId":"arbitrary-diagonal",
                 "placement":{"origin":[1000,2000,3000],"xAxis":[q,q,0],"yAxis":[0,0,1]},
                 "contours":[contour],"vector":world}
        answer=RUNTIME.evaluate("structural-stock-fit",process({"receivers":[section]}),{})
        self.assertTrue(answer["applicable"],answer["reason"])
        nodes={node["key"]:node for node in answer["geometry"]}
        profile=nodes["receiver.0.profile"]["arguments"]
        self.assertEqual(profile["placement"],{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]})
        self.assertEqual(profile["contours"],[contour])
        placement=nodes["receiver.0.solid"]["arguments"]["placement"]
        self.assertEqual(placement["origin"],[1000,2000,3000])
        local=nodes["receiver.0.extrude"]["arguments"]["vector"]
        reconstructed=[sum(local[j]*placement[axis][i] for j,axis in enumerate(("xAxis","yAxis","zAxis"))) for i in range(3)]
        for actual,expected in zip(reconstructed,world):self.assertAlmostEqual(actual,expected)
        self.assertNotEqual(local[:2],[0,0],"full signed vector projection must retain its lateral components")

    def test_rules_have_no_product_shape_or_global_parameter_dependency(self):
        examples={
            "structural-stock-fit":(process({"keepRegions":[region(25)],"receivers":[region(30)]}),{}),
            "structural-profile-pocket":(process({"section":region(30),"limits":[region(25)]}),{}),
            "structural-apertures":(process({"holes":[{"diameter":8,"placement":region(25)["placement"],"vector":[0,0,25]}]}),{}),
            "structural-plate-apertures":(process({"width":100,"height":80,"thickness":4,"center":[100,200,300],"xAxis":[0,1,0],"yAxis":[0,0,1]}),
                                            {"holes":[{"x":20,"y":10,"diameter":8}]}),
            "structural-male-head":(process({"visibleLeft":20,"visibleRight":280,"centerZ":50,"profileWidth":20,"profileDepth":20,"wall":1}),
                                    {"length":10,"width":8,"corner":"round","size":2}),
            "structural-planar-trim":(process({"boundsPolygon":[[-100,-100],[100,-100],[100,100],[-100,100]],
                                       "planes":[{"normal":[1,1],"limit":0}],"placement":region()["placement"],"vector":[0,0,40]}),{})}
        for function_id,(data,parameters) in examples.items():
            with self.subTest(function=function_id):
                old_input=deepcopy(data);old_parameters=deepcopy(parameters)
                answer=RUNTIME.evaluate(function_id,data,parameters)
                self.assertTrue(answer["applicable"],answer["reason"])
                self.assertEqual(answer["parameters"],parameters)
                self.assertEqual(data,old_input);self.assertEqual(parameters,old_parameters)
                self.assertTrue(answer["operations"])
                self.assertTrue(all(operation["role"]=="stock" for operation in answer["operations"]))

    def test_plate_hole_uses_actual_local_frame_and_rejects_cutting_edge_material(self):
        data=process({"width":100,"height":80,"thickness":4,"center":[100,200,300],"xAxis":[0,1,0],"yAxis":[0,0,1]})
        answer=RUNTIME.evaluate("structural-plate-apertures",data,{"holes":[{"x":20,"y":10,"diameter":8}]})
        profile=answer["geometry"][0]["arguments"]["placement"]
        self.assertEqual(profile["origin"],[97,220,310])
        self.assertEqual(answer["geometry"][1]["arguments"]["vector"],[6,0,0])
        failed=RUNTIME.evaluate("structural-plate-apertures",data,{"holes":[{"x":46,"y":0,"diameter":8}]})
        self.assertFalse(failed["applicable"])
        self.assertEqual(failed["geometry"],[]);self.assertEqual(failed["operations"],[])

    def test_two_operations_on_same_stock_and_retry_preserve_one_execution(self):
        model=NeutralModel(template_id="local-stock",template_version="1",package_digest="",parameters={})
        profile=model.geometry("stock.profile","profile2d",arguments={"placement":region()["placement"],"contours":[RULES.path(region()["polygon"])]})
        raw=model.geometry("stock.solid","extrude",inputs=[profile],arguments={"vector":[0,0,300]})
        def hole(station):
            return process({"holes":[{"diameter":8,"placement":{"origin":[-11,0,station],"xAxis":[0,1,0],"yAxis":[0,0,1]},"vector":[22,0,0]}]})
        first=RUNTIME.invoke(model,raw,"structural-apertures",hole(80),{},"left-hole","same-stock")
        second=RUNTIME.invoke(model,first,"structural-apertures",hole(220),{},"right-hole","same-stock")
        snapshot=deepcopy(model.build())
        self.assertEqual(RUNTIME.invoke(model,first,"structural-apertures",hole(220),{},"right-hole","same-stock"),second)
        self.assertEqual(model.build(),snapshot)
        records=model.build()["extensions"][RUNTIME.EXTENSION]["instances"]
        self.assertEqual([record["stockId"] for record in records],["same-stock","same-stock"])
        self.assertEqual(records[1]["targetGeometry"],first)
        with self.assertRaises(ValueError):RUNTIME.invoke(model,second,"structural-apertures",hole(230),{},"right-hole","same-stock")
        self.assertEqual(model.build(),snapshot)

    def test_actual_neighbor_controls_bisector_and_blank_projection(self):
        recipe,normal=RULES.joint_plane([10,20,30],[1,0,0],[0,1,0],500)
        self.assertEqual(recipe["placement"]["origin"],[10,20,30])
        self.assertAlmostEqual(normal[0],2**-.5);self.assertAlmostEqual(normal[1],-2**-.5)
        self.assertAlmostEqual(RULES.joint_blank_extension([normal],[0,1,0],[0,0,1],[1,0,0],20,40),10)
        _,square=RULES.joint_plane([10,20,30],[1,0,0],None,500)
        self.assertEqual(square,[1,0,0])
        with self.assertRaises(ValueError):RULES.joint_plane([0,0,0],[1,0,0],[1,0,0],500)

if __name__=="__main__":unittest.main()
