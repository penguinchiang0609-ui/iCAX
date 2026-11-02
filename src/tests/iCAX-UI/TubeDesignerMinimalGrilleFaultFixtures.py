"""Install/remove two owned, hidden negative-test packages in an isolated runtime."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

parser=argparse.ArgumentParser()
parser.add_argument('action',choices=('install','remove'))
parser.add_argument('runtime',type=Path)
parser.add_argument('evidence',type=Path)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[3]
source=repo/'src/apps/tube-designer/templates/product/minimal_protective_grille'
root=(args.runtime/'apps/tube-designer/templates/product').resolve()
args.evidence.mkdir(parents=True,exist_ok=True)
marker='.icax-grille-fault-fixture.json'
variants={
 'icax-test-grille-missing-association': 'missing',
 'icax-test-grille-wrong-target': 'wrong-target',
}

wrapper='''

# Acceptance-only fault injection. Both participants already have real executed
# machining. The added fault must therefore be detected per connection/process.
def manufacturing(parameter_values):
    model = _load(ROOT / 'manufacturing_rules.py').manufacturing(parameter_values, layout(parameter_values))
    connection = next(c for c in model['connections'] if c['kind'] == 'assembly')
    assert connection['properties']['manufacturingProcesses']
    if FAULT_KIND == 'missing':
        fault = deepcopy(connection)
        fault['key'] += '.fault_without_process'
        fault['properties'].pop('manufacturingProcesses')
        model['connections'].append(fault)
    else:
        host_key = connection['items'][0]
        other = next(p for p in model['processes']
                     if p['definition']['templateId'] == 'structural-apertures'
                     and p['definition']['targets']['stock'] != host_key)
        connection['properties']['manufacturingProcesses'][host_key] = [other['key']]
    return model
'''

def checked_target(template_id):
    target=root/template_id.replace('-','_')
    resolved=target.resolve()
    assert resolved.parent == root and resolved.name.startswith('icax_test_grille_')
    for item in [root,*target.parents]:
        assert not item.is_symlink(),f'Fixture path cannot follow links: {item}'
    return target

records=[]
for template_id,kind in variants.items():
    target=checked_target(template_id)
    if args.action == 'remove':
        if target.exists():
            assert not target.is_symlink()
            owned=json.loads((target/marker).read_text(encoding='utf-8'))
            assert owned['owner']=='TubeDesignerMinimalGrilleFaultFixtures' and owned['id']==template_id
            assert not any(p.is_symlink() for p in target.rglob('*'))
            shutil.rmtree(target)
        records.append({'id':template_id,'path':str(target),'removed':not target.exists()})
        continue
    assert not target.exists(),f'Remove the existing owned fixture before reinstalling: {target}'
    shutil.copytree(source,target,ignore=shutil.ignore_patterns('__pycache__','*.pyc','*.pyo'))
    descriptor=json.loads((target/'template.json').read_text(encoding='utf-8'))
    descriptor['id']=template_id
    descriptor['displayName']={'zh-CN':'验收故障注入 '+kind}
    descriptor['extensions']['catalog']['listed']=False
    (target/'template.json').write_text(json.dumps(descriptor,ensure_ascii=False,indent=2),encoding='utf-8')
    script=(target/'template.py').read_text(encoding='utf-8').replace("TEMPLATE_ID = 'minimal-protective-grille'",f'TEMPLATE_ID = {template_id!r}')
    assert f'TEMPLATE_ID = {template_id!r}' in script
    (target/'template.py').write_text(script+f'\nFAULT_KIND = {kind!r}\n'+wrapper,encoding='utf-8')
    ownership={'owner':'TubeDesignerMinimalGrilleFaultFixtures','id':template_id,'kind':kind,'source':str(source)}
    (target/marker).write_text(json.dumps(ownership,ensure_ascii=False,indent=2),encoding='utf-8')
    records.append({**ownership,'path':str(target),'sha256':{str(p.relative_to(target)):hashlib.sha256(p.read_bytes()).hexdigest() for p in target.rglob('*') if p.is_file()}})
(args.evidence/f'fault-fixtures-{args.action}.json').write_text(json.dumps({'formalTemplatesModified':False,'records':records},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'action':args.action,'fixtures':len(records),'formalTemplatesModified':False}))
