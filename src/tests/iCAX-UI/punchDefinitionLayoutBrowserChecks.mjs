import assert from 'node:assert/strict';
import {checkPunchFloatingDiagram} from './punchFloatingDiagramBrowserChecks.mjs';

// Production controls and DOM patching, with native preview/resources when
// selected by the host. This focused UI check must never create final parts.
export async function runPunchDefinitionLayoutBrowserChecks(page,{idle,ready,screenshot,report}) {
  const definition=page.locator('[data-punch-batch-definition-editor]');
  const field=key=>definition.locator('[data-tube-designer-punch-field="'+key+'"]');
  const parameter=key=>definition.locator('[data-tube-designer-punch-parameter="'+key+'"]');
  const frame=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  const previewCount=()=>page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('PreviewPunchWizard')).length);
  const rememberScene=()=>page.evaluate(()=>{
    const f=window.fixture;f.definitionScene=[...f.viewport.sceneObjects.entries()];
    f.definitionVisibility=f.definitionScene.map(([id,object])=>[id,object.visible]);
    f.definitionMeshSignature=()=>JSON.stringify([...f.viewport.sceneObjects.entries()].filter(([id])=>id.startsWith('punch-preview-tool:')).map(([id,object])=>{
      const geometry=[];object.traverse(node=>{if(node.geometry)geometry.push({positions:[...(node.geometry.attributes.position?.array??[])],
        indices:[...(node.geometry.index?.array??[])],matrix:node.matrixWorld.elements});});return {id,geometry};
    }));f.definitionSceneMesh=f.definitionMeshSignature();
    return {calls:f.calls.filter(call=>call.method.endsWith('PreviewPunchWizard')).length,
      tools:f.view.tubeDesignerPunchWizard.preview?.toolCount??0,
      features:f.view.tubeDesignerPunchWizard.features.length,ids:[...f.viewport.sceneObjects.keys()]};
  });
  const unusedUnchanged=async(before,label)=>{
    await ready();assert.equal(await previewCount(),before.calls,label+' does not start a native preview');
    const result=await page.evaluate(()=>{
      const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
      return {tools:s.preview?.toolCount??0,features:s.features.length,ids:[...f.viewport.sceneObjects.keys()],
        sameMeshes:f.definitionScene.every(([id,object])=>f.viewport.sceneObjects.get(id)===object),
        visibility:f.definitionVisibility.every(([id,visible])=>f.viewport.sceneObjects.get(id)?.visible===visible)};
    });
    assert.equal(result.tools,before.tools,label+' does not instantiate an unused definition');
    assert.equal(result.features,before.features);assert.deepEqual(result.ids,before.ids);assert(result.sameMeshes,label+' retains real displayed meshes');
    assert(result.visibility,label+' keeps the existing displayed meshes visible');
  };
  const change=async(control,value)=>{
    await control.fill(value);
    await control.press('Tab');
    // Return to the same control while its real change preview is outstanding.
    // Tab commits the native browser edit once instead of creating a second
    // dirty blur event after an artificial change dispatch.
    await control.focus();await ready();
  };
  await page.locator('[data-tube-designer-nesting-punch-field="profileKey"]').selectOption('system:rect');await ready();
  await page.evaluate(()=>{
    const f=window.fixture;f.diagramCanvas=f.layoutCanvas=f.viewport.renderer.domElement;
    f.layoutRegions=[...document.querySelectorAll('[data-punch-region]')];
  });
  const staticLayout=await page.evaluate(()=>{
    const modal=document.querySelector('.tube-designer-punch-batch-dialog'),main=modal.querySelector('[data-punch-region="main"]'),scene=modal.querySelector('[data-punch-region="scene"]');
    return {historyInTitle:!!modal.querySelector(':scope > .tube-designer-dialog-header .punch-batch-history'),
      undoCount:modal.querySelectorAll('[data-cam-action$="-undo"]').length,redoCount:modal.querySelectorAll('[data-cam-action$="-redo"]').length,
      applyInMain:!!main.querySelector('[data-cam-action$="-apply"]'),cancelInMain:!!main.querySelector('[data-cam-action$="-cancel"]'),
      totalInMain:!!main.querySelector('.punch-batch-total'),totalInTitle:!!modal.querySelector(':scope > .tube-designer-dialog-header .punch-batch-header-count'),
      footer:!!modal.querySelector(':scope > .tube-designer-punch-footer'),
      sceneFooter:!!scene.querySelector('footer'),sceneExpand:!!modal.querySelector('[data-punch-batch-scene-expand]'),
      collapse:!!modal.querySelector('[data-punch-batch-definition-editor] [data-cam-action$="batch-definition-collapse"]'),
      shortcut:!!modal.querySelector('.punch-batch-shortcuts'),fitButton:[...scene.querySelectorAll('button')].some(node=>node.textContent.includes('适合窗口'))};
  });
  assert.deepEqual(staticLayout,{historyInTitle:true,undoCount:1,redoCount:1,applyInMain:true,cancelInMain:true,totalInMain:false,totalInTitle:true,
    footer:false,sceneFooter:false,sceneExpand:false,collapse:false,shortcut:false,fitButton:false});
  report.checks.push('Undo/redo and counts occur once beside the title; plain confirm/cancel are in the main region without repeated totals; no persistent footer, legend, shortcut or scene expand action');
  await field('tool').selectOption('circle');await ready();
  await checkPunchFloatingDiagram(page,{entry:page.locator('[data-punch-batch-definition-diagram]'),key:'punch-definition',kind:'tool',parameter:'diameter',
    control:parameter('diameter'),ready,screenshot,report,name:'hole-floating-circle',first:true});
  const emptyScene=await rememberScene();assert.equal(emptyScene.tools,0);assert.equal(emptyScene.features,0);
  assert.equal(await parameter('diameter').count(),1);
  await field('blindHole').evaluate(node=>window.fixture.layoutBlind=node);
  await field('opposite').evaluate(node=>window.fixture.layoutOpposite=node);
  await field('blindHole').check();await ready();
  assert.equal(await field('cutDepth').count(),1);
  assert(await field('blindHole').evaluate(node=>node===window.fixture.layoutBlind&&node===document.activeElement),
    'Native preview after enabling blind depth preserves the edited checkbox and its focus');
  assert(await field('opposite').evaluate(node=>node===window.fixture.layoutOpposite),'Conditional depth does not replace its peer checkbox');
  await change(field('cutDepth'),'1');
  assert(await field('cutDepth').evaluate(node=>node===document.activeElement),'Editing depth retains focus across preview');
  await field('blindHole').uncheck();await ready();
  assert.equal(await field('cutDepth').count(),0);
  assert(await field('blindHole').evaluate(node=>node===window.fixture.layoutBlind&&node===document.activeElement));
  assert(await page.evaluate(()=>window.fixture.layoutCanvas===window.fixture.viewport.renderer.domElement));
  await unusedUnchanged(emptyScene,'Editing unused H1 circle/blind/depth');
  for(const type of ['slot','edge-arc-groove']){
    await page.locator('[data-cam-action$="batch-definition-add"]').click();await ready();
    await field('tool').selectOption(type);await ready();
    const key=type==='slot'?'spanAlong':'angle';
    await change(parameter(key),type==='slot'?'44':'80');
    await unusedUnchanged(emptyScene,'Adding/editing unassigned '+type+' with only a blank');
    await page.locator('[data-cam-action$="batch-definition-remove"]').click();await ready();
    await unusedUnchanged(emptyScene,'Removing unassigned '+type+' with only a blank');
  }
  report.checks.push('With only a blank, unassigned circle, slot and edge-arc-groove definitions never create draft scene objects or send native Preview requests');
  await page.locator('[data-cam-action="tube-designer-nesting-punch-create-add"]').click();await ready();
  const h1Scene=await rememberScene();assert.equal(h1Scene.tools,1);assert.equal(h1Scene.features,1);
  assert(h1Scene.ids.some(id=>id.startsWith('punch-preview-tool:feature:')));
  await page.evaluate(()=>window.fixture.layoutH1=window.fixture.view.tubeDesignerPunchBatch.selectedDefinitionId);
  const layout=async(shape,width)=>{
    await page.setViewportSize({width,height:900});await frame();
    const result=await definition.evaluate(node=>{
      const parts=[...node.querySelector('.tube-designer-punch-parameter-content').children].map(child=>child.className);
      const controls=[...node.querySelectorAll('input,select')].filter(n=>n.getClientRects().length).map(control=>{
        const box=control.closest('label')?.getBoundingClientRect()??control.getBoundingClientRect();
        return {key:control.dataset.tubeDesignerPunchParameter??control.dataset.tubeDesignerPunchField??'source',left:box.left,right:box.right,top:box.top,bottom:box.bottom};
      });
      const r=node.getBoundingClientRect(),main=document.querySelector('.punch-batch-main-actions').getBoundingClientRect();
      const strip=document.querySelector('.punch-batch-definition-strip'),diagram=strip.querySelector('[data-punch-batch-definition-diagram]'),d=diagram.getBoundingClientRect();
      const peerRight=Math.max(...[...strip.children].filter(n=>n!==diagram).map(n=>n.getBoundingClientRect().right));
      return {parts,controls,left:r.left,right:r.right,scrollWidth:node.scrollWidth,width:node.clientWidth,mainRight:main.right,
        illustration:{left:d.left,right:d.right,peerRight,last:strip.lastElementChild===diagram}};
    });
    assert(result.parts[0].includes('punch-inline-operation'),'Descriptor operation group precedes machining source');
    assert(result.parts[1].includes('punch-inline-source'),'Source/tool follow the operation group');
    assert(result.parts[2].includes('tube-designer-punch-sheet-parameters'),'Template dimensions remain last');
    assert.deepEqual(result.controls.slice(0,2).map(item=>item.key),['blindHole','opposite']);
    assert(result.mainRight<=width+1,'Main-region actions fit at '+width);
    assert(result.illustration.last&&result.illustration.left>=result.illustration.peerRight-1,'Shared hole illustration occupies the strip far right after its other controls');
    for(let a=0;a<result.controls.length;a++)for(let b=a+1;b<result.controls.length;b++){
      const x=result.controls[a],y=result.controls[b];
      assert(!(Math.min(x.right,y.right)-Math.max(x.left,y.left)>1&&Math.min(x.bottom,y.bottom)-Math.max(x.top,y.top)>1),
        'Controls do not overlap at '+shape+'/'+width);
    }
    if(width===1600){
      const blind=result.controls.find(item=>item.key==='blindHole'),opposite=result.controls.find(item=>item.key==='opposite');
      assert(Math.abs(blind.top-opposite.top)<2,'Blind and opposite operation fields share a visual row');
      assert(result.controls.every(item=>Math.abs(item.top-blind.top)<8),'Wide shared definition stays in one visual row');
    }
    report.definitionLayouts??=[];report.definitionLayouts.push({shape,viewport:width,...result});
    await screenshot('definition-'+shape+'-'+width);
  };
  for(const width of [1600,1280,1024])await layout('circle',width);
  await page.setViewportSize({width:1600,height:900});await frame();
  // Tab follows the descriptor operation group before source/tool/size.
  await field('blindHole').focus();await page.keyboard.press('Tab');
  assert(await field('opposite').evaluate(node=>node===document.activeElement));
  await page.keyboard.press('Tab');
  assert(await definition.locator('[data-cam-change-action$="record-kind-change"]').evaluate(node=>node===document.activeElement));
  await page.locator('[data-cam-action$="batch-definition-add"]').click();await ready();
  await page.evaluate(()=>window.fixture.layoutH2=window.fixture.view.tubeDesignerPunchBatch.selectedDefinitionId);
  await field('tool').selectOption('circle');await change(parameter('diameter'),'20');
  await unusedUnchanged(h1Scene,'Adding/editing unused H2 circle beside H1 hole');
  await field('tool').selectOption('slot');await ready();
  await checkPunchFloatingDiagram(page,{entry:page.locator('[data-punch-batch-definition-diagram]'),key:'punch-definition',kind:'tool',parameter:'spanAlong',
    control:parameter('spanAlong'),ready,screenshot,report,name:'hole-floating-slot'});
  assert.equal(await parameter('spanAlong').count(),1);assert.equal(await parameter('spanAcross').count(),1);
  for(const width of [1600,1280,1024])await layout('slot',width);
  report.checks.push('Circle and slot use the same front operation group and descriptor order; wide operation/source/size controls share a row, and 1600/1280/1024 layouts do not overlap');
  await page.setViewportSize({width:1600,height:900});await frame();
  await field('blindHole').evaluate(node=>window.fixture.layoutBlind=node);
  await field('blindHole').check();await ready();
  assert(await field('blindHole').evaluate(node=>node===window.fixture.layoutBlind&&node===document.activeElement));
  await change(field('cutDepth'),'1');
  await field('blindHole').uncheck();await ready();
  assert.equal(await field('cutDepth').count(),0);
  await parameter('spanAlong').evaluate(node=>window.fixture.layoutDimension=node);
  await change(parameter('spanAlong'),'44');
  await unusedUnchanged(h1Scene,'Changing unused H2 to slot and editing its size/blind options');
  assert(await parameter('spanAlong').evaluate(node=>node===window.fixture.layoutDimension&&node===document.activeElement));
  await page.setViewportSize({width:1024,height:900});await frame();
  await page.evaluate(()=>{
    const f=window.fixture;f.view.tubeDesignerPunchBatch.punchRegionSizes={definitions:64};f.render();
  });await frame();
  await page.evaluate(()=>{
    const f=window.fixture,ref=f.view.tubeDesignerPunchWizard.preview.baseGeometry;
    f.deferResource=true;f.layoutResourceReply=f.context.sceneProxy.resources.get(ref.url,
      {headers:new Headers({'ICAX-Resource-Version':String(ref.version)})}).then(response=>response.arrayBuffer()).then(()=>f.render());
  });
  await page.waitForFunction(()=>!!window.fixture.releaseResource);
  const name=page.locator('[data-punch-batch-part-row]').first().locator('[data-punch-batch-field="name"]');
  await name.fill('继续输入的零件名称');
  await page.evaluate(()=>{
    const f=window.fixture,node=document.querySelector('.punch-batch-definition-scroll');
    f.layoutName=document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]');
    f.layoutName.setSelectionRange(2,6,'backward');
    node.scrollLeft=13;node.scrollTop=5;f.layoutLatestScroll=[node.scrollLeft,node.scrollTop];f.releaseResource();
  });await page.evaluate(()=>window.fixture.layoutResourceReply);await ready();
  assert(await name.evaluate(node=>node===window.fixture.layoutName&&node===document.activeElement&&node.selectionStart===2&&node.selectionEnd===6&&node.selectionDirection==='backward'));
  assert(await parameter('spanAlong').evaluate(node=>node===window.fixture.layoutDimension&&node.value==='44'));
  assert(await page.evaluate(()=>{
    const f=window.fixture,node=document.querySelector('.punch-batch-definition-scroll');
    return node.scrollLeft===f.layoutLatestScroll[0]&&node.scrollTop===f.layoutLatestScroll[1]&&node.scrollTop>0&&f.layoutCanvas===f.viewport.renderer.domElement;
  }));
  report.checks.push('Blind/depth conditions preserve edited checkbox identity and focus; delayed actual resource reply retains live dimension input, name caret, latest scroll and original canvas');
  await page.setViewportSize({width:1600,height:900});await frame();
  await page.locator('[data-cam-action$="batch-definition-add"]').click();await ready();
  await field('tool').selectOption('edge-arc-groove');await change(parameter('angle'),'80');
  await unusedUnchanged(h1Scene,'Adding/editing unused edge-arc-groove beside H1');
  await page.locator('[data-cam-action$="batch-definition-remove"]').click();await ready();
  await unusedUnchanged(h1Scene,'Deleting unused edge-arc-groove beside H1');
  await page.locator('[data-cam-action$="batch-definition-add"]').click();await ready();
  await definition.locator('[data-cam-change-action$="record-kind-change"]').selectOption('branch');await ready();
  await definition.locator('[data-cam-change-action$="profile-select"]').selectOption('system:round');await ready();
  await checkPunchFloatingDiagram(page,{entry:page.locator('[data-punch-batch-definition-diagram]'),key:'punch-definition',kind:'profile',parameter:'width',
    control:definition.locator('[data-tube-designer-punch-profile-parameter="width"]'),ready,screenshot,report,name:'hole-floating-branch-round'});
  const branchWidth=definition.locator('[data-tube-designer-punch-profile-parameter="width"]');
  const branchWall=definition.locator('[data-tube-designer-punch-profile-parameter="wallThickness"]');
  await page.evaluate(()=>{const f=window.fixture;f.holdActualProfileReplies=true;f.branchWidth=document.querySelector('[data-punch-batch-definition-editor] [data-tube-designer-punch-profile-parameter="width"]');
    f.branchWall=document.querySelector('[data-punch-batch-definition-editor] [data-tube-designer-punch-profile-parameter="wallThickness"]');});
  await branchWidth.fill('44');await branchWidth.press('Tab');await branchWidth.focus();
  await page.waitForFunction(()=>window.fixture.actualProfileHolds?.length===1);
  assert(await branchWidth.evaluate(node=>!node.disabled&&node===document.activeElement&&node===window.fixture.branchWidth),
    'Batch branch dimensions remain editable during actual native section generation');
  await branchWall.fill('1.5');await branchWall.press('Tab');await branchWall.focus();
  await page.waitForFunction(()=>window.fixture.actualProfileHolds?.length===2);
  await page.evaluate(()=>{const f=window.fixture,scroll=document.querySelector('.punch-batch-definition-scroll');scroll.scrollTop=7;scroll.scrollLeft=11;
    f.branchLatestScroll=[scroll.scrollTop,scroll.scrollLeft];f.holdActualProfileReplies=false;f.actualProfileHolds[1].release();});
  await page.waitForFunction(()=>{const f=window.fixture,d=f.view.tubeDesignerPunchBatch.definitions.find(d=>d.id===f.view.tubeDesignerPunchBatch.selectedDefinitionId);
    return d.recipe.section?.profile?.width===44&&d.recipe.section?.profile?.wallThickness===1.5;});
  await page.evaluate(()=>window.fixture.actualProfileHolds[0].release());await ready();
  const latestBranch=await page.evaluate(()=>{const f=window.fixture,d=f.view.tubeDesignerPunchBatch.definitions.find(d=>d.id===f.view.tubeDesignerPunchBatch.selectedDefinitionId),scroll=document.querySelector('.punch-batch-definition-scroll');
    return {parameters:d.recipe.section.parameters,width:d.recipe.section.profile.width,wall:d.recipe.section.profile.wallThickness,
      widthNode:f.branchWidth===document.querySelector('[data-punch-batch-definition-editor] [data-tube-designer-punch-profile-parameter="width"]'),
      wallNode:f.branchWall===document.querySelector('[data-punch-batch-definition-editor] [data-tube-designer-punch-profile-parameter="wallThickness"]'),
      active:f.branchWall===document.activeElement,scroll:[scroll.scrollTop,scroll.scrollLeft],expectedScroll:f.branchLatestScroll,
      canvas:f.layoutCanvas===f.viewport.renderer.domElement,holds:f.actualProfileHolds.map(h=>({parameters:h.parameters,nativeMilliseconds:h.nativeMilliseconds,released:h.released}))};});
  assert.equal(latestBranch.width,44);assert.equal(latestBranch.wall,1.5);
  assert.equal(latestBranch.parameters.width,44);assert.equal(latestBranch.parameters.wallThickness,1.5);
  assert(latestBranch.widthNode&&latestBranch.wallNode&&latestBranch.active&&latestBranch.canvas);assert.deepEqual(latestBranch.scroll,latestBranch.expectedScroll);
  report.branchProfileAsync=latestBranch;await screenshot('branch-profile-async-latest');
  await unusedUnchanged(h1Scene,'Unused branch profile and its real floating parameter diagram');
  await page.locator('[data-cam-action$="batch-definition-remove"]').click();await ready();
  await unusedUnchanged(h1Scene,'Deleting the unused branch definition');
  report.checks.push('Shared circle/slot/branch-profile illustrations occupy the strip far right; real template SVG annotations locate original controls, follow edited dimensions, and retain dragged positions after reopening');
  report.checks.push('Two actual asynchronous branch-section responses settle out of order without overwriting latest dimensions, contour, effective refocus, local scroll or original controls/canvas');
  for(const key of ['layoutH1','layoutH2']){
    await page.evaluate(async key=>{
      const f=window.fixture;await f.dispatch('batch-definition-select',{dataset:{punchBatchDefinitionId:f[key]}});
    },key);await ready();await unusedUnchanged(h1Scene,'Switching shared definition tabs');
  }
  const assignment=page.locator('[data-cam-change-action$="batch-hole-definition"]').first();
  await assignment.selectOption(await page.evaluate(()=>window.fixture.layoutH2));await ready();
  assert(await previewCount()>h1Scene.calls,'Assigning H2 to an actual hole row starts native preview');
  const assigned=await page.evaluate(()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    return {tools:s.preview.toolCount,tool:s.previewRecipe.features[0].toolRef.id,
      parameter:s.previewRecipe.features[0].toolParameters.spanAlong,ids:[...f.viewport.sceneObjects.keys()],
      changed:f.definitionSceneMesh!==f.definitionMeshSignature()};
  });
  assert.equal(assigned.tools,1);assert.equal(assigned.tool,'slot');assert.equal(assigned.parameter,44);assert(assigned.changed);
  assert(!assigned.ids.some(id=>id.includes('draft')));report.assignedSharedDefinition=assigned;
  report.checks.push('With a real H1 hole, unused H2 circle/slot/edge-arc edits and tab/removal actions make zero native requests and preserve H1 meshes; assigning H2 alone updates the real hole');
  // A main geometry edit supplies a real history transaction. Header buttons
  // must restore the displayed input and shared geometry in both directions.
  const width=page.locator('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="width"]');
  const beforeWidth=await width.inputValue();await change(width,String(Number(beforeWidth)+4));
  const history=page.locator('.punch-batch-history');
  assert(await history.locator('[data-cam-action$="-undo"]').isEnabled());
  await history.locator('[data-cam-action$="-undo"]').click();await ready();
  assert.equal(await width.inputValue(),beforeWidth,'Title undo restores the real shared profile edit');
  assert(await history.locator('[data-cam-action$="-redo"]').isEnabled());
  await history.locator('[data-cam-action$="-redo"]').click();await ready();
  assert.equal(await width.inputValue(),String(Number(beforeWidth)+4),'Title redo reapplies the same shared profile edit');
  const splitter=page.locator('[data-punch-region-splitter="holes:scene"]'),box=await splitter.boundingBox();
  const beforeDrag=await page.locator('[data-punch-region="scene"]').evaluate(node=>node.getBoundingClientRect().height);
  const beforeCalls=await page.evaluate(()=>window.fixture.calls.length);
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2-35,{steps:5});await page.mouse.up();await frame();
  assert(await page.locator('[data-punch-region="scene"]').evaluate((node,before)=>node.getBoundingClientRect().height>before+25,beforeDrag));
  assert.equal(await page.evaluate(()=>window.fixture.calls.length),beforeCalls,'Divider resizing remains purely local');
  assert(await page.evaluate(()=>window.fixture.layoutCanvas===window.fixture.viewport.renderer.domElement&&window.fixture.layoutRegions.every(node=>node===document.querySelector('[data-punch-region="'+node.dataset.punchRegion+'"]'))));
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')).length),0);
  report.checks.push('Header undo/redo restore shared profile geometry; scene divider remains draggable with identical canvas/region nodes and zero creation calls');
  await screenshot('definition-final-native-preview');
}
