import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { renderProfileParameterDiagram } from '../../apps/tube-designer/webpage/profileParameterDiagram.mjs';
import { renderProfileSvg } from '../../apps/tube-designer/webpage/profileSvg.mjs';
import { tubeDesignerCss } from '../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs';

const profiles=JSON.parse(execFileSync(process.env.ICAX_PYTHON || 'python',['-c',String.raw`
import importlib.util,json,sys
sys.dont_write_bytecode=True
s=importlib.util.spec_from_file_location('diagrams','src/apps/tube-designer/templates/_shared/profile_package_runtime.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
packages=m.generate({'action':'list-system'}, {})['systemProfiles']
assert len(packages)==24
for p in packages:
    assert p['previewProfile']['contours'],p['name']
    assert p['previewProfile']['parameterDiagram']['annotations'],p['name']
print(json.dumps([p['previewProfile'] for p in packages]))
`],{encoding:'utf8',maxBuffer:8*1024*1024}));
const { chromium }=await import(process.env.ICAX_PLAYWRIGHT_MODULE || 'playwright');
const browser=await chromium.launch({headless:true,channel:'msedge'});
try {
  const page=await browser.newPage({viewport:{width:1320,height:1000}});
  await page.setContent(`<style>${tubeDesignerCss}body{background:#fff;font:14px sans-serif;margin:20px}main{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}article{border:1px solid #aaa;min-width:0;padding:8px}.thumb{width:70px;height:70px}.thumb svg{width:100%;height:100%}.td-profile-parameter-legend{display:none}</style><main>${profiles.map(p=>`<article><h3>${p.name}</h3><div class="thumb">${renderProfileSvg(p)}</div>${renderProfileParameterDiagram(p)}</article>`).join('')}</main>`);
  assert.equal(await page.locator('.td-profile-parameter-svg').count(),24);
  const invalid=await page.locator('svg').evaluateAll(nodes=>nodes.filter(n=>/NaN|Infinity|undefined/.test(n.outerHTML)).length);
  assert.equal(invalid,0);
  mkdirSync('artifacts/profile-diagrams',{recursive:true});
  await page.screenshot({path:'artifacts/profile-diagrams/catalog.png',fullPage:true});
  for(const p of profiles){
    await page.setViewportSize({width:380,height:700});
    await page.setContent(`<style>${tubeDesignerCss}body{margin:0}</style>${renderProfileParameterDiagram(p)}`);
    const clipped=await page.locator('.td-profile-parameter-svg').evaluate(svg=>{
      const b=svg.viewBox.baseVal;
      return [...svg.querySelectorAll('text')].some(t=>{const r=t.getBoundingClientRect(),s=svg.getBoundingClientRect();return r.left<s.left-1||r.right>s.right+1||r.top<s.top-1||r.bottom>s.bottom+1;});
    });
    assert.equal(clipped,false,p.name+' annotation clipped');
  }
  console.log('24 actual catalog thumbnails/diagrams verified, including narrow-panel label bounds.');
} finally {await browser.close();}
