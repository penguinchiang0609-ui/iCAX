import unittest
from contextlib import contextmanager, ExitStack
from unittest.mock import patch
from ProfileFamilyTests import runtime, ROOT

CATALOG=ROOT/"src/apps/tube-designer/templates/profile"


@contextmanager
def forbid_forward_evaluation():
    original_context = runtime._script_context
    attempts = []

    def forbidden(*args, **kwargs):
        attempts.append(True)
        raise AssertionError("Forward geometry evaluation is forbidden during fitting")

    @contextmanager
    def guarded_script(*args, **kwargs):
        with original_context(*args, **kwargs) as namespace:
            namespace["build"] = forbidden
            yield namespace

    with ExitStack() as stack:
        stack.enter_context(patch.object(runtime, "_evaluate", side_effect=forbidden))
        stack.enter_context(patch.object(runtime, "evaluate_section", side_effect=forbidden))
        stack.enter_context(patch.object(runtime, "_script_context", guarded_script))
        yield
    if attempts:
        raise AssertionError("The inverse path attempted forward geometry evaluation")


class Fitting(unittest.TestCase):
    def test_p_tube_zero_inner_radii_survive_transformed_inverse_rebuild(self):
        import copy
        import importlib.util
        import math
        spec = importlib.util.spec_from_file_location(
            "p_tube_rounding_geometry",
            ROOT / "src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py")
        g = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(g)
        package = runtime._system_package(CATALOG / "p-tube", preview=False)

        def evaluate(values):
            original_values = copy.deepcopy(values)
            section = runtime._evaluate(package["descriptor"], package["scriptSource"], values,
                package["packageDigest"], package["sourceFileName"], package["resources"])
            self.assertEqual(original_values, values)
            self.assertEqual(original_values, section["parameters"])
            return section

        for scale in (1, 1.23, 2.17):
            for mirror in (False, True):
                values = {**package["defaultParameters"], "width": 40 * scale,
                    "depth": 40 * scale, "wallThickness": 3 * scale,
                    "flangeLength": 30 * scale, "flangeThickness": 3 * scale,
                    "outerRadius": 3 * scale, "mirrorX": mirror}
                base = g.normalize(evaluate(values), 0.001)
                for angle in (0, 37, 123):
                    with self.subTest(scale=scale, mirror=mirror, angle=angle):
                        original = g.transform(base, math.radians(angle), [123, -45])
                        with forbid_forward_evaluation():
                            response = runtime.generate({"action": "recognize-system",
                                "profileRoot": str(CATALOG), "section": g.to_section(original)}, {})
                        match = next(item for item in response["results"] if item["profileId"] == "p-tube")
                        self.assertEqual("matched", match["status"], match)
                        candidate = match["candidates"][0]
                        self.assertFalse(candidate["parameters"]["useIndependentInnerRadii"])
                        rebuilt = evaluate({**package["defaultParameters"], **candidate["parameters"]})
                        self.assertTrue(all(edge["kind"] == "line"
                            for edge in rebuilt["contours"][1]["segments"]))
                        equivalent, error = g.verify(original, g.normalize(rebuilt, 0.001),
                                                     candidate["transform"], 0.001)
                        self.assertTrue(equivalent, (candidate, error))

        # A small but real positive inner radius must remain an arc. The
        # rounding fix must not use the much larger recognition tolerance.
        values = {**package["defaultParameters"], "outerRadius": 3.0001}
        section = evaluate(values)
        arcs = [edge for edge in section["contours"][1]["segments"] if edge["kind"] == "arc"]
        self.assertEqual(3, len(arcs))
        for edge in arcs:
            self.assertAlmostEqual(0.0001, g.arc(edge["start"], edge["middle"], edge["end"])["radius"], places=8)

    def test_inactive_end_radius_drafts_do_not_affect_inverse_geometry(self):
        import importlib.util
        import math
        spec = importlib.util.spec_from_file_location(
            "inactive_radius_geometry",
            ROOT / "src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py")
        g = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(g)
        for name in ("angle", "channel"):
            with self.subTest(template=name):
                package = runtime._system_package(CATALOG / name, preview=False)
                values = {**package["defaultParameters"], "useHotRolled": False,
                    "useIndependentFreeEndRadii": True, "freeEndRadius": 1,
                    "freeEndRadius1": 0.5, "freeEndRadius2": 0.75}
                raw = runtime._evaluate(package["descriptor"], package["scriptSource"], values,
                    package["packageDigest"], package["sourceFileName"], package["resources"])
                original = g.transform(g.normalize(raw, 0.001), math.radians(37), [123, -45])
                with forbid_forward_evaluation():
                    response = runtime.generate({"action": "recognize", "package": package,
                        "section": g.to_section(original)}, {})
                self.assertTrue(response["matched"], response)
                candidate = response["results"][0]["candidates"][0]
                recovered = candidate["parameters"]
                self.assertFalse(recovered["useHotRolled"])
                self.assertFalse(recovered["useIndependentFreeEndRadii"])
                self.assertEqual([0, 0, 0], [recovered[key]
                    for key in ("freeEndRadius", "freeEndRadius1", "freeEndRadius2")])
                rebuilt = runtime._evaluate(package["descriptor"], package["scriptSource"],
                    {**package["defaultParameters"], **recovered}, package["packageDigest"],
                    package["sourceFileName"], package["resources"])
                equivalent, error = g.verify(original, g.normalize(rebuilt, 0.001),
                                             candidate["transform"], 0.001)
                self.assertTrue(equivalent, (candidate, error))

    def test_angle_sharp_roots_and_larger_inside_radius(self):
        import importlib.util
        import math
        from contextlib import contextmanager
        from unittest.mock import patch
        spec=importlib.util.spec_from_file_location('sharp_angle_geometry',ROOT/'src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py')
        g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
        package=runtime._system_package(CATALOG/'angle',preview=False)
        original_context=runtime._script_context
        def forbidden(*args,**kwargs):
            raise AssertionError('forward forbidden')
        @contextmanager
        def guarded_context(*args,**kwargs):
            with original_context(*args,**kwargs) as namespace:
                namespace['build']=forbidden
                yield namespace
        for outer,inner,toe in ((0,0,0),(0,6,6),(3,0,0),(2,6,1)):
            for mirror in (False,True):
                with self.subTest(outer=outer,inner=inner,toe=toe,mirror=mirror):
                    values={**package['defaultParameters'],'width':96,'depth':96,
                        'wallThickness':8,'outerRadius':outer,'useInnerRadius':True,
                        'innerRadius':inner,'useHotRolled':toe>0,
                        'freeEndRadius':toe,'mirrorX':mirror}
                    raw=runtime._evaluate(package['descriptor'],package['scriptSource'],values,
                        package['packageDigest'],package['sourceFileName'],package['resources'])
                    original=g.transform(g.normalize(raw,0.001),math.radians(37),[123,-45])
                    with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')), \
                         patch.object(runtime,'evaluate_section',side_effect=AssertionError('forward forbidden')), \
                         patch.object(runtime,'_script_context',guarded_context):
                        # Include catalogue loading in the guarded inverse path.
                        response=runtime.generate(dict(action='recognize-system',
                            profileRoot=str(CATALOG),section=g.to_section(original)),{})
                    self.assertTrue(response['matched'],response)
                    match=next(r for r in response['results'] if r['profileId']=='angle')
                    candidate=match['candidates'][0]
                    self.assertAlmostEqual(outer,candidate['parameters']['outerRadius'])
                    self.assertAlmostEqual(inner,candidate['parameters']['innerRadius'])
                    rebuilt=runtime._evaluate(package['descriptor'],package['scriptSource'],
                        {**package['defaultParameters'],**candidate['parameters']},
                        package['packageDigest'],package['sourceFileName'],package['resources'])
                    ok,error=g.verify(original,g.normalize(rebuilt,0.001),candidate['transform'],0.001)
                    self.assertTrue(ok,(candidate,error))

    def test_unsupported_exact_geometry_is_reported_without_forward_calls(self):
        from unittest.mock import patch
        section={"contours":[{"kind":"path","closed":True,"segments":[
            {"kind":"bezier","start":[0,0],"end":[0,0],"poles":[[0,0],[1,2],[2,1],[0,0]]}]}]}
        with patch.object(runtime,"_evaluate",side_effect=AssertionError("forward forbidden")), \
             patch.object(runtime,"evaluate_section",side_effect=AssertionError("forward forbidden")):
            result=runtime.generate({"action":"recognize-system","section":section,
                "profileRoot":str(CATALOG)},{})
        self.assertFalse(result["matched"])
        self.assertEqual(21,len(result["results"]))
        self.assertTrue(all(r["status"]=="unsupported-geometry" for r in result["results"]),result)

    def test_all_declared_models_and_scaled_sections(self):
        import importlib.util
        import math
        from unittest.mock import patch
        spec=importlib.util.spec_from_file_location('all_models_geometry',ROOT/'src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py')
        g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
        count=0
        expected=0
        for directory in CATALOG.iterdir():
            if not (directory/'profile.json').exists():continue
            package=runtime._system_package(directory,preview=False)
            selector=next((d for d in package['descriptor']['parameters'] if d['key'] in ('sectionModel','manufacturingRoute')),None)
            variants=[{selector['key']:o['value']} for o in selector['options']] if selector else [{}]
            for variant in variants:
                for scale in (1,1.23):
                    expected+=1
                    with self.subTest(template=directory.name,variant=variant,scale=scale):
                        parameters={**package['defaultParameters'],**variant}
                        definitions={item['key']:item for item in package['descriptor']['parameters']}
                        parameters={k:(v*scale if definitions[k]['valueType']=='number'
                            and not any(s in k.lower() for s in ('angle','phase','slope')) else v)
                            for k,v in parameters.items()}
                        raw=runtime._evaluate(package['descriptor'],package['scriptSource'],parameters,package['packageDigest'],package['sourceFileName'],package['resources'])
                        original=g.transform(g.normalize(raw,0.001),math.radians(37),[123,-45])
                        # Imported contours may start anywhere, run clockwise,
                        # and split a straight edge into multiple segments.
                        imported=[]
                        for loop in original:
                            if loop['kind']!='path':imported.append(loop);continue
                            edges=[]
                            for e in loop['edges']:
                                if e['kind']=='line':
                                    mid=[(e['start'][d]+e['end'][d])/2 for d in (0,1)]
                                    edges.extend([{**e,'end':mid},{**e,'start':mid}])
                                else:edges.append(e)
                            edges=[g._reverse(e) for e in reversed(edges)]
                            imported.append({**loop,'edges':edges[1:]+edges[:1]})
                        with forbid_forward_evaluation():
                            response=runtime.generate(dict(action='recognize',package=package,section=g.to_section(list(reversed(imported)))),{})
                        self.assertTrue(response['matched'],response)
                        candidate=response['results'][0]['candidates'][0]
                        # Forward evaluation here is an independent TEST oracle only;
                        # it was unavailable for the entire inverse call above.
                        recovered={**package['defaultParameters'],**candidate['parameters']}
                        rebuilt=runtime._evaluate(package['descriptor'],package['scriptSource'],recovered,package['packageDigest'],package['sourceFileName'],package['resources'])
                        ok,error=g.verify(original,g.normalize(rebuilt,0.001),candidate['transform'],0.001)
                        self.assertTrue(ok,(candidate,error))
                        count+=1
        self.assertEqual(count,expected)

    def test_all_templates_reject_extra_unaccounted_boundary(self):
        import copy
        from unittest.mock import patch
        for directory in CATALOG.iterdir():
            if not (directory/'profile.json').exists():continue
            with self.subTest(template=directory.name):
                package=runtime._system_package(directory,preview=True)
                section=copy.deepcopy(package['previewProfile'])
                section['contours'].append(dict(kind='circle',center=[10000,10000],radius=1))
                with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')):
                    result=runtime.generate(dict(action='recognize',package=package,section=section),{})
                self.assertFalse(result['matched'],result)
                self.assertEqual('no-match',result['results'][0]['status'])

    def test_new_direct_inverses_transformed_nondefaults(self):
        import math
        import importlib.util
        from unittest.mock import patch
        spec=importlib.util.spec_from_file_location('direct_test_geometry',ROOT/'src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py')
        g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
        cases=[('u-section',dict(width=320,bottomWidth=180,depth=250,wallThickness=9,bendRadius=25)),
            ('omega',dict(width=70,depth=90,flangeWidth=25,wallThickness=3,bendRadius=4)),
            ('z-section',dict(depth=90,topFlangeWidth=43,bottomFlangeWidth=60,
                              wallThickness=3,bendRadius=4,useLips=False)),
            ('z-section',dict(depth=90,topFlangeWidth=43,bottomFlangeWidth=60,
                              wallThickness=3,bendRadius=4,useLips=True,
                              lipLength=18,lipAngle=90)),
            ('sigma',dict(depth=230,flangeWidth=75,lipLength=18,wallThickness=3,
                          bendRadius=4,centerWebOffset=24,outerWebHeight=38,
                          transitionRise=22,lipAngle=85)),
            ('bulb-flat',dict(width=23,depth=133,wallThickness=7,bulbRadius1=6,bulbRadius2=3,endRadius=2)),
            ('bulb-flat',dict(width=23,depth=133,wallThickness=7,bulbRadius1=6,bulbRadius2=3,endRadius=2,mirrorX=True)),
            ('p-tube',dict(width=53,depth=47,wallThickness=4,flangeLength=23,flangeThickness=2,outerRadius=5)),
            ('p-tube',dict(width=53,depth=47,wallThickness=4,flangeLength=23,flangeThickness=2,outerRadius=5,mirrorX=True)),
            ('angle',dict(width=50,depth=80,wallThickness=6,outerRadius=9,innerRadius=3,
                          useHotRolled=True,useInnerRadius=False,useIndependentFreeEndRadii=True,
                          freeEndRadius=0,freeEndRadius1=1,freeEndRadius2=2)),
            ('channel',dict(width=50,depth=80,wallThickness=2,outerRadius=5,useHotRolled=True,
                            useOuterRadii=False,outerRadius1=5,outerRadius2=5,
                            useInnerRadius=False,innerRadius1=3,innerRadius2=3,
                            useIndependentFreeEndRadii=True,freeEndRadius=0,
                            freeEndRadius1=0.5,freeEndRadius2=0.75)),
            ('curve-hollow',dict(width=33,wallThickness=3)),
            ('curve-hollow',dict(width=33,wallThickness=3,innerOffsetX=1.5,innerOffsetY=-1.0)),
            ('t-section',dict(width=80,depth=120,wallThickness=6,flangeThickness=8,rootRadius=4,
                              webOffset=5,useIndependentRadii=True,rootRadius1=3,rootRadius2=6)),
            ('i-section',dict(width=93,depth=153,wallThickness=7,flangeThickness=11,rootRadius=5,
                              useIndependentFlangeWidths=True,topFlangeWidth=101,bottomFlangeWidth=87,
                              webOffset=5,useIndependentRadii=True,rootRadius1=3,rootRadius2=4,
                              rootRadius3=5,rootRadius4=2)),
            ]
        for name,values in cases:
            with self.subTest(name=name,values=values):
                package=runtime._system_package(CATALOG/name,preview=False)
                raw=runtime._evaluate(package['descriptor'],package['scriptSource'],{**package['defaultParameters'],**values},package['packageDigest'],package['sourceFileName'],package['resources'])
                for angle in (0,37,123):
                    original=g.transform(g.normalize(raw,0.001),math.radians(angle),[123,-45])
                    section=g.to_section(original)
                    with forbid_forward_evaluation():
                        response=runtime.generate(dict(action='recognize',package=package,section=section),{})
                    self.assertTrue(response['matched'],response)
                    candidate=response['results'][0]['candidates'][0]
                    expected_values=dict(values)
                    if name=='angle' and candidate['parameters']['mirrorX']!=values.get('mirrorX',False):
                        # Swapping the legs and toe radii, mirroring and
                        # rotating describes the same angle-section contour.
                        expected_values.update(width=values['depth'],depth=values['width'],
                            freeEndRadius1=values['freeEndRadius2'],
                            freeEndRadius2=values['freeEndRadius1'])
                        self.assertEqual('non-unique',candidate['rotationUniqueness'])
                    for key,value in expected_values.items():
                        # These are retained UI draft values once their
                        # independent advanced overrides are enabled; they
                        # are not recoverable from the resulting contour.
                        if (key == 'width' and values.get('useIndependentFlangeWidths')) or \
                           (key == 'rootRadius' and values.get('useIndependentRadii')):
                            continue
                        actual=candidate['parameters'][key]
                        if isinstance(value,str):self.assertEqual(value,actual)
                        else:self.assertAlmostEqual(value,actual,places=5)
                    self.assertAlmostEqual(123,candidate['translation'][0],places=5)
                    self.assertAlmostEqual(-45,candidate['translation'][1],places=5)
                    rebuilt=runtime._evaluate(package['descriptor'],package['scriptSource'],
                        {**package['defaultParameters'],**candidate['parameters']},
                        package['packageDigest'],package['sourceFileName'],package['resources'])
                    equivalent,error=g.verify(original,g.normalize(rebuilt,0.001),candidate['transform'],0.001)
                    self.assertTrue(equivalent,(candidate,error))

    def test_angle_mirror_direct_inverse(self):
        import importlib.util
        import math
        from unittest.mock import patch
        spec=importlib.util.spec_from_file_location('angle_mirror_geometry',ROOT/'src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py')
        g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
        package=runtime._system_package(CATALOG/'angle',preview=False)
        values={**package['defaultParameters'],
                'width':50,'depth':80,'wallThickness':6,'outerRadius':9,
                'mirrorX':True}
        raw=runtime._evaluate(package['descriptor'],package['scriptSource'],values,
            package['packageDigest'],package['sourceFileName'],package['resources'])
        section=g.to_section(g.transform(g.normalize(raw,0.001),math.radians(37),[123,-45]))
        with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')), \
             patch.object(runtime,'evaluate_section',side_effect=AssertionError('forward forbidden')):
            response=runtime.generate(dict(action='recognize',package=package,section=section),{})
        self.assertTrue(response['matched'],response)
        candidate=response['results'][0]['candidates'][0]
        self.assertTrue(candidate['parameters']['mirrorX'])
        self.assertAlmostEqual(50,candidate['parameters']['width'],places=5)
        self.assertAlmostEqual(80,candidate['parameters']['depth'],places=5)
        self.assertAlmostEqual(123,candidate['translation'][0],places=5)
        self.assertAlmostEqual(-45,candidate['translation'][1],places=5)

    def test_hot_rolled_i_direct_inverse_recovers_slope_and_toe_radius(self):
        import math
        import importlib.util
        from unittest.mock import patch
        spec=importlib.util.spec_from_file_location('hot_i_geometry',ROOT/'src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py')
        g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
        package=runtime._system_package(CATALOG/'i-section',preview=False)
        values=dict(width=93,depth=153,wallThickness=7,flangeThickness=11,rootRadius=5,
                    useHotRolled=True,legEndRadius=2,useIndependentFlangeWidths=True,
                    topFlangeWidth=101,bottomFlangeWidth=87,webOffset=5,
                    useIndependentRadii=True,rootRadius1=3,rootRadius2=4,
                    rootRadius3=5,rootRadius4=2)
        parameters={**package['defaultParameters'],**values}
        raw=runtime._evaluate(package['descriptor'],package['scriptSource'],parameters,
            package['packageDigest'],package['sourceFileName'],package['resources'])
        section=g.to_section(g.transform(g.normalize(raw,0.001),math.radians(37),[123,-45]))
        with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')), \
             patch.object(runtime,'evaluate_section',side_effect=AssertionError('forward forbidden')):
            response=runtime.generate({'action':'recognize','package':package,'section':section},{})
        self.assertTrue(response['matched'],response)
        candidate=response['results'][0]['candidates'][0]['parameters']
        for key in ('depth','wallThickness','flangeThickness','legEndRadius','webOffset',
                    'topFlangeWidth','bottomFlangeWidth','rootRadius1','rootRadius2',
                    'rootRadius3','rootRadius4'):
            self.assertAlmostEqual(values[key],candidate[key],places=5)
        self.assertTrue(candidate['useHotRolled'])

    def test_rotated_and_translated_section_reports_degrees(self):
        import math
        import importlib.util
        spec=importlib.util.spec_from_file_location("fitting_geometry",ROOT/"src/apps/tube-designer/templates/_shared/profile_recognition_geometry.py")
        geometry=importlib.util.module_from_spec(spec);spec.loader.exec_module(geometry)
        package=runtime._system_package(CATALOG/"rect-bar",preview=True)
        loops=geometry.normalize(package["previewProfile"],0.001)
        rotated=geometry.to_section(geometry.transform(loops,math.radians(37),[123,-45]))
        result=runtime.generate({"action":"recognize","package":package,"section":rotated},{})
        self.assertTrue(result["matched"],result)
        candidate=result["results"][0]["candidates"][0]
        # A rectangle has a 180-degree symmetry: either pose is legitimate.
        self.assertAlmostEqual(37,candidate["rotationDegrees"]%180,places=5)
        self.assertAlmostEqual(123,candidate["transform"]["translation"][0],places=5)
        self.assertAlmostEqual(-45,candidate["transform"]["translation"][1],places=5)
        self.assertEqual("non-unique",candidate["rotationUniqueness"])

    def test_every_template_default_has_direct_inverse(self):
        for directory in CATALOG.iterdir():
            if not (directory/"profile.json").exists():continue
            with self.subTest(template=directory.name):
                package=runtime._system_package(directory,preview=True)
                result=runtime.generate({"action":"recognize","package":package,"section":package["previewProfile"]},{})
                self.assertTrue(result["matched"],result)

    def test_every_template_packages_its_inverse_entry(self):
        for directory in CATALOG.iterdir():
            if not (directory/"profile.json").exists():continue
            package=runtime._system_package(directory,preview=False)
            self.assertEqual("fitting.py",package["descriptor"]["recognition"]["entryPoint"])
            with runtime._script_context(package["scriptSource"],package["packageDigest"],
                    package["resources"],package["descriptor"],entry="fitting") as namespace:
                self.assertTrue(callable(namespace["fitting"]))

    def test_nondefault_parameters_reconstruct_original_section(self):
        for name,values in (("round",{"width":57,"wallThickness":3}),
                            ("round",{"width":57,"wallThickness":3,"innerOffsetX":2,"innerOffsetY":-1}),
                            ("round-bar",{"width":47}),
                            ("rect-bar",{"width":51,"depth":11,"cornerRadius":0}),
                            ("racetrack",{"width":73,"depth":37,"wallThickness":3}),
                            ("racetrack",{"width":73,"depth":37,"wallThickness":3,"innerOffsetX":1,"innerOffsetY":-1}),
                            ("rect",{"width":55,"depth":33,"wallThickness":2,"cornerRadius":4}),
                            ("oval",{"width":61,"depth":33,"wallThickness":3}),
                            ("oval",{"width":61,"depth":33,"wallThickness":3,"innerOffsetX":1,"innerOffsetY":-1}),
                            ("open-tube",{"width":53,"wallThickness":3,"openingAngle":77}),
                            ("multi-cell",{"cellSize":30,"wallThickness":3,"ribThickness":4}),
                            ("polygon-bar",{"radius":37,"sideCount":7,"cornerRadius":2.5})):
            with self.subTest(name=name):
                package=runtime._system_package(CATALOG/name,preview=False)
                parameters={**package["defaultParameters"],**values}
                section=runtime._evaluate(package["descriptor"],package["scriptSource"],parameters,
                    package["packageDigest"],package["sourceFileName"],package["resources"])
                result=runtime.generate({"action":"recognize","package":package,"section":section},{})
                self.assertTrue(result["matched"],result)
                candidates=result["results"][0]["candidates"]
                if name in ("round","round-bar"):
                    self.assertTrue(all(c["rotationUniqueness"]=="non-unique" for c in candidates))
                self.assertTrue(all(c["verification"]=="direct-geometric-constraints" for c in candidates))
                self.assertTrue(any(all(abs(c["parameters"].get(k,1e9)-v)<0.001
                    for k,v in values.items()) for c in candidates),candidates)

    def test_circle_does_not_match_square_or_missing_hole(self):
        package=runtime._system_package(CATALOG/"round",preview=False)
        for section in ({"contours":[{"kind":"circle","radius":20}]},
                        {"contours":[{"kind":"roundedRectangle","width":40,"height":40,"radius":0}]}):
            result=runtime.generate({"action":"recognize","package":package,"section":section},{})
            self.assertFalse(result["matched"],result)
            self.assertEqual("no-match",result["results"][0]["status"])

    def test_inverse_cannot_call_forward_even_for_system_catalogue(self):
        from unittest.mock import patch
        section={"contours":[{"kind":"circle","radius":27},{"kind":"circle","radius":24}]}
        with patch.object(runtime,"_evaluate",side_effect=AssertionError("forward forbidden")), \
             patch.object(runtime,"evaluate_section",side_effect=AssertionError("forward forbidden")):
            result=runtime.generate({"action":"recognize-system","section":section,
                "profileRoot":str(CATALOG)},{})
        self.assertTrue(result["matched"],result)
        self.assertFalse(any(r["status"]=="error" for r in result["results"]),result)

if __name__=="__main__":unittest.main()
