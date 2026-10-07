import assert from 'node:assert/strict';

// Gates delay already-completed real native responses, never substitute a
// business result. Table timing stops at the new tr, independently of ready.
export async function runPunchRowAddResponseBrowserChecks(page,{idle,ready,screenshot,report,baseline=false}) {
  const add=page.locator('[data-cam-action="tube-designer-nesting-punch-create-add"]');
  const rowSelector='[data-punch-region="holes"] tr[data-tube-designer-punch-row]:not([data-tube-designer-punch-row="draft"])';
  await page.evaluate(rowSelector=>{
    const f=window.fixture;f.rowAddCanvas=f.viewport.renderer.domElement;f.rowAddInput=document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]');
    f.rowAddTimings=[];f.rowAddApplied=[];f.rowAddObserver=new MutationObserver(()=>{
      const count=document.querySelectorAll(rowSelector).length;
      for(const timing of f.rowAddTimings)if(timing.appeared===undefined&&count>=timing.expected){timing.appeared=performance.now();timing.milliseconds=timing.appeared-timing.clicked;}
    });f.rowAddObserver.observe(document.querySelector('[data-punch-region="holes"]'),{subtree:true,childList:true});
    document.querySelector('#mount').addEventListener('click',event=>{
      if(event.target.closest('[data-cam-action="tube-designer-nesting-punch-create-add"]'))
        f.rowAddTimings.push({clicked:performance.now(),expected:f.view.tubeDesignerPunchWizard.features.length+1});
    },true);
    const original=f.viewport.applyViewSnapshot.bind(f.viewport);
    f.viewport.applyViewSnapshot=async(snapshot,...args)=>{
      const result=await original(snapshot,...args);
      f.rowAddApplied.push({revision:snapshot.revision,ids:[...f.viewport.sceneObjects.keys()],applied:result?.applied,superseded:result?.superseded,
        snapshotCurrent:typeof snapshot.isCurrent==='function'?snapshot.isCurrent():null,
        selectedPartId:f.view.tubeDesignerPunchBatch?.selectedPartId,time:performance.now()});return result;
    };
    f.holdActualPreviewReplies=true;
  },rowSelector);
  await add.click();
  await page.waitForFunction(selector=>document.querySelectorAll(selector).length===1,rowSelector);
  await page.waitForFunction(()=>window.fixture.actualPreviewHolds?.length===1);
  const first=await page.evaluate(()=>({timing:window.fixture.rowAddTimings[0],native:window.fixture.actualPreviewHolds[0].nativeMilliseconds,
    pending:window.fixture.view.pending,disabled:document.querySelector('[data-cam-action="tube-designer-nesting-punch-create-add"]').disabled,
    overlay:!!document.querySelector('[data-tube-designer-operation-message]'),rows:window.fixture.view.tubeDesignerPunchWizard.features.length}));
  report.rowAddBaseline=first;
  await screenshot(baseline?'row-add-baseline-held-native-response':'row-add-first-held-native-response');
  if(baseline) {
    await page.evaluate(()=>{const f=window.fixture;f.holdActualPreviewReplies=false;for(const hold of f.actualPreviewHolds)hold.release();});await ready();
    report.checks.push('Measured click-to-first-table-row independently of native completion; recorded busy state and add availability while the actual native response is delayed');
  } else {
    assert(first.timing.milliseconds<180,'The row is visible promptly before native response delivery');
    assert.equal(first.disabled,false,'Another row remains addable while an earlier real preview reply is held');
    assert.equal(first.pending,false,'Background preview does not own the entire form operation gate');
    assert.equal(first.overlay,false,'Adding a table row does not cover the form with a waiting overlay');
    await page.setViewportSize({width:1024,height:900});
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    for(const expected of [2,3]){
      await add.click();
      await page.waitForFunction(({selector,count})=>document.querySelectorAll(selector).length===count,{selector:rowSelector,count:expected});
      assert(await add.isEnabled());
    }
    await page.evaluate(()=>{
      const f=window.fixture,n=f.rowAddInput;n.focus({preventScroll:true});n.value='持续编辑中的孔件';n.setSelectionRange(1,5,'backward');
      const scroll=document.querySelector('.tube-designer-punch-sheet-scroll');scroll.scrollLeft=Math.min(80,scroll.scrollWidth-scroll.clientWidth);
      f.rowAddLatestScroll=[scroll.scrollTop,scroll.scrollLeft];f.rowAddFeatureIds=f.view.tubeDesignerPunchWizard.features.map(item=>item.id);
      f.actualPreviewHolds[0].release();
    });
    await page.waitForFunction(()=>window.fixture.actualPreviewHolds.some(hold=>hold.features===3));
    const newest=await page.evaluate(()=>{
      const f=window.fixture,s=f.view.tubeDesignerPunchWizard,holds=f.actualPreviewHolds;
      return {holds:holds.map(h=>({features:h.features,released:h.released,nativeMilliseconds:h.nativeMilliseconds})),
        previewFeatures:s.previewRecipe?.features?.length??0,applied:f.rowAddApplied.map(item=>item.ids)};
    });
    assert.notEqual(newest.previewFeatures,1,'An outdated one-row response cannot become the current preview after three rows exist');
    await page.evaluate(()=>{
      const f=window.fixture;f.holdActualPreviewReplies=false;
      // If requests overlap, deliver newest first and older replies afterwards.
      const holds=[...f.actualPreviewHolds].sort((a,b)=>b.features-a.features);
      for(const hold of holds)if(!hold.released)hold.release();
    });
    await page.waitForFunction(()=>{
      const s=window.fixture.view.tubeDesignerPunchWizard;
      return s.previewRecipe?.features?.length===3&&s.preview?.revision===s.revision;
    });await ready();
    const result=await page.evaluate(()=>{
      const f=window.fixture,s=f.view.tubeDesignerPunchWizard,scroll=document.querySelector('.tube-designer-punch-sheet-scroll');
      return {timings:f.rowAddTimings,rows:s.features.length,previewRows:s.previewRecipe?.features?.length,toolCount:s.preview?.toolCount,
        previewCurrent:s.preview?.revision===s.revision,canvasSame:f.rowAddCanvas===f.viewport.renderer.domElement,
        inputSame:f.rowAddInput===document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]'),
        focusSame:f.rowAddInput===document.activeElement,value:f.rowAddInput.value,selection:[f.rowAddInput.selectionStart,f.rowAddInput.selectionEnd,f.rowAddInput.selectionDirection],
        scroll:[scroll.scrollTop,scroll.scrollLeft],expectedScroll:f.rowAddLatestScroll,featureIds:f.rowAddFeatureIds,sceneIds:[...f.viewport.sceneObjects.keys()],
        applied:f.rowAddApplied.map(item=>({ids:item.ids,time:item.time})),holds:f.actualPreviewHolds.map(h=>({features:h.features,released:h.released,nativeMilliseconds:h.nativeMilliseconds}))};
    });
    assert.equal(result.timings.length,3);assert(result.timings.every(timing=>timing.milliseconds<180));
    assert.equal(result.rows,3);assert.equal(result.previewRows,3);assert.equal(result.toolCount,3);assert(result.previewCurrent);
    assert(result.featureIds.every(id=>result.sceneIds.includes('punch-preview-tool:feature:'+id)),'The displayed native scene includes each newest feature');
    assert(result.canvasSame&&result.inputSame&&result.focusSame);assert.equal(result.value,'持续编辑中的孔件');
    assert.deepEqual(result.selection,[1,5,'backward']);assert.deepEqual(result.scroll,result.expectedScroll);
    assert(result.scroll[1]>0,'Latest horizontal table scroll is nonzero during the delayed reply');
    report.rowAddResponse={...result,newestHeld:newest};
    report.checks.push('Three real add clicks display their rows immediately while native preview is delayed; form remains usable and no old response overwrites the newest three-feature scene');
    report.checks.push('Native tools for all latest feature IDs render on the original canvas; current name input/caret/focus and latest table scroll survive response completion');
    await screenshot('row-add-three-immediate-and-native-current');
    await page.setViewportSize({width:1600,height:1000});
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    await screenshot('row-add-three-wide-native-scene');
    const diameter=page.locator('[data-punch-batch-definition-editor] [data-tube-designer-punch-parameter="diameter"]');
    const editDiameter=async value=>{
      await diameter.fill(value);
      // A real Tab commits the browser's dirty value once and chooses the next
      // focused input before a preview render can run. Synthetic change leaves
      // the native blur change pending and does not model the user interaction.
      await diameter.press('Tab');
    };
    const releaseResources=()=>page.evaluate(()=>{
      const f=window.fixture;f.holdActualResourceReplies=false;for(const hold of f.actualResourceHolds??[])if(!hold.released)hold.release();
    });
    await page.setViewportSize({width:1024,height:900});
    await page.evaluate(()=>{
      const f=window.fixture;f.holdActualResourceReplies=true;f.actualResourceHolds=[];f.rowAddFocusEvents=[];f.rowAddResourceWizard=f.view.tubeDesignerPunchWizard;
      for(const type of ['focusin','change'])document.querySelector('#mount').addEventListener(type,event=>{
        const active=document.activeElement;f.rowAddFocusEvents.push({type,target:event.target.outerHTML?.slice(0,250),active:active?.outerHTML.slice(0,250),
          targetValue:event.target.value,activeValue:active?.value,uiFocus:f.view.tubeDesignerPunchWizard?.uiFocus});
      },true);
    });
    await editDiameter('12');
    await page.waitForFunction(()=>window.fixture.actualResourceHolds.length>0);
    assert(await add.isEnabled(),'The form remains editable while actual mesh bytes are held');
    await page.locator('[data-punch-batch-part-row] [data-punch-batch-field="name"]').first().fill('资源响应期间继续编辑');
    await page.evaluate(()=>{
      const f=window.fixture,n=f.rowAddInput,scroll=document.querySelector('.tube-designer-punch-sheet-scroll');
      n.setSelectionRange(2,7,'backward');
      scroll.scrollLeft=75;f.rowAddResourceScroll=[scroll.scrollTop,scroll.scrollLeft];
      f.rowAddBeforeResourceRelease={active:document.activeElement?.outerHTML.slice(0,400),uiFocus:f.view.tubeDesignerPunchWizard.uiFocus,
        name:n.value,defaultValue:n.defaultValue,draftName:f.view.tubeDesignerNestingPunchPartDraft.name,
        sameWizard:f.view.tubeDesignerPunchWizard===f.rowAddResourceWizard,events:[...f.rowAddFocusEvents]};
    });
    await releaseResources();await ready();
    report.rowAddResource=await page.evaluate(()=>{
      const f=window.fixture,n=f.rowAddInput,scroll=document.querySelector('.tube-designer-punch-sheet-scroll');
      return {inputSame:n===document.querySelector('[data-punch-batch-part-row] [data-punch-batch-field="name"]'),focusSame:n===document.activeElement,
        active:document.activeElement?.outerHTML.slice(0,400),value:n.value,selection:[n.selectionStart,n.selectionEnd,n.selectionDirection],
        scroll:[scroll.scrollTop,scroll.scrollLeft],expectedScroll:f.rowAddResourceScroll,canvasSame:f.rowAddCanvas===f.viewport.renderer.domElement,
        beforeRelease:f.rowAddBeforeResourceRelease,events:f.rowAddFocusEvents};
    });
    const resource=report.rowAddResource;
    assert(resource.inputSame&&resource.focusSame&&resource.canvasSame,'Actual resource completion preserves current input focus and canvas');
    assert.equal(resource.value,'资源响应期间继续编辑');assert.deepEqual(resource.selection,[2,7,'backward']);
    assert.deepEqual(resource.scroll,resource.expectedScroll);assert(resource.scroll[1]>0);
    await page.evaluate(()=>{window.fixture.holdActualResourceReplies=true;window.fixture.actualResourceHolds=[];});
    await editDiameter('14');await page.waitForFunction(()=>window.fixture.actualResourceHolds.length>0);
    await page.locator('[data-cam-action$="batch-add"]').click();
    await page.waitForFunction(()=>window.fixture.view.tubeDesignerPunchBatch.parts.length===2);
    const secondId=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchBatch.selectedPartId);
    await releaseResources();await ready();
    assert(await page.evaluate(id=>{
      const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
      return f.view.tubeDesignerPunchBatch.selectedPartId===id&&s.features.length===0&&s.previewRecipe?.features?.length===0
        &&s.preview?.toolCount===0&&![...f.viewport.sceneObjects.keys()].some(key=>key.startsWith('punch-preview-tool:'))
        &&f.rowAddCanvas===f.viewport.renderer.domElement;
    },secondId),'An old resource flight cannot put the first part tools into the newly selected empty part');
    const firstId=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchBatch.parts[0].id);
    assert(await page.evaluate(id=>window.fixture.rowAddApplied.some(item=>item.applied===false&&item.superseded===true&&item.snapshotCurrent===false&&item.selectedPartId===id),secondId),
      'The shared viewport rejects the stale first-part snapshot before installing its objects');
    report.rowAddPartSwitch=await page.evaluate(()=>({selectedPartId:window.fixture.view.tubeDesignerPunchBatch.selectedPartId,
      previewFeatures:window.fixture.view.tubeDesignerPunchWizard.previewRecipe?.features?.length,
      discardedSnapshots:window.fixture.rowAddApplied.filter(item=>item.applied===false)}));
    await page.locator('[data-cam-action$="batch-select"][data-punch-batch-part-id="'+firstId+'"]').first().click();await ready();
    await page.evaluate(()=>{window.fixture.holdActualResourceReplies=true;window.fixture.actualResourceHolds=[];});
    await editDiameter('16');await page.waitForFunction(()=>window.fixture.actualResourceHolds.length>0);
    const closingCalls=await page.evaluate(()=>window.fixture.actualPreviewResponses.length);
    await page.locator('[data-punch-region="main"] [data-cam-action$="-cancel"]').click();await idle();
    assert.equal(await page.locator('.tube-designer-punch-batch-dialog').count(),0);
    await releaseResources();
    await page.waitForFunction(()=>!window.fixture.view.tubeDesignerPunchBatch&&window.fixture.actualResourceHolds.every(hold=>hold.released));
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    assert(await page.evaluate(count=>{
      const f=window.fixture;return !f.view.tubeDesignerPunchWizard&&!document.querySelector('.tube-designer-punch-batch-dialog')
        &&f.background===document.querySelector('#background')&&!f.background.inert&&document.activeElement===document.querySelector('#opener')
        &&f.actualPreviewResponses.length===count;
    },closingCalls),'Cancellation stays closed after old actual resources settle and restores the existing background/opener');
    report.rowAddCancellation=await page.evaluate(()=>({modalClosed:!document.querySelector('.tube-designer-punch-batch-dialog'),
      backgroundSame:window.fixture.background===document.querySelector('#background'),backgroundInert:window.fixture.background.inert,
      openerFocused:document.activeElement===document.querySelector('#opener'),
      creationCalls:window.fixture.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')).length,
      discardedSnapshots:window.fixture.rowAddApplied.filter(item=>item.applied===false)}));
    report.checks.push('Held actual native mesh replies preserve active edits/caret and latest scroll; switching parts discards old tools, and cancellation stays closed after outstanding replies settle');
  }
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')).length),0);
}

// Confirm while an older actual preview reply is outstanding. This one case
// creates exactly one part in the host's freshly reset isolated test scene.
export async function runPunchRowAddApplyBrowserChecks(page,{idle,ready,screenshot,report}) {
  const add=page.locator('[data-cam-action="tube-designer-nesting-punch-create-add"]');
  await page.evaluate(()=>{
    const f=window.fixture;f.holdActualPreviewReplies=true;f.rowAddApplyEvents=[];
    const original=f.context.sceneProxy.invoke.bind(f.context.sceneProxy);
    f.context.sceneProxy.invoke=async(method,...args)=>{
      f.rowAddApplyEvents.push({method,event:'start',time:performance.now()});
      try{return await original(method,...args);}finally{f.rowAddApplyEvents.push({method,event:'end',time:performance.now()});}
    };
  });
  await add.click();await page.waitForFunction(()=>window.fixture.actualPreviewHolds?.length===1);
  await add.click();await add.click();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),3);
  await page.locator('[data-punch-region="main"] [data-cam-action$="-apply"]').click();
  assert.equal(await page.evaluate(()=>window.fixture.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')).length),0,
    'Confirmation waits for the running real preview before beginning final creation');
  await page.evaluate(()=>{const f=window.fixture;f.holdActualPreviewReplies=false;for(const hold of f.actualPreviewHolds)hold.release();});
  await page.waitForFunction(()=>!window.fixture.view.tubeDesignerPunchBatch);await idle();
  const result=await page.evaluate(()=>{
    const f=window.fixture;return {events:f.rowAddApplyEvents,creates:f.calls.filter(call=>call.method.endsWith('AddNestingPunchPart')),
      previews:f.calls.filter(call=>call.method.endsWith('PreviewPunchWizard')).map(call=>({features:call.payload.features.length})),
      backgroundSame:f.background===document.querySelector('#background'),backgroundInert:f.background.inert};
  });
  assert.equal(result.creates.length,1,'One final confirmation produces one native creation request');
  assert.equal(result.creates[0].payload.features.length,3,'Final creation contains all three current table rows');
  assert.equal(result.creates[0].scope,'scene');
  const creationStart=result.events.find(item=>item.method.endsWith('AddNestingPunchPart')&&item.event==='start');
  const previewEnd=result.events.filter(item=>item.method.endsWith('PreviewPunchWizard')&&item.event==='end').at(-1);
  assert(creationStart.time>=previewEnd.time,'The final native calculation starts after the old preview reply completes');
  assert(result.backgroundSame&&!result.backgroundInert);
  report.rowAddApply=result;report.checks.push('Confirm during a held actual native preview drains the previous job and creates exactly one current three-hole part through the scene channel');
  await screenshot('row-add-confirm-after-running-preview');
}
