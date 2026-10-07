import assert from 'node:assert/strict';
import {checkPunchFloatingDiagram} from './punchFloatingDiagramBrowserChecks.mjs';

// Actual batch toolbar, descriptor fields, modal and local DOM patching. The
// fixture supplies real scene resources when the native host is selected.
export async function runPunchProfileToolbarBrowserChecks(page,{ready,screenshot,report}) {
  const select=page.locator('[data-punch-region="main"] [data-tube-designer-nesting-punch-field="profileKey"]');
  const frame=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await page.evaluate(()=>window.fixture.diagramCanvas=window.fixture.toolbarCanvas=window.fixture.viewport.renderer.domElement);
  report.profileToolbarLayouts=[];
  for(const [profile,keys] of [['round',['width','wallThickness']],['rect',['width','depth','wallThickness','cornerRadius','innerRadius']]]){
    await select.selectOption('system:'+profile);await ready();
    assert.deepEqual(await page.locator('[data-punch-region="main"] [data-tube-designer-main-profile-parameter]').evaluateAll(nodes=>nodes.map(n=>n.dataset.tubeDesignerMainProfileParameter)),keys);
    await page.setViewportSize({width:1600,height:900});await frame();
    await checkPunchFloatingDiagram(page,{entry:page.locator('.punch-main-diagram-button'),key:'punch-section',kind:'profile',parameter:'width',
      control:page.locator('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="width"]'),ready,screenshot,report,
      name:'profile-floating-'+profile,first:profile==='round'});
    await page.evaluate(()=>{
      const f=window.fixture;f.toolbarNodes=[...document.querySelectorAll('.punch-batch-profile-toolbar input,.punch-batch-profile-toolbar select,.punch-batch-profile-toolbar button')];
    });
    for(const width of [1600,1280,1024]){
      await page.setViewportSize({width,height:900});await frame();
      const layout=await page.locator('.punch-batch-profile-toolbar').evaluate(node=>{
        const main=node.closest('[data-punch-region="main"]'),scroll=node.closest('.punch-main-scroll'),region=main.getBoundingClientRect(),r=scroll.getBoundingClientRect();
        const controls=[...node.querySelectorAll('input,select,button')].filter(n=>n.getClientRects().length).map(n=>{
          const b=n.getBoundingClientRect();return {key:n.hasAttribute('data-punch-profile-advanced-open')?'advanced':n.dataset.tubeDesignerMainProfileParameter??n.dataset.tubeDesignerNestingPunchField??n.className,
            left:b.left,right:b.right,top:b.top,bottom:b.bottom,center:(b.top+b.bottom)/2};
        });
        const buttons=[...main.querySelectorAll('.punch-batch-main-actions button')].map(n=>{const b=n.getBoundingClientRect();return {action:n.dataset.camAction,left:b.left,right:b.right};});
        return {controls,buttons,main:{left:region.left,right:region.right},scroll:{left:r.left,right:r.right,width:scroll.clientWidth,content:scroll.scrollWidth},
          selected:node.querySelector('select').selectedOptions[0].textContent};
      });
      assert(!/[×]|\d+(?:\.\d+)?\s*mm/.test(layout.selected),'Profile choice uses a short name without repeated dimension summary');
      assert(layout.scroll.left>=layout.main.left-1&&layout.scroll.right<=layout.main.right+1,'Horizontal scroll remains inside the main region');
      assert(layout.controls.some(n=>n.key.includes('punch-main-diagram-button')));
      assert(layout.controls.some(n=>n.key==='advanced'));
      const diagram=layout.controls.find(n=>n.key.includes('punch-main-diagram-button'));
      assert(diagram.left>Math.max(...layout.controls.filter(n=>n!==diagram).map(n=>n.right))-1,'Profile illustration follows parameters and advanced at the far right');
      assert(diagram.right<=Math.min(...layout.buttons.map(n=>n.left))+1,'Profile illustration precedes Cancel/Confirm');
      if(width>=1280)assert(layout.controls.every(n=>Math.abs(n.center-layout.controls[0].center)<3),'Profile/diagram/basic/advanced share one desktop visual row');
      for(let i=0;i<layout.controls.length;i++)for(let j=i+1;j<layout.controls.length;j++){
        const a=layout.controls[i],b=layout.controls[j];
        assert(!(Math.min(a.right,b.right)-Math.max(a.left,b.left)>1&&Math.min(a.bottom,b.bottom)-Math.max(a.top,b.top)>1),profile+'/'+width+' controls do not overlap');
      }
      assert(await page.evaluate(()=>window.fixture.toolbarNodes.every(n=>n.isConnected)&&window.fixture.toolbarCanvas===window.fixture.viewport.renderer.domElement));
      report.profileToolbarLayouts.push({profile,width,...layout});await screenshot('profile-toolbar-'+profile+'-'+width);
    }
  }
  await page.locator('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="width"]').focus();
  await page.evaluate(()=>{
    const f=window.fixture,scope=document.querySelector('.punch-main-scroll'),ref=f.view.tubeDesignerPunchWizard.preview.baseGeometry;
    f.toolbarActive=document.activeElement;f.deferResource=true;
    f.toolbarResourceReply=f.context.sceneProxy.resources.get(ref.url,{headers:new Headers({'ICAX-Resource-Version':String(ref.version)})})
      .then(response=>response.arrayBuffer()).then(()=>f.render());
    scope.scrollLeft=scope.scrollWidth-scope.clientWidth;f.toolbarScroll=[scope.scrollTop,scope.scrollLeft];
  });
  await page.waitForFunction(()=>!!window.fixture.releaseResource);
  await page.evaluate(()=>window.fixture.releaseResource());await page.evaluate(()=>window.fixture.toolbarResourceReply);await ready();
  assert(await page.evaluate(()=>{
    const f=window.fixture,scope=document.querySelector('.punch-main-scroll');return f.toolbarActive===document.activeElement&&f.toolbarActive.isConnected
      &&scope.scrollTop===f.toolbarScroll[0]&&scope.scrollLeft===f.toolbarScroll[1]&&f.toolbarCanvas===f.viewport.renderer.domElement;
  }));
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(c=>c.method.endsWith('AddNestingPunchPart')).length),0);
  report.checks.push('Short round/rect choices, illustration, descriptor basic fields and advanced entry share a compact desktop row; 1600/1280/1024 controls fit or scroll inside their main region with identical live nodes/canvas');
  report.checks.push('A delayed real resource reply preserves the toolbar input focus and latest local scroll; no final part creation');
  report.checks.push('Round/rect profile diagrams use real SVG annotations, initially center in the modal, retain dragged position after reopening and locate/update their existing template controls');
}
