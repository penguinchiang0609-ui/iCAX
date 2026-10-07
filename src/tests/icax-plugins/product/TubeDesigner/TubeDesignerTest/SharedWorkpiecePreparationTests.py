"""Shared design stays fixed while downstream prepares real route workpieces."""
from copy import deepcopy
from pathlib import Path
import unittest

from WindowCatalogueTests import SRC, package
from ProductManufacturingDeclarationTests import assert_declaration, execute_definition, forbid_processing
from AllProductsGenericRecheck import part_facts, semantic, world_geometry


VERIFIED = SRC.parent / 'output/tests/assembly-process/all-products-recheck-generic-verified-synthetic-system.json'


class SharedWorkpiecePreparationTests(unittest.TestCase):
    def assert_execution(self, name, changes):
        descriptor, defaults, module = package(name)
        values = {**defaults, **changes}
        frozen = deepcopy(values)
        expected = module.generate(values, {'template':descriptor, 'geometryPurpose':'manufacturing'})
        design = module.display(values)
        with forbid_processing():
            declarations = module.manufacturing(values)
        assert_declaration(self, declarations, design)
        saved_design, saved_declarations = deepcopy(design), deepcopy(declarations)
        actual = execute_definition(declarations, design=design)
        self.assertEqual(design, saved_design)
        self.assertEqual(declarations, saved_declarations)
        self.assertEqual(values, frozen)
        self.assertEqual(world_geometry(expected), world_geometry(actual))
        self.assertEqual({item['key']:part_facts(item) for item in expected['items']},
                         {item['key']:part_facts(item) for item in actual['items']})
        def processes(document):
            nodes = world_geometry(document, True)
            return {entry['instanceId']:semantic(entry['result'], nodes) for entry in document.get('extensions', {})
                    .get('tubeDesigner.assemblyGeometryProcesses', {}).get('instances', [])}
        self.assertEqual(processes(expected), processes(actual))
        return values, design, declarations, expected, actual

    def test_whole_display_invariant_across_routes(self):
        for name, changes in (
                ('minimal_protective_grille', {'frameType':'closed_frame'}),
                ('minimal_protective_grille', {'frameType':'two_vertical', 'innerProfileOrientation':'rotate_90'})):
            with self.subTest(name=name, changes=changes):
                _, defaults, module = package(name)
                values = {**defaults, **changes}
                before = deepcopy(values)
                legacy = module.display({**values, 'assemblyPlanningMode':'legacy_processed'})
                external = module.display({**values, 'assemblyPlanningMode':'external_templates'})
                self.assertEqual(legacy, external)
                self.assertEqual(values, before)
                for item in external['items']:
                    record = item['properties'].get('window.profilePart', {})
                    self.assertTrue(set(record).isdisjoint({'cutPlanes','machiningFeatures','assemblyTransform'}))
                    if record:
                        self.assertEqual(record['length'], item['properties']['length'])

    @unittest.skip("Deferred aluminium-window reference; no active product generation")
    def test_external_window_prepares_real_uncut_members(self):
        for kind in ('fixed','sliding','hinged'):
            with self.subTest(kind=kind):
                _, design, raw, _, actual = self.assert_execution('aluminium_window', {
                    'systemSource':'file','systemFile':str(VERIFIED),'windowType':kind,
                    'columns':2,'rows':2,'assemblyPlanningMode':'external_templates'})
                preparations = actual['extensions']['tubeDesigner.workpiecePreparations']
                members = {item['key'] for item in design['items'] if 'assemblyFrame.member' in item['properties']}
                self.assertEqual({entry['itemKey'] for entry in preparations}, members)
                self.assertTrue(all(entry['status'] == 'await-external' and entry['ready'] is False for entry in preparations))
                self.assertNotIn('resources', raw)
                self.assertNotIn('items', raw)
                self.assertFalse(actual['extensions'].get('tubeDesigner.assemblyGeometryProcesses', {}).get('instances'))

    def test_minimal_route_extents_and_profile_provenance(self):
        for mode in ('legacy_processed','external_templates'):
            with self.subTest(mode=mode):
                _, _, raw, _, actual = self.assert_execution('minimal_protective_grille', {
                    'frameType':'closed_frame','assemblyPlanningMode':mode})
                if mode == 'legacy_processed':
                    prep = {entry['key']:entry['definition'] for entry in raw['processes']
                            if entry['definition']['templateId'] == 'profile-stock-preparation'}
                    self.assertIn('sourceProcessKey', prep['inner.bar.0001.prepare']['processInput']['geometry'])
                    records = actual['extensions']['tubeDesigner.assemblyGeometryProcesses']['instances']
                    ends = [record for record in records if record['templateId'] == 'tube-end-joint']
                    self.assertEqual(len(ends), 8)
                    for record in ends:
                        self.assertEqual(record['processInput']['parts']['stock']['profileRef'], {
                            'scope':'template','id':record['stockId'],'templateId':'minimal-protective-grille'})

    @unittest.skip("Deferred decorative-door reference; no active product generation")
    def test_unprocessed_door_leaf_prepares_intrinsic_plate(self):
        for pattern, layout in (('lines','center_band'),('round_scene','two_blocks'),
                                ('panels','center_band'),('panels','two_blocks')):
            with self.subTest(pattern=pattern, layout=layout):
                _, _, raw, _, actual = self.assert_execution('decorative_door', {
                    'doorType':'mother','motherSide':'right','composition':'continuous','width':3000,
                    'pattern':pattern,'layout':layout})
                self.assertFalse(any('leaf.1' in call['definition'].get('targets', {}).values() for call in raw['processes']))
                self.assertIn('leaf.1', {entry['itemKey'] for entry in actual['extensions']['tubeDesigner.workpiecePreparations']})

    @unittest.skip("Deferred product reference; no current active product generation")
    def test_deferred_window_bad_preparation_references_and_endpoints_leave_inputs_unchanged(self):
        _, defaults, module = package('aluminium_window')
        values = {**defaults,'systemSource':'file','systemFile':str(VERIFIED),'windowType':'fixed',
                  'columns':2,'rows':2,'assemblyPlanningMode':'external_templates'}
        design, raw = module.display(values), module.manufacturing(values)
        mutations = []
        unknown = deepcopy(raw)
        unknown['processes'][-1]['definition']['processInput']['geometry']['connectionKeys'] = ['missing.connection']
        mutations.append(unknown)
        missing = deepcopy(raw)
        missing['connections'][0]['properties']['participantAnchors'][0]['anchor'].pop('stockAllowance')
        mutations.append(missing)
        off_axis = deepcopy(raw)
        off_axis['connections'][0]['properties']['participantAnchors'][0]['centerlinePoint'][1] += 1
        mutations.append(off_axis)
        competing = deepcopy(raw)
        competing['processes'][-1]['definition']['processInput']['geometry']['direction'] = 'reverse'
        mutations.append(competing)
        for invalid in mutations:
            before = deepcopy(invalid)
            saved = deepcopy(design)
            with self.assertRaises(ValueError):
                execute_definition(invalid, design=design)
            self.assertEqual(before, invalid)
            self.assertEqual(saved, design)
    def test_active_grille_bad_preparation_references_and_endpoints_leave_inputs_unchanged(self):
        _, defaults, module = package('minimal_protective_grille')
        values = {**defaults, 'frameType':'closed_frame'}
        design, raw = module.display(values), module.manufacturing(values)
        for change in ('unknown-source', 'reversed-sign', 'invalid-coordinate-map', 'left-handed-frame'):
            with self.subTest(change=change):
                invalid, invalid_design = deepcopy(raw), deepcopy(design)
                requirements = invalid['processes'][0]['definition']['processInput']['geometry']
                if change == 'unknown-source':
                    requirements.clear()
                    requirements.update(sourceProcessKey='missing.process', status='prepared', ready=True)
                elif change == 'reversed-sign':
                    requirements.clear()
                    requirements.update(axialInterval={'startStation':0.0,'endStation':100.0},
                                        direction='reverse', status='prepared', ready=True)
                elif change == 'invalid-coordinate-map':
                    invalid_design['items'][0]['properties']['tubeDesigner.profile']['sectionCoordinateMap'] = [[2,0],[0,1]]
                else:
                    frame = invalid_design['items'][0]['properties']['assemblyFrame.member']['sectionFrame']
                    frame['zAxis'] = [-value for value in frame['zAxis']]
                saved, saved_design = deepcopy(invalid), deepcopy(invalid_design)
                with self.assertRaises(ValueError):
                    execute_definition(invalid, design=invalid_design)
                self.assertEqual(saved, invalid)
                self.assertEqual(saved_design, invalid_design)
        repeated = deepcopy(raw)
        duplicate = deepcopy(repeated['processes'][0])
        duplicate['key'] = 'frame.left.second-preparation'
        repeated['processes'].insert(1, duplicate)
        frozen = deepcopy(repeated)
        with self.assertRaisesRegex(ValueError, 'one preparation'):
            execute_definition(repeated, design=design)
        self.assertEqual(repeated, frozen)


if __name__ == '__main__':
    unittest.main()
