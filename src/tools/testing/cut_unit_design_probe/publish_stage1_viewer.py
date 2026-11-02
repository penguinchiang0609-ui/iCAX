"""Publish the stage-one inspector next to its own immutable geometry outputs."""
import hashlib, shutil
from run_stage1_units import DEST, ROOT, HERE

for source,target in [('stage1_viewer.html','index.html'),('stage1_viewer.css','viewer.css'),('stage1_viewer.mjs','viewer.js'),('stage1_edge_evidence.mjs','edge-evidence.js')]:
    shutil.copy2(HERE/source,DEST/target)
viewer=(DEST/'viewer.js').read_text('utf-8')
viewer=viewer.replace("'./edge-evidence.js'", "'./edge-evidence.js?v="+hashlib.sha256((DEST/'edge-evidence.js').read_bytes()).hexdigest()[:12]+"'")
(DEST/'viewer.js').write_text(viewer,encoding='utf-8')
entry=(DEST/'index.html').read_text('utf-8')
for name in ['viewer.js','viewer.css']:
    entry=entry.replace('"'+name+'"','"'+name+'?v='+hashlib.sha256((DEST/name).read_bytes()).hexdigest()[:12]+'"')
(DEST/'index.html').write_text(entry,encoding='utf-8')
(DEST/'three').mkdir(exist_ok=True)
for name in ['three.module.js','three.core.js','LICENSE']:
    shutil.copy2(ROOT/'src/iCAX-UI/SDK/ThirdParty/three'/name,DEST/'three'/name)
(DEST/'three-lines').mkdir(exist_ok=True)
for source in (HERE/'vendor/three-lines').glob('*.js'):
    shutil.copy2(source,DEST/'three-lines'/source.name)
print(DEST/'index.html')
