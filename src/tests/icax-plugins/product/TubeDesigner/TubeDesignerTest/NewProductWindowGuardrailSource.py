"""Current five-product source proofs and Native input preparation.

No historical report is a baseline. Source-only success does not assert Native
descriptor, BRep, or persistence acceptance. Profile build is sample preparation.
"""
import argparse
import ast
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest

SRC = Path(__file__).resolve().parents[5]
REPOSITORY = SRC.parent
TEMPLATES = SRC / 'apps/tube-designer/templates'
SDK = SRC / 'iCAX-Engine/framework/TemplateRuntime/python'
sys.path.insert(0, str(SDK))
sys.path.insert(0, str(TEMPLATES / '_shared'))
import icax_template_worker as worker
from icax_template_sdk.profile_constraints import validate_product_profile_parameters
from icax_template_sdk.manufacturing import compose_manufacturing_model
from icax_template_sdk.display import expand_display_model
from ProductManufacturingDeclarationTests import assert_declaration, forbid_processing
from WindowManufacturingInputTests import forbid_product_callbacks
import tube_profile_catalog as catalog
from NewProductWindowGuardrailFinishedReference import finished_reference

PRODUCTS = ['single_face_security_window', 'modular_guardrail',
            'modular_guardrail_cross-straight', 'modular_guardrail_diamond-straight',
            'modular_guardrail_glass-straight']
OUT = Path(os.environ.get('ICAX_NEW_PRODUCT_ACCEPTANCE_OUTPUT') or
           REPOSITORY / 'output/tests/new-product-cleanup-current-connections-20261003').resolve()
MANIFEST = OUT / 'NewProductWindowGuardrail-input-manifest.json'


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False,
                               indent=2) + '\n', encoding='utf-8')


def descriptor(name):
    return read(TEMPLATES / 'product' / name / 'template.json')


def defaults(name):
    return {p['key']: deepcopy(p['defaultValue']) for p in descriptor(name)['parameters']}


def freeze_sources():
    roots = [TEMPLATES / '_shared', TEMPLATES / 'profile', TEMPLATES / 'mold',
             TEMPLATES / 'assembly', TEMPLATES / 'finished-product', SDK]
    roots += [p for p in (TEMPLATES / 'product').iterdir()
              if p.is_dir() and (p / 'template.json').is_file()]
    files = sorted({p for root in roots if root.exists() for p in root.rglob('*')
                    if p.is_file() and p.suffix in ('.py', '.json')
                    and '__pycache__' not in p.parts})
    return {p.relative_to(SRC).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in files}


def check_frozen(expected):
    actual = freeze_sources()
    if actual != expected:
        keys = sorted(set(actual) | set(expected))
        first = next(key for key in keys if actual.get(key) != expected.get(key))
        raise AssertionError('Frozen source changed: ' + first)


def profile_snapshot(parameters, role):
    profile = catalog.load_profile(parameters, role)
    value = {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
             'kind': 'profile-package', 'profileForm': 'parametric',
             'sectionKind': profile.kind, 'name': profile.display_name,
             'specification': profile.specification, 'packageVersion': profile.package_version,
             'width': profile.width, 'depth': profile.depth, 'wallThickness': profile.wall,
             'contours': profile.contours()}
    value['contentDigest'] = hashlib.sha256(json.dumps(value, sort_keys=True,
        ensure_ascii=False, allow_nan=False).encode('utf-8')).hexdigest()
    return value


def cases():
    result = []
    def add(name, product, changes=None, *, full=False, reject=False, control=None,
            parameter_control=None):
        values = defaults(product)
        for key, value in (changes or {}).items():
            if key not in values and key != 'tubeDesignerProfileOverrides':
                raise AssertionError('Unknown current parameter: ' + key)
            values[key] = deepcopy(value)
        if not reject:
            for field in descriptor(product)['parameters']:
                if field.get('choices'):
                    assert values[field['key']] in [row['value'] for row in field['choices']], field['key']
        result.append({'name': name, 'product': product,
                       'templateId': descriptor(product)['id'], 'parameters': values,
                       'expected': 'reject' if reject else 'accept',
                       'fullNativeScene': full, 'inactiveOverrideControl': control,
                       'inactiveParameterControl': deepcopy(parameter_control)})
        return result[-1]
    window = PRODUCTS[0]
    compact = dict(horizontalMaximumCenterSpacing=360, verticalMaximumCenterSpacing=360,
                   sideHorizontalMaximumCenterSpacing=360, sideVerticalMaximumCenterSpacing=360,
                   topBottomCrossbarMaximumCenterSpacing=350, topBottomRodMaximumCenterSpacing=350,
                   doorHorizontalMaximumCenterSpacing=200, doorVerticalMaximumCenterSpacing=200)
    small_door = dict(doorClearWidth=350, doorClearHeight=300, doorLeft=80, doorBottom=100,
                      doorUOffset=80, doorVOffset=100, doorHorizontalTopCenterOffset=80,
                      doorHorizontalBottomCenterOffset=80, doorVerticalLeftCenterOffset=50,
                      doorVerticalRightCenterOffset=50)
    routes = [row['value'] for row in next(p for p in descriptor(window)['parameters']
              if p['key'] == 'frameManufacturingMode')['choices']]
    for f, face in enumerate(('single', 'two', 'three', 'five')):
        for r, route in enumerate(routes):
            add(f'window-{face}-{route}', window, {**compact, 'accessDoorEnabled': False,
                'faceType': face, 'frameManufacturingMode': route,
                'infillPattern': ('grid', 'vertical', 'horizontal')[(f+r) % 3],
                'frameCornerJoin': ('post_butt', 'rail_miter')[(f+r) % 2]}, full=face == 'single')
    for layout, pattern in [('left_right', 'horizontal'), ('top_bottom', 'vertical')]:
        add('window-open-' + layout, window, {**compact, 'accessDoorEnabled': False,
            'frameLayout': layout, 'infillPattern': pattern}, full=layout == 'left_right')
    for pattern in ('horizontal', 'vertical'):
        add('window-external-' + pattern, window, {**compact, 'accessDoorEnabled': False,
            'assemblyPlanningMode': 'external_templates', 'infillPattern': pattern}, full=pattern == 'vertical')
    openings = [('two', 'side', 'left'), ('two', 'side', 'right'), ('three', 'left', None),
                ('three', 'right', None), ('five', 'left', None), ('five', 'right', None),
                ('five', 'bottom', None)]
    for index, (face, position, side) in enumerate(openings):
        change = {**compact, **small_door, 'accessDoorEnabled': True, 'faceType': face,
                  'accessDoorFace' + {'two': '2', 'three': '3', 'five': '5'}[face]: position,
                  'frameManufacturingMode': routes[index % 3],
                  'doorFrameJoinType': ('miter_45', 'butt_90', 'v_groove_90:tool_library')[index % 3],
                  'doorLeafFrameJoinType': ('v_groove_90:tool_library', 'miter_45', 'butt_90')[index % 3]}
        if side: change['sidePosition'] = side
        add(f'window-door-{face}-{position}-{side or "fixed"}', window, change,
            full=face == 'five' and position == 'bottom')
    for prefix in ('frame', 'doorFrame', 'doorLeafFrame'):
        for wrap in ('side_wraps_horizontal', 'horizontal_wraps_side'):
            add(f'window-butt-{prefix}-{wrap}', window, {**compact, **small_door,
                'accessDoorEnabled': True, prefix + 'JoinType': 'butt_90', prefix + 'ButtWrapMode': wrap})
    draft_parameters = {**defaults(window), **compact, **small_door}
    for role, change in [('doorFrame', {'accessDoorEnabled': False}),
                         ('vertical', {'accessDoorEnabled': False, 'infillPattern': 'horizontal'}),
                         ('horizontal', {'accessDoorEnabled': False, 'infillPattern': 'vertical'})]:
        snapshot = profile_snapshot(draft_parameters, role)
        snapshot['sectionKind'] = 'arbitrary'
        add('window-inactive-' + role, window, {**compact, **change,
            'tubeDesignerProfileOverrides': {role: snapshot}}, control=role)
    for product in PRODUCTS[1:]:
        common = dict(sideLength1=1200, sideLength2=900, sideLength3=900, maximumPostSpacing=600)
        variants = [
            ('rect', {role+'ProfileType': 'rect' for role in ('handrail','post','rail','infill')}),
            ('round', {role+'ProfileType': 'round' for role in ('handrail','post','rail','infill')}),
            ('left-double-middle', dict(layout='left_l', cornerPostMode='double', railCount=3,
                                        largePostMode='middle', postCapEnabled=True)),
            ('right-side-plate', dict(layout='right_l', railCount=2, installation='side_plate')),
            ('u-base-plate-ends', dict(layout='u', largePostMode='ends', installation='base_plate')),
            ('continuous', dict(pathMode='continuous', handrailMode='continuous', slopeAngle=30)),
            ('stepped', dict(pathMode='stepped', slopeAngle=30)),
            ('treads', dict(pathMode='continuous', elevationSource='treads', treadCount1=5,
                            treadGoing=240, treadRise=140))]
        for index, (label, change) in enumerate(variants):
            if product == 'modular_guardrail':
                change['barDistribution'] = ('equal', 'fixed_center', 'manual_count', 'half_edge')[index % 4]
                if label == 'right-side-plate': change.update(barOrientation='horizontal', guardrailUse='wall')
            full = ((product == 'modular_guardrail' and label in ('rect','round','continuous','stepped'))
                    or (product != 'modular_guardrail' and label == 'round')
                    or (product == 'modular_guardrail_glass-straight' and label == 'u-base-plate-ends'))
            add(product + '-' + label, product, {**common, **change}, full=full)
    assert len(result) == 64 and sum(row['fullNativeScene'] for row in result) == 14
    post_route = {**compact, 'accessDoorEnabled': False, 'faceType': 'three',
                  'frameManufacturingMode': 'plane_v_notch', 'infillPattern': 'grid'}
    for joint in ('insert', 'tabs'):
        add('window-three-planar-post-' + joint, window,
            {**post_route, 'foldedPostJoint': joint}, full=True)
    add('window-three-post-insert-inactive-tab-drafts', window,
        {**post_route, 'foldedPostJoint': 'insert', 'foldedPostTabWidth': 100.,
         'foldedPostTabLength': 100., 'foldedPostSideClearance': 100.},
        parameter_control=['foldedPostTabWidth', 'foldedPostTabLength', 'foldedPostSideClearance'])
    add('window-three-post-tabs-inactive-insert-drafts', window,
        {**post_route, 'foldedPostJoint': 'tabs', 'foldedPostInsertDepth': 100.,
         'foldedPostFitGap': 100.}, parameter_control=['foldedPostInsertDepth', 'foldedPostFitGap'])
    invalid_post = profile_snapshot(defaults(window), 'foldedPost')
    invalid_post['sectionKind'] = 'oval'
    for label, change in (
            ('weld', {'foldedPostJoint': 'weld'}),
            ('segment', {'frameManufacturingMode': 'segment_weld', 'foldedPostJoint': 'insert'}),
            ('spatial', {'frameManufacturingMode': 'spatial_v_notch', 'foldedPostJoint': 'tabs'}),
            ('single', {'faceType': 'single', 'foldedPostJoint': 'insert'})):
        add('window-inactive-folded-post-' + label, window,
            {**post_route, **change, 'tubeDesignerProfileOverrides': {'foldedPost': invalid_post}},
            control='foldedPost')
    for product in (window, 'modular_guardrail'):
        p = defaults(product)
        p.update(compact if product == window else dict(sideLength1=1200, maximumPostSpacing=600))
        if product == window:
            p.update(small_door, infillPattern='grid', accessDoorEnabled=True,
                     faceType='three', frameManufacturingMode='plane_v_notch', foldedPostJoint='insert')
        roles = descriptor(product)['extensions']['resourceRoles']['profiles']
        overrides = {role: profile_snapshot(p, role) for role in roles}
        add(product + '-library-profiles', product, {**p, 'tubeDesignerProfileOverrides': overrides})
    for role in descriptor(window)['extensions']['resourceRoles']['profiles']:
        p = {**defaults(window), **compact, **small_door, 'infillPattern': 'grid', 'accessDoorEnabled': True}
        if role == 'foldedPost':
            p.update(faceType='three', frameManufacturingMode='plane_v_notch', foldedPostJoint='insert')
        snapshot = profile_snapshot(p, role); snapshot['sectionKind'] = 'oval'
        add('reject-window-' + role + '-oval', window, {**p, 'tubeDesignerProfileOverrides': {role: snapshot}}, reject=True)
    for product in PRODUCTS[1:]:
        snapshot = profile_snapshot(defaults(product), 'handrail'); snapshot['sectionKind'] = 'oval'
        add('reject-' + product + '-oval', product, {'tubeDesignerProfileOverrides': {'handrail': snapshot}}, reject=True)
    for label in ('arbitrary', 'missing', 'resource-kind-alias'):
        snapshot = profile_snapshot(defaults(window), 'frame')
        if label == 'arbitrary': snapshot['sectionKind'] = 'arbitrary'
        else: snapshot.pop('sectionKind')
        if label == 'resource-kind-alias': snapshot['kind'] = 'round'
        add('reject-window-' + label, window, {'tubeDesignerProfileOverrides': {'frame': snapshot}}, reject=True)
    assert len(result) == 89 and sum(row['fullNativeScene'] for row in result) == 16
    return result


def reference(request):
    product = request['product']
    template = deepcopy(request.get('template') or descriptor(product))
    values = deepcopy(request['parameters']); frozen = deepcopy(values)
    validate_product_profile_parameters(template, values)
    # Native NormalizeSingleParameter represents declared numbers as double.
    # JSON capture can spell integral doubles as integers, which changes Python
    # digest identity. Restore only these private declared scalar types; the
    # public host snapshot and exact returned parameters remain untouched.
    generation_values = deepcopy(values)
    typed_number_fields = []
    for field in template['parameters']:
        if field['valueType'] == 'number':
            assert not isinstance(generation_values[field['key']], bool)
            generation_values[field['key']] = float(generation_values[field['key']])
            typed_number_fields.append(field['key'])
    private_frozen = deepcopy(generation_values)
    consumed = {}
    documents = {}
    for purpose in ('display', 'manufacturing'):
        roles = []
        with forbid_processing() as forbidden:
            documents[purpose] = worker._evaluate({'template': template, 'parameters': generation_values,
                'templatePath': str(TEMPLATES / 'product' / product / 'template.py'),
                'context': {'geometryPurpose': purpose}}, roles)
        assert not forbidden and values == frozen and generation_values == private_frozen
        consumed[purpose] = sorted(set(roles))
    test = unittest.TestCase()
    assert_declaration(test, documents['manufacturing'])
    pure_before = deepcopy(documents)
    expand_display_model(documents['display'])
    combined = compose_manufacturing_model(documents['manufacturing'], documents['display'])
    assert documents == pure_before and combined['schemaVersion'] == 3
    actual_members = {item['key'] for item in combined['items']
                      if item.get('geometry') or 'designInput' in item}
    assert {mapping['itemKey'] for mapping in combined['sourceMappings']} == actual_members
    for mapping in combined['sourceMappings']:
        assert mapping['sources'] == [{'itemKey': mapping['itemKey']}]
    frozen_docs = deepcopy(documents)
    with forbid_product_callbacks():
        execution = worker._execute_manufacturing({'manufacturingDefinition': documents['manufacturing'],
            'designModel': documents['display'], 'context': {'sharedRoot': str(TEMPLATES / '_shared'),
            'template': {key: template.get(key, '') for key in ('id','version','packageDigest')},
            'parameters': deepcopy(generation_values)}})
    assert documents == frozen_docs and values == frozen and generation_values == private_frozen
    # The executor is a pure geometry boundary, not the product API envelope.
    # TubeDesignerSDO.cpp installs the already-normalized product input afterward.
    assert execution['parameters'] == {}
    execution['parameters'] = deepcopy(frozen)
    items = execution['items']
    assert items and len({item['key'] for item in items}) == len(items)
    stock_allocation = 'tubeDesigner.assemblyProcessSource' in execution['extensions']
    if stock_allocation:
        assert execution['extensions']['tubeDesigner.productStockMappingVersion'] == 1
        assert all(item['properties'].get('manufacturing.sourceMembers') for item in items)
    if request.get('fullNativeScene'):
        finished = finished_reference(execution)
    else:
        finished = {'finishedDocument': None, 'sourceAssemblyPlan': None,
            'finishedReferenceProof': {'requiredForThisCase': False,
                'finishedGeometryOracleConstructed': False,
                'actualNativeEntityShapeValidationSelected': False}}
    return {'templateId': template['id'], 'parameters': frozen, **documents, 'document': execution, **finished,
            'consumedProfileRoles': consumed, 'partFacts': {'parts': len(items),
                'checkedLengths': sum(isinstance(item['properties'].get('length'), (int,float)) for item in items)},
            'sourceChecks': {'inputUnchanged': True, 'executionParametersEqual': True,
                'declarationDidNotExecuteProcessing': True, 'independentExecutorNoProductCallback': True,
                'declarationsUnchangedAfterExecution': True, 'explicitSourceMappingsVerifiedInComposition': True,
                'hostEnvelopeAppliedAfterPureExecutor': True},
            'sourcePrivateParameterTyping': {'rule': 'Native descriptor number fields use double; public host snapshot is untouched',
                'typedNumberFields': typed_number_fields},
            'sourceMappingKind': 'allocated-stock-source-members' if stock_allocation else 'composed-explicit-design-item-identity'}


def source(manifest, output, case_name=None, artifact_directory=None):
    check_frozen(manifest['sourceDigests'])
    report = {'schema': 'icax.new-product-window-guardrail-source', 'schemaVersion': 1,
              'nativeExecuted': False, 'fullAcceptanceAsserted': False, 'passed': False,
              'sourceDigests': manifest['sourceDigests'], 'cases': []}
    selected = [case for case in manifest['cases'] if case_name is None or case['name'] == case_name]
    assert selected and (case_name is None or len(selected) == 1)
    report['limitedCase'] = case_name
    report['expectedCaseCount'] = len(selected)
    artifact_directory = Path(artifact_directory or Path(output).parent)
    for case in selected:
        row = {'name': case['name'], 'expected': case['expected'], 'passed': False}
        report['cases'].append(row)
        try:
            proof = reference(case)
            if case['expected'] == 'reject': raise AssertionError('Rejected profile was accepted')
            if case['inactiveOverrideControl']:
                control = deepcopy(case); control['parameters'].pop('tubeDesignerProfileOverrides')
                baseline = reference(control)
                assert proof['display'] == baseline['display']
                assert proof['manufacturing'] == baseline['manufacturing']
                assert all(case['inactiveOverrideControl'] not in roles
                           for roles in proof['consumedProfileRoles'].values())
                row['inactiveDraftDoesNotAffectGeometryOrDeclaration'] = True
            elif case.get('inactiveParameterControl'):
                control = deepcopy(case)
                current_defaults = defaults(case['product'])
                for key in case['inactiveParameterControl']:
                    control['parameters'][key] = deepcopy(current_defaults[key])
                baseline = reference(control)
                assert proof['display'] == baseline['display']
                assert proof['manufacturing'] == baseline['manufacturing']
                assert {key: value for key, value in proof['document'].items() if key != 'parameters'} == {
                    key: value for key, value in baseline['document'].items() if key != 'parameters'}
                row['inactiveParameterDraftsDoNotAffectGeometryOrDeclaration'] = case['inactiveParameterControl']
            elif case['name'].endswith('-library-profiles'):
                selected_roles = set(case['parameters']['tubeDesignerProfileOverrides'])
                assert all(set(roles) == selected_roles for roles in proof['consumedProfileRoles'].values())
                row['allSelectedLibraryRolesActuallyConsumed'] = True
            file = artifact_directory / (case['name'] + '-source-reference.json')
            write(file, proof)
            row.update(passed=True, sourceChecks=proof['sourceChecks'], partFacts=proof['partFacts'],
                       referenceFile=str(file), referenceSha256=hashlib.sha256(file.read_bytes()).hexdigest())
        except ValueError as error:
            if case['expected'] != 'reject':
                row['error'] = str(error)
            else:
                # Only the intended profile admission failure proves rejection.
                text = str(error)
                if not (text.startswith('profile section kind is not applicable to role ')
                        or text == 'unsupported product profile snapshot schema'):
                    row['error'] = text
                else: row.update(passed=True, actualProfileRejection=text)
        except Exception as error:
            row['error'] = type(error).__name__ + ': ' + str(error)
        write(output, report)
        print(json.dumps({'case': case['name'], 'passed': row['passed'], 'error': row.get('error')}, ensure_ascii=False), flush=True)
    check_frozen(manifest['sourceDigests'])
    report['sourceBeforeAfterUnchanged'] = True
    report['passed'] = len(report['cases']) == len(selected) and all(row['passed'] for row in report['cases'])
    write(output, report)
    return report['passed']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--source', action='store_true')
    parser.add_argument('--case', help='One explicit current case only; never full acceptance')
    parser.add_argument('--reference', nargs=2, metavar=('REQUEST', 'OUTPUT'))
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output-directory', type=Path, default=OUT)
    args = parser.parse_args()
    args.output_directory = args.output_directory.resolve()
    args.manifest = args.manifest or args.output_directory / MANIFEST.name
    if args.prepare:
        before = freeze_sources(); entries = cases(); check_frozen(before)
        manifest = {'schema': 'icax.new-product-window-guardrail-input-manifest', 'schemaVersion': 1,
                    'products': PRODUCTS, 'sourceDigests': before, 'cases': entries,
                    'coverage': {'validBranches': 72, 'allowedLibraryCases': 2, 'profileRejections': 15,
                        'actualNativeSceneCases': 16, 'pointsPerEntity': 343,
                        'foldedPostCompleteScenes': ['window-three-planar-post-insert', 'window-three-planar-post-tabs'],
                        'inactiveFoldedPostProfileControls': 4, 'inactiveFoldedPostParameterControls': 2},
                    'preparedOnly': True, 'nativeExecuted': False, 'sourceExecutionPerformed': False,
                    'fullAcceptanceAsserted': False}
        write(args.manifest, manifest)
        print(json.dumps({'manifest': str(args.manifest), 'cases': len(entries), 'nativeExecuted': False}))
    elif args.reference:
        write(args.reference[1], reference(read(args.reference[0])))
    elif args.source:
        source_report = args.output_directory / ('NewProductWindowGuardrail-source-first-case.json' if args.case else 'NewProductWindowGuardrail-source.json')
        if not source(read(args.manifest), source_report, args.case, args.output_directory): sys.exit(1)
    else: parser.error('Select --prepare, --source, or --reference REQUEST OUTPUT')


if __name__ == '__main__': main()
