// Exercises the rendered dialog with native browser inputs and the production styles.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { browserAssetPath, browserAssetRoot, importBrowserAsset } from "./browserPackageRuntime.mjs";
const { tubeDesignerCss } = await importBrowserAsset("apps/tube-designer/webpage/styles/tubeDesigner.css.mjs");

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const root = browserAssetRoot;
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://standard-part.test/**", (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body><div id='app' class='tube-designer-workspace'></div></body>" });
    const file = resolve(root, path.replace(/^\/src\//, ""));
    if (!path.startsWith("/src/") || !file.startsWith(root.replace(/[\\/]$/, "") + sep) || !/\.(mjs|js|css)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: path.endsWith('.css') ? 'text/css' : "text/javascript", body: readFileSync(browserAssetPath(path.slice(5)), "utf8") });
  });
  await page.goto("http://standard-part.test/");
  const commonCss = readFileSync(browserAssetPath("apps/_shared/workbench/styles/laser3dcam.css"), "utf8");
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,sans-serif}" + commonCss + tubeDesignerCss });
  const descriptor = JSON.parse(readFileSync(resolve('src/apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
  descriptor.parameters.push({key:'fixtureText',displayName:'文字草稿',valueType:'string',defaultValue:'',presentation:{advanced:true,line:'full'}});
  await page.evaluate(async descriptor => {
    const standard = await import("/src/apps/tube-designer/webpage/nestingStandardPart.mjs");
    const profile = (width = 40) => ({
      schema: "icax.imported-tube-profile", schemaVersion: 1, kind: "imported-dxf",
      name: "矩形管", width, depth: 20,
      contours: [
        { kind: "polygon", points: [[-width / 2, -10], [width / 2, -10], [width / 2, 10], [-width / 2, 10]] },
        { kind: "polygon", points: [[-width / 2 + 2, -8], [width / 2 - 2, -8], [width / 2 - 2, 8], [-width / 2 + 2, 8]] },
      ],
    });
    const program = {
      id: "rect", name: "程式矩形管", profileType: "parametric-package", profileForm: "parametric", previewProfile: profile(),
      defaultParameters: { width: 40, depth: 20, wallThickness: 2, cornerRadius: 2 },
      descriptor,
    };
    const calls = [];
    const view = {
      activeAreaId: "nesting", pending: false, scene: { tubeDesigner: { nestingGroups: [] } },
      tubeDesignerSystemProfiles: [program],
      tubeDesignerUserData: { profiles: [{ ...profile(24), id: "fixed", name: "我的定式管型" }] },
      tubeDesignerTemplateProfiles: [{ ...program, id: "template", templateId: "guard", name: "模板不可出现" }],
    };
    const context = {
      mount: document.querySelector('#app'),
      appProxy: { bridge: { async openFileDialog() { throw Error('Native file dialog must not be used'); }, async openDirectoryDialog() { return "D:\\Profiles"; } } },
      productProxy: { async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.ImportProfileDxf') {
          await new Promise(resolve => setTimeout(resolve, 60));
          if (payload.sourcePath.endsWith('invalid.dxf')) throw Error('DXF 截面没有封闭轮廓');
          return { sourcePath: payload.sourcePath, sizeBytes: 123, lastModifiedNs: '456', profile: { ...profile(60), name: "本地定式 DXF" } };
        }
        return { profile: profile(payload.parameters.width) };
      } },
      sceneProxy: { async invoke(method, payload) {
        calls.push({ method, payload: structuredClone(payload) });
        if (method === 'TubeDesigner.ListNestingPartFiles') return { directory: payload.directory || 'D:\\Profiles', parentDirectory: 'D:\\', entries: [
          { name:'fixture.dxf', path:'D:\\Profiles\\fixture.dxf', sizeBytes:123, lastModifiedNs:'456' },
          { name:'ellipse.dxf', path:'D:\\Profiles\\ellipse.dxf', sizeBytes:123, lastModifiedNs:'456' },
          { name:'invalid.dxf', path:'D:\\Profiles\\invalid.dxf', sizeBytes:123, lastModifiedNs:'456' },
          { name:'excluded.step', path:'D:\\Profiles\\excluded.step' },
        ] };
        return { partEntityId: "added-standard-part", tubeDesigner: { nestingGroups: [] } };
      } },
    };
    const { bindProfileParameterDiagrams } = await import('/src/apps/tube-designer/webpage/profileParameterDiagram.mjs');
    const ops = { renderProject() {
      const mount=document.querySelector('#app');
      standard.captureNestingStandardPartDisclosures(view,mount);
      const html=standard.renderNestingStandardPartDialog(view);
      if(!standard.patchNestingStandardPartDom(view,mount,html,context.sceneProxy)) {
        mount.innerHTML=html; standard.rememberNestingStandardPartDom(view,mount,context.sceneProxy);
      }
      bindProfileParameterDiagrams(mount);
    } };
    window.fixture = { view, calls, standard, context, ops };
    document.addEventListener("change", async (event) => {
      await standard.handleNestingStandardPartAction(context, view, event.target.dataset.camChangeAction ?? "", event.target, ops);
    });
    document.addEventListener("click", async (event) => {
      const target = event.target.closest("[data-cam-action]");
      if (target) await standard.handleNestingStandardPartAction(context, view, target.dataset.camAction, target, ops);
    });
    await standard.handleNestingStandardPartRibbonCommand(context, view, "nesting.add-standard-part", ops);
  }, descriptor);

  const control = (name) => page.locator(`[data-cam-change-action="tube-designer-nesting-standard-${name}"]`);
  const action = (name) => page.locator(`[data-cam-action="tube-designer-nesting-standard-${name}"]`);
  for (const size of [{ width: 1600, height: 1000 }, { width: 1024, height: 768 }, { width: 620, height: 700 }]) {
    await page.setViewportSize(size);
    assert.equal(await page.locator('option[value^="template:"]').count(), 0);
    assert.equal(await page.locator(".tube-nesting-standard-part-preview svg").count(), 1);
    const bounds = await page.locator('[role="dialog"]').evaluate((element) => {
      const rect = element.getBoundingClientRect();
      return { left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom, overflow: element.scrollWidth - element.clientWidth };
    });
    assert.ok(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= size.width && bounds.bottom <= size.height, JSON.stringify({ size, bounds }));
    assert.ok(bounds.overflow <= 2, JSON.stringify(bounds));
    const previewBounds = await page.locator(".tube-nesting-standard-part-preview").evaluate((element) => {
      const section = element.querySelector(".tube-nesting-standard-part-section, .td-profile-parameter-canvas").getBoundingClientRect();
      const svg = element.querySelector("svg").getBoundingClientRect();
      const caption = element.querySelector("figcaption, .td-profile-parameter-legend")?.getBoundingClientRect();
      return { sectionBottom: section.bottom, svgBottom: svg.bottom, captionTop: caption?.top ?? section.bottom };
    });
    assert.ok(previewBounds.svgBottom <= previewBounds.sectionBottom + 1 && previewBounds.svgBottom <= previewBounds.captionTop, JSON.stringify({ size, previewBounds }));
    assert.equal(await action("confirm").isVisible(), true);
    const appearance = await page.locator('[role="dialog"]').evaluate(dialog => {
      const header = dialog.querySelector('.tube-designer-dialog-header');
      const dxf = dialog.querySelector('[data-cam-action="tube-designer-nesting-standard-import-dxf"]');
      const cancel = dialog.querySelector('footer button');
      const style = getComputedStyle(dxf);
      return { headerHeight: header.getBoundingClientRect().height, headerText: header.innerText.replace('×', '').trim(),
        secondaryColor: style.color, secondaryWeight: style.fontWeight,
        sameSecondaryStyle: style.color === getComputedStyle(cancel).color && style.fontWeight === getComputedStyle(cancel).fontWeight,
        explanations: ['仅用于本次零件，不修改管型库', '添加后可在零件列表调整数量', '仅显示系统和我的管型'].filter(text => dialog.innerText.includes(text)),
        footerButtons: [...dialog.querySelectorAll('footer button')].map(button => button.innerText) };
    });
    assert.ok(appearance.headerHeight <= 50, JSON.stringify({ size, appearance }));
    assert.equal(appearance.headerText, '添加标准零件');
    assert.equal(appearance.secondaryColor, 'rgb(54, 87, 96)');
    assert.equal(appearance.secondaryWeight, '400');
    assert.equal(appearance.sameSecondaryStyle, true);
    assert.deepEqual(appearance.explanations, []);
    assert.deepEqual(appearance.footerButtons, ['取消', '添加零件']);
    const advanced = page.locator('[data-parameter-advanced]');
    await advanced.evaluate(node => node.open = true);
    const layout = await page.locator('.tube-nesting-standard-part-parameters').evaluate(element => {
      const controls = ['useOuterRadii', 'useInnerRadii'].map(key => element.querySelector(`[data-standard-part-parameter="${key}"]`));
      return controls.map(control => { const field = control.closest('label'), grid = field.closest('.tube-profile-library-field-grid');
        const rect = field.getBoundingClientRect(); return { key: control.dataset.standardPartParameter,
          full: field.classList.contains('is-line-full'), width: rect.width, gridWidth: grid.clientWidth,
          checkboxWidth: control.getBoundingClientRect().width, top: rect.top, bottom: rect.bottom }; });
    });
    assert(layout.every(item => item.full && Math.abs(item.width - item.gridWidth) < 3 && item.checkboxWidth === 16), JSON.stringify(layout));
    assert(layout[0].bottom <= layout[1].top, JSON.stringify(layout));
    if (process.env.ICAX_ARTIFACT_DIR) {
      mkdirSync(process.env.ICAX_ARTIFACT_DIR, { recursive: true });
      await page.screenshot({ path: resolve(process.env.ICAX_ARTIFACT_DIR, `nesting-standard-part-${size.width}.png`) });
    }
  }
  await page.setViewportSize({ width: 1024, height: 768 });
  assert.equal(await page.locator('[data-standard-part-parameter="outerRadius1"]').count(), 0);
  await page.locator('[data-standard-part-parameter="useOuterRadii"]').check();
  await page.waitForFunction(() => !window.fixture.view.pending && window.fixture.view.tubeDesignerNestingStandardPartDraft.parameters.useOuterRadii === true);
  assert.equal(await page.locator('[data-parameter-advanced]').getAttribute('open'), '', 'Editing must retain the expanded advanced section');
  assert.equal(await page.locator('[data-standard-part-parameter="outerRadius1"]').count(), 1);
  await page.locator('[data-standard-part-parameter="useOuterRadii"]').uncheck();
  await page.waitForFunction(() => !window.fixture.view.pending && window.fixture.view.tubeDesignerNestingStandardPartDraft.parameters.useOuterRadii === false);
  assert.equal(await page.locator('[data-standard-part-parameter="outerRadius1"]').count(), 0);
  assert.equal(await page.locator('[data-parameter-advanced]').getAttribute('open'), '');
  // An async response must retain the exact controls, diagram and latest
  // scroll/draft rather than reconstructing this dialog after a field edit.
  await page.setViewportSize({width:620,height:700});
  await page.evaluate(()=>{
    const mount=document.querySelector('#app'), dialog=mount.querySelector('.tube-nesting-standard-part-dialog');
    const body=dialog.querySelector('.tube-nesting-standard-part-body'), input=dialog.querySelector('[data-standard-part-parameter="fixtureText"]');
    const svg=dialog.querySelector('svg');
    input.value='request-time draft'; input.focus({preventScroll:true}); input.setSelectionRange(0,3); body.scrollTop=0;
    window.retained={input,svg,body,dialog,listeners:0};
    input.addEventListener('probe',()=>retained.listeners++);svg.addEventListener('probe',()=>retained.listeners++);
    window.responseRendered=false;
    window.pendingResponse=new Promise(resolve=>window.deliverResponse=resolve).then(()=>{
      fixture.ops.renderProject(); window.responseRendered=true;
    });
  });
  // Continue typing and scrolling while the earlier request is still pending.
  await page.locator('[data-standard-part-parameter="fixtureText"]').fill('1450 draft');
  await page.locator('[data-standard-part-parameter="fixtureText"]').evaluate(input=>input.setSelectionRange(2,5,'backward'));
  await page.locator('.tube-nesting-standard-part-body').evaluate(body=>{
    body.scrollTop=body.scrollHeight; retained.scroll=body.scrollTop;
  });
  await page.evaluate(()=>window.deliverResponse());
  await page.waitForFunction(()=>window.responseRendered);
  const retained = await page.evaluate(()=>{
    const r=retained, dialog=document.querySelector('.tube-nesting-standard-part-dialog');
    r.input.dispatchEvent(new Event('probe'));r.svg.dispatchEvent(new Event('probe'));
    return {dialog:r.dialog===dialog,input:r.input===document.activeElement,svg:r.svg===dialog.querySelector('svg'),body:r.body.isConnected,
      scroll:r.body.scrollTop,savedScroll:r.scroll,listeners:r.listeners,value:r.input.value,selection:[r.input.selectionStart,r.input.selectionEnd,r.input.selectionDirection],open:dialog.querySelector('[data-parameter-advanced]').open};
  });
  assert(retained.dialog&&retained.input&&retained.svg&&retained.body&&retained.open);
  assert.equal(retained.scroll,retained.savedScroll);assert.equal(retained.listeners,2);
  assert.equal(retained.value,'1450 draft'); assert.deepEqual(retained.selection,[2,5,'backward']);
  await page.locator('[data-standard-part-parameter="fixtureText"]').press('Tab');
  await page.waitForFunction(()=>!fixture.view.pending && fixture.view.tubeDesignerNestingStandardPartDraft.parameters.fixtureText==='1450 draft');
  await page.setViewportSize({width:1024,height:768});
  await page.locator('[data-standard-part-parameter="width"]').fill("55");
  await page.locator('[data-standard-part-parameter="width"]').press("Tab");
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.parameters.width === 55 && !window.fixture.view.pending);
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.profile.width), 55);
  await control("profile-select").selectOption("user:fixed");
  assert.equal(await page.locator("[data-standard-part-parameter]").count(), 0);
  await control("profile-select").selectOption("__dxf__");
  const picker = page.locator('.tube-nesting-file-picker');
  await picker.waitFor();
  assert.equal(await picker.locator('header strong').innerText(), '选择 DXF 截面');
  assert.equal(await picker.locator('[data-nesting-file-action="fit"]').count(), 0);
  assert.equal(await picker.locator('.tube-nesting-file-preview-caption').count(), 0);
  assert.equal(await picker.locator('[data-nesting-file-path]').count(), 3);
  assert.equal(await picker.locator('.tube-nesting-file-selection-hint').isVisible(), false);
  await picker.locator('[data-nesting-file-path$="fixture.dxf"]').click();
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  assert.equal(await picker.locator('[data-nesting-dxf-preview] .outer').count(), 1);
  assert.equal(await picker.locator('[data-nesting-dxf-preview] .hole').count(), 1);
  await picker.locator('[data-nesting-file-path$="ellipse.dxf"]').click({ modifiers:['Control'] });
  assert.equal(await picker.locator('[aria-selected="true"]').count(), 1, 'One section per standard part');
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  const cachedBefore = await page.evaluate(() => fixture.calls.filter(call => call.method === 'TubeDesigner.ImportProfileDxf').length);
  await picker.locator('[data-nesting-file-path$="fixture.dxf"]').click();
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  assert.equal(await page.evaluate(() => fixture.calls.filter(call => call.method === 'TubeDesigner.ImportProfileDxf').length), cachedBefore);
  const fitBox = await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox');
  await picker.locator('[data-nesting-dxf-preview]').hover();
  await page.mouse.wheel(0, -180);
  assert.notEqual(await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox'), fitBox);
  const zoomBox = await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox');
  await picker.locator('[data-nesting-dxf-preview] .hole').dblclick();
  assert.equal(await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox'), zoomBox, 'Double clicking geometry must keep the current view');
  await picker.locator('[data-nesting-dxf-preview]').dblclick({position:{x:12,y:12},button:'right'});
  assert.equal(await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox'), zoomBox, 'Only left double click restores the view');
  await picker.locator('[data-nesting-dxf-preview]').dblclick({position:{x:12,y:12},button:'left'});
  assert.equal(await picker.locator('[data-nesting-dxf-preview]').getAttribute('viewBox'), fitBox);
  assert.equal(await page.evaluate(() => fixture.calls.filter(call => call.method === 'TubeDesigner.AddNestingStandardPart').length), 0);
  await picker.locator('footer [data-nesting-file-action="cancel"]').click();
  assert.equal(await page.evaluate(() => fixture.view.tubeDesignerNestingStandardPartDraft.importedProfile), null);
  await action('import-dxf').click();
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  assert.equal(await picker.locator('[data-nesting-file-directory]').inputValue(), 'D:\\Profiles');
  await picker.locator('[data-nesting-file-path$="invalid.dxf"]').click();
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'error');
  assert.match(await picker.locator('[data-nesting-file-preview-status]').innerText(), /没有封闭轮廓/);
  await picker.locator('[data-nesting-file-path$="fixture.dxf"]').click();
  await page.waitForFunction(() => fixture.view.tubeDesignerNestingFilePicker.previewStatus === 'ready');
  await picker.locator('[data-nesting-file-action="open"]').click();
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft.importedProfile?.name === "本地定式 DXF");
  await control("length").fill("875");
  await action("confirm").click();
  await page.waitForFunction(() => window.fixture.view.tubeDesignerNestingStandardPartDraft === null);
  const submission = await page.evaluate(() => window.fixture.calls.find((call) => call.method === "TubeDesigner.AddNestingStandardPart"));
  assert.equal(submission.payload.length, 875);
  assert.equal(submission.payload.profile.name, "本地定式 DXF");
  assert.equal(await page.evaluate(() => window.fixture.view.tubeDesignerActiveNestingPartId), "added-standard-part");
  assert.deepEqual(errors, []);
  if (process.env.ICAX_ARTIFACT_DIR) writeFileSync(resolve(process.env.ICAX_ARTIFACT_DIR, 'report.json'), JSON.stringify({ passed: true,
    layouts: ['1600x1000', '1024x768', '620x700'], compactHeader: true, secondaryButtonStyle: true, redundantHintsRemoved: true,
    configuredFullRowSwitches: true, advancedGroupingAndConditions: true, retainedDialogControlsAndDiagram:true, retainedAdvancedDisclosureAndScroll:true, retainedLatestAsyncDraftAndSelection:true,
    editableProfileParameters: true, dxfImport: true, dxfSvgPreview: true, dxfCacheAndErrorRetry: true, dxfBlankDoubleClickFit:true, dxfRedundantFitControlsRemoved:true,
    dxfCancelAndDirectoryMemory: true, confirmedSceneSubmission: true, nativeTransport: 'controlled' }, null, 2));
  console.log("Nesting standard part browser: 3 layouts, eligible choices, editable parameters, DXF and confirmed selection passed.");
} finally {
  await browser.close();
}
