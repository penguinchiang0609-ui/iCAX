import assert from 'node:assert/strict';

// Run against the production batch fixture. Only preview/resource calls are
// allowed here; a compact layout or draft edit must never create a part.
export async function runPunchProfileAdvancedBrowserChecks(page, { idle, ready, screenshot, report }) {
  const popup = page.locator('[data-punch-profile-advanced-dialog]');
  const opener = page.locator('[data-punch-profile-advanced-open]');
  const input = key => popup.locator('[data-tube-designer-main-profile-parameter="' + key + '"]');
  const counts = () => page.evaluate(() => ({
    calls: window.fixture.calls.length,
    previews: window.fixture.calls.filter(call => call.method.endsWith('PreviewPunchWizard')).length,
    evaluations: window.fixture.calls.filter(call => call.method.endsWith('EvaluateProfilePackage')).length,
    creates: window.fixture.calls.filter(call => call.method.endsWith('AddNestingPunchPart')).length,
  }));
  await page.locator('[data-tube-designer-nesting-punch-field="profileKey"]').selectOption('system:rect');
  await ready();
  await page.evaluate(() => {
    const f = window.fixture;
    f.advancedBaseline = JSON.stringify(f.view.tubeDesignerPunchBatch.profileParameters);
    f.advancedMainInput = document.querySelector('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="width"]');
    f.advancedCanvas = f.viewport.renderer.domElement;
    f.advancedOpener = document.querySelector('[data-punch-profile-advanced-open]');
  });
  assert.equal(await page.locator('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="useOuterRadii"]').count(), 0,
    'Advanced controls are separate from the always-visible basic fields');
  const before = await counts();
  await opener.click(); await idle();
  assert.equal(await popup.count(), 1);
  assert(await popup.evaluate(node => node.contains(document.activeElement)), 'Opening the nested modal moves focus inside it');
  assert(await input('useOuterRadii').isVisible(),'Advanced draft controls are immediately visible after opening');
  await input('useOuterRadii').evaluate(control => window.fixture.advancedCheckbox = control);
  await input('useOuterRadii').check(); await idle();
  assert.equal(await input('outerRadius1').count(), 1);
  assert(await input('useOuterRadii').evaluate(control => control === window.fixture.advancedCheckbox && control === document.activeElement));
  await input('useInnerRadii').check(); await idle();
  await input('outerRadius1').fill('4');
  await input('outerRadius1').evaluate(control => control.dispatchEvent(new Event('change', { bubbles: true }))); await idle();
  assert(await page.evaluate(() => JSON.stringify(window.fixture.view.tubeDesignerPunchBatch.profileParameters) === window.fixture.advancedBaseline),
    'Editing a popup draft leaves shared main parameters untouched');
  assert.deepEqual(await counts(), before, 'Popup opening, conditional switches and draft edits call no native business method');
  await input('outerRadius1').fill('-1');
  await input('outerRadius1').evaluate(control=>control.dispatchEvent(new Event('change',{bubbles:true})));await idle();
  await popup.locator('[data-cam-action$="batch-profile-advanced-apply"]').click();await idle();
  assert.equal(await popup.count(),1,'An invalid visible field keeps its draft modal open');
  assert(await popup.locator('[role="alert"]').count(),'Visible invalid fields show their error inside the popup');
  assert.deepEqual(await counts(),before,'Invalid draft confirmation cannot reach native generation');
  await input('outerRadius1').fill('4');
  await input('outerRadius1').evaluate(control=>control.dispatchEvent(new Event('change',{bubbles:true})));await idle();
  report.checks.push('Actual advanced draft controls are visible; switches edit only the draft; invalid visible confirmation remains local');
  const expected = ['useOuterRadii','outerRadius1','outerRadius2','outerRadius3','outerRadius4',
    'useInnerRadii','innerRadius1','innerRadius2','innerRadius3','innerRadius4','innerOffsetX','innerOffsetY'];
  assert.deepEqual(await popup.locator('[data-tube-designer-main-profile-parameter]').evaluateAll(nodes => nodes.map(node => node.dataset.tubeDesignerMainProfileParameter)), expected);
  report.advancedLayout = [];
  for (const width of [1600,1280,1024]) {
    await page.setViewportSize({ width, height: 900 });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    const layout = await popup.evaluate(node => {
      const box = node.getBoundingClientRect();
      const fields = [...node.querySelectorAll('[data-tube-designer-main-profile-parameter]')].map(control => {
        const r = control.closest('.tube-designer-field').getBoundingClientRect();
        return { key: control.dataset.tubeDesignerMainProfileParameter, left:r.left, right:r.right, top:r.top, bottom:r.bottom };
      });
      return { left:box.left, right:box.right, top:box.top, bottom:box.bottom, fields };
    });
    assert(layout.left >= 0 && layout.right <= width + 1 && layout.top >= 0 && layout.bottom <= 900,
      'Advanced popup stays in the viewport at ' + width);
    for (let a=0;a<layout.fields.length;a++) for(let b=a+1;b<layout.fields.length;b++) {
      const x=layout.fields[a],y=layout.fields[b];
      assert(!(Math.min(x.right,y.right)-Math.max(x.left,y.left)>1 && Math.min(x.bottom,y.bottom)-Math.max(x.top,y.top)>1),
        'Advanced descriptor fields do not overlap at ' + width);
    }
    for (const key of ['useOuterRadii','useInnerRadii']) {
      const index=layout.fields.findIndex(field=>field.key===key);
      assert(layout.fields[index+1].top >= layout.fields[index].bottom-1, 'Template full-line checkbox stays on its own line');
    }
    report.advancedLayout.push({ width, ...layout });
    await screenshot('advanced-profile-' + width);
  }
  report.checks.push('Advanced template order and full-line booleans retained; 1600/1280/1024 popup fields have no overlap or viewport overflow');
  // Modal tab/escape stay local; returning from a nested popup preserves the
  // existing batch, its opener and canvas instead of closing the whole wizard.
  await popup.evaluate(node => {
    const controls=[...node.querySelectorAll('button,input,select,textarea,[tabindex]')]
      .filter(control=>!control.disabled&&!control.closest('[inert]')&&control.tabIndex>=0&&control.getClientRects().length);
    window.fixture.advancedFirst=controls[0];window.fixture.advancedLast=controls.at(-1);controls.at(-1).focus();
  });
  await page.keyboard.press('Tab');
  assert(await page.evaluate(()=>document.activeElement===window.fixture.advancedFirst));
  await page.keyboard.press('Shift+Tab');
  assert(await page.evaluate(()=>document.activeElement===window.fixture.advancedLast));
  await page.keyboard.press('Escape'); await idle();
  assert.equal(await popup.count(),0);
  assert.equal(await page.locator('.tube-designer-punch-batch-dialog').count(),1);
  assert(await opener.evaluate(node=>node===window.fixture.advancedOpener&&node===document.activeElement));
  assert(await page.evaluate(()=>JSON.stringify(window.fixture.view.tubeDesignerPunchBatch.profileParameters)===window.fixture.advancedBaseline));
  assert.deepEqual(await counts(),before,'Nested Escape discards its draft without creating or previewing');
  for(const suffix of ['cancel','close']) {
    await opener.click();await idle();await input('useOuterRadii').check();await idle();
    if(suffix==='close')await popup.locator('.tube-designer-dialog-close').click();
    else await popup.locator('[data-cam-action$="batch-profile-advanced-cancel"]').last().click();
    await idle();assert.equal(await popup.count(),0);
    assert(await opener.evaluate(node=>node===document.activeElement));
    assert(await page.evaluate(()=>JSON.stringify(window.fixture.view.tubeDesignerPunchBatch.profileParameters)===window.fixture.advancedBaseline));
  }
  assert.deepEqual(await counts(),before,'Cancel and close also discard all advanced drafts');
  report.checks.push('Nested Tab trap and Escape/Cancel/close preserve the batch, discard drafts and return focus to the original opener');
  await opener.click();await idle();
  await popup.locator('[data-cam-action$="batch-profile-advanced-apply"]').click();await idle();
  assert.deepEqual(await counts(),before,'Confirming an unchanged draft does not regenerate');
  await opener.click();await idle();
  await input('useOuterRadii').check();await idle();await input('useInnerRadii').check();await idle();
  await input('outerRadius1').fill('4');
  await input('outerRadius1').evaluate(control=>control.dispatchEvent(new Event('change',{bubbles:true})));await idle();
  await popup.locator('[data-cam-action$="batch-profile-advanced-apply"]').click();await ready();
  assert.equal(await popup.count(),0);
  const after=await counts();assert.equal(after.previews,before.previews+1,'One changed draft confirmation generates exactly one scene preview');
  assert.equal(after.evaluations,before.evaluations+1,'A changed draft evaluates its shared profile exactly once');
  assert.equal(after.creates,before.creates,'Advanced confirmation never creates a finished part');
  assert(await opener.evaluate(node=>node===document.activeElement),'Changed draft confirmation returns focus after native preview unlocks');
  assert(await page.evaluate(()=>{
    const f=window.fixture,p=f.view.tubeDesignerPunchBatch.profileParameters['system:rect'];
    return p.useOuterRadii===true&&p.useInnerRadii===true&&Number(p.outerRadius1)===4
      &&f.advancedMainInput===document.querySelector('[data-punch-region="main"] [data-tube-designer-main-profile-parameter="width"]')
      &&f.advancedCanvas===f.viewport.renderer.domElement;
  }),'Confirmed parameters update once while the original basic input and canvas stay mounted');
  report.checks.push('Unchanged confirmation skips native calls; changed confirmation evaluates once and previews once with the same basic input/canvas and opener focus');
  await opener.click();await idle();
  assert(await input('useOuterRadii').isChecked()&&await input('useInnerRadii').isChecked(),'Reopening starts from the confirmed parameters');
  assert(!await page.locator('[data-tube-designer-punch-viewport]').getByRole('button',{name:'适合窗口',exact:true}).count());
  await screenshot('advanced-profile-confirmed');
  // Simulate a constrained popup body, then keep typing and scrolling after
  // an earlier resource request began. The reply must use this current state.
  await page.addStyleTag({content:'.punch-batch-profile-advanced-body{max-height:96px!important;overflow-y:auto!important}'});
  await page.evaluate(()=>{
    const f=window.fixture,ref=f.view.tubeDesignerPunchWizard.preview.baseGeometry;
    f.deferResource=true;f.advancedResourceReply=f.context.sceneProxy.resources.get(ref.url,
      {headers:new Headers({'ICAX-Resource-Version':String(ref.version)})}).then(response=>response.arrayBuffer()).then(()=>f.render());
  });
  await page.waitForFunction(()=>typeof window.fixture.releaseResource==='function');
  await input('outerRadius3').evaluate(control=>{
    const f=window.fixture,body=document.querySelector('.punch-batch-profile-advanced-body');
    f.advancedTyping=control;control.focus({preventScroll:true});control.value='5';
    body.scrollTop=36;f.advancedScroll=body.scrollTop;f.advancedScrollNode=body;f.releaseResource();
  });
  await page.evaluate(()=>window.fixture.advancedResourceReply);await idle();
  assert(await input('outerRadius3').evaluate(control=>{
    const f=window.fixture,body=document.querySelector('.punch-batch-profile-advanced-body');
    return control===f.advancedTyping&&control===document.activeElement&&control.value==='5'
      &&body===f.advancedScrollNode&&body.scrollTop===f.advancedScroll&&f.advancedScroll>0
      &&f.advancedCanvas===f.viewport.renderer.domElement;
  }),'Late resources preserve the same popup input, unsubmitted text, latest scroll, focus and canvas');
  report.checks.push('Late preview resources preserve the current popup input/value/focus, latest nested scroll and original WebGL canvas');
  await popup.locator('[data-cam-action$="batch-profile-advanced-cancel"]').last().click();await idle();
  await opener.click();await idle();
  await input('outerRadius1').fill('-5');
  await input('outerRadius1').evaluate(control=>control.dispatchEvent(new Event('change',{bubbles:true})));await idle();
  await input('useOuterRadii').uncheck();await idle();
  assert.equal(await input('outerRadius1').count(),0,'Disabling the parent hides its retained draft fields');
  await popup.locator('[data-cam-action$="batch-profile-advanced-apply"]').click();await ready();
  assert.equal(await popup.count(),0,'An inactive hidden draft cannot block valid confirmation');
  assert.equal((await counts()).previews,after.previews+1,'The hidden-field case still previews exactly once');
  assert(await page.evaluate(()=>{
    const p=window.fixture.view.tubeDesignerPunchBatch.profileParameters['system:rect'];
    return p.useOuterRadii===false&&p.outerRadius1===-5;
  }),'Turning off a parent preserves its inactive draft values');
  report.checks.push('Inactive out-of-range radius drafts remain stored; disabling their parent allows exactly one real preview without creating a part');
}
