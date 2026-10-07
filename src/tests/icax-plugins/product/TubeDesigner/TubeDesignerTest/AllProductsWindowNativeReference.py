"""Same-input old physical reference for window native branch acceptance."""
from copy import deepcopy
import json
from pathlib import Path
import sys
from AllProductsWindowRecheck import (CORE, COARSE, DEFAULTS, DESCRIPTOR, OUTPUT,
    cases, check_declarations, equivalent, execute, forbid_display_callback,
    forbid_product_processing, source_snapshot)


def reference(request):
    values=deepcopy(request['parameters']); frozen=deepcopy(values)
    with forbid_product_processing():
        display=CORE.display(values)
        with forbid_display_callback(): manufacturing=CORE.manufacturing(values)
    declarations=check_declarations(display,manufacturing)
    current=execute(manufacturing,display)
    old=CORE.generate(values,{'template':request['template'],'geometryPurpose':'manufacturing'})
    differences=equivalent(old,current)['differences']
    assert all(not value if isinstance(value,list) else not value['missing'] and not value['extra']
               for value in differences.values()),differences
    assert values==frozen and old['parameters']==frozen
    pending=manufacturing['processes'][0]['definition']['parameters']['assemblyPlanningMode']=='external_templates'
    if pending:
        assert manufacturing['connections']==old['relationships']
        assert all(item['properties']['tubeDesigner.assemblyPlanning']=={'stockState':'uncut','ready':False}
                   for item in current['items'])
    counterpart=None; planning_probe=None
    if values['faceType']=='single' and not values['accessDoorEnabled'] and values['frameLayout']=='four_sides' and values['infillPattern']!='grid':
        planning_probe=deepcopy(values)
        # Compare the two modes with identical applicable, valid drafts. The
        # separate original request still proves inactive negative preservation.
        for key in ('assemblyClearance','horizontalBranchReserve','verticalBranchReserve'):
            if planning_probe[key]<0: planning_probe[key]=DEFAULTS[key]
        counterpart={**planning_probe,'assemblyPlanningMode':'legacy_processed' if pending else 'external_templates'}
        assert CORE.display(planning_probe)==display
        assert CORE.display(counterpart)==display
    return {'parameters':values,'display':display,'manufacturing':manufacturing,'document':old,
        'sourceExecution':current,'planningProbeParameters':planning_probe,'planningCounterpart':counterpart,'sourceDigests':source_snapshot(),
        'sourceChecks':{'pureDeclaration':True,'independentExecutor':True,'hostInputsUnchanged':True,
            'partStockMaterialEffectiveOperationsEqual':True,'pendingLAndTConnectionsPreserved':pending,
            'pendingUncutNotReady':pending,'displayIndependentOfPlanning':counterpart is not None},
        'declarations':declarations}


if __name__=='__main__':
    if sys.argv[1]=='--prepare-scenarios':
        by_name={case['name']:case for case in cases()}
        names=('external-horizontal-segment_weld','external-vertical-plane_v_notch',
            'inactive-grid-horizontal','inactive-grid-vertical','inactive-external-two-plane_v_notch',
            'inactive-external-three-spatial_v_notch','inactive-external-five-plane_v_notch',
            'open-left_right-plane_v_notch','open-top_bottom-spatial_v_notch',
            'five-door-bottom-segment_weld','nonsquare-door-left-plane_v_notch',
            'shifted-asymmetric-datum-segment_weld','sharp-rect-zero-radius-segment_weld')
        data=[{'name':name,'parameters':{**DEFAULTS,**by_name[name]['changes']}} for name in names]
        Path(sys.argv[2]).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    else:
        request_path,output_path=map(Path,sys.argv[1:])
        output_path.write_text(json.dumps(reference(json.loads(request_path.read_text(encoding='utf-8'))),
                                          ensure_ascii=False,separators=(',',':')),encoding='utf-8')
        print('reference accepted',output_path)
