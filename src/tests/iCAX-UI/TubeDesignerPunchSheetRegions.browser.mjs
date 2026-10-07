// Four independently scrollable regions with inline parameter transactions and a real Three viewport.
// Native computation alone is replaced with deterministic local mesh resources.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, readdirSync, writeFileSync, existsSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const sourceRoot = fileURLToPath(new URL("../../", import.meta.url)).replace(/[\\/]$/, "");
const assetRoot = resolve(process.env.ICAX_PUNCH_ASSET_ROOT || sourceRoot);
const densityBaseline = process.env.ICAX_PUNCH_DENSITY_BASELINE === '1';
const artifacts = resolve(process.env.ICAX_PUNCH_ARTIFACTS || resolve(sourceRoot, "../output/tests/punch-sheet-regions-browser"));
const catalogue = readdirSync(resolve(sourceRoot, "apps/tube-designer/templates/mold"), {withFileTypes:true})
  .filter(x=>x.isDirectory()&&existsSync(resolve(sourceRoot,"apps/tube-designer/templates/mold",x.name,"tool.json"))).map(x=>JSON.parse(readFileSync(resolve(sourceRoot,"apps/tube-designer/templates/mold",x.name,"tool.json"),"utf8")))
  .map(d=>({...d,digest:"regions-fixture",defaultParameters:Object.fromEntries((d.parameters??[]).map(p=>[p.key,p.defaultValue]))}));
const profiles = ["rect","round"].map(id=>{
  const descriptor=JSON.parse(readFileSync(resolve(sourceRoot,"apps/tube-designer/templates/profile",id,"profile.json"),"utf8"));
  const displayPath=resolve(sourceRoot,"apps/tube-designer/templates/profile",id,"display.json");
  if(existsSync(displayPath))descriptor.display=JSON.parse(readFileSync(displayPath,"utf8"));
  return {id,name:descriptor.displayName,profileType:"parametric-package",descriptor,
    defaultParameters:Object.fromEntries(descriptor.parameters.map(p=>[p.key,p.defaultValue])),
    previewProfile:{kind:id,name:descriptor.displayName,width:60,depth:40,diameter:40,wallThickness:2,
      parameterDiagram:{schemaVersion:1,annotations:[{parameter:'width',kind:'linear',axis:'x',side:'top',from:[0,0],to:[60,0]}]},
      contours:[{kind:'path',closed:true,segments:[{kind:"line",start:[0,0],end:[60,0]},{kind:"line",start:[60,0],end:[60,40]},{kind:"line",start:[60,40],end:[0,40]},{kind:"line",start:[0,40],end:[0,0]}]}]}};
});
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
mkdirSync(artifacts, { recursive: true });
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
let page;
try {
  page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  page.setDefaultTimeout(15000);
  const errors = [], externalRequests = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== "http://punch-window.test") { externalRequests.push(url.href); return route.abort(); }
    if (url.pathname === "/") return route.fulfill({ contentType: "text/html", body: '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><body><div id="app" class="tube-designer-workspace"></div></body></html>' });
    const file = resolve(assetRoot, url.pathname.replace(/^\/src\//, ""));
    if (!url.pathname.startsWith("/src/") || !file.startsWith(assetRoot + sep) || !/\.(mjs|js)$/.test(file)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://punch-window.test/");
  const css = assetRoot === sourceRoot ? tubeDesignerCss : (await import(new URL('file:///' + assetRoot.replace(/\\/g, '/') + '/apps/tube-designer/webpage/styles/tubeDesigner.css.mjs'))).tubeDesignerCss;
  await page.addStyleTag({ content: "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,Arial,sans-serif}" + readFileSync(resolve(assetRoot, "apps/_shared/workbench/styles/laser3dcam.css"), "utf8") + css });
  await page.evaluate(async ({catalogue,profiles}) => {
    const wizard = await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const creation = await import("/src/apps/tube-designer/webpage/nestingPunchPart.mjs");
    const editor = await import("/src/apps/tube-designer/webpage/punchEditor.mjs");
    const { patchPunchDom } = await import("/src/apps/tube-designer/webpage/punchDomPatch.mjs");
    const { renderDesignerOperationOverlay } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { ThreeRenderViewport } = await import("/src/iCAX-UI/SDK/Viewport/threeViewport.mjs");
    const THREE = await import("/src/iCAX-UI/SDK/ThirdParty/three/three.module.js");
    const { encodeNestingGeometry, encodePreviewMaterial } = await import("/src/apps/tube-designer/webpage/nestingPreview.mjs");
    const profile = profiles[0];
    const draft = { profileKey: "system:rect", length: "1000", name: "非模态窗口测试", quantity: "1", material: "", error: "" };
    const state = wizard.createPunchWizardState({ entityId: "__new_nesting_punch_part__", length: 1000, name: draft.name, profile: profile.previewProfile });
    state.creationMode = "main-tube-punch";
    state.creationInput = { draft, profileParameters: { "system:rect": structuredClone(profile.defaultParameters) } };
    await Promise.resolve();
    wizard.installPunchCatalogue(state, { tools: catalogue });
    if (state.draft.toolRef?.id !== "circle") throw new Error("The new draft did not bind the current circle catalogue asynchronously.");
    const unreferenced = wizard.createPunchWizardState({ length: 1000, properties: {
      "tubeDesigner.punchWizard": { features: [{ id: "unreferenced-saved", type: "circle", diameter: 24 }],
        ends: { start: { type: "miter", angle: 30 }, end: { type: "keep" } } },
    } });
    const beforeSaved = JSON.stringify({ features: unreferenced.features, ends: unreferenced.ends });
    wizard.installPunchCatalogue(unreferenced, { tools: catalogue });
    if (JSON.stringify({ features: unreferenced.features, ends: unreferenced.ends }) !== beforeSaved
        || !/请选择刀具模板/.test(wizard.validatePunchWizard({ tubeDesignerPunchWizard: unreferenced }, { length: 1000 })))
      throw new Error("A saved record without an explicit tool reference was migrated or accepted.");
    unreferenced.features = [];
    if (!/请选择端部刀具模板/.test(wizard.validatePunchWizard({ tubeDesignerPunchWizard: unreferenced }, { length: 1000 })))
      throw new Error("An unreferenced saved end was accepted.");
    const feature = wizard.normalizePunchFeature({ recordKind: "tool", layoutDatum: "base", face: "top", station: 300, arrayCount: 1 });
    wizard.selectPunchTool(state, feature, "circle"); feature.toolParameters.diameter = 12;
    state.features = [feature]; state.draft = structuredClone(feature); state.draft.id += "-draft"; state.draft.station = 600;
    const view = { pending: false, activeAreaId: "nesting", scene: { tubeDesigner: {} }, tubeDesignerPunchWizard: state,
      tubeDesignerNestingPunchPartDraft: draft, tubeDesignerSystemProfiles: profiles, tubeDesignerUserData: { profiles: [] }, tubeDesignerTemplateProfiles: [] };
    const f = window.fixture = { view, wizard, catalogue, profiles, pending: 0, calls: [], failures: [], resources: [], patches: 0 };
    const originalMount = ThreeRenderViewport.prototype.mount;
    ThreeRenderViewport.prototype.mount = function (...args) { f.viewport = this; return originalMount.apply(this, args); };
    const meshes = new Map(), material = encodePreviewMaterial(0x55a8baff), toolMaterial = encodePreviewMaterial(0xf0a630ff);
    const boxBytes = (length, width, depth, x) => {
      const box = new THREE.BoxGeometry(length, width, depth).toNonIndexed(); box.translate(x, 0, 35);
      const bytes = encodeNestingGeometry({ positions: [...box.attributes.position.array], indices: Array.from({ length: box.attributes.position.count }, (_, i) => i) });
      box.dispose(); return bytes;
    };
    meshes.set("blank", boxBytes(1000, 80, 80, 500));
    const mount = document.querySelector("#app");
    const context = { mount, sceneProxy: { async invoke(method, payload) {
      if(method==='TubeDesigner.GenerateProfilePreview')return {profile:structuredClone(profiles.find(p=>p.id===payload.profileRef.id).previewProfile)};
      if (method !== "TubeDesigner.PreviewPunchWizard") throw new Error("Unexpected fixture operation: " + method);
      f.calls.push(structuredClone(payload));
      const serial = f.calls.length, item = payload.features[0];
      const key = "tools-" + serial;
      meshes.set(key, boxBytes(Number(item?.toolParameters?.diameter??item?.toolParameters?.spanAlong??20), 110, 110, Number(item?.station??100)));
      // A controllable completion gate exercises camera ownership across an
      // outstanding response. It does not measure native execution time.
      if (f.deferOnePreview) { f.deferOnePreview = false; await new Promise(resolve => { f.releasePreview = resolve; }); f.releasePreview = null; }
      return { toolsOnly: true, baseGeometry: { url: "blank", version: 1 }, baseMaterial: { url: "material", version: 1 },
        toolGeometry: { url: key, version: 1 }, toolMaterial: { url: "tool-material", version: 1 },
        length: 1000, baseBounds: { min: [0, -40, -40], max: [1000, 40, 40] }, toolCount: 1, toolCountExact: false };
    }, resources: { async get(url) {
      f.resources.push(url); const bytes = url === "material" ? material : url === "tool-material" ? toolMaterial : meshes.get(url);
      if (!bytes) throw new Error("Unexpected fixture resource: " + url); return new Response(bytes);
    } } } };
    const ops = { renderProject() {
      const html = creation.renderNestingPunchPartDialog(view) + renderDesignerOperationOverlay(context, view);
      if (patchPunchDom(view, mount, html)) f.patches++; else mount.innerHTML = html;
      editor.attachPunchEditor(context, view, mount, ops);
    } };
    f.render = ops.renderProject;
    f.dispatch = async (action, target) => {
      if (!action?.startsWith("tube-designer-nesting-punch-create-")) return;
      f.pending++;
      try { await creation.handleNestingPunchPartAction(context, view, action, target, ops); }
      catch (error) { f.failures.push(error.message); } finally { f.pending--; }
    };
    document.addEventListener("change", event => void f.dispatch(event.target.dataset.camChangeAction, event.target));
    document.addEventListener("click", event => { const target = event.target.closest("[data-cam-action]"); if (target && !target.disabled) void f.dispatch(target.dataset.camAction, target); });
    f.render();
  }, {catalogue,profiles});
  const region=name=>page.locator('[data-punch-region="'+name+'"]');
  const inline=page.locator('[data-punch-inline-editor]');
  const idle=async()=>{
    await page.waitForFunction(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard;return !f.pending&&!f.view.pending&&!s?.previewPending&&!s?.previewRenderPending&&!s?.uiPendingClick;});
    assert.deepEqual(await page.evaluate(()=>window.fixture.failures),[]);
  };
  const frame=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  const row=index=>page.locator('[data-tube-designer-punch-row="'+index+'"]');
  const open=async(index,mode='shape')=>{
    if(mode==='shape')await row(index).locator('[data-punch-record-column="shape"]').click();
    else {const button=region('holes').locator('[data-punch-column-group="'+mode+'"]').first();if(await button.getAttribute('aria-expanded')==='false')await button.click();}
    await idle();
  };
  const checkShapeRow=async(index,end='')=>{
    const mother=end?page.locator('[data-tube-designer-punch-end-row="'+end+'"]'):row(index);
    assert.equal(await inline.evaluate(n=>n.closest('tr').previousElementSibling.dataset.tubeDesignerPunchRow??n.closest('tr').previousElementSibling.dataset.tubeDesignerPunchEndRow),String(end||index));
    assert.ok(await inline.evaluate(n=>n.closest('tr').hasAttribute('data-punch-inline-row')),'Shape parameters expand below their mother row');
    assert.equal(await inline.evaluate(n=>getComputedStyle(n).backgroundColor),await mother.evaluate(n=>getComputedStyle(n).backgroundColor));
    const width=await inline.evaluate(n=>n.getBoundingClientRect().width);
    const visible=await region(end?'ends':'holes').boundingBox();
    assert.ok(width>=visible.width-25&&width<=visible.width+1,'Expanded shape editor uses the entire visible table width');
    assert.equal(await page.locator('[data-punch-cell-editor]').count(),0,'No parameter form is squeezed inside a table cell');
  };
  const checkColumns=async(index,mode)=>{
    assert.ok(await row(index).locator('[data-punch-record-column^="'+mode+':"]').count()>3,'Parameter group expands into separate columns');
    assert.equal(await page.locator('[data-punch-cell-editor]').count(),0);
    assert.equal(await row(index).locator('[data-punch-record-column^="'+mode+':"] input,[data-punch-record-column^="'+mode+':"] select').count()>0,true,'Fields edit directly in data cells');
  };
  const resize=async(pair,delta)=>{
    const box=await page.locator('[data-punch-region-splitter="'+pair+'"]').boundingBox();
    await page.mouse.move(box.x+box.width/2,box.y+3);await page.mouse.down();
    await page.mouse.move(box.x+box.width/2,box.y+3+delta,{steps:8});await page.mouse.up();await frame();
  };
  const sizes=()=>page.evaluate(()=>Object.fromEntries([...document.querySelectorAll('[data-punch-region]')].map(n=>[n.dataset.punchRegion,n.getBoundingClientRect().height])));
  await page.evaluate(()=>window.fixture.dispatch('tube-designer-nesting-punch-create-preview',{}));await idle();
  await page.waitForFunction(()=>document.querySelector('[data-tube-designer-punch-viewport]')?.dataset.punchPreviewReady==='true');
  assert.equal(await page.locator('[data-punch-region]').count(),4);
  assert.equal(await page.locator('[data-punch-region-splitter]').count(),3);
  assert.equal(await region('main').locator(':scope > header').count(),0,'Main fields need no redundant region heading');
  assert.equal(await region('ends').locator('.tube-designer-punch-ends > header').count(),0,'The end table is identified by its own data columns');
  assert.equal(await region('holes').locator('.tube-designer-punch-sheet > header').count(),0,'Hole actions share the existing window title row');
  assert.equal(await page.locator('.tube-designer-dialog-header .tube-designer-punch-sheet-toolbar').count(),1);
  assert.equal(await page.locator('[data-tube-designer-punch-end-row]').count(),2);
  assert.equal(await page.locator('[data-cam-action$="-cancel"]').count(),2);
  assert.equal(await page.locator('[data-cam-action$="-apply"]').count(),1);
  assert.equal(await page.locator('.tube-designer-punch-footer button').allTextContents().then(x=>x.join(',')),'取消,确定');
  const closeButton=page.getByRole('button',{name:'关闭向导',exact:true});
  assert.equal(await closeButton.count(),1);
  assert.equal(await region('main').locator('details[data-profile-display-group]:not([data-parameter-advanced])').count(),0,'Basic dimensions always display without a disclosure');
  assert.equal(await region('main').locator('[data-profile-display-group="dimensions"] > summary').count(),0);
  assert.equal(await region('main').locator('[data-tube-designer-main-profile-parameter="width"]').isVisible(),true);
  assert.equal(await page.locator('button[data-cam-action$="parameters-open"]').count(),0,'Parameter cells need no separate edit buttons');
  assert.deepEqual(await region('ends').locator('thead th').allTextContents(),['端面','加工来源','切形 / 截面','参数']);
  const boxes=await Promise.all(['main','ends','holes','scene'].map(async name=>({name,...await region(name).boundingBox()})));
  for(let i=1;i<boxes.length;i++){
    assert.ok(boxes[i].y>=boxes[i-1].y+boxes[i-1].height);
    assert.ok(Math.abs(boxes[i].x-boxes[0].x)<1&&Math.abs(boxes[i].width-boxes[0].width)<1,'Full width vertically stacked regions');
  }
  const beforeResize=await sizes();
  const basicBoxes=await region('main').locator('.tube-profile-library-field-grid > label:not(.is-line-full)').evaluateAll(nodes=>nodes.map(n=>({key:n.querySelector('input')?.dataset.tubeDesignerMainProfileParameter,y:n.getBoundingClientRect().y,left:n.getBoundingClientRect().x,right:n.getBoundingClientRect().right})).filter(n=>['width','depth','wallThickness','cornerRadius','innerRadius'].includes(n.key)));
  assert.equal(basicBoxes.length,5,'All five basic dimensions remain present');
  assert.ok(basicBoxes.every(n=>Math.abs(n.y-basicBoxes[0].y)<1),'Five basic dimension fields fit on one line in a wide window');
  assert.deepEqual(basicBoxes.map(n=>n.key),['width','depth','wallThickness','cornerRadius','innerRadius'],'Template field order retained');
  assert.ok(basicBoxes.at(-1).right-basicBoxes[0].left<=800,'Basic parameters stay compact instead of spreading over the entire window');
  const compactControlGaps=async(scope)=>scope.locator('.tube-profile-library-field-grid .tube-designer-field').evaluateAll(nodes=>nodes.map(n=>{
    const label=n.querySelector(':scope > span,:scope > small'),control=n.querySelector(':scope > input,:scope > select,:scope > .tube-designer-punch-sheet-number');
    return label&&control?{gap:control.getBoundingClientRect().left-label.getBoundingClientRect().right,width:n.getBoundingClientRect().width,overflow:control.getBoundingClientRect().right-n.getBoundingClientRect().right}:null;
  }).filter(Boolean));
  assert.ok((await compactControlGaps(region('main'))).every(n=>n.gap>=0&&n.gap<=8),'Labels stay next to their controls');
  assert.ok((await compactControlGaps(region('main'))).every(n=>n.overflow<=1),'Compact main controls stay inside their own fields');
  const titleToolbar=await page.locator('.tube-designer-dialog-header .tube-designer-punch-sheet-toolbar').boundingBox();
  const titleClose=await page.getByRole('button',{name:'关闭向导',exact:true}).boundingBox();
  assert.ok(titleClose.x-(titleToolbar.x+titleToolbar.width)<20,'Hole actions align beside the existing header close button');
  await page.screenshot({path:resolve(artifacts,'00-initial-four-regions.png')});
  const diagram=page.locator('[data-library-floating-diagram="punch-section"]');
  const diagramBox=()=>diagram.boundingBox();
  const dragDiagram=async(selector,dx,dy)=>{
    const box=await diagram.locator(selector).boundingBox();
    const x=box.x+Math.min(15,box.width/2),y=box.y+box.height/2;
    await page.mouse.move(x,y);await page.mouse.down();await page.mouse.move(x+dx,y+dy,{steps:6});await page.mouse.up();await frame();
  };
  await region('main').locator('[data-cam-action$="section-diagram"]').click();await idle();
  assert.equal(await diagram.getAttribute('aria-modal'),'false');
  assert.equal(await region('main').locator('[data-profile-parameter-diagram]').count(),0,'Diagram opens outside the form scroll region');
  assert.equal(await diagram.locator('[data-profile-annotation-key="width"]').count(),1);
  await region('main').locator('[data-tube-designer-nesting-punch-field="name"]').fill('拖动浮窗时继续输入');
  await page.evaluate(()=>{const n=document.activeElement;n.setSelectionRange(2,5);window.fixture.diagramTyping=n;});
  await dragDiagram('[data-floating-diagram-drag]',-220,180);
  await dragDiagram('[data-diagram-resize="se"]',80,40);
  assert.ok(await page.evaluate(()=>{const n=window.fixture.diagramTyping;return n===document.activeElement&&n.value==='拖动浮窗时继续输入'&&n.selectionStart===2&&n.selectionEnd===5;}),'Dragging or resizing the diagram preserves the unsubmitted input and its caret');
  await page.evaluate(()=>{const n=window.fixture.diagramTyping;n.value=window.fixture.view.tubeDesignerNestingPunchPartDraft.name;n.blur();});await idle();
  const floatedBox=await diagramBox();
  await page.evaluate(()=>window.fixture.render());await frame();
  assert.deepEqual(await diagramBox(),floatedBox,'The shared floating window keeps its dragged position and size after refresh');
  await diagram.locator('[data-profile-annotation-key="width"] .td-profile-dimension-label').click();
  assert.equal(await page.evaluate(()=>document.activeElement.dataset.tubeDesignerMainProfileParameter),'width','Diagram annotation focuses its own main control');
  await page.screenshot({path:resolve(artifacts,'08-main-floating-diagram.png')});
  await diagram.locator('[data-cam-action$="section-diagram-close"]').click();await idle();
  assert.equal(await diagram.count(),0);
  await resize('holes:scene',90);
  assert.ok((await sizes()).holes>=beforeResize.holes+85);
  const afterResize=await sizes();
  await page.evaluate(()=>window.fixture.render());await frame();
  assert.deepEqual(await sizes(),afterResize,'A preview patch keeps resized region heights');
  await resize('main:ends',-1000);
  assert.ok(Math.abs((await sizes()).main-96)<1,'Mouse resize clamps the main region minimum');
  await resize('ends:holes',1000);
  assert.ok(Math.abs((await sizes()).holes-128)<1,'Mouse resize clamps the holes region minimum');
  await page.locator('[data-punch-region-splitter="ends:holes"]').focus();
  const beforeKey=await sizes();await page.keyboard.press('ArrowUp');await frame();
  assert.ok(Math.abs((await sizes()).holes-beforeKey.holes-20)<1,'Keyboard resizing works without rebuilding controls');
  await resize('ends:holes',-1000);await resize('holes:scene',130);

  // Main fields obey the real profile template, including advanced full rows.
  const advanced=region('main').locator('details[data-parameter-advanced]').first();
  await advanced.locator('summary').click();
  const outer=region('main').locator('[data-tube-designer-main-profile-parameter="useOuterRadii"]');
  assert.equal(await outer.evaluate(n=>getComputedStyle(n.closest('label')).gridColumn),'1 / -1');
  await outer.check();await idle();
  assert.ok(await advanced.evaluate(n=>n.open),'Editing conditional values retains the advanced disclosure');
  assert.equal(await region('main').locator('[data-tube-designer-main-profile-parameter="outerRadius1"]').count(),1);
  await page.evaluate(()=>{const f=window.fixture;f.canvas=f.viewport.renderer.domElement;f.mainInput=document.querySelector('[data-tube-designer-nesting-punch-field="name"]');});

  // Type-specific shape controls are inline. Switching region finishes the live edit.
  await open(0);
  await checkShapeRow(0);
  const editBeforeClick=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.parameterEditor);
  await inline.locator('[data-tube-designer-punch-parameter="diameter"]').click();await idle();
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.parameterEditor),editBeforeClick,'Clicking a field inside an active cell never collapses or restarts the edit');
  // One click commits a dirty number and selects another cell, even while its
  // native preview is still in flight. No second click is required.
  await page.evaluate(()=>{window.fixture.deferOnePreview=true;});
  await inline.locator('[data-tube-designer-punch-parameter="diameter"]').fill('18');
  await row(0).locator('[data-tube-designer-punch-field="station"]').click();
  await page.waitForFunction(()=>typeof window.fixture.releasePreview==='function');
  await page.evaluate(()=>window.fixture.releasePreview());await idle();
  await checkColumns(0,'pose');
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].toolParameters.diameter),18,'The preceding cell value commits before changing cells');
  await page.locator('[data-tube-designer-punch-row="0"] [data-punch-record-column="shape"]').focus();
  await page.keyboard.press('Enter');await idle();await checkShapeRow(0);
  assert.equal(await page.locator('[data-punch-parameter-dialog]').count(),0);
  assert.equal(await inline.locator('[data-tube-designer-punch-parameter="diameter"]').count(),1);
  await inline.locator('[data-tube-designer-punch-field="tool"]').selectOption('slot');await idle();
  assert.equal(await inline.locator('[data-tube-designer-punch-parameter="diameter"]').count(),0);
  assert.equal(await inline.locator('[data-tube-designer-punch-parameter="spanAlong"]').count(),1);
  assert.equal(await inline.locator('[data-tube-designer-punch-parameter="spanAcross"]').count(),1);
  assert.equal(await inline.locator('[data-tube-designer-punch-field="blindHole"]').count(),1);
  // Exercise the descriptor's layout contract rather than a UI field-name list.
  await page.evaluate(()=>{
    const f=window.fixture,d=f.catalogue.find(d=>d.id==='slot');f.originalSlotDisplay=d.display;
    d.display={schema:'icax.template-display',schemaVersion:1,shared:{groups:{dimensions:{title:'模板尺寸',order:1}},fields:{
      spanAlong:{group:'dimensions',order:2,line:'full',title:'模板总长',width:{min:60,preferred:120,max:180}},
      spanAcross:{group:'dimensions',order:1,title:'模板宽度'}}}};f.render();
  });await frame();
  const group=inline.locator('[data-profile-display-group="dimensions"]');
  assert.equal(await group.locator('label').evaluateAll(nodes=>nodes.map(n=>n.querySelector('input')?.dataset.tubeDesignerPunchParameter).filter(Boolean)).then(x=>x.join(',')),'spanAcross,spanAlong');
  const full=group.locator('[data-tube-designer-punch-parameter="spanAlong"]').locator('..').locator('..');
  assert.equal(await full.evaluate(n=>getComputedStyle(n).gridColumn),'1 / -1');
  assert.match(await full.textContent(),/模板总长/);
  assert.equal(await group.locator('[data-tube-designer-punch-parameter="spanAlong"]').evaluate(n=>Math.round(n.getBoundingClientRect().width)),120,'Template control width honored');
  await page.evaluate(()=>{const f=window.fixture,d=f.catalogue.find(d=>d.id==='slot');d.display=f.originalSlotDisplay;f.render();});await frame();
  await inline.locator('[data-tube-designer-punch-parameter="spanAlong"]').fill('38');await page.keyboard.press('Tab');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].toolParameters.spanAlong),38,'Original preview payload carries the slot parameter');
  await page.screenshot({path:resolve(artifacts,'01-inline-slot-four-regions.png')});
  // Commit by changing the main name, without requiring a separate close dialog.
  await region('main').locator('[data-tube-designer-nesting-punch-field="name"]').fill('区域交互测试');await page.keyboard.press('Tab');await idle();
  assert.equal(await inline.count(),0);
  await region('main').locator('[data-tube-designer-nesting-punch-field="profileKey"]').selectOption('system:round');await idle();
  assert.equal(await region('main').locator('[data-tube-designer-main-profile-parameter="depth"]').count(),0);
  assert.match(await region('main').locator('[data-tube-designer-main-profile-parameter="width"]').locator('..').textContent(),/外径/);
  await region('main').locator('[data-tube-designer-nesting-punch-field="profileKey"]').selectOption('system:rect');await idle();

  const left=page.locator('[data-tube-designer-punch-end-row="start"]');
  await left.locator('[data-tube-designer-punch-field="tool"]').selectOption('end-miter');await idle();
  await left.locator('[data-cam-action$="parameters-open"]').click();await idle();
  assert.equal(await inline.locator('[data-tube-designer-punch-parameter="angle"]').count(),1);
  await inline.locator('[data-tube-designer-punch-parameter="angle"]').fill('25');await page.keyboard.press('Tab');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).ends.start.toolParameters.angle),25);
  assert.equal(await page.locator('[data-tube-designer-punch-end-row]').count(),2,'Endpoint parameter rows never add an endpoint record');
  assert.equal(await page.locator('[data-punch-parameter-dialog]').count(),0);
  assert.equal(await region('ends').locator('.punch-end-table-scroll').evaluate(n=>getComputedStyle(n).overflowY),'auto');
  assert.equal(await inline.evaluate(n=>getComputedStyle(n).backgroundColor),await left.evaluate(n=>getComputedStyle(n).backgroundColor),'An expanded endpoint keeps its parent color');
  await page.screenshot({path:resolve(artifacts,'02-inline-end.png')});
  await inline.locator('[data-cam-action$="parameters-close"]').click();await idle();
  // The denser form fits the full variable end-joint parameter set without
  // changing the template's order, conditions or dedicated operation fields.
  await left.locator('[data-tube-designer-punch-field="tool"]').selectOption('end-key-joint');await idle();
  await left.locator('[data-punch-cell-activate]').click();await idle();
  await inline.locator('[data-tube-designer-punch-parameter="gender"]').selectOption('female');await idle();
  const endDensity=await inline.evaluate(n=>{
    const controls=[...n.querySelectorAll('input:not([type=checkbox]),select')];
    return {height:n.getBoundingClientRect().height,controls:controls.map(input=>({height:input.getBoundingClientRect().height,font:getComputedStyle(input).fontSize,width:input.getBoundingClientRect().width,field:input.dataset.tubeDesignerPunchParameter??input.dataset.tubeDesignerPunchField,full:input.closest('label').classList.contains('is-line-full'),sized:input.closest('label').classList.contains('has-control-size')}))};
  });
  assert.equal(endDensity.controls.length,8,'Conditional insertion keeps all end-joint and placement inputs');
  assert.ok(endDensity.height<170,JSON.stringify(endDensity));
  assert.ok(endDensity.controls.every(input=>input.height<=27&&input.font==='11px'),JSON.stringify(endDensity));
  assert.ok(endDensity.controls.filter(input=>input.field!=='gender'&&!input.full&&!input.sized).every(input=>input.width<=85),'Default numbers use compact widths; declared template widths remain authoritative');
  assert.equal(await inline.locator('header,strong').count(),0,'An expanded end row does not repeat its mother row title');
  assert.ok((await compactControlGaps(inline)).every(n=>n.gap>=0&&n.gap<=8),'End labels sit next to their values without full-window spreading');
  assert.ok((await compactControlGaps(inline)).every(n=>n.overflow<=1),'Compact end controls stay inside their own fields');
  await page.evaluate(()=>{const f=window.fixture,s=f.view.tubeDesignerPunchWizard;f.densityRegionSizes=structuredClone(s.punchRegionSizes);s.punchRegionSizes={main:180,ends:260,holes:210};f.render();document.querySelector('.punch-end-table-scroll').scrollTop=0;});await frame();
  await page.screenshot({path:resolve(artifacts,'12-compact-end-joint.png')});
  const compactWidths=[];
  for(const width of [1600,2560,3200]) {
    await page.setViewportSize({width,height:1000});await frame();
    const mainFields=await region('main').locator('.tube-profile-library-parameter-group.is-inline .tube-profile-library-field-grid > label').evaluateAll(nodes=>nodes.map(n=>({left:n.getBoundingClientRect().left,right:n.getBoundingClientRect().right})));
    const mainSpan=mainFields.at(-1).right-mainFields[0].left;
    const endFields=await compactControlGaps(inline);
    assert.ok(mainSpan<=800,'Wide monitors do not stretch the five basic fields');
    assert.ok(endFields.every(n=>n.gap>=0&&n.gap<=8&&n.overflow<=1),'Wide end forms keep controls beside their labels and inside their fields');
    compactWidths.push({viewportWidth:width,mainParameterSpan:mainSpan,endFieldMaxWidth:Math.max(...endFields.map(n=>n.width)),endLabelControlMaxGap:Math.max(...endFields.map(n=>n.gap)),endControls:endFields.length});
    if(width===3200)await page.screenshot({path:resolve(artifacts,'14-compact-wide-end.png')});
  }
  writeFileSync(resolve(artifacts,'compact-layout.json'),JSON.stringify(compactWidths,null,2));
  await page.setViewportSize({width:1600,height:1000});await frame();
  await inline.locator('[data-cam-action$="parameters-close"]').click();await idle();
  await left.locator('[data-cam-change-action$="record-kind-change"]').selectOption('branch');await idle();
  await left.locator('[data-punch-cell-activate]').click();await idle();
  await checkShapeRow('start','start');
  await inline.locator('[data-cam-action$="section-diagram"]').click();await idle();
  assert.match(await diagram.getAttribute('aria-label'),/端面/);
  await diagram.locator('[data-profile-annotation-key="width"] .td-profile-dimension-label').click();
  assert.equal(await page.evaluate(()=>document.activeElement.dataset.tubeDesignerPunchEnd),'start','Endpoint illustration owns the endpoint field, not the main or branch field');
  await diagram.locator('[data-cam-action$="section-diagram-close"]').click();await idle();
  await inline.locator('[data-cam-action$="parameters-close"]').click();await idle();
  await page.evaluate(()=>{const f=window.fixture;f.view.tubeDesignerPunchWizard.punchRegionSizes=f.densityRegionSizes;f.render();});await frame();

  await open(0,'pose');await checkColumns(0,'pose');
  for(const key of ['station','reference','face','offset','rotation'])
    assert.equal(await row(0).locator('[data-tube-designer-punch-field="'+key+'"]').count(),1,'Pose column retained: '+key);
  await page.evaluate(()=>{window.fixture.poseInput=document.querySelector('[data-tube-designer-punch-row="0"] [data-tube-designer-punch-field="station"]');});
  await row(0).locator('[data-tube-designer-punch-field="station"]').fill('320');await page.keyboard.press('Tab');await idle();
  assert.ok(await page.evaluate(()=>window.fixture.poseInput===document.querySelector('[data-tube-designer-punch-row="0"] [data-tube-designer-punch-field="station"]')),'Native preview retains the same column input');
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].station),320,'Pose columns retain the original native payload');
  await page.screenshot({path:resolve(artifacts,'06-pose-columns.png')});
  await open(0,'arrays');await checkColumns(0,'arrays');
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="enabled"]').count(),0,'Array dimensions replace the separate enable state');
  assert.equal(await region('holes').locator('[data-cam-change-action$="array-group-create"],[data-cam-action$="array-group-add"],[data-cam-action$="array-group-remove"]').count(),0,'The sheet has no array group management buttons');
  const arrayMode=row(0).locator('[data-cam-change-action$="array-mode-change"]');
  assert.deepEqual(await arrayMode.locator('option').allTextContents(),['无','一维阵列','二维阵列']);
  await arrayMode.evaluate(n=>window.fixture.arrayModeNode=n);
  await arrayMode.selectOption('two');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features[0].arrayGroups.length),2,'Multiple independent arrays edit in the same sheet');
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="count"]').count(),2,'Each array group has its own count in the shared count column');
  await row(0).locator('[data-tube-designer-punch-array-group="array-group-1"][data-tube-designer-punch-array-field="spacing"]').fill('25');await page.keyboard.press('Tab');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].arrayGroups.find(g=>g.id==='array-group-1').spacing),25);
  const activeArrayPayload=await page.evaluate(()=>window.fixture.calls.at(-1).features[0]);
  await arrayMode.selectOption('none');await idle();
  assert.equal(await arrayMode.inputValue(),'none');
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="count"]').count(),0,'No array hides both parameter lines');
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].arrayCandidateCount),1);
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].arrayGroups.find(g=>g.id==='array-group-1').enabled),false);
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].arrayGroups.find(g=>g.id==='array-group-1').type),'linear');
  await page.screenshot({path:resolve(artifacts,'10-array-none.png')});
  await arrayMode.selectOption('one');await idle();
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="count"]').count(),1,'One dimension shows one parameter line');
  await arrayMode.selectOption('two');await idle();
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="count"]').count(),2);
  assert.ok(await arrayMode.evaluate(n=>n===window.fixture.arrayModeNode),'Dimension changes retain the identical active selector');
  assert.deepEqual(await page.evaluate(()=>window.fixture.calls.at(-1).features[0].arrayTransforms),activeArrayPayload.arrayTransforms,'Re-enabling an array restores every two-dimensional placement');
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-group="array-group-1"][data-tube-designer-punch-array-field="spacing"]').inputValue(),'25');
  const linePositions=await row(0).locator('[data-tube-designer-punch-array-field="count"]').evaluateAll(nodes=>nodes.map(n=>n.getBoundingClientRect().top));
  assert.ok(linePositions[1]-linePositions[0]>=30,'Two dimensions have two distinct aligned parameter lines');
  await page.evaluate(()=>{
    const f=window.fixture,n=document.querySelector('[data-tube-designer-punch-row="0"] [data-tube-designer-punch-array-field="arraySkips"]');
    n.focus({preventScroll:true});n.value='2:*, 3:1';n.setSelectionRange(1,4);f.columnTypingInput=n;
    const scroll=n.closest('.tube-designer-punch-sheet-scroll');scroll.scrollLeft=scroll.scrollWidth;f.columnLatestLeft=scroll.scrollLeft;f.render();
  });await frame();
  assert.ok(await page.evaluate(()=>{const f=window.fixture,n=f.columnTypingInput,c=n.closest('.tube-designer-punch-sheet-scroll');return n.isConnected&&n===document.activeElement&&n.value==='2:*, 3:1'&&n.selectionStart===1&&n.selectionEnd===4&&Math.abs(c.scrollLeft-f.columnLatestLeft)<1;}),'Refresh retains dirty cell text, selection, focus and horizontal scrolling');
  await page.evaluate(()=>{const n=window.fixture.columnTypingInput;n.value=n.defaultValue;n.blur();});
  const sharedArrayFields=await row(0).locator('[data-tube-designer-punch-array-field]').evaluateAll(nodes=>nodes.map(n=>n.dataset.tubeDesignerPunchArrayField));
  await region('holes').locator('.tube-designer-punch-sheet-scroll').evaluate(n=>n.scrollLeft=0);
  await page.screenshot({path:resolve(artifacts,'03-array-columns.png')});
  // Column groups can collapse into summaries, then a single cell click restores all columns.
  await region('holes').locator('[data-punch-column-group="pose"]').first().click();await idle();
  assert.equal(await row(0).locator('[data-punch-record-column="pose:station"]').count(),0);
  await row(0).locator('[data-punch-record-column="pose"]').click();await idle();await checkColumns(0,'pose');

  // Native preview holds permit scrolling; editing remains locked by the existing operation.
  await open(0,'shape');
  await page.evaluate(()=>{const f=window.fixture;f.deferOnePreview=true;void f.dispatch('tube-designer-nesting-punch-create-preview',{});});
  await page.waitForFunction(()=>typeof window.fixture.releasePreview==='function');
  await page.evaluate(()=>{
    const f=window.fixture;
    const main=document.querySelector('.punch-main-scroll'),ends=document.querySelector('.punch-end-table-scroll'),holes=document.querySelector('.tube-designer-punch-sheet-scroll');
    main.scrollTop=main.scrollHeight;ends.scrollTop=25;holes.scrollTop=holes.scrollHeight;
    f.scrolls=[main,ends,holes].map(n=>({node:n,top:n.scrollTop,left:n.scrollLeft}));f.heights=f.view.tubeDesignerPunchWizard.punchRegionSizes;
    f.releasePreview();
  });await idle();
  assert.ok(await page.evaluate(()=>window.fixture.scrolls.every(({node,top,left})=>node.isConnected&&Math.abs(node.scrollTop-top)<1&&node.scrollLeft===left)),'Each region keeps its latest scroll');
  assert.ok(await page.evaluate(()=>window.fixture.canvas===window.fixture.viewport.renderer.domElement),'Resizing and async previews retain the real canvas');
  // A delayed UI response captures interaction state at patch time, after subsequent typing/scrolling.
  await page.evaluate(()=>{const f=window.fixture;f.uiResponse=new Promise(resolve=>{f.releaseUiResponse=()=>{f.render();resolve();};});});
  await page.evaluate(()=>{
    const f=window.fixture,n=document.querySelector('[data-tube-designer-nesting-punch-field="name"]');
    n.focus({preventScroll:true});n.value='继续输入的文本';n.setSelectionRange(2,5);f.typingInput=n;
    document.querySelector('.punch-main-scroll').scrollTop=12;
    document.querySelector('.tube-designer-punch-sheet-scroll').scrollTop=30;
    f.releaseUiResponse();
  });await frame();
  assert.ok(await page.evaluate(()=>{const f=window.fixture,n=f.typingInput;return n===document.activeElement&&n.isConnected&&n.value==='继续输入的文本'&&n.selectionStart===2&&n.selectionEnd===5;}),'Latest caret and dirty input survive async UI patch');
  await page.evaluate(()=>{const f=window.fixture;f.typingInput.value=f.view.tubeDesignerNestingPunchPartDraft.name;f.typingInput.blur();});await idle();
  await inline.locator('[data-cam-action$="parameters-close"]').click();await idle();

  // Existing copy/add/select/delete and undo contracts remain reachable from table controls.
  await page.locator('[data-tube-designer-punch-row="0"] [data-cam-change-action$="selection-change"]').check();await idle();
  await page.locator('.tube-designer-punch-sheet-toolbar [data-cam-action$="copy-selected"]').click();await idle();
  assert.equal(await page.locator('[data-tube-designer-punch-row="draft"]').count(),1);
  await page.locator('[data-punch-inline-row="draft-actions"] [data-cam-action$="-add"]').click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),2);
  assert.equal(await page.locator('[data-tube-designer-punch-row="draft"]').count(),0);
  const rowColor=index=>page.locator('[data-tube-designer-punch-row="'+index+'"]').evaluate(n=>getComputedStyle(n).backgroundColor);
  assert.notEqual(await rowColor(0),await rowColor(1),'Data rows alternate their backgrounds');
  const endColors=await page.locator('[data-tube-designer-punch-end-row]').evaluateAll(nodes=>nodes.map(n=>getComputedStyle(n).backgroundColor));
  assert.notEqual(endColors[0],endColors[1],'Both endpoint rows have alternating backgrounds');
  await open(1);
  assert.equal(await inline.evaluate(n=>getComputedStyle(n).backgroundColor),await rowColor(1),'Expanded parameters share their own parent row color');
  assert.notEqual(await rowColor(0),await rowColor(1),'Expanding a row never shifts the striping of other records');
  await inline.locator('[data-cam-change-action$="record-kind-change"]').selectOption('branch');await idle();
  assert.equal(await inline.locator('[data-tube-designer-punch-profile-parameter="width"]').count(),1);
  await inline.locator('[data-cam-action$="section-diagram"]').click();await idle();
  assert.match(await diagram.getAttribute('aria-label'),/支管/);
  await diagram.locator('[data-profile-annotation-key="width"] .td-profile-dimension-label').click();
  assert.equal(await page.evaluate(()=>document.activeElement.dataset.tubeDesignerPunchProfileParameter),'width','Branch diagram focuses its own cell, not the main tube width');
  const activeEditor=await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.parameterEditor);
  await diagram.locator('[data-cam-action$="section-diagram-close"]').click();await idle();
  assert.deepEqual(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.parameterEditor),activeEditor,'Closing the illustration never closes or commits its parameter cell');
  await inline.locator('[data-cam-action$="parameters-close"]').click();await idle();
  await open(1,'pose');await checkColumns(1,'pose');
  for(const key of ['station','reference','angle','azimuth','roll','offsetY','offsetZ','direction','length'])
    assert.equal(await row(1).locator('[data-tube-designer-punch-field="'+key+'"]').count(),1,'Branch pose column retained: '+key);
  assert.equal(await row(0).locator('[data-tube-designer-punch-field="angle"]').count(),0,'Inapplicable branch fields stay absent in a side-hole row');
  await page.screenshot({path:resolve(artifacts,'05-striped-branch-columns.png')});
  await open(1,'arrays');await checkColumns(1,'arrays');
  assert.deepEqual(await row(1).locator('[data-tube-designer-punch-array-field]').evaluateAll(nodes=>nodes.map(n=>n.dataset.tubeDesignerPunchArrayField)),sharedArrayFields,'Branch and slot use the same array column fields');
  await row(1).locator('[data-tube-designer-punch-array-group="array-group-1"][data-tube-designer-punch-array-field="type"]').selectOption('polar');await idle();
  await row(1).locator('[data-tube-designer-punch-array-field="angleMode"]').selectOption('pitch');await idle();
  assert.equal(await row(1).locator('[data-tube-designer-punch-array-field="angleStep"]').count(),1,'Switching to polar expands the applicable angle columns');
  assert.equal(await row(0).locator('[data-tube-designer-punch-array-field="angleStep"]').count(),0,'Changing one row does not expose inapplicable fields in another');
  await row(1).locator('[data-tube-designer-punch-array-field="originAuto"]').uncheck();await idle();
  await row(1).locator('[data-tube-designer-punch-array-field="originY"]').fill('10');await page.keyboard.press('Tab');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[1].arrayGroups.find(g=>g.id==='array-group-1').origin[1]),10,'Custom polar axis survives the unchanged native payload');
  const frozen=await row(1).locator('[data-punch-frozen-column="shape"]').boundingBox();
  const sheet=await region('holes').locator('.tube-designer-punch-sheet-scroll').boundingBox();
  assert.ok(Math.abs(frozen.x-sheet.x-88)<2,'Row identity stays visible while parameter columns scroll horizontally');
  await page.screenshot({path:resolve(artifacts,'07-multiple-array-columns.png')});
  // A length array combined with a full circular array creates dense rings.
  await row(1).locator('[data-tube-designer-punch-array-field="count"]').first().fill('5');await page.keyboard.press('Tab');await idle();
  await row(1).locator('[data-tube-designer-punch-array-group="array-group-1"][data-tube-designer-punch-array-field="axis"]').selectOption('X');await idle();
  await row(1).locator('[data-tube-designer-punch-array-field="angleMode"]').selectOption('full-circle');await idle();
  await row(1).locator('[data-tube-designer-punch-array-group="array-group-1"][data-tube-designer-punch-array-field="count"]').fill('12');await page.keyboard.press('Tab');await idle();
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[1].arrayCandidateCount),60,'Five axial stations and twelve angles produce sixty circumferential placements');
  assert.equal(await page.evaluate(()=>window.fixture.calls.at(-1).features[1].arrayTransforms.length),60);
  await region('holes').locator('[data-punch-column-group="pose"]').first().click();await idle();
  await page.evaluate(()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    s.punchRegionSizes={main:220,ends:150,holes:260};f.render();
    document.querySelector('.punch-main-scroll').scrollTop=0;
    document.querySelector('.punch-end-table-scroll').scrollTop=0;
  });await frame();
  await region('holes').locator('.tube-designer-punch-sheet-scroll').evaluate(n=>{n.scrollLeft=n.scrollWidth;n.scrollTop=0;});await frame();
  await page.screenshot({path:resolve(artifacts,'11-two-dimensional-circumferential-array.png')});
  await region('holes').locator('[data-punch-column-group="pose"]').first().click();await idle();
  await page.locator('.tube-designer-punch-sheet-toolbar [data-cam-action$="remove-selected"]').click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),1);
  await region('scene').locator('[data-cam-action$="-undo"]').click();await idle();
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerPunchWizard.features.length),2);

  // Small windows keep the minimum region sizes; only the workspace needs to scroll.
  await page.setViewportSize({width:1000,height:600});await frame();
  assert.ok(await region('main').locator('.tube-designer-punch-main-profile-parameters').evaluate(n=>n.scrollWidth<=n.clientWidth+1),'Parameter fields wrap at narrow widths without horizontal overflow');
  await open(1,'pose');
  await checkColumns(1,'pose');
  assert.ok(await region('holes').locator('.tube-designer-punch-sheet-scroll').evaluate(n=>n.scrollWidth>n.clientWidth),'Narrow windows scroll only inside the parameter sheet');
  for(const [name,min] of Object.entries({main:96,ends:104,holes:128,scene:180}))assert.ok((await sizes())[name]>=min-1);
  assert.ok(await page.locator('.tube-designer-punch-sheet-workspace').evaluate(n=>n.scrollHeight>n.clientHeight));
  await page.screenshot({path:resolve(artifacts,'04-small-window.png')});
  await page.setViewportSize({width:1600,height:1000});await frame();
  await page.evaluate(()=>{
    const f=window.fixture;delete f.view.tubeDesignerPunchWizard.punchRegionSizes;f.render();
    document.querySelector('.punch-main-scroll').scrollTop=0;
    document.querySelector('.punch-end-table-scroll').scrollTop=0;
    document.querySelector('.tube-designer-punch-sheet-scroll').scrollTop=0;
    document.querySelector('.tube-designer-punch-sheet-scroll').scrollLeft=0;
  });await frame();
  await page.screenshot({path:resolve(artifacts,'09-final-table-interaction.png')});
  // Measure the rendered table with enough real records to expose row/chrome
  // costs, at an ordinary desktop width and a wide monitor width.
  await page.evaluate(()=>{
    const f=window.fixture,s=f.view.tubeDesignerPunchWizard;
    s.features=Array.from({length:16},(_,index)=>{
      const item=f.wizard.normalizePunchFeature({recordKind:'tool',layoutDatum:'base',face:'top',station:60+index*50,arrayGroups:[],arraySkips:[]});
      f.wizard.selectPunchTool(s,item,'circle');item.toolParameters.diameter=12;return item;
    });
    s.selectedFeatureId=s.features[0].id;s.selectedFeatureIds=[];s.parameterEditor=null;s.showDraftRow=false;
    s.ends={start:{type:'keep'},end:{type:'keep'}};s.sheetColumns={pose:true,arrays:true};
    delete s.punchRegionSizes;f.render();
    document.querySelectorAll('.punch-main-scroll,.punch-end-table-scroll,.tube-designer-punch-sheet-scroll').forEach(n=>{n.scrollTop=0;n.scrollLeft=0;});
  });await frame();
  const density=[];
  for(const width of [1600,2560]) {
    await page.setViewportSize({width,height:1000});await frame();
    const measured=await page.evaluate(()=>{
      const dialog=document.querySelector('.tube-designer-punch-sheet-dialog'),sheet=document.querySelector('.tube-designer-punch-sheet-scroll');
      const rows=[...sheet.querySelectorAll('[data-tube-designer-punch-row]')];
      const bounds=sheet.getBoundingClientRect(),header=sheet.querySelector('thead').getBoundingClientRect();
      const dialogBounds=dialog.getBoundingClientRect();
      return {viewportWidth:innerWidth,dialogWidth:dialogBounds.width,dialogLeft:dialogBounds.left,dialogRight:dialogBounds.right,font:getComputedStyle(rows[0]).fontSize,
        rowHeight:rows[0].getBoundingClientRect().height,tableHeaderHeight:header.height,
        fullyVisibleRows:rows.filter(n=>{const b=n.getBoundingClientRect();return b.top>=header.bottom-1&&b.bottom<=bounds.bottom+1;}).length,
        regionHeights:Object.fromEntries([...document.querySelectorAll('[data-punch-region]')].map(n=>[n.dataset.punchRegion,n.getBoundingClientRect().height]))};
    });
    density.push(measured);
    if(!densityBaseline) {
      assert.ok(measured.dialogWidth>=width-26,'Wide monitors use their available width without a fixed cap');
      assert.ok(measured.dialogLeft>=0&&measured.dialogRight<=width,'Using the full width keeps both window edges on screen');
      assert.ok(measured.rowHeight<=32,'Ordinary data rows use spreadsheet density without reducing the font');
      assert.ok(measured.tableHeaderHeight<=48,'Grouped column headers leave room for data');
      assert.ok(measured.fullyVisibleRows>=6,'Default split shows several complete records at once');
      assert.equal(measured.font,'11px');
    }
    await page.screenshot({path:resolve(artifacts,'13-density-'+width+'.png')});
  }
  writeFileSync(resolve(artifacts,'density.json'),JSON.stringify(density,null,2));
  const caption=await page.locator('[data-tube-designer-punch-window-drag]').boundingBox();
  await page.mouse.move(caption.x+100,caption.y+10);await page.mouse.down();
  await page.mouse.move(caption.x+300,caption.y+80);await page.mouse.up();await frame();
  const moved=await page.locator('.tube-designer-punch-sheet-dialog').boundingBox();
  assert.ok(moved.x>=0&&moved.x+moved.width<=2560&&moved.y>=0&&moved.y+moved.height<=1000,'Dragging a full-width window keeps the close and footer actions on screen');
  await closeButton.click();await idle();
  assert.equal(await page.locator('.tube-designer-punch-sheet-dialog').count(),0,'The header close uses the existing cancel action');
  assert.equal(await page.evaluate(()=>window.fixture.view.tubeDesignerNestingPunchPartDraft),null);
  assert.deepEqual(errors,[]);assert.deepEqual(externalRequests,[]);
  writeFileSync(resolve(artifacts,'report.json'),JSON.stringify({fourRegions:true,threeSplitters:true,minimumClamping:true,inlineEndAndPunch:true,
    actualTemplatePresentation:true,wideBasicDimensionsOneLine:true,compactLabelControlGaps:true,noRedundantRegionHeadings:true,holeActionsInWindowTitle:true,basicDimensionsAlwaysVisible:true,headerCloseUsesCancel:true,compactEightParameterEndJoint:true,slotAndBranchAndPoseAndMultipleArrays:true,stripedRowsAndMatchingExpandedColor:true,asyncFocusAndScroll:true,canvasPreserved:true,
    directShapeCellAndFullWidthRows:true,expandedPoseAndArrayColumns:true,arrayDimensionSelector:true,twoAlignedArrayParameterLines:true,noArrayGroupButtons:true,noneRestoresArrayParameters:true,denseCircumferentialArray60Instances:true,floatingDiagramDragResizeAndOwnership:true,sharedArrayFields:true,columnFocusAndHorizontalScroll:true,copyAddDeleteUndo:true,nativeComputation:'deterministic proxy; original scene method and payload checked'},null,2));
  console.log('PASS: four regions, drag minima, template conditions, full-row slot/end and expanded pose/array columns, async input/scroll/canvas retention, copy/add/delete/undo.');
} finally { await browser.close(); }
