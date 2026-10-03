"""Two true horizontal sections must coexist inside their shared corner post."""
from copy import deepcopy
import unittest

from FoldedPostConnectionTests import window
from WindowManufacturingInputTests import load, execute, package, forbid_product_processing
from AssemblyContinuousCornerInsertionTests import profile


def geometry(size=30, first_width=22, second_width=22):
    receiver = {'start': [0.,0.,0.], 'end': [0.,0.,1000.], 'wallThickness': 1.,
        'section': {'placement': {'origin': [0.,0.,0.], 'xAxis': [1.,0.,0.], 'yAxis': [0.,1.,0.]},
                    'contours': profile(size,size,1.)['contours']}}
    branches = [
        {'key': 'first', 'start': [-100.,0.,500.], 'end': [-10.,0.,500.],
         'section': {'placement': {'origin': [-100.,0.,500.], 'xAxis': [0.,1.,0.], 'yAxis': [0.,0.,1.]},
                     'contours': profile(first_width,18,1.)['contours']}},
        {'key': 'second', 'start': [0.,-100.,500.], 'end': [0.,-10.,500.],
         'section': {'placement': {'origin': [0.,-100.,500.], 'xAxis': [1.,0.,0.], 'yAxis': [0.,0.,1.]},
                     'contours': profile(second_width,16,1.)['contours']}},
    ]
    return receiver, branches


class SharedPostHorizontalInsertionTests(unittest.TestCase):
    def limits(self, size=30, clearance=.1, first_width=22, second_width=22):
        receiver, branches = geometry(size,first_width,second_width)
        before = deepcopy((receiver,branches))
        result = load('security_window_post_connections').shared_post_horizontal_limits(receiver,branches,5.,clearance)
        self.assertEqual(before,(receiver,branches))
        return result

    def test_c28_c30_c32_and_c38_use_the_true_outer_section_projection(self):
        for size, expected in ((28,2.8),(30,3.8),(32,4.8)):
            with self.subTest(size=size):
                limits = self.limits(size)
                self.assertEqual({('first','end'),('second','end')},set(limits))
                self.assertTrue(all(abs(value['depth']-expected)<1.e-8 for value in limits.values()))
        self.assertEqual({},self.limits(38))
        self.assertTrue(all(abs(value['depth']-4)<1.e-8 for value in self.limits(30,0).values()))

    def test_unequal_rectangles_use_each_other_actual_transverse_width(self):
        limits = self.limits(first_width=22,second_width=26)
        self.assertAlmostEqual(limits[('first','end')]['depth'],1.8)
        self.assertAlmostEqual(limits[('second','end')]['depth'],3.8)

    def test_clearance_limits_both_opening_envelopes_and_insufficient_wall_penetration_fails(self):
        self.assertTrue(all(abs(value['depth']-3.2)<1.e-8 for value in self.limits(clearance=.4).values()))
        with self.assertRaisesRegex(ValueError,'可用入榫深度不足'):
            self.limits(clearance=1.)
        with self.assertRaisesRegex(ValueError,'可用入榫深度不足'):
            self.limits(size=24)

    def test_different_heights_and_nonadjacent_bars_do_not_limit_the_selected_insertion(self):
        receiver, branches = geometry()
        branches[1]['start'][2]=branches[1]['end'][2]=550.
        branches[1]['section']['placement']['origin'][2]=550.
        runtime=load('security_window_post_connections')
        self.assertEqual({},runtime.shared_post_horizontal_limits(receiver,branches,5.,.1))
        receiver, branches = geometry()
        for key in ('start','end'):
            branches[1][key][0]=100.
        branches[1]['section']['placement']['origin'][0]=100.
        self.assertEqual({},runtime.shared_post_horizontal_limits(receiver,branches,5.,.1))

    def test_clearance_changes_manufacturing_ends_without_changing_product_design_or_input_facts(self):
        left=window('insert',assemblyClearance=.1)
        right=window('insert',assemblyClearance=.4)
        self.assertEqual(left[2],right[2])
        left_input=left[1]['processes'][0]['definition']['processInput']
        right_input=right[1]['processes'][0]['definition']['processInput']
        self.assertEqual(left_input['parts'],right_input['parts'])
        self.assertEqual(left_input['geometry'],right_input['geometry'])
        for case, expected in ((left,3.8),(right,3.2)):
            calls = [call for call in case[3]['processes'] if call['definition']['templateId']=='tube-end-joint'
                     and call['key'].startswith('main_grid.horizontal.')]
            restricted = [call['definition']['parameters']['insertionDepth'] for call in calls
                          if call['definition']['parameters']['insertionDepth'] < 5.]
            self.assertTrue(restricted)
            self.assertTrue(all(abs(value-expected)<1.e-8 for value in restricted))
        # A product declaration can compute its geometry limit, but cannot
        # invoke processing, cutter generation, or a manufacturing allocator.
        _, _, core = package('single_face_security_window')
        with forbid_product_processing():
            original = deepcopy(left[0])
            core.display(left[0])
            core.manufacturing(left[0])
            self.assertEqual(original,left[0])

    def test_c28_c30_and_c32_complete_sources_keep_nominal_input_and_real_endpoints(self):
        for size, expected in ((28,2.8),(30,3.8),(32,4.8)):
            with self.subTest(size=size):
                values, declaration, design, compiled = window('insert',foldedPostWidth=size,foldedPostDepth=size)
                original = deepcopy(values)
                worker = execute(declaration,design)
                source = worker['extensions']['tubeDesigner.assemblyProcessSource']
                plan = load('assembly_template_runtime').resolve_process_plan(source['stocks'],source['instances'])
                self.assertTrue(plan['operations'])
                self.assertEqual(original,values)
                self.assertEqual(5,values['horizontalBranchReserve'])
                depths = [instance['parameters']['insertionDepth'] for instance in source['instances']
                          if instance['templateId']=='tube-end-joint' and instance['instanceId'].startswith('main_grid.horizontal.')]
                self.assertTrue(any(abs(value-expected)<1.e-8 for value in depths))
                self.assertTrue(all(abs(value-expected)<1.e-8 or value==5 for value in depths))

    def test_complete_source_replay_passes_for_all_post_modes_without_disabling_aperture_fit(self):
        for joint in ('weld','insert','tabs'):
            with self.subTest(joint=joint):
                values, declaration, design, compiled = window(joint)
                before=deepcopy(values)
                worker = execute(declaration,design)
                source = worker['extensions']['tubeDesigner.assemblyProcessSource']
                runtime = load('assembly_template_runtime')
                plan = runtime.resolve_process_plan(source['stocks'],source['instances'])
                self.assertTrue(plan['operations'])
                self.assertEqual(before,values)
                apertures=[instance for instance in source['instances'] if instance['templateId']=='tube-profile-aperture']
                self.assertTrue(apertures)
                self.assertTrue(all(instance['parameters']['checkFit'] for instance in apertures))
                self.assertFalse(any('main_grid.horizontal.0001.aperture.main_grid.horizontal.0001.main_grid.horizontal.0004'
                                     ==instance['instanceId'] for instance in apertures))
                end_depths=[instance['parameters']['insertionDepth'] for instance in source['instances']
                            if instance['templateId']=='tube-end-joint' and instance['instanceId'].startswith('main_grid.horizontal.')]
                expected=5. if joint=='weld' else 3.8
                self.assertTrue(any(abs(depth-expected)<1.e-8 for depth in end_depths))

    def test_five_face_folded_paths_keep_frame_height_and_complete_insert_and_tabs_sources(self):
        for joint in ('insert','tabs'):
            with self.subTest(joint=joint):
                values, declaration, design, compiled=window(joint,faceType='five')
                worker=execute(declaration,design)
                source=worker['extensions']['tubeDesigner.assemblyProcessSource']
                plan=load('assembly_template_runtime').resolve_process_plan(source['stocks'],source['instances'])
                self.assertTrue(plan['operations'])
                calls=[call for call in source['instances'] if call['templateId']=='orthogonal-corner']
                self.assertEqual(8,len(calls))
                for call in calls:
                    for part in call['processInput']['parts'].values():
                        station=part['length'] if part['anchor']['end']=='end' else 0.
                        self.assertAlmostEqual(part['matrix'][8]*station+part['matrix'][11],
                                               981. if '.top.' in call['instanceId'] else 19.)

    def test_actual_section_datum_rebase_preserves_projections_and_public_origin_contract_is_explicit(self):
        from icax_template_sdk.manufacturing import compose_manufacturing_model
        values,declaration,design,reference=window('insert')
        document=compose_manufacturing_model(declaration,design)
        item=next(item for item in document['items'] if item['key']=='main_grid.horizontal.0004')
        data=item['designInput']
        profile=item['properties']['tubeDesigner.profile']
        resource=deepcopy(next(node for node in document['resources'] if node['key']==data['profileResource']))
        resource['key'] += '.offset-datum'
        # Change only the section coordinate datum. The same original line
        # and arc contours retain exactly the same world geometry.
        mapping=profile['sectionCoordinateMap']
        local_shift=[-mapping[0][0],-mapping[0][1]]
        for contour in resource['arguments']['contours']:
            for segment in contour['segments']:
                for name in ('start','end','middle'):
                    if name in segment:
                        segment[name]=[value+offset for value,offset in zip(segment[name],local_shift)]
        data['sectionFrame']['origin']=[value+offset for value,offset in
                                       zip(data['sectionFrame']['origin'],data['sectionFrame']['xAxis'])]
        data['profileResource']=resource['key']
        profile['sectionResource']=resource['key']
        profile['contours']=deepcopy(resource['arguments']['contours'])
        document['resources'].append(resource)
        before=deepcopy(document)
        # The current public declaration contract deliberately rejects an
        # independent section datum; imported offsets remain in the contours.
        with self.assertRaisesRegex(ValueError,'sectionFrame origin must equal start'):
            load('assembly_window_process').compile_window_assembly(document)
        self.assertEqual(before,document)
        # The pure geometric helper nevertheless preserves the true placement
        # of a contour, rather than silently centering it around the member.
        receiver,branches=geometry()
        runtime=load('security_window_post_connections')
        reference=runtime.shared_post_horizontal_limits(receiver,branches,5.,.1)
        section=branches[1]['section']
        section['placement']['origin']=[value+offset for value,offset in
                                       zip(section['placement']['origin'],section['placement']['xAxis'])]
        for contour in section['contours']:
            for segment in contour['segments']:
                for name in ('start','end','middle'):
                    if name in segment:
                        segment[name]=[segment[name][0]-1.,segment[name][1]]
        actual=runtime.shared_post_horizontal_limits(receiver,branches,5.,.1)
        self.assertEqual(reference.keys(),actual.keys())
        for key in reference:
            self.assertAlmostEqual(reference[key]['depth'],actual[key]['depth'])


if __name__=='__main__':
    unittest.main()
