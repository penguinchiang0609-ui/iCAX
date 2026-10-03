"""Automatic window material dependencies use actual source-to-stock ownership."""
from copy import deepcopy
import unittest

from AssemblyProcessStageWindowTests import (
    FRAME, FOLD, allocate_compiled, compiled_window, resolve_allocation, semantic_snapshot,
)
from WindowManufacturingInputTests import execute, load
from icax_template_sdk import expand_resource_model
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model


def window_case(**changes):
    compiled, values, _, core = compiled_window()
    values.update(changes)
    original = deepcopy(values)
    declaration, design = core.manufacturing(values), core.display(values)
    compiled = load('assembly_window_process').compile_window_assembly(
        compose_manufacturing_model(declaration, design))
    assert values == original
    validate_manufacturing_model(compiled)
    return compiled, declaration, design, values


def owners_of(compiled):
    owners = {item['key']: item['key'] for item in compiled['items']}
    for mapping in compiled['sourceMappings']:
        for source in mapping['sources']:
            owners[source['itemKey']] = mapping['itemKey']
    return owners


def related_stocks(process, owners):
    definition = process['definition']
    stocks = set(definition.get('targets', {}).values())
    for role in definition['processInput']['parts'].values():
        if role.get('scope') in ('display', 'design', 'manufacturing'):
            stocks.add(owners[role['itemKey']])
    return stocks


def dependency_graph(compiled):
    return {call['key']: list(call['definition']['dependencies']) for call in compiled['processes']}


def find_call(compiled, key):
    return next(call for call in compiled['processes'] if call['key'] == key)


def manufactured_geometry(document):
    nodes = {node['key']: node for node in document['geometry']}
    roots = {item['key']: item['representations']['result'] for item in document['items']
             if item.get('representations', {}).get('result')}
    pending, reachable = list(roots.values()), {}
    while pending:
        key = pending.pop()
        if key in reachable:
            continue
        node = nodes[key]
        reachable[key] = node
        pending.extend(node['inputs'])
    return roots, reachable


class DependencyWindowTests(unittest.TestCase):
    def assert_topological(self, plan, graph):
        positions = {key: index for index, key in enumerate(plan['orderedKeys'])}
        self.assertEqual(set(positions), set(graph))
        self.assertEqual(plan['dependencies'], graph)
        for key, predecessors in graph.items():
            for predecessor in predecessors:
                self.assertLess(positions[predecessor], positions[key], (predecessor, key))

    def test_single_window_declares_real_frame_and_end_to_aperture_dependencies(self):
        compiled, _, _, _ = window_case()
        graph = dependency_graph(compiled)
        folds = {call['key'] for call in compiled['processes'] if call['definition']['templateId'] == FOLD}
        self.assertEqual(len(folds), 4)
        self.assertTrue(any(graph.values()), 'the compiled process graph must contain real data dependencies')
        self.assertTrue(all(not graph[key] for key in folds), 'four independent bends must not be chained')
        horizontal = 'main_grid.horizontal.0001'
        vertical = 'main_grid.vertical.0001'
        end = find_call(compiled, horizontal + '.end.start')
        self.assertEqual(end['definition']['processInput']['parts']['mate']['itemKey'], 'outer_frame.left.0001')
        self.assertEqual(set(graph[end['key']]), folds,
                         'the observed display member must map to its continuous frame stock')
        aperture = next(call for call in compiled['processes']
                        if call['definition']['templateId'] == 'tube-profile-aperture'
                        and call['definition']['targets']['stock'] == FRAME
                        and call['definition']['processInput']['parts']['branch']['itemKey'] == horizontal)
        horizontal_ends = {horizontal + '.end.start', horizontal + '.end.end'}
        self.assertEqual(set(graph[aperture['key']]), folds | horizontal_ends)
        grid_aperture = next(call for call in compiled['processes']
                            if call['definition']['templateId'] == 'tube-profile-aperture'
                            and call['definition']['targets']['stock'] == horizontal
                            and call['definition']['processInput']['parts']['branch']['itemKey'] == vertical)
        self.assertEqual(set(graph[grid_aperture['key']]), horizontal_ends |
                         {vertical + '.end.start', vertical + '.end.end'},
                         'an aperture must wait for both real member lengths without unrelated mate expansion')
        self.assert_topological(load('assembly_process_stages').plan_stages(compiled['processes']), graph)

    def test_three_continuous_window_frames_do_not_gain_unrelated_fold_dependencies(self):
        compiled, _, _, _ = window_case(accessDoorEnabled=True,
            doorFrameJoinType='v_groove_90:tool_library',
            doorLeafFrameJoinType='v_groove_90:tool_library')
        owners = owners_of(compiled)
        folds = {call['key']: next(iter(call['definition']['targets'].values()))
                 for call in compiled['processes'] if call['definition']['templateId'] == FOLD}
        self.assertEqual(len(set(folds.values())), 3)
        self.assertEqual(len(folds), 12)
        for call in compiled['processes']:
            dependencies = set(call['definition']['dependencies'])
            if call['key'] in folds:
                self.assertFalse(dependencies & set(folds), 'bend inputs must remain parallel declarations')
                continue
            stocks = related_stocks(call, owners)
            self.assertEqual(dependencies & set(folds),
                {key for key, stock in folds.items() if stock in stocks}, call['key'])
        self.assert_topological(load('assembly_process_stages').plan_stages(compiled['processes']),
                                dependency_graph(compiled))

    def test_open_frame_bends_wait_for_own_boundaries_not_observed_post_boundaries(self):
        compiled, _, _, _ = window_case(faceType='three')
        boundaries = {call['key']: set(call['definition']['targets'].values())
                      for call in compiled['processes']
                      if call['definition']['processInput']['geometry'].get('allocation') == 'initial-material-boundary'}
        self.assertTrue(any('outer_frame.vertical.' in key for key in boundaries))
        covered = 0
        for call in compiled['processes']:
            if call['definition']['templateId'] != FOLD:
                continue
            stocks = set(call['definition']['targets'].values())
            expected = {key for key, written in boundaries.items() if stocks & written}
            self.assertEqual(set(call['definition']['dependencies']), expected, call['key'])
            covered += bool(expected)
        self.assertGreater(covered, 0)
        self.assert_topological(load('assembly_process_stages').plan_stages(compiled['processes']),
                                dependency_graph(compiled))

    def test_reversed_submission_keeps_declared_graph_geometry_and_public_frozen_output(self):
        compiled, declaration, design, values = window_case()
        original = deepcopy((compiled, declaration, design, values))
        allocation = allocate_compiled(compiled)
        reference = semantic_snapshot(allocation, resolve_allocation(allocation))
        reversed_input = deepcopy(compiled)
        reversed_input['processes'].reverse()
        reversed_allocation = allocate_compiled(reversed_input)
        self.assertEqual(semantic_snapshot(reversed_allocation, resolve_allocation(reversed_allocation)), reference)
        graph = dependency_graph(compiled)
        self.assertEqual(dependency_graph(reversed_input), graph)
        for result in (allocation, reversed_allocation):
            self.assert_topological(result['stagePlan'], graph)
        public = execute(declaration, design)
        self.assertEqual((compiled, declaration, design, values), original)
        self.assertEqual(public['extensions']['tubeDesigner.assemblyStagePlan']['dependencies'], graph)
        self.assertEqual(public['extensions']['tubeDesigner.assemblyMaterialPlan'], allocation['materialPlan'])
        source = public['extensions']['tubeDesigner.assemblyProcessSource']
        self.assertEqual({call['instanceId']: call for call in source['instances']},
                         {call['instanceId']: call for call in allocation['assemblySource']['instances']})
        expanded_public = expand_resource_model(public)
        # The public model retains design observer resources as well. Compare
        # every node reachable from the actual manufactured representations.
        self.assertEqual(manufactured_geometry(expanded_public),
                         manufactured_geometry(allocation['model']))

    def test_explicit_dependency_survives_automatic_completion_and_stage_conflicts_fail(self):
        compiled, _, _, _ = window_case(accessDoorEnabled=True,
            doorFrameJoinType='v_groove_90:tool_library',
            doorLeafFrameJoinType='v_groove_90:tool_library')
        owners = owners_of(compiled)
        related = {call['key']: related_stocks(call, owners) for call in compiled['processes']}
        runtime = load('assembly_process_stages')
        apertures = [call for call in compiled['processes']
                     if call['definition']['templateId'] == 'tube-profile-aperture'
                     and call['definition']['targets']['stock'] == FRAME]
        self.assertGreater(len(apertures), 1)
        before = deepcopy(compiled)
        annotated = deepcopy(compiled['processes'])
        consumer = next(call for call in annotated if call['key'] == apertures[1]['key'])
        consumer['definition']['dependencies'].insert(0, apertures[0]['key'])
        annotated_before = deepcopy(annotated)
        completed = runtime.with_material_dependencies(annotated, material_bindings=related)
        self.assertEqual(annotated, annotated_before)
        self.assertEqual(compiled, before)
        completed_consumer = next(call for call in completed if call['key'] == consumer['key'])
        self.assertEqual(completed_consumer['definition']['dependencies'][0], apertures[0]['key'])
        self.assert_topological(runtime.plan_stages(completed),
                                {call['key']: call['definition']['dependencies'] for call in completed})
        invalid = deepcopy(compiled['processes'])
        outer_fold = next(call for call in invalid if call['definition']['templateId'] == FOLD
                          and call['definition']['targets']['stock'] == FRAME)
        leaf_end = next(call for call in invalid if call['key'].startswith('access_door.leaf.horizontal.')
                        and call['definition']['templateId'] == 'tube-end-joint')
        outer_fold['definition']['dependencies'].append(leaf_end['key'])
        invalid_before = deepcopy(invalid)
        with self.assertRaisesRegex(ValueError, 'stage dependency conflict|阶段'):
            runtime.with_material_dependencies(invalid, material_bindings=related)
        self.assertEqual(invalid, invalid_before)


if __name__ == '__main__':
    unittest.main()
