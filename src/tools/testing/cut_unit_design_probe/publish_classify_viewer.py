"""Publish the inspection UI beside the generated per-file data. No source CAD changes."""
import hashlib,shutil
from pathlib import Path
from run_classify_units import DEST, ROOT

HERE=Path(__file__).resolve().parent
for source,target in [('classify_viewer.html','index.html'),('classify_viewer.css','viewer.css'),('classify_viewer.mjs','viewer.js')]:
    shutil.copy2(HERE/source,DEST/target)
# A resumed browser may still cache the old index even after a model refresh.
entry=(DEST/'index.html').read_text(encoding='utf-8')
for name in ['index-data.js','viewer.js','viewer.css']:
    revision=hashlib.sha256((DEST/name).read_bytes()).hexdigest()[:12]
    entry=entry.replace('"'+name+'"','"'+name+'?v='+revision+'"')
(DEST/'index.html').write_text(entry,encoding='utf-8')
(DEST/'three').mkdir(exist_ok=True)
for name in ['three.module.js','three.core.js','LICENSE']:
    shutil.copy2(ROOT/'src/iCAX-UI/SDK/ThirdParty/three'/name,DEST/'three'/name)
print(DEST/'index.html')
