import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {renderOverlay,renderRight} from '../../apps/five-axis-cam/webpage/workspaceViews.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

function fixture(){
  const view={fiveAxisArea:'toolpath',fiveGenerationOpen:true,fiveGeneration:{running:true},fiveRapidBatch:{running:true},pending:true,fiveDialogPositions:{generate:{left:12,top:17}},scene:{model:{isLoaded:true},machines:[],intentToolpaths:[],domain:{activeSetupId:'setup',placements:[],workpieces:[],setups:[{id:'setup',generatedGeometry:{status:'Current',operations:[{},{}],transitions:[{}]}}]}}};
  selectTool(view,'generate');return {view,context:{activeRibbonTabId:'toolpath'}};
}

test('generation uses the shared centered modal and ignores floating-editor position/drag state',()=>{
  const {view,context}=fixture(),html=renderOverlay(context,view);
  assert.match(html,/<div class="cam-progress-backdrop" data-five-key="generation-modal">/);
  assert.match(html,/class="five-position-dialog five-centered-operation-dialog"/);assert.match(html,/role="dialog" aria-modal="true" aria-label="生成空移"/);
  assert.doesNotMatch(html,/data-five-floating="generate"|data-five-dialog-drag|left:12px/);assert.match(html,/five.generate.cancel/);assert.doesNotMatch(renderRight(context,view),/generation-dialog/);
});

test('the actual browser keeps progress centered through resize, nested scroll and attempted title drag',async()=>{
  const require=createRequire(import.meta.url),{chromium}=require(process.env.ICAX_PLAYWRIGHT_MODULE||'C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
  const browser=await chromium.launch({headless:true,channel:process.env.ICAX_BROWSER_CHANNEL||'msedge'});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:900}}),{view,context}=fixture();
    const styles=['../../apps/_shared/workbench/styles/workbench.css','../../apps/five-axis-cam/webpage/workspace.css'].map(p=>readFileSync(new URL(p,import.meta.url),'utf8')).join('\n');
    // The deliberately unequal side panes and offset viewport catch accidental
    // centering in the canvas instead of the application client window.
    await page.setContent(`<style>${styles}html,body{margin:0;width:100%;height:100%;overflow:auto}.cam-workbench.five-workspace{position:relative;width:100%;height:100%;display:block}.cam-context-pane{position:absolute;left:0;top:100px;width:180px;height:400px;overflow:auto}.cam-info-pane{position:absolute;right:0;top:100px;width:340px;height:400px;overflow:auto}.cam-viewport{position:absolute!important;left:180px;right:340px;top:190px;height:450px}.scroll-content{height:2400px}</style><div class="cam-workbench five-workspace"><aside class="cam-context-pane"><div class="scroll-content"></div></aside><div class="cam-viewport"><div class="five-overlay">${renderOverlay(context,view)}</div></div><aside class="cam-info-pane"><div class="scroll-content"></div></aside></div>`);
    const geometry=()=>page.evaluate(()=>{const node=document.querySelector('[data-five-key="generation-dialog"]'),r=node.getBoundingClientRect(),footer=node.querySelector('footer').getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height,dx:r.x+r.width/2-innerWidth/2,dy:r.y+r.height/2-innerHeight/2,footerInside:footer.top>=r.top&&footer.bottom<=r.bottom+1,drag:node.querySelector('[data-five-dialog-drag]')!==null};});
    for(const size of [{width:1440,height:900},{width:1024,height:720},{width:560,height:380}]){
      await page.setViewportSize(size);const r=await geometry();assert.ok(Math.abs(r.dx)<1&&Math.abs(r.dy)<1,JSON.stringify({size,...r}));assert.ok(r.x>=23&&r.y>=23);assert.equal(r.footerInside,true);assert.equal(r.drag,false);assert.equal(await page.locator('[data-five-command="five.generate.cancel"]').isVisible(),true);
    }
    await page.setViewportSize({width:1440,height:900});const before=await geometry();await page.evaluate(()=>{document.querySelector('.cam-context-pane').scrollTop=150;document.querySelector('.cam-info-pane').scrollTop=260;document.querySelector('.cam-viewport').scrollTop=100;});assert.deepEqual(await geometry(),before);
    const title=await page.locator('[data-five-key="generation-dialog"] header').boundingBox();await page.mouse.move(title.x+20,title.y+15);await page.mouse.down();await page.mouse.move(title.x+160,title.y+75,{steps:6});await page.mouse.up();assert.deepEqual(await geometry(),before);
    // Long errors scroll inside the dialog while Cancel stays in the footer.
    await page.evaluate(()=>{const body=document.querySelector('.five-generation-body');for(let i=0;i<40;i++){const p=document.createElement('p');p.textContent='Error detail '+i;body.append(p);}});await page.setViewportSize({width:560,height:380});const constrained=await geometry();assert.ok(constrained.height<=332);assert.equal(constrained.footerInside,true);assert.ok(Math.abs(constrained.dx)<1&&Math.abs(constrained.dy)<1);assert.equal(await page.locator('.five-generation-body').evaluate(n=>n.scrollHeight>n.clientHeight),true);assert.equal(await page.locator('[data-five-command="five.generate.cancel"]').isVisible(),true);
  }finally{await browser.close();}
});
