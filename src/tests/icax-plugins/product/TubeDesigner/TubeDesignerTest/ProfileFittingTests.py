import unittest
from ProfileFamilyTests import runtime, ROOT

CATALOG=ROOT/"src/apps/tube-designer/templates/profile"

class Fitting(unittest.TestCase):
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
                        with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')),patch.object(runtime,'evaluate_section',side_effect=AssertionError('forward forbidden')):
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
            ('p-tube',dict(width=53,depth=47,wallThickness=4,flangeLength=23,flangeThickness=2,cornerRadius=5,innerRadius=1)),
            ('p-tube',dict(width=53,depth=47,wallThickness=4,flangeLength=23,flangeThickness=2,cornerRadius=5,innerRadius=1,mirrorX=True)),
            ('angle',dict(width=50,depth=80,wallThickness=6,outerRadius=9,innerRadius=3,
                          useInnerRadius=False,useIndependentFreeEndRadii=True,
                          freeEndRadius=0,freeEndRadius1=1,freeEndRadius2=2)),
            ('channel',dict(width=50,depth=80,wallThickness=2,outerRadius=5,
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
                    section=g.to_section(g.transform(g.normalize(raw,0.001),math.radians(angle),[123,-45]))
                    with patch.object(runtime,'_evaluate',side_effect=AssertionError('forward forbidden')),patch.object(runtime,'evaluate_section',side_effect=AssertionError('forward forbidden')):
                        response=runtime.generate(dict(action='recognize',package=package,section=section),{})
                    self.assertTrue(response['matched'],response)
                    candidate=response['results'][0]['candidates'][0]
                    for key,value in values.items():
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
