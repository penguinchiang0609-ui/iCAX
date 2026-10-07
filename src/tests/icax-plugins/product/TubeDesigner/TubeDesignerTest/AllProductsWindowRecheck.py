"""Independent non-default branch audit; never changes production or deployment.

Run with bundled Python -B. Every successful case executes the public v4
declaration from its display v2 input under a forbidden-product-callback guard.
The historical generator is used separately as the material/process oracle.
"""
from collections import Counter
from copy import deepcopy
import argparse
import contextlib
import hashlib
import json
import math
from pathlib import Path
import sys
import time

from WindowCatalogueTests import package, SRC
from WindowManufacturingInputTests import execute, forbid_product_processing
from WindowManufacturingLegacyTopologyTests import machining, differences, native_source_intervals
from icax_template_sdk.manufacturing import compose_manufacturing_model

OUTPUT = SRC.parent / 'output/tests/assembly-process'
REPORT = OUTPUT / 'all-products-recheck-window-branches.json'
DESCRIPTOR, DEFAULTS, CORE = package('single_face_security_window')
ENUMS = {p['key']: [c['value'] for c in p.get('choices', [])]
         for p in DESCRIPTOR['parameters'] if p.get('choices')}
CHOICES = {'assemblyPlanningMode','frameManufacturingMode','frameJoinType','frameCornerJoin','frameButtWrapMode',
           'doorFrameJoinType','doorFrameButtWrapMode','doorLeafFrameJoinType','doorLeafFrameButtWrapMode',
           'assemblyClearance','horizontalBranchReserve','verticalBranchReserve'}
ROUTES = ('segment_weld','plane_v_notch','spatial_v_notch')
COARSE = {'verticalMaximumCenterSpacing':360., 'sideVerticalMaximumCenterSpacing':360.,
          'topBottomRodMaximumCenterSpacing':350., 'doorVerticalMaximumCenterSpacing':200.}
SMALL_DOOR = {'doorClearWidth':350.,'doorClearHeight':300.,'doorUOffset':80.,'doorVOffset':100.,
              'doorHorizontalTopCenterOffset':80.,'doorHorizontalBottomCenterOffset':80.,
              'doorVerticalLeftCenterOffset':50.,'doorVerticalRightCenterOffset':50.}
AUDITED_SOURCES = [SRC/'apps/tube-designer/templates'/name for name in (
    '_shared/product_window_manufacturing.py','_shared/assembly_window_process.py',
    '_shared/plate_geometry.py',
    '_shared/assembly_material_allocation.py','_shared/assembly_manufacturing_executor.py','_shared/security_window_product_contract.py',
    '_shared/security_window_frame_geometry.py','_shared/multi_face_security_window.py',
    'product/single_face_security_window/template.py','product/single_face_security_window/template.json')]
AUDITED_SOURCES += sorted((SRC/'iCAX-Engine/framework/TemplateRuntime/python').rglob('*.py'))


def source_snapshot():
    return {str(path.relative_to(SRC)):hashlib.sha256(path.read_bytes()).hexdigest() for path in AUDITED_SOURCES}


def cases():
    result=[]
    def add(name, changes, group=None, expected_rejection=False, reference_changes=None):
        result.append({'name':name,'changes':{**COARSE,**changes},'stableGroup':group,
                       'expectedRejection':expected_rejection,'referenceChanges':reference_changes or {}})
    # Focus first on branches omitted by the old 24 default combinations.
    for pattern in ('horizontal','vertical'):
        add('external-design-reference-'+pattern, {'assemblyPlanningMode':'legacy_processed',
            'infillPattern':pattern,'accessDoorEnabled':False},'external-'+pattern)
        for route in ROUTES:
            add('external-'+pattern+'-'+route, {'assemblyPlanningMode':'external_templates',
                'infillPattern':pattern,'accessDoorEnabled':False,'frameManufacturingMode':route,
                'assemblyClearance':-1.,'horizontalBranchReserve':-1.,'verticalBranchReserve':-1.},
                'external-'+pattern)
    for layout in ('left_right','top_bottom'):
        for route in ROUTES:
            add('open-'+layout+'-'+route, {'frameLayout':layout,'accessDoorEnabled':False,
                'frameManufacturingMode':route,'outerFrameGrooveTool':'stale-invalid-reference'}, 'open-'+layout)
    # All public alternate access-door surfaces, including a bottom cap.
    surfaces=[('two','side','left'),('two','side','right'),('three','left',None),
              ('three','right',None),('five','left',None),('five','right',None),('five','bottom',None)]
    for index,(face,surface,side) in enumerate(surfaces):
        for route in ROUTES:
            changes={**SMALL_DOOR,'faceType':face,'accessDoorEnabled':True,
                'accessDoorFace'+{'two':'2','three':'3','five':'5'}[face]:surface,
                'infillPattern':('horizontal','vertical','grid')[index%3],
                'frameManufacturingMode':route,'doorFrameJoinType':'butt_90',
                'doorLeafFrameJoinType':'v_groove_90:tool_library',
                'doorFrameButtWrapMode':'horizontal_wraps_side'}
            if side: changes['sidePosition']=side
            add(face+'-door-'+surface+(('-'+side) if side else '')+'-'+route,changes,
                face+'-door-'+surface+(('-'+side) if side else ''))
        for part in ('doorFrame','doorLeafFrame'):
            for wrap in ENUMS[part+'ButtWrapMode']:
                changes={**SMALL_DOOR,'faceType':face,'accessDoorEnabled':True,
                    'accessDoorFace'+{'two':'2','three':'3','five':'5'}[face]:surface,
                    'infillPattern':'grid','frameManufacturingMode':ROUTES[index%3],
                    part+'JoinType':'butt_90',part+'ButtWrapMode':wrap}
                if side: changes['sidePosition']=side
                add('wrap-'+face+'-'+surface+(('-'+side) if side else '')+'-'+part+'-'+wrap,
                    changes,'wrap-'+face+'-'+surface+(('-'+side) if side else ''))
    # Every face, every grid orientation and every manufacturing route, with
    # different pitches/dimensions rather than rerunning the default matrix.
    for face in ('single','two','three','five'):
        for pattern in ('horizontal','vertical','grid'):
            for route in ROUTES:
                add('grid-'+face+'-'+pattern+'-'+route, {'faceType':face,'infillPattern':pattern,
                    'accessDoorEnabled':False,'width':1350.,'height':1950.,'depth':720.,
                    'leftWidth':550.,'rightWidth':740.,'sideWidth':670.,
                    'frameManufacturingMode':route,'frameCornerJoin':'rail_miter'}, 'grid-'+face+'-'+pattern)
    # Frame and leaf joints are independent, each butt-wrap orientation matters.
    for prefix in ('frame','doorFrame','doorLeafFrame'):
        for join in ENUMS[prefix+'JoinType']:
            wraps=ENUMS[prefix+'ButtWrapMode'] if join=='butt_90' else [DEFAULTS[prefix+'ButtWrapMode']]
            for wrap in wraps:
                add('joint-'+prefix+'-'+join.replace(':','-')+'-'+wrap,
                    {prefix+'JoinType':join,prefix+'ButtWrapMode':wrap,'accessDoorEnabled':True,
                     'infillPattern':'grid','frameManufacturingMode':'segment_weld'},'join-stable')
    # Closed/absent options must retain drafts without consuming them.
    for face in ('single','three','five'):
        add('inactive-door-drafts-'+face, {'faceType':face,'accessDoorEnabled':False,
            'doorClearWidth':-123.,'doorClearHeight':-456.,'doorGap':-10.,
            'doorFrameWidth':-40.,'doorLeafFrameWidth':-30.,'doorHorizontalWidth':-20.,
            'doorVerticalWidth':-10.,'doorFrameJoinType':'v_groove_90:tool_library',
            'doorLeafFrameJoinType':'v_groove_90:tool_library',
            'doorFrameGrooveTool':'stale-invalid-reference','doorLeafFrameGrooveTool':'stale-invalid-reference'},
            'inactive-door-'+face)
        add('inactive-door-baseline-'+face, {'faceType':face,'accessDoorEnabled':False},'inactive-door-'+face)
    for face in ('two','three','five'):
        for route in ROUTES:
            add('inactive-external-'+face+'-'+route, {'faceType':face,'accessDoorEnabled':False,
                'assemblyPlanningMode':'external_templates','frameManufacturingMode':route},
                'inactive-external-'+face, reference_changes={'assemblyPlanningMode':'legacy_processed'})
    for pattern,inactive in (('horizontal',{'verticalWidth':-30.,'doorVerticalWidth':-30.,
            'verticalLeftCenterOffset':-90.,'doorVerticalLeftCenterOffset':-100.,'verticalBranchReserve':-1.}),
            ('vertical',{'horizontalWidth':-30.,'doorHorizontalWidth':-30.,
             'horizontalMaximumCenterSpacing':-50.,'doorHorizontalMaximumCenterSpacing':-50.,'horizontalBranchReserve':-1.})):
        add('inactive-grid-'+pattern,{**inactive,'infillPattern':pattern,'accessDoorEnabled':False},'inactive-grid-'+pattern)
        add('inactive-grid-baseline-'+pattern,{'infillPattern':pattern,'accessDoorEnabled':False},'inactive-grid-'+pattern)
    # Different non-square and asymmetric pipe components, including door pipes.
    profile={'schema':'icax.imported-tube-profile','schemaVersion':1,'kind':'fixed-section',
        'profileForm':'fixed','sectionKind':'rect','contentDigest':'window-recheck-chamfer-38x28',
        'name':'偏角空心矩形','specification':'38x28x1.2','width':38.,'depth':28.,'wallThickness':1.2,
        'contours':[{'kind':'polygon','points':[[-19.,-14.],[19.,-14.],[19.,10.],[15.,14.],[-19.,14.]]},
                    {'kind':'polygon','points':[[-17.8,-12.8],[-17.8,12.8],[14.5029437252,12.8],
                                                [17.8,9.5029437252],[17.8,-12.8]]}]}
    dimensions={'tubeSpecificationPreset':'custom','frameWidth':38.,'frameDepth':28.,
        'horizontalWidth':24.,'horizontalDepth':20.,'verticalWidth':14.,'verticalDepth':14.,
        'doorFrameWidth':30.,'doorFrameDepth':24.,'doorLeafFrameWidth':24.,'doorLeafFrameDepth':20.,
        'doorHorizontalWidth':18.,'doorHorizontalDepth':16.,'doorVerticalWidth':10.,
        'doorHorizontalTopCenterOffset':100.,'doorHorizontalBottomCenterOffset':100.}
    for asymmetric in (False,True):
        for route in ROUTES:
            changes={**dimensions,**SMALL_DOOR,'faceType':'three','accessDoorFace3':'left',
                'frameManufacturingMode':route,'doorFrameJoinType':'butt_90',
                'doorLeafFrameJoinType':'v_groove_90:tool_library'}
            if asymmetric: changes['tubeDesignerProfileOverrides']={'frame':profile}
            add(('asymmetric' if asymmetric else 'nonsquare')+'-door-left-'+route,changes,
                'asymmetric-door-left' if asymmetric else 'nonsquare-door-left',expected_rejection=route=='spatial_v_notch')
    translated=deepcopy(profile)
    translated['contentDigest']='window-recheck-offset-chamfer-38x28'
    for contour in translated['contours']:
        contour['points']=[[x+3.,y-2.] for x,y in contour['points']]
    for route in ROUTES:
        add('shifted-asymmetric-datum-'+route,{**dimensions,'faceType':'two','accessDoorEnabled':False,
            'frameManufacturingMode':route,'tubeDesignerProfileOverrides':{'frame':translated}},'shifted-asymmetric-datum',
            expected_rejection=route!='segment_weld')
        add('rect-vertical-'+route,{'faceType':'five','accessDoorEnabled':False,'verticalProfileType':'rect',
            'verticalWidth':14.,'verticalDepth':10.,'verticalCornerRadius':1.,
            'frameManufacturingMode':route},'rect-vertical')
        add('nondefault-insertion-'+route,{'faceType':'three','accessDoorEnabled':True,
            'horizontalBranchReserve':6.,'verticalBranchReserve':7.,'assemblyClearance':.25,
            'frameManufacturingMode':route},'nondefault-insertion')
    # A real zero-radius branch with an exact imported rectangular section.
    # The component retains polygon facts; only the shared geometry language
    # spells its same edges as generic lines.
    sharp={**deepcopy(profile),'contentDigest':'window-recheck-sharp-rect-38x28',
           'contours':[{'kind':'polygon','points':[[-19.,-14.],[19.,-14.],[19.,14.],[-19.,14.]]},
                       {'kind':'polygon','points':[[-17.8,-12.8],[-17.8,12.8],[17.8,12.8],[17.8,-12.8]]}]}
    add('sharp-rect-zero-radius-segment_weld',{'faceType':'single','accessDoorEnabled':False,
        'frameManufacturingMode':'segment_weld','frameWidth':38.,'frameDepth':28.,'frameCornerRadius':0.,
        'horizontalCornerRadius':0.,'tubeDesignerProfileOverrides':{'frame':sharp}},'sharp-rect-zero-radius')
    # Descriptor presets are actual selection branches even though the host owns
    # any associated parameter propagation; the script must preserve input.
    for preset in ENUMS['tubeSpecificationPreset']:
        add('preset-'+preset,{'tubeSpecificationPreset':preset,'accessDoorEnabled':False,
                             'infillPattern':'vertical'},'presets-no-script-propagation')
    return result


@contextlib.contextmanager
def forbid_display_callback():
    old=CORE.display
    def blocked(*args,**kwargs): raise AssertionError('manufacturing called display')
    CORE.display=blocked
    try: yield
    finally: CORE.display=old


def sub(a,b): return [x-y for x,y in zip(a,b)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]


def check_declarations(design,raw):
    assert set(design)=={'schema','schemaVersion','coordinateSystem','lengthUnit','resources','items','roots','annotations'}
    assert design['schemaVersion']==2
    assert set(raw)=={'schema','schemaVersion','connections','processes'} and raw['schemaVersion']==4
    resources={node['key']:node for node in design['resources']}; items={item['key']:item for item in design['items']}
    assert len(resources)==len(design['resources']) and len(items)==len(design['items'])
    assert set(design['roots'])==set(items) and len(design['roots'])==len(set(design['roots']))
    for node in resources.values():
        assert set(node.get('inputs',[]))<=set(resources)
        if node['operator']=='profile2d':
            assert all(contour['kind']=='path' for contour in node['arguments']['contours']),node['key']
    shared_sections={}; facts=Counter(); anchors=0
    for key,item in items.items():
        assert item['geometry']['resource'] in resources
        p=item['properties']; m=p['assemblyFrame.member']; f=m['sectionFrame']
        assert math.dist(m['start'],m['end'])>1.e-8
        axis=sub(m['end'],m['start']); norm=math.sqrt(dot(axis,axis)); z=[v/norm for v in axis]
        for vector in ('xAxis','yAxis','zAxis'): assert abs(dot(f[vector],f[vector])-1.)<1.e-7,(key,vector)
        assert max(abs(x-y) for x,y in zip(z,f['zAxis']))<1.e-7,(key,'actual-endpoint-axis')
        assert max(abs(x-y) for x,y in zip(cross(f['zAxis'],f['xAxis']),f['yAxis']))<1.e-7,(key,'right-handed')
        profile=p['tubeDesigner.profile']; section=resources[profile['sectionResource']]
        assert section['operator']=='profile2d',key
        generic=section['arguments']['contours']
        assert len(generic)==len(profile['contours']),key
        for original,emitted in zip(profile['contours'],generic):
            assert emitted['kind']=='path',key
            if original['kind']=='polygon':
                vertices=original['points'];segments=emitted['segments']
                assert emitted['closed'] is True and len(segments)==len(vertices),key
                for index,segment in enumerate(segments):
                    assert segment=={'kind':'line','start':vertices[index],'end':vertices[(index+1)%len(vertices)]},key
            else:
                assert emitted==original,key
        canonical=json.dumps(profile['contours'],sort_keys=True)
        if canonical in shared_sections: assert profile['sectionResource']==shared_sections[canonical],key
        shared_sections[canonical]=profile['sectionResource']
        assert not any(k.startswith('manufacturing.') for k in p),key
        for fact in ('categoryKey','categoryName','partKind','sourcing','materialCategory'):
            assert fact in p,(key,'missing intrinsic fact',fact); facts[fact]+=1
        for fact in ('material','materialGrade'):
            if fact in p: facts[fact]+=1
    assert len(raw['processes'])==1
    definition=raw['processes'][0]['definition']; local=definition['processInput']
    assert set(definition['parameters'])==CHOICES
    assert local['parts']=={'members':{'scope':'design','itemKeys':list(items)}}
    assert set(local['geometry'])=={'layout'} and definition['dependencies']==[]
    assert definition['outputs']=={'manufacturing':{'kind':'manufacturing-set','key':'window.manufacturing'}}
    for connection in raw['connections']:
        participants=connection['items']; assert set(participants)<=set(items)
        assert len(participants)==len(set(participants)),connection['key']
        seen=set()
        for anchor in connection['properties'].get('participantAnchors',[]):
            member=anchor['itemKey']; assert member in participants,(connection['key'],'nonparticipant anchor',member)
            assert member not in seen,(connection['key'],'duplicate anchor',member); seen.add(member); anchors+=1
            p=anchor.get('centerlinePoint')
            if p is not None: assert len(p)==3 and all(math.isfinite(v) for v in p)
    d0,r0=deepcopy(design),deepcopy(raw)
    composed=compose_manufacturing_model(raw,design)
    assert d0==design and r0==raw
    assert {i['key'] for i in composed['items']}==set(items)
    return {'displayItems':len(items),'displayResources':len(resources),'sharedSectionResources':len(shared_sections),
            'connections':len(raw['connections']),'participantAnchors':anchors,'intrinsicFacts':dict(facts),
            'manufacturingResourceRoles':list(local.get('resources',{}))}


def equivalent(old,new):
    old_items={i['key']:i for i in old['items']}; new_items={i['key']:i for i in new['items']}
    material=[]; topology=[]
    for key in old_items.keys()|new_items.keys():
        if key not in old_items or key not in new_items: material.append({'item':key,'kind':'missing-item'}); continue
        a,b=old_items[key]['properties'],new_items[key]['properties']
        for field in ('length','manufacturing.material','manufacturing.materialGrade','manufacturing.categoryKey',
                      'manufacturing.categoryName','material','materialGrade'):
            left,right=a.get(field),b.get(field)
            different=(abs(left-right)>.00051 if field=='length' else left!=right)
            if different: material.append({'item':key,'field':field,'old':left,'new':right})
        sources=lambda p: sorted(s['itemKey'] for s in p['manufacturing.sourceMembers'])
        if sources(a)!=sources(b): topology.append({'item':key,'old':sources(a),'new':sources(b)})
    old_src=old['extensions'].get('tubeDesigner.assemblyProcessSource'); new_src=new['extensions']['tubeDesigner.assemblyProcessSource']
    if old_src is None:
        after=machining(new_src)
        effective={kind:sum(after[kind].values()) for kind in ('apertures','ends','folds','posts')}
        planning=[]
        if any(effective.values()):
            planning.append({'kind':'independent-node-selection-lost','oldAssemblyPlanningMode':'external_templates',
                'oldOuterFrameConnection':old['extensions']['tubeDesigner.securityWindowReview']['outerFrameConnection'],
                'oldEffectiveCalls':0,'newEffectiveCalls':effective})
        return {'differences':{'material':material,'sourceTopology':topology,'planningMode':planning,
                              'sourceIntervals':native_source_intervals(new)},
                'oldItems':len(old_items),'newItems':len(new_items),'oldCalls':{},
                'newCalls':dict(Counter(c['templateId'] for c in new_src['instances']))}
    a_stocks={s['id']:s for s in old_src['stocks']}; b_stocks={s['id']:s for s in new_src['stocks']}; stocks=[]; unused=[]
    for key in a_stocks.keys()|b_stocks.keys():
        if key not in a_stocks:
            # The old source collected stocks only while emitting operations.
            # Extra records for unchanged, unprocessed output pipes are inert.
            if key in old_items and abs(old_items[key]['properties']['length']-b_stocks[key]['length'])<=.00051:
                unused.append(key);continue
            stocks.append({'item':key,'kind':'additional-stock-with-different-material'});continue
        if key not in b_stocks: stocks.append({'item':key,'kind':'missing-required-stock'});continue
        a,b=a_stocks[key],b_stocks[key]
        if abs(a['length']-b['length'])>1.e-6 or any(abs(x-y)>1.e-6 for x,y in zip(a['matrix'],b['matrix'])):
            stocks.append({'item':key,'old':{'length':a['length'],'matrix':a['matrix']},
                           'new':{'length':b['length'],'matrix':b['matrix']}})
    before,after=machining(old_src),machining(new_src)
    result={'material':material,'sourceTopology':topology,'stocks':stocks,'sourceIntervals':native_source_intervals(new)}
    for kind in ('apertures','ends','folds','posts'): result[kind]=differences(before[kind],after[kind])
    return {'differences':result,'oldItems':len(old_items),'newItems':len(new_items),'additionalUnprocessedStockRecords':unused,
            'oldCalls':dict(Counter(c['templateId'] for c in old_src['instances'])),
            'newCalls':dict(Counter(c['templateId'] for c in new_src['instances']))}


def contract_compatibility():
    values={**DEFAULTS,**COARSE,'accessDoorEnabled':False}
    saved=deepcopy(values)
    design=CORE.display(values); raw=CORE.manufacturing(values)
    assert set(raw['processes'][0]['definition']['parameters'])==CHOICES
    current=execute(raw,design)
    historical=deepcopy(raw); historical['processes'][0]['definition']['parameters'].pop('assemblyPlanningMode')
    frozen=deepcopy(historical)
    historical_result=execute(historical,design)
    diff=equivalent(current,historical_result)['differences']
    assert all(not value if isinstance(value,list) else not value['missing'] and not value['extra'] for value in diff.values()),diff
    assert historical==frozen and values==saved
    malformed=[]
    for name,mutate in (
        ('different-eleven',lambda choices:choices.pop('frameJoinType')),
        ('unknown-thirteen',lambda choices:choices.update(unknownLocalChoice=True)),
        ('missing-two',lambda choices:(choices.pop('assemblyPlanningMode'),choices.pop('frameJoinType')))):
        altered=deepcopy(raw);mutate(altered['processes'][0]['definition']['parameters']); frozen=deepcopy(altered)
        try: execute(altered,design)
        except ValueError as error:
            assert 'explicit local choices' in str(error),str(error)
            malformed.append({'name':name,'rejected':True,'error':str(error),'inputsUnchanged':altered==frozen})
        else: raise AssertionError(name+' unexpectedly accepted')
    return {'currentChoiceCount':len(CHOICES),'exactHistoricalElevenAccepted':True,
        'historicalActualResultMatchesCurrent':True,'inputsUnchanged':values==saved,'malformedCases':malformed}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--match',default='');parser.add_argument('--limit',type=int)
    parser.add_argument('--names',default='');parser.add_argument('--suffix',default='')
    parser.add_argument('--contract-only',action='store_true')
    args=parser.parse_args(); matrix=[c for c in cases() if args.match in c['name']]
    snapshot=source_snapshot()
    if args.contract_only:
        report={**contract_compatibility(),'sourceHashes':snapshot,'sourceOnly':True,'nativeVerified':False}
        dest=OUTPUT/'all-products-recheck-window-local-choice-compatibility.json'
        OUTPUT.mkdir(parents=True,exist_ok=True)
        dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print('REPORT',dest,'PASS',flush=True)
        return 0
    if args.names: matrix=[c for c in matrix if c['name'] in args.names.split(',')]
    if args.limit: matrix=matrix[:args.limit]
    results=[]; stable={}; started=time.time()
    for case in matrix:
        begin=time.time(); record=deepcopy(case); values={**DEFAULTS,**case['changes']}; saved=deepcopy(values)
        stage='descriptor choices'
        try:
            for key,options in ENUMS.items(): assert values[key] in options,(key,values[key],options)
            stage='public declarations'
            with forbid_product_processing():
                design=CORE.display(values)
                with forbid_display_callback(): raw=CORE.manufacturing(values)
            assert values==saved,'input parameters mutated'
            stage='shared facts and actual anchor invariants'; record['declarations']=check_declarations(design,raw)
            if case['stableGroup']:
                state={'design':design,'connections':raw['connections'],'parts':raw['processes'][0]['definition']['processInput']['parts'],
                       'geometry':raw['processes'][0]['definition']['processInput']['geometry']}
                if case['stableGroup'].startswith('external-'):
                    # Choosing pending L/T nodes changes the assembly intent,
                    # while the entire shared display and member input stay fixed.
                    state.pop('connections')
                if case['stableGroup'] in stable: assert state==stable[case['stableGroup']],'route/joint/inactive draft changed design or identity'
                else: stable[case['stableGroup']]=deepcopy(state)
            stage='independent downstream execution'; new=execute(raw,design)
            assert raw['schemaVersion']==4 and values==saved
            stage='historical actual material/process reference'
            reference_values={**saved,**case['referenceChanges']}
            old=CORE.generate(reference_values,{'template':DESCRIPTOR,'geometryPurpose':'manufacturing'})
            assert old['parameters']==reference_values and values==saved,'historical output/input parameters drift'
            if raw['processes'][0]['definition']['parameters']['assemblyPlanningMode']=='external_templates':
                assert raw['connections']==old['relationships'],'pending L/T anchors/contact/allowance lost'
                assert all(item['properties']['tubeDesigner.assemblyPlanning']=={'stockState':'uncut','ready':False}
                           for item in new['items']),'pending stocks marked ready'
                record['pendingConnectionsMatchOriginal']=True
                record['pendingStocksUncutAndNotReady']=True
            stage='actual material/stock/effective process comparison'; record.update(equivalent(old,new))
            mismatch=[]
            for kind,diff in record['differences'].items():
                count=len(diff) if isinstance(diff,list) else len(diff['missing'])+len(diff['extra'])
                if count: mismatch.append({'kind':kind,'count':count})
            record['mismatches']=mismatch;record['passed']=not mismatch
        except Exception as error:
            record.update(passed=False,stage=stage,error=type(error).__name__+': '+str(error))
            try:
                old=CORE.generate(saved,{'template':DESCRIPTOR,'geometryPurpose':'manufacturing'})
                record['historicalAccepted']=True;record['historicalItems']=len(old['items'])
            except Exception as reference_error:
                record['historicalAccepted']=False
                record['historicalError']=type(reference_error).__name__+': '+str(reference_error)
                if case['expectedRejection'] and stage=='independent downstream execution':
                    record['passed']=True
                    record['status']='actual-process-inapplicability-preserved'
        record['elapsedSeconds']=round(time.time()-begin,3);results.append(record)
        report={'templateId':'single_face_security_window','descriptorChoices':ENUMS,'newMatrixCases':len(matrix),
            'sourceOnly':True,'nativeVerified':False,'productionModified':True,
            'sourceHashes':snapshot,'sourceHashesUnchangedDuringRun':snapshot==source_snapshot(),
            'caseResults':results,'passed':sum(c['passed'] for c in results),'failed':sum(not c['passed'] for c in results),
            'elapsedSeconds':round(time.time()-started,3)}
        OUTPUT.mkdir(parents=True,exist_ok=True)
        dest=REPORT if not args.match else OUTPUT/('all-products-recheck-window-'+args.match.replace(':','-')+'.json')
        if args.suffix: dest=OUTPUT/('all-products-recheck-window-'+args.suffix+'.json')
        dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(record['name'], 'PASS' if record['passed'] else 'FAIL',
              record.get('mismatches') or record.get('error') or record.get('declarations'),flush=True)
    print('REPORT',dest,'PASS',report['passed'],'FAIL',report['failed'],flush=True)
    return 0 if all(c['passed'] for c in results) else 1


if __name__=='__main__': sys.exit(main())
