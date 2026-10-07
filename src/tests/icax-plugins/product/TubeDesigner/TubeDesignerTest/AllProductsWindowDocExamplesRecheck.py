"""Read-only verification of checked-in v4 example inputs and public outputs."""
from copy import deepcopy
from pathlib import Path
import hashlib
import json

from AllProductsWindowRecheck import CORE, SRC, OUTPUT, DESCRIPTOR, check_declarations, forbid_display_callback, equivalent
from WindowManufacturingInputTests import execute, forbid_product_processing, forbid_product_callbacks


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


folder=SRC.parent/'doc/examples/product-manufacturing'
paths=sorted(folder.glob('v4-*.json'))
before={str(path.relative_to(SRC.parent)):digest(path) for path in paths}
rows=[]
for input_path in sorted(folder.glob('v4-*.host-input.json')):
    host=json.loads(input_path.read_text(encoding='utf-8'))
    values=deepcopy(host['parameters']); original=deepcopy(values)
    prefix=input_path.name.removesuffix('.host-input.json')
    display_path=folder/(prefix+'.display.actual.json')
    raw_path=folder/(prefix+'.actual.json')
    with forbid_product_processing():
        display=CORE.display(values)
        with forbid_display_callback(): raw=CORE.manufacturing(values)
    assert values==original
    assert display==json.loads(display_path.read_text(encoding='utf-8')),display_path.name
    assert raw==json.loads(raw_path.read_text(encoding='utf-8')),raw_path.name
    declarations=check_declarations(display,raw)
    d0,r0=deepcopy(display),deepcopy(raw)
    with forbid_product_callbacks(): actual=execute(raw,display)
    assert display==d0 and raw==r0 and values==original
    row={'name':prefix,'hostInput':input_path.name,'displayFile':display_path.name,
                 'manufacturingFile':raw_path.name,'publicDocumentsDeepEqual':True,
                 'hostParametersUnchanged':True,'manufacturingIndependentOfProductCallbacks':True,
                 **declarations,'displayAnnotations':len(display['annotations']),
                 'wholeProcesses':len(raw['processes']),
                 'localProcessParameters':len(raw['processes'][0]['definition']['parameters']),
                 'actualManufacturingItems':len(actual['items']),
                 'actualOuterFrameItems':sum(item['key'].startswith('outer_frame.') for item in actual['items']),
                 'passed':True}
    if values['faceType']=='three':
        routes=[]
        for route in ('plane_v_notch','spatial_v_notch'):
            routed=deepcopy(raw)
            routed['processes'][0]['definition']['parameters']['frameManufacturingMode']=route
            with forbid_product_callbacks(): result=execute(routed,display)
            legacy=CORE.generate({**values,'frameManufacturingMode':route},{'template':DESCRIPTOR,'geometryPurpose':'manufacturing'})
            compared=equivalent(legacy,result)
            assert all(not difference if isinstance(difference,list) else
                       not difference['missing'] and not difference['extra']
                       for difference in compared['differences'].values()),compared
            routes.append({'route':route,'sameDisplayDocument':True,'oldActualResultEqual':True,
                           'manufacturingItems':len(result['items']),
                           'outerFrameItems':sum(item['key'].startswith('outer_frame.') for item in result['items'])})
        assert [(route['manufacturingItems'],route['outerFrameItems']) for route in routes]==[(25,6),(22,3)]
        row['sameDisplayDownstreamRoutes']=routes
    rows.append(row)
after={str(path.relative_to(SRC.parent)):digest(path) for path in paths}
assert before==after
report={'schema':'icax.all-products-recheck-window-doc-examples','readOnly':True,
        'filesChecked':len(paths),'sourceFileHashes':before,'checkedInFilesUnchanged':True,
        'examples':rows,'passed':len(rows),'failed':0,'nativeVerified':False}
out=OUTPUT/'all-products-recheck-public-examples-final.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=True))
