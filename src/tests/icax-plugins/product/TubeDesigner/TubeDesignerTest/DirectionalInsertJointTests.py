"""Compare mating directions after both real manufacturing part poses."""
import copy
import json
import math
import unittest
from WindowManufacturingInputTests import load


class DirectionalInsertJointTests(unittest.TestCase):
    def test_example_male_and_mother_share_physical_phase(self):
        assembly=load('assembly_template_runtime');punch=load('punch_tool_runtime')
        catalog=load('directional_tube_geometry')._module('tube_profile_catalog')
        def profile(request):
            return {'contours':catalog._built_system_profile(request['profileRef']['id'],
                json.dumps(request['parameters'],sort_keys=True)).contours()}
        def world(matrix,vector):
            result=[sum(matrix[4*i+j]*vector[j] for j in range(3)) for i in range(3)]
            length=math.sqrt(sum(v*v for v in result));return [v/length for v in result]
        for template in ('end-side-tab-slot','wrap-a-over-b'):
            for kind in ('rect','round'):
                for count in ('two','four'):
                    for angle in (0,27):
                        values={'pairCount':count,'tabAngle':angle,
                            **({'maleFemale':True} if template.startswith('wrap') else {})}
                        product=assembly.get_example_product(template,values)['finishedProduct']
                        for key,span in product['spans'].items():
                            host=key in ('main','armA')
                            span['profileRef']={'scope':'system','id':kind}
                            span['parameters']={'width':80 if host else 20,'wallThickness':2,
                                **({'depth':40 if host else 20,'cornerRadius':2} if kind=='rect' else {})}
                        plan=assembly.preview_plan(template,values,finished_product=product)
                        vectors={}
                        for part in plan['manufacturingParts']:
                            request=copy.deepcopy(part['request']);target=profile(request)
                            measured=load('directional_tube_geometry').shell(target)['outside']
                            request.update(targetSection=target,bounds={'min':[0,*measured['min']],
                                'max':[request['length'],*measured['max']]})
                            for feature in request['features']:
                                feature['section']={**feature['section'],'profile':profile(feature['section'])}
                            for entry in request['ends'].values():
                                if entry.get('section'):entry['section']={'profile':profile(entry['section'])}
                            prepared=punch.prepare(request)
                            for end,entry in prepared['ends'].items():
                                if entry.get('toolRef',{}).get('id')!='paired-end-tabs':continue
                                graph=entry['frozenTool']['geometry']['model']['geometry']
                                axes=[n['arguments']['vector'] for n in graph if n['operator']=='extrude'
                                      and n['key'].startswith('tab-band.')]
                                sign=1 if end=='start' else -1
                                vectors['male']=[world(part['compareMatrix'],[a[0]*sign,a[1],a[2]*sign]) for a in axes]
                            for entry in prepared['features']:
                                if entry['toolRef']['id']!='paired-side-slots':continue
                                graph=entry['frozenTool']['geometry']['model']['geometry']
                                vectors['mother']=[world(part['compareMatrix'],n['arguments']['placement']['yAxis'])
                                    for n in graph if n['operator']=='profile2d' and n['key'].startswith('socket-band.')]
                        with self.subTest(template=template,kind=kind,count=count,angle=angle):
                            self.assertEqual(len(vectors['male']),len(vectors['mother']))
                            for axis in vectors['male']:
                                self.assertTrue(any(abs(sum(a*b for a,b in zip(axis,other)))>1-1.e-7
                                    for other in vectors['mother']),vectors)


if __name__=='__main__':unittest.main()
