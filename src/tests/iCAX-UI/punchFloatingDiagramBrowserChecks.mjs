import assert from 'node:assert/strict';

// Exercise the real shared draggable window and descriptor annotation binding.
export async function checkPunchFloatingDiagram(page,{entry,key,kind,parameter,control,ready,screenshot,report,name,first=false}) {
  const frame=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  const panel=page.locator('[data-library-floating-diagram="'+key+'"]');
  const callCount=()=>page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('PreviewPunchWizard')).length);
  const before=await callCount();
  await entry.click();await ready();await frame();
  assert.equal(await panel.count(),1);assert(await panel.locator('svg').count()>0,'Illustration renders the actual template SVG');
  assert.equal(await panel.getAttribute('aria-modal'),'false');
  const centered=await panel.evaluate(node=>{
    const p=node.getBoundingClientRect(),s=node.closest('[data-floating-parameter-diagram-scope]').getBoundingClientRect();
    return {left:p.left,top:p.top,width:p.width,height:p.height,dx:(p.left+p.width/2)-(s.left+s.width/2),dy:(p.top+p.height/2)-(s.top+s.height/2)};
  });
  if(first){assert(Math.abs(centered.dx)<2);assert(Math.abs(centered.dy)<2);}
  const attr='data-'+kind+'-annotation-key';
  const annotation=panel.locator('['+attr+'="'+parameter+'"][tabindex="0"]').first();
  assert.equal(await annotation.count(),1,'Real template declares an annotation for '+parameter);
  await control.evaluate(node=>window.fixture.diagramControl=node);
  await annotation.focus();await annotation.press('Enter');await frame();
  if(!await control.evaluate(node=>node===document.activeElement&&node===window.fixture.diagramControl)){
    report.floatingDiagramFailure=await page.evaluate(()=>({active:document.activeElement.outerHTML,
      scopes:[...document.querySelectorAll('[data-profile-parameter-scope],[data-tool-parameter-scope]')].map(n=>({owner:n.dataset.parameterDiagramOwner,
        kind:n.hasAttribute('data-profile-parameter-scope')?'profile':'tool',keys:[...n.querySelectorAll('[data-profile-parameter-key],[data-tool-parameter-key]')].map(c=>c.outerHTML)})),
      diagrams:[...document.querySelectorAll('[data-parameter-diagram-for]')].map(n=>({owner:n.dataset.parameterDiagramFor,html:n.outerHTML.slice(0,200)}))}));
    await screenshot(name+'-annotation-failure');
  }
  assert(await control.evaluate(node=>node===document.activeElement&&node===window.fixture.diagramControl),
    'Template annotation locates its actual matching control');
  const original=await control.inputValue(),svg=await panel.locator('svg').first().evaluate(node=>node.outerHTML);
  await control.fill(String(Number(original)+2));await control.press('Tab');
  const immediatelyAfterTab=await control.evaluate(node=>({active:document.activeElement.outerHTML.slice(0,350),disabled:node.disabled,
    pending:window.fixture.view.pending,uiFocus:window.fixture.view.tubeDesignerPunchWizard.uiFocus}));
  await control.focus();
  const immediatelyAfterRefocus=await control.evaluate(node=>({active:document.activeElement.outerHTML.slice(0,350),disabled:node.disabled,
    pending:window.fixture.view.pending,uiFocus:window.fixture.view.tubeDesignerPunchWizard.uiFocus}));
  await ready();
  if(!await control.evaluate(node=>node===window.fixture.diagramControl&&node===document.activeElement)){
    report.floatingDiagramEditFailure=await control.evaluate(node=>({active:document.activeElement.outerHTML,current:node.outerHTML,
      original:window.fixture.diagramControl.outerHTML,originalConnected:window.fixture.diagramControl.isConnected,
      same:node===window.fixture.diagramControl,scope:node.closest('[data-profile-parameter-scope]')?.dataset.parameterDiagramOwner,
      recipe:window.fixture.view.tubeDesignerPunchBatch.definitions.find(d=>d.id===window.fixture.view.tubeDesignerPunchBatch.selectedDefinitionId).recipe,
      error:window.fixture.view.tubeDesignerPunchWizard.error,uiFocus:window.fixture.view.tubeDesignerPunchWizard.uiFocus,
      restoreParameterFocus:window.fixture.view.tubeDesignerPunchWizard.restoreParameterFocus,sectionDiagram:window.fixture.view.tubeDesignerPunchWizard.sectionDiagram}));
    Object.assign(report.floatingDiagramEditFailure,{immediatelyAfterTab,immediatelyAfterRefocus});
    await screenshot(name+'-edit-failure');
  }
  assert(await control.evaluate(node=>node===window.fixture.diagramControl&&node===document.activeElement));
  assert.notEqual(await panel.locator('svg').first().evaluate(node=>node.outerHTML),svg,'SVG dimensions follow edited template values');
  await control.fill(original);await control.press('Tab');await control.focus();await ready();
  const moveCalls=await callCount(),header=await panel.locator('[data-floating-diagram-drag]').boundingBox();
  await page.mouse.move(header.x+80,header.y+header.height/2);await page.mouse.down();
  await page.mouse.move(header.x+125,header.y+header.height/2+30,{steps:5});await page.mouse.up();await frame();
  const moved=await panel.boundingBox();assert(Math.abs(moved.x-centered.left-45)<2);assert(Math.abs(moved.y-centered.top-30)<2);
  await screenshot(name);
  await panel.locator('[data-cam-action$="section-diagram-close"]').click();await ready();assert.equal(await panel.count(),0);
  await entry.click();await ready();await frame();
  const reopened=await panel.boundingBox();assert(Math.abs(reopened.x-moved.x)<2);assert(Math.abs(reopened.y-moved.y)<2);
  await panel.locator('[data-cam-action$="section-diagram-close"]').click();await ready();
  assert.equal(await callCount(),moveCalls,'Dragging and reopening a floating illustration are presentation-only');
  assert(await page.evaluate(()=>window.fixture.viewport.renderer.domElement===window.fixture.diagramCanvas));
  report.floatingDiagrams??=[];report.floatingDiagrams.push({name,first,initial:centered,moved,reopened,previewRequests:await callCount()-before});
}
