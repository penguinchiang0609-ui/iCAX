"""Reusable tube operations, exact section placement and rejection checks."""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path
import sys
import unittest

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'src/apps/tube-designer/templates').is_dir())
SHARED=ROOT/'src/apps/tube-designer/templates/_shared'
spec=importlib.util.spec_from_file_location('tube_machining_acceptance_runtime',SHARED/'assembly_geometry_process_runtime.py')
RUNTIME=importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUNTIME)
IDENTITY=[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]


def part(size=38,wall=1.6,length=600,matrix=None,center=(0,0),key='stock'):
    return {'id':key,'profileRef':{'scope':'system','id':'rect'},'parameters':{},
        'length':length,'matrix':deepcopy(matrix or IDENTITY),
        'section':{'wallThickness':wall,'contours':[
            {'kind':'roundedRectangle','width':size,'height':size,'radius':2,'center':list(center)},
            {'kind':'roundedRectangle','width':size-2*wall,'height':size-2*wall,'radius':.4,'center':list(center)}]}}


def input_of(stock,geometry,**observers):
    return {'schema':'icax.assembly-process-input','schemaVersion':1,'parts':{'stock':stock,**observers},'geometry':geometry}


def branch_at(station,rotation=0,length=100):
    # X along stock Z, branch section Y along rotated stock XY.
    a=math.radians(rotation)
    x=[0,math.sin(a),math.cos(a)];y=[1,0,0]
    z=[0,math.cos(a),-math.sin(a)]
    origin=[station,-50*math.sin(a),-50*math.cos(a)]
    matrix=[v for i in range(3) for v in (x[i],y[i],z[i],origin[i])]+[0,0,0,1]
    return part(16,1.2,length,matrix,key='branch')


class AssemblyTubeMachiningTests(unittest.TestCase):
    def test_miter_computes_bisector_for_both_ends_and_echoes_input(self):
        stock=part()
        for end,station,direction in (('start',19,[0,1,0]),('end',581,[0,-1,0])):
            local=input_of(stock,{'mode':'miter','end':end,'station':station,'mateDirection':direction})
            original=deepcopy(local)
            result=RUNTIME.evaluate('tube-end-joint',local,{})
            self.assertTrue(result['applicable'],result['reason'])
            self.assertEqual(original,local)
            self.assertEqual({},result['parameters'])
            self.assertEqual('stock',result['geometrySpace'])
            plane=result['checks'][0]
            self.assertAlmostEqual(abs(plane['inwardNormal'][0]),1/math.sqrt(2))
            self.assertEqual([station,0,0],plane['origin'])
            self.assertEqual(result,RUNTIME.evaluate('tube-end-joint',local,{}))

    def test_arbitrary_miter_angle_is_geometric_without_product_shape(self):
        result=RUNTIME.evaluate('tube-end-joint',input_of(part(),{
            'mode':'miter','end':'start','station':25,'mateDirection':[.5,math.sqrt(.75),0]}),{})
        self.assertTrue(result['applicable'],result['reason'])
        self.assertAlmostEqual(result['checks'][0]['inwardNormal'][0],.5)
        self.assertAlmostEqual(result['checks'][0]['inwardNormal'][1],-math.sqrt(.75))

    def test_three_apertures_share_one_stock_and_keep_exact_actual_frames(self):
        stock=part(center=(4,-2))
        outputs=[]
        for station,rotation in ((80,0),(250,45),(470,90)):
            local=input_of(stock,{},branch=branch_at(station,rotation))
            original=deepcopy(local)
            params={'clearance':.2,'checkFit':True}
            result=RUNTIME.evaluate('tube-profile-aperture',local,params)
            self.assertTrue(result['applicable'],result['reason'])
            self.assertEqual(original,local)
            self.assertEqual(params,result['parameters'])
            self.assertEqual(['stock'],result['materialRoles'])
            outputs.append(result['geometry'][0]['arguments']['placement'])
        self.assertEqual([80,250,470],[p['origin'][0] for p in outputs])
        self.assertNotEqual(outputs[0]['yAxis'],outputs[1]['yAxis'])

    def test_aperture_crop_respects_own_span_and_no_contact_is_explicit_noop(self):
        local=input_of(part(),{'clipInterval':[0,100]},branch=branch_at(95))
        result=RUNTIME.evaluate('tube-profile-aperture',local,{'clearance':.3})
        self.assertTrue(result['applicable'],result['reason'])
        crop=next(n for n in result['geometry'] if n['key']=='aperture.clip.profile')
        xs=[s['start'][0] for s in crop['arguments']['contours'][0]['segments']]
        self.assertEqual((0,100),(min(xs),max(xs)))
        local['geometry']['clipInterval']=[200,300]
        noop=RUNTIME.evaluate('tube-profile-aperture',local,{'clearance':.3})
        self.assertTrue(noop['applicable'],noop['reason'])
        self.assertEqual([],noop['operations'])
        self.assertEqual('no-contact-in-stock-interval',noop['checks'][0]['kind'])

    def test_aperture_cannot_cross_a_real_miter_plane(self):
        local=input_of(part(),{'keepPlanes':[{'origin':[19,0,0],'inwardNormal':[1,-1,0]}]},branch=branch_at(20))
        rejected=RUNTIME.evaluate('tube-profile-aperture',local,{'clearance':.3})
        self.assertFalse(rejected['applicable'])
        self.assertIn('斜切端面',rejected['reason'])
        local['parts']['branch']=branch_at(80)
        self.assertTrue(RUNTIME.evaluate('tube-profile-aperture',local,{'clearance':.3})['applicable'])

    def test_insertion_checks_both_wall_limits_and_neighbor_interference(self):
        local=input_of(part(),{'receiverSpan':38},branch=branch_at(100))
        self.assertTrue(RUNTIME.evaluate('tube-insertion-check',local,{'depth':5,'clearance':.3})['applicable'])
        for depth in (1,37):
            result=RUNTIME.evaluate('tube-insertion-check',local,{'depth':depth,'clearance':.3})
            self.assertFalse(result['applicable'])
            self.assertIn('保留外侧管壁',result['reason'])
        local['parts']['other']=deepcopy(local['parts']['branch'])
        local['geometry']['checkInterference']=True
        self.assertFalse(RUNTIME.evaluate('tube-insertion-check',local,{'depth':5})['applicable'])
        local['parts']['other']['matrix'][3]=150
        self.assertTrue(RUNTIME.evaluate('tube-insertion-check',local,{'depth':5})['applicable'])

    def test_missing_exact_section_unknown_parameters_and_bad_frame_reject(self):
        local=input_of(part(),{},branch=branch_at(100))
        for alter,params in ((lambda i:i['parts']['stock'].pop('section'),{}),
                             (lambda i:None,{'finishedShape':'L'}),
                             (lambda i:i['parts']['branch']['matrix'].__setitem__(0,2),{})):
            bad=deepcopy(local);alter(bad)
            result=RUNTIME.evaluate('tube-profile-aperture',bad,params)
            self.assertFalse(result['applicable'])
            self.assertEqual([],result['geometry'])
            self.assertEqual([],result['operations'])

    def test_section_fit_needs_no_fabricated_length_or_pose(self):
        local=input_of({'section':part()['section']},{'receiverSpan':38,'checkThrough':True},
                       branch={'section':part(16,1.2)['section']})
        result=RUNTIME.evaluate('tube-insertion-check',local,{'depth':5,'clearance':.3})
        self.assertTrue(result['applicable'],result['reason'])
        self.assertNotIn('length',result['processInput']['parts']['stock'])
        self.assertNotIn('matrix',result['processInput']['parts']['branch'])
        local['geometry']['checkInterference']=True
        self.assertFalse(RUNTIME.evaluate('tube-insertion-check',local,{'depth':5})['applicable'])


if __name__=='__main__':
    unittest.main()
