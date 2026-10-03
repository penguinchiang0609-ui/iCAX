"""Compare material and effective machining topology with the legacy window path."""
from collections import Counter
import importlib.util
import itertools
import json
import math
from pathlib import Path
import sys
import unittest

from WindowCatalogueTests import package, SRC
from WindowManufacturingInputTests import execute, load, forbid_product_callbacks

ROOT=SRC/'apps/tube-designer/templates/_shared'
spec=importlib.util.spec_from_file_location('window_legacy_topology_allocator',ROOT/'assembly_material_allocation.py')
allocator=importlib.util.module_from_spec(spec); spec.loader.exec_module(allocator)


def normalized(value):
    if isinstance(value,dict): return {key:normalized(child) for key,child in value.items()}
    if isinstance(value,list): return [normalized(child) for child in value]
    if isinstance(value,(float,int)) and not isinstance(value,bool): return round(float(value),6) or 0.
    return value


def signature(value): return json.dumps(normalized(value),sort_keys=True,separators=(',',':'))


def differences(first,second):
    return {'missing':list((first-second).elements()),'extra':list((second-first).elements())}


def native_source_intervals(model):
    """Mirror the native material extent and per-source overlap invariants."""
    errors=[]
    for item in model['items']:
        props=item['properties']
        length=props.get('tubeDesigner.assemblyProcessStockLength',props['length'])
        for source in props['manufacturing.sourceMembers']:
            intervals=[]
            for span in source['spans']:
                size=math.dist(span['start'],span['end']); start=span['stockStart']
                a,b=span['startReserve'],span['endReserve']; end=start+a+size+b
                if (not all(math.isfinite(value) for value in (length,start,a,b,end))
                        or length<=0 or size<=1.e-8 or min(start,a,b)<0 or end>length+.001):
                    errors.append({'item':item['key'],'source':source['itemKey'],
                                   'length':length,'start':start,'end':end,'span':span})
                fraction=span['sourceRange']
                if (len(fraction)!=2 or fraction[0]==fraction[1]
                        or any(not math.isfinite(value) or not 0<=value<=1 for value in fraction)):
                    errors.append({'item':item['key'],'source':source['itemKey'],'kind':'invalid-source-fraction'})
                if any(min(previous[1],end)-max(previous[0],start)>1.e-6 for previous in intervals):
                    errors.append({'item':item['key'],'source':source['itemKey'],'kind':'overlapping-spans'})
                intervals.append((start,end))
    return errors


def machining(source):
    runtime=allocator._load('assembly_geometry_process_runtime')
    apertures,ends,folds,posts=Counter(),Counter(),Counter(),Counter()
    empty=0; empty_ends=0
    for call in source['instances']:
        kind=call['templateId']; local=call['processInput']; targets=call['targets']
        if kind=='tube-profile-aperture':
            result=runtime.evaluate(kind,local,call['parameters'])
            if not result['applicable']: raise AssertionError(result['reason'])
            if not result['operations']:
                empty+=1
                continue
            bounds=next(check['bounds'] for check in result['checks'] if check['kind']=='profile-aperture')
            branch=local['parts']['branch']['id'].split('.unfolded.')[0]
            apertures[signature({'stock':targets['stock'],'branch':branch,'bounds':bounds,
                                 'parameters':call['parameters']})]+=1
        elif kind=='tube-end-joint':
            result=runtime.evaluate(kind,local,call['parameters'])
            if not result['applicable']: raise AssertionError(result['reason'])
            if not result['operations']:
                empty_ends+=1
                continue
            plane=next(check for check in result['checks'] if check['kind']=='end-plane')
            # A half-space end cut is defined by its actual plane and retained
            # side. Unused drafts or equivalent insertion syntax do not alter it.
            ends[signature({'stock':targets['stock'],'mode':local['geometry']['mode'],
                            'end':local['geometry']['end'],'origin':plane['origin'],
                            'normal':plane['inwardNormal']})]+=1
        elif kind=='orthogonal-corner':
            # The reference identity is transported separately from each local
            # member's real pose. Names and private template headers are inert.
            parts={role:{key:part[key] for key in ('length','matrix','anchor')}
                   for role,part in local['parts'].items()}
            posts[signature({'targets':targets,'parts':parts,'parameters':call['parameters']})]+=1
        else:
            folds[signature({'targets':targets,'kind':kind,'geometry':local['geometry'],
                             'resources':local.get('resources',{}),'parameters':call['parameters'],
                             'processDrafts':call.get('processDrafts',{})})]+=1
    return {'apertures':apertures,'ends':ends,'folds':folds,'posts':posts,
            'emptyApertures':empty,'emptyEnds':empty_ends}


def compare(changes):
    descriptor,defaults,core=package('single_face_security_window')
    values={**defaults,'doorFrameJoinType':'v_groove_90:tool_library',
            'doorLeafFrameJoinType':'v_groove_90:tool_library',**changes}
    legacy=core.generate(values,{'template':descriptor,'geometryPurpose':'manufacturing'})
    declaration=core.manufacturing(values)
    executed=execute(declaration,core.display(values))
    old_items={item['key']:item for item in legacy['items']}
    new_items={item['key']:item for item in executed['items']}
    material=[]; topology=[]
    for key in old_items.keys()|new_items.keys():
        if key not in old_items or key not in new_items:
            material.append({'item':key,'kind':'missing-item'})
            continue
        a,b=old_items[key]['properties'],new_items[key]['properties']
        for field in ('length','manufacturing.material','manufacturing.materialGrade',
                      'manufacturing.categoryKey','manufacturing.categoryName','material','materialGrade'):
            left,right=a.get(field),b.get(field)
            different=(abs(left-right)>.00051 if field=='length' else left!=right)
            if different: material.append({'item':key,'field':field,'old':left,'new':right})
        sources=lambda props: sorted(member['itemKey'] for member in props['manufacturing.sourceMembers'])
        if sources(a)!=sources(b): topology.append({'item':key,'old':sources(a),'new':sources(b)})
    old_source=legacy['extensions']['tubeDesigner.assemblyProcessSource']
    new_source=executed['extensions']['tubeDesigner.assemblyProcessSource']
    old_stocks={stock['id']:stock for stock in old_source['stocks']}
    new_stocks={stock['id']:stock for stock in new_source['stocks']}
    stocks=[]
    for key in old_stocks.keys()|new_stocks.keys():
        if key not in old_stocks or key not in new_stocks:
            stocks.append({'item':key,'kind':'missing-stock'}); continue
        a,b=old_stocks[key],new_stocks[key]
        if abs(a['length']-b['length'])>1.e-6 or any(abs(x-y)>1.e-6 for x,y in zip(a['matrix'],b['matrix'])):
            stocks.append({'item':key,'old':{'length':a['length'],'matrix':a['matrix']},
                           'new':{'length':b['length'],'matrix':b['matrix']}})
    before,after=machining(old_source),machining(new_source)
    return {'scenario':changes,'items':len(new_items),'materialDifferences':material,
            'sourceTopologyDifferences':topology,'stockDifferences':stocks,
            'sourceIntervalDifferences':native_source_intervals(executed),
            'oldCalls':dict(Counter(call['templateId'] for call in old_source['instances'])),
            'newCalls':dict(Counter(call['templateId'] for call in new_source['instances'])),
            'oldEmptyApertures':before['emptyApertures'],'newEmptyApertures':after['emptyApertures'],
            'oldEmptyEnds':before['emptyEnds'],'newEmptyEnds':after['emptyEnds'],
            **{kind+'Differences':differences(before[kind],after[kind]) for kind in ('apertures','ends','folds','posts')}}


class WindowManufacturingLegacyTopologyTests(unittest.TestCase):
    def equivalent(self,changes):
        report=compare(changes)
        for name in ('materialDifferences','sourceTopologyDifferences','stockDifferences','sourceIntervalDifferences'):
            self.assertEqual(report[name],[],(name,report[name]))
        for name in ('aperturesDifferences','endsDifferences','foldsDifferences','postsDifferences'):
            self.assertEqual(report[name],{'missing':[],'extra':[]},(name,report[name]))
        return report

    def test_all_faces_routes_and_openings_preserve_effective_legacy_topology(self):
        reports=[]
        for face,mode,opening in itertools.product(('single','two','three','five'),
                ('segment_weld','plane_v_notch','spatial_v_notch'),(False,True)):
            changes={'faceType':face,'frameManufacturingMode':mode,'accessDoorEnabled':opening}
            with self.subTest(changes=changes): reports.append(self.equivalent(changes))
        output=SRC.parent/'output/tests/assembly-process/window-manufacturing-legacy-topology-v4-probe.json'
        output.write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')

    def test_initial_material_boundaries_preserve_design_sources_without_recuts(self):
        descriptor,defaults,core=package('single_face_security_window')
        runtime=allocator._load('assembly_geometry_process_runtime')
        allocation_reports=[]
        for face,mode,count in (('two','segment_weld',6),('three','plane_v_notch',8),('five','segment_weld',8)):
            with self.subTest(face=face,mode=mode):
                values={**defaults,'faceType':face,'frameManufacturingMode':mode,'accessDoorEnabled':False}
                raw=core.manufacturing(values)
                host_design=core.display(values)
                with forbid_product_callbacks():
                    from icax_template_sdk.manufacturing import compose_manufacturing_model
                    compiled=load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(raw,host_design))
                    allocated=allocator.allocate_window_manufacturing(compiled,{})
                boundary=[call for call in compiled['processes']
                          if call['definition']['processInput']['geometry'].get('allocation')=='initial-material-boundary']
                self.assertEqual(len(boundary),count)
                frozen={call['instanceId']:call for call in allocated['assemblySource']['instances']}
                raw_items={item['key']:item for item in compiled['items']}
                new_items={item['key']:item for item in allocated['model']['items']}
                stocks={stock['id']:stock for stock in allocated['assemblySource']['stocks']}
                nominal_evidence=allocated['initialMaterialAllocations']
                for call in boundary:
                    definition=call['definition']; local=definition['processInput']
                    self.assertEqual(definition['parameters'],{})
                    self.assertNotIn('station',local['geometry'])
                    key=local['parts']['stock']['itemKey']
                    if local['geometry'].get('extentPolicy')=='nominal-member-section':
                        self.assertNotIn(call['key'],frozen,'factory stock preparation must not masquerade as an end cut')
                        evidence=[entry for entry in nominal_evidence if entry['stockId']==key
                                  and entry['sourceItemKey']==local['parts']['stock']['data']['sourceMember']
                                  and entry['end']==local['geometry']['anchor']['end']]
                        self.assertEqual(len(evidence),1)
                        self.assertEqual(evidence[0]['policy'],'nominal-member-section')
                        stock=stocks[key]
                        self.assertAlmostEqual(stock['matrix'][11],0.)
                        self.assertAlmostEqual(stock['length'],values['height'])
                        self.assertAlmostEqual(evidence[0]['point'][2],
                                               0. if evidence[0]['end']=='start' else values['height'])
                        self.assertFalse(any(executed['templateId']=='tube-end-joint'
                            and executed['processInput']['parts']['stock']['id']==key
                            for executed in frozen.values()),'full-height post must remain free of recuts')
                    else:
                        executed=frozen[call['key']]
                        result=runtime.evaluate(executed['templateId'],executed['processInput'],executed['parameters'])
                        self.assertTrue(result['applicable'],result['reason'])
                        self.assertEqual(result['operations'],[])
                    if key.startswith('outer_frame.vertical.'):
                        item=raw_items[key]; path=item['manufacturingInput']['path']; vertices={vertex['key']:vertex['point'] for vertex in path['vertices']}; segment=path['segments'][0]
                        design={'start':vertices[segment['from']], 'end':vertices[segment['to']]}
                        span=new_items[key]['properties']['manufacturing.sourceMembers'][0]['spans'][0]
                        self.assertEqual(span['start'],design['start'])
                        self.assertEqual(span['end'],design['end'])
                        self.assertEqual(span['sourceRange'],[0.,1.])
                allocation_reports.append({'faceType':face,'frameManufacturingMode':mode,
                    'initialMaterialAllocations':nominal_evidence,
                    'stocks':{entry['stockId']:stocks[entry['stockId']] for entry in nominal_evidence},
                    'consumedFactoryRequests':[call['key'] for call in boundary
                        if call['definition']['processInput']['geometry'].get('extentPolicy')=='nominal-member-section'],
                    'ordinaryBoundaryRequests':[call['key'] for call in boundary
                        if 'extentPolicy' not in call['definition']['processInput']['geometry']],
                    'factoryRequestsAbsentFromActualOperations':True,'designSourcesFullyCovered':True})
        output=SRC.parent/'output/tests/assembly-process/all-products-recheck-window-initial-allocation-evidence.json'
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(allocation_reports,ensure_ascii=False,indent=2),encoding='utf-8')


if __name__=='__main__':
    if '--probe' in sys.argv:
        reports=[]
        for face,mode,opening in itertools.product(('single','two','three','five'),
                ('segment_weld','plane_v_notch','spatial_v_notch'),(False,True)):
            changes={'faceType':face,'frameManufacturingMode':mode,'accessDoorEnabled':opening}
            try: report=compare(changes)
            except Exception as error: report={'scenario':changes,'error':str(error)}
            reports.append(report)
            print(face,mode,opening,'error' if 'error' in report else {key:len(value) if isinstance(value,list)
                else len(value['missing'])+len(value['extra']) for key,value in report.items() if key.endswith('Differences')},flush=True)
        output=SRC.parent/'output/tests/assembly-process/window-manufacturing-legacy-topology-v4-probe.json'
        output.write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
        print('report',output)
        if any('error' in report or any(value if isinstance(value,list) else value['missing'] or value['extra']
                for key,value in report.items() if key.endswith('Differences')) for report in reports):
            sys.exit(1)
    else:
        unittest.main()
