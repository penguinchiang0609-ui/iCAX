"""Stock decomposition, face transforms and immutable host parameters."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode=True
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'src/apps/tube-designer/templates').is_dir())
sys.path.insert(0,str(ROOT/'src/iCAX-Engine/framework/TemplateRuntime/python'))
PACKAGE=ROOT/'src/apps/tube-designer/templates/product/single_face_security_window'
spec=importlib.util.spec_from_file_location('security_frames_acceptance',PACKAGE/'template.py')
SUBJECT=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=SUBJECT
spec.loader.exec_module(SUBJECT)
DESCRIPTOR=json.loads((PACKAGE/'template.json').read_text(encoding='utf-8'))
DEFAULTS={f['key']:f['defaultValue'] for f in DESCRIPTOR['parameters']}


def generate(face,mode,purpose='manufacturing',**extra):
    values={**deepcopy(DEFAULTS),'faceType':face,'frameManufacturingMode':mode,**extra}
    original=deepcopy(values)
    result=SUBJECT.generate(values,{'template':DESCRIPTOR,'geometryPurpose':purpose})
    assert values==original and result['parameters']==original
    return result


def frames(document):
    return [i for i in document['items'] if i['key'].startswith('outer_frame.')]


class FrameManufacturingTests(unittest.TestCase):
    def test_all_faces_and_modes_keep_host_parameters_and_real_part_counts(self):
        counts={'single':(4,1,1),'two':(7,5,2),'three':(10,6,3),'five':(12,6,6)}
        for face,expected in counts.items():
            for mode,count in zip(('segment_weld','plane_v_notch','spatial_v_notch'),expected):
                for purpose in ('display','manufacturing'):
                    with self.subTest(face=face,mode=mode,purpose=purpose):
                        doc=generate(face,mode,purpose,accessDoorEnabled=True)
                        self.assertEqual(count,len(frames(doc)))
                        keys={i['key'] for i in doc['items']}
                        for r in doc.get('relationships',[]):
                            self.assertTrue(set(r['items'])<=keys,r)
                        for item in doc['items']:
                            props=item['properties']
                            for key in props.get('tubeDesigner.connectionProcess',{}).get('passesInto',[]):
                                self.assertIn(key,keys)
                        if purpose=='display':
                            self.assertFalse(any('.export.groove.' in n['key'] for n in doc['geometry']))
                        else:
                            for item in frames(doc):
                                if 'continuous' in item['key'] or '.plane.' in item['key']:
                                    self.assertGreater(len(props:=item['properties']['tubeDesigner.cornerProcess']['bendLocations']),0)

    def test_five_face_has_two_closed_rectangles_and_four_vertical_posts(self):
        doc=generate('five','plane_v_notch',accessDoorEnabled=False)
        fs=frames(doc)
        self.assertEqual(4,sum('.vertical.' in f['key'] for f in fs))
        for f in fs:
            if '.vertical.' in f['key']:
                self.assertEqual(DEFAULTS['height']-2*DEFAULTS['frameWidth'],f['properties']['length'])
        loops=[f for f in fs if '.plane.' in f['key']]
        self.assertEqual(2,len(loops))
        for f in loops:
            p=f['properties']
            self.assertTrue(p['tubeDesigner.frameManufacturing']['closed'])
            self.assertEqual(4,len(p['tubeDesigner.cornerProcess']['bendLocations']))
            self.assertEqual(4,len(p['tubeDesigner.frameManufacturing']['referenceMembers']))
        self.assertFalse(any('.back.' in f['key'] for f in fs))

    def test_closed_seam_apertures_never_cut_an_unrelated_span(self):
        doc=generate('five','plane_v_notch',accessDoorEnabled=False,
                     topBottomCrossbarFrontCenterOffset=243,
                     topBottomCrossbarBackCenterOffset=243)
        self.assertTrue(any('.aperture.crop.' in n['key'] for n in doc['geometry']))
        for f in frames(doc):
            path=f['properties'].get('tubeDesigner.frameManufacturing',{})
            for span in path.get('spans',[]):
                self.assertEqual(3,len(span['placement']['origin']))

    def test_hidden_join_drafts_do_not_change_folded_parts(self):
        for face in ('single','two','three','five'):
            a=generate(face,'plane_v_notch',frameCornerJoin='rail_miter',frameJoinType='butt_90')
            b=generate(face,'plane_v_notch',frameCornerJoin='post_butt',frameJoinType='miter_45')
            self.assertEqual(a['geometry'],b['geometry'])
            self.assertEqual(a['items'],b['items'])

    def test_spatial_frame_has_no_stock_limit_and_preserves_section_validation(self):
        self.assertNotIn('spatialFrameMaximumStockLength',DEFAULTS)
        doc=generate('three','spatial_v_notch',height=6000,accessDoorEnabled=False)
        path=next(f['properties']['tubeDesigner.frameManufacturing'] for f in frames(doc) if '.spatial.' in f['key'])
        self.assertGreater(next(f['properties']['length'] for f in frames(doc) if '.spatial.' in f['key']),12000)
        generate('three','plane_v_notch',frameDepth=25)
        with self.assertRaisesRegex(ValueError,'宽深相等'):
            generate('three','spatial_v_notch',frameDepth=25)

    def test_spatial_stations_rotations_fold_order_and_clearance_are_explicit(self):
        for face in ('two','three'):
            for side in ('left','right'):
                doc=generate(face,'spatial_v_notch',sidePosition=side)
                path=next(f['properties']['tubeDesigner.frameManufacturing'] for f in frames(doc) if '.spatial.' in f['key'])
                self.assertEqual('clear',path['clearanceCheck']['status'])
                self.assertEqual(sorted(path['foldOrder'],reverse=True),path['foldOrder'])
                self.assertGreater(len({b['rotation'] for b in path['bends']}),1)
                self.assertEqual(sorted(b['station'] for b in path['bends']),[b['station'] for b in path['bends']])

    def test_shared_posts_stop_at_folded_frame_inner_faces(self):
        for face in ('two','three'):
            for mode in ('plane_v_notch','spatial_v_notch'):
                posts=[f for f in frames(generate(face,mode)) if '.vertical.' in f['key']]
                expected_long=2 if mode=='plane_v_notch' else 0
                self.assertEqual(expected_long,sum(p['properties']['length']==DEFAULTS['height'] for p in posts))
                self.assertEqual(len(posts)-expected_long,
                    sum(p['properties']['length']==DEFAULTS['height']-2*DEFAULTS['frameWidth'] for p in posts))


if __name__=='__main__':
    unittest.main()
