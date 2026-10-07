"""Owned model boundaries and source-dependent machining module identities."""
from copy import deepcopy
import hashlib
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

from WindowCatalogueTests import package
from ProductGeometryResourceTests import instance_document, nested_alias_document
from ManufacturingExecutorBoundaryTests import SHARED, load_executor
from icax_template_sdk import to_resource_model, expand_resource_model


def load_shared(name):
    path = SHARED / (name + '.py')
    spec = importlib.util.spec_from_file_location('ownership_test_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mutable_ids(value):
    result = set()
    def collect(child):
        if isinstance(child, (dict, list)):
            result.add(id(child))
            for entry in child.values() if isinstance(child, dict) else child:
                collect(entry)
    collect(value)
    return result


class PythonResourceOwnershipTests(unittest.TestCase):
    def test_real_forty_one_tab_parts_replay_exactly_and_own_frozen_recipes(self):
        descriptor, values, template = package('single_face_security_window')
        values.update(frameManufacturingMode='segment_weld', frameJoinType='miter_45',
                      horizontalEndConnection='tabs', verticalEndConnection='tabs')
        design = template.display(values)
        declaration = template.manufacturing(values)
        saved = deepcopy([design, declaration, values])
        executor = load_executor()
        context = {'sharedRoot': str(SHARED)}
        first = executor.execute_manufacturing(declaration, context, design_model=design)
        frozen = deepcopy(first)
        self.assertEqual(41, first['extensions']['tubeDesigner.manufacturingPartCount'])
        self.assertFalse(mutable_ids(first) & mutable_ids([design, declaration, values, context]))
        first['resources'][0]['arguments']['testMutation'] = [1, 2, 3]
        first['extensions']['tubeDesigner.assemblyProcessSource']['ownershipProbe'] = ['changed result']
        replayed = executor.execute_manufacturing(declaration, context, design_model=design)
        self.assertEqual(frozen, replayed)
        self.assertEqual(saved, [design, declaration, values])
        self.assertFalse(mutable_ids(first) & mutable_ids(replayed))

    def test_publish_expand_and_builder_own_every_nested_container(self):
        original, base = nested_alias_document()
        original['extensions'] = {'frozen': {'nodes': deepcopy(original['geometry']),
                                           'aliases': {'inner': [base]},
                                           'largePayload': 'a' * 128000}}
        saved = deepcopy(original)
        published = to_resource_model(original)
        republished = to_resource_model(published)
        expanded = expand_resource_model(published)
        builder = load_executor()._builder(published)
        boundaries = [original, published, republished, expanded, builder._document]
        for index, value in enumerate(boundaries):
            for other in boundaries[index + 1:]:
                self.assertFalse(mutable_ids(value) & mutable_ids(other))
        self.assertEqual(original, saved)
        self.assertEqual(published, republished)
        for value in boundaries[1:]:
            value['extensions']['frozen']['nodes'][0]['arguments']['changed'] = True
        self.assertEqual(original, saved)

    def test_publishing_v2_checks_every_invalid_graph_and_instance_without_mutation(self):
        valid = to_resource_model(instance_document())
        variants = []
        def invalid(message, mutate):
            value = deepcopy(valid)
            mutate(value)
            variants.append((message, value))
        invalid('mixed neutral', lambda d: d.update(geometry=[]))
        invalid('duplicate resources key', lambda d: d['resources'].append(deepcopy(d['resources'][0])))
        invalid('unknown geometry resource', lambda d: d['resources'][1]['inputs'].append('missing'))
        invalid('cyclic geometry resource', lambda d: d['resources'][0].update(inputs=[d['resources'][1]['key']]))
        invalid('duplicate item key', lambda d: d['items'][1].update(key=d['items'][0]['key']))
        invalid('requires a geometry resource reference', lambda d: d['items'][0]['representations']['display'].update(extra=1))
        invalid('unknown resource', lambda d: d['items'][0]['representations']['display'].update(resource='missing'))
        invalid('instanceKey requires', lambda d: d['items'][0]['representations']['display'].pop('placement'))
        invalid('finite numbers', lambda d: d['items'][0]['representations']['display']['placement']['origin'].__setitem__(0, float('inf')))
        invalid('right-handed frame', lambda d: d['items'][0]['representations']['display']['placement']['xAxis'].__setitem__(0, 2))
        invalid('colliding instanceKey', lambda d: d['items'][0]['representations']['display'].update(instanceKey=d['resources'][0]['key']))
        invalid('conflicting instanceKey', lambda d: d['items'][1]['representations']['display'].update(instanceKey=d['items'][0]['representations']['display']['instanceKey']))
        for message, value in variants:
            with self.subTest(message=message):
                saved = deepcopy(value)
                errors = []
                for convert in (to_resource_model, expand_resource_model):
                    with self.assertRaisesRegex(ValueError, message) as caught:
                        convert(value)
                    errors.append(str(caught.exception))
                    self.assertEqual(saved, value)
                self.assertEqual(errors[0], errors[1])

    def test_post_modules_reload_exact_content_and_recover_after_load_errors(self):
        post = load_shared('assembly_post_machining')
        section = SHARED / 'section_geometry.py'
        original_read = Path.read_bytes
        def source(text):
            return patch.object(Path, 'read_bytes', lambda path: text if path == section else original_read(path))
        with source(b'marker = "first"\n'):
            first = post._load('section_geometry')
            self.assertIs(first, post._load('section_geometry'))
            self.assertEqual('first', first.marker)
        with source(b'marker = "later"\n'):
            later = post._load('section_geometry')
            self.assertIsNot(first, later)
            self.assertEqual('later', later.marker)
        with source(b'raise ValueError("bad content")\n'):
            for unused in range(2):
                with self.assertRaisesRegex(ValueError, 'bad content'):
                    post._load('section_geometry')
        with source(b'marker = "first"\n'):
            self.assertIs(first, post._load('section_geometry'))

    def test_post_tool_identity_includes_exact_tool_and_section_content(self):
        post = load_shared('assembly_post_machining')
        section = SHARED / 'section_geometry.py'
        tool = SHARED.parent / 'mold/paired-end-tabs/tool.py'
        original_read = Path.read_bytes
        selected = {section: b'marker = 1\n', tool: b'marker = "a"\n'}
        with patch.object(Path, 'read_bytes', lambda path: selected.get(path, original_read(path))):
            first = post._tool('paired-end-tabs')
            self.assertIs(first, post._tool('paired-end-tabs'))
            selected[section] = b'marker = 2\n'
            changed_section = post._tool('paired-end-tabs')
            self.assertIsNot(first, changed_section)
            self.assertEqual(1, first.section_geometry.marker)
            self.assertEqual(2, changed_section.section_geometry.marker)
            selected[tool] = b'marker = "b"\n'
            changed_tool = post._tool('paired-end-tabs')
            self.assertIsNot(changed_section, changed_tool)
            self.assertEqual('b', changed_tool.marker)

    def test_geometry_runtime_cache_tracks_content_without_adding_data_to_results(self):
        functions = load_shared('assembly_process_functions')
        path = SHARED / 'assembly_geometry_process_runtime.py'
        original_read = Path.read_bytes
        selected = [b'marker = "first"\n']
        with patch.object(Path, 'read_bytes', lambda file: selected[0] if file == path else original_read(file)):
            first = functions._geometry_module()
            self.assertIs(first, functions._geometry_module())
            selected[0] = b'marker = "later"\n'
            later = functions._geometry_module()
            self.assertIsNot(first, later)
            self.assertEqual('later', later.marker)
            self.assertIn(hashlib.sha256(selected[0]).hexdigest(), later.__name__)

    def test_actual_wall_cache_owns_results_and_tracks_contours_and_parser_content(self):
        post = load_shared('assembly_post_machining')
        section = post._load('section_geometry')
        contours = [
            {'kind': 'polygon', 'points': [[-30, -20], [30, -20], [30, 20], [-30, 20]]},
            {'kind': 'polygon', 'points': [[-28, -18], [28, -18], [28, 18], [-28, 18]]},
        ]
        original_read = Path.read_bytes
        parser = SHARED / 'profile_recognition_geometry.py'
        parser_version = [b'first analysis dependency']
        post._WALL_CACHE.clear()
        with patch.object(Path, 'read_bytes', lambda file: parser_version[0] if file == parser else original_read(file)), \
                patch.object(section, 'closed_shell_metrics', wraps=section.closed_shell_metrics) as analyze:
            first = post._actual_walls({'stock': {'contours': deepcopy(contours)}})['stock']
            saved = deepcopy(first)
            first['outside']['max'][0] = 999999
            repeated = post._actual_walls({'branch': {'contours': deepcopy(contours)}})['branch']
            self.assertEqual(1, analyze.call_count)
            self.assertEqual(saved, repeated)
            self.assertFalse(mutable_ids(first) & mutable_ids(repeated))
            changed = deepcopy(contours)
            changed[0]['points'][0][0] += 1e-10
            post._actual_walls({'stock': {'contours': changed}})
            self.assertEqual(2, analyze.call_count)
            parser_version[0] = b'later analysis dependency'
            post._actual_walls({'stock': {'contours': deepcopy(contours)}})
            self.assertEqual(3, analyze.call_count)


if __name__ == '__main__':
    unittest.main()
