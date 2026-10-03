import assert from "node:assert/strict";
import { readFileSync, mkdirSync, readdirSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep, dirname } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const fixedAngleTemplate = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/end-side-tab-slot/assembly.json", import.meta.url), "utf8"));
const cornerTemplate = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/orthogonal-corner/assembly.json", import.meta.url), "utf8"));
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://assembly-bindings.test/**", (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, path.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT && path.startsWith("/src/apps/tube-designer/") ? resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT, "apps/tube-designer", path.slice("/src/apps/tube-designer/".length)) : file, "utf8") });
  });
  await page.goto("http://assembly-bindings.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:300px minmax(0,1fr) 360px;height:820px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;min-width:0;background:#13252d}.cam-viewport canvas{width:100%;height:100%}.cam-context-pane{overflow:auto}.test-search{position:sticky;top:0;background:#fff}.tube-assembly-binding-workflow{max-height:125px;overflow:auto}` });
  const result = await page.evaluate(async (fixedAngleTemplate) => {
    const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const model = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const { capturePaneInteraction } = await import("/src/apps/_shared/workbench/utils/paneInteractionState.mjs");
    const template = model.assemblyProcessWithInput({ ...fixedAngleTemplate,
      id: "joint", displayName: "孔槽连接" });
    const view = { activeAreaId: "assemblies", scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: {} } },
      tubeDesignerAssemblyTemplates: [template], tubeDesignerAssemblyLibrary: { selectedId: template.id, workMode: "product", parameterDrafts: {}, processDrafts: {} } };
    const input = { template, parameters: Object.fromEntries(template.parameters.map((p) => [p.key, p.defaultValue])),
      processDrafts: {}, parameterHtml: `<details class="tube-assembly-binding-parameters" open><summary>加工参数 · 修改后需重新检查</summary>${["轴线夹角", "轮廓间隙", "开口长度", "开口宽度"].map((label, index) => `<label class="tube-designer-field"><span>${label}</span><input type="number" data-test-parameter="${index}" value="20"></label>`).join("")}</details>` };
    const getInput = () => input;
    const pending = [];
    const calls = [];
    let deferReads = false;
    const nativeResult = (wholeGeometryAudit = { status: "measured", conflicts: [], interfaceFitCertified: false },
      nodeFinalAudit = { status: "measured", nodeCount: 1, measuredNodeCount: 1, conflictNodeCount: 0, interfaceFitCertified: false }) => ({ productEntityId: "product-1", generationRunId: "run-1", sourceRevision: "1", bindings: [],
      members: [{ memberEntityId: "a", itemKey: "frame.left", name: "真实横梁", length: 500 },
        { memberEntityId: "b", itemKey: "frame.middle", name: "真实竖杆", length: 300 }],
      manufacturingPlan: { schema: "icax.product-assembly-manufacturing-plan", schemaVersion: 1,
        selectedConnectionCount: 0, unassignedConnectionCount: 1, fitStatus: "not-verified", wholeGeometryAudit, nodeFinalAudit,
        members: [{ itemKey: "frame.left", name: "真实横梁", partMappingStatus: "persisted", partLength: 500,
          connections: [{ key: "joint-ab", status: "unassigned" }], operations: [] }] },
      connections: [{ key: "joint-ab", participants: [
        { memberEntityId: "a", itemKey: "frame.left", name: "真实横梁" },
        { memberEntityId: "b", itemKey: "frame.middle", name: "真实竖杆" }],
        properties: { topology: "T", participantAnchors: [
          { itemKey: "frame.left", anchor: { kind: "side", face: "right", reference: "start", station: 250 } },
          { itemKey: "frame.middle", anchor: { kind: "end", end: "start", approachFace: "left" } },
        ] }, nodeGeometry: { nodeGeometryStatus: "verified", axisAngleDeg: 90 } }],
      capabilities: { anchorKinds: ["end", "side"], detail: "端部及侧面减料加工", templates: [{ templateId: "joint", supported: true, detail: "支持轮廓开孔，装配位置仍需核对。" }] } });
    const candidate = (wholeGeometryAudit = { status: "measured", conflicts: [], interfaceFitCertified: false },
      nodeFinalAudit = { status: "measured", nodeCount: 1, measuredNodeCount: 1, conflictNodeCount: 0, interfaceFitCertified: false }) => ({ canApply: true, supportStatus: "supported", candidateToken: "token", manufacturingEffect: "incremental-cut", assemblyFitStatus: "not-verified", wholeGeometryAudit, nodeFinalAudit, geometryChanges: [{ memberEntityId: "a" }], checks: [{ status: "pass", detail: "真实截面与加工位置有效" }], workflow: {
      blankParts: [{ label: "横梁下料件", participantRoles: ["host"] }],
      partOperations: Array.from({ length: 8 }, (_, i) => ({ label: `轮廓开孔 ${i + 1}`, participantRoles: ["host"] })),
      assemblySteps: [{ label: "加工后定位", distance: 20, unit: "mm" }], bom: [{ label: "螺钉", quantity: 2, unit: "件", specificationStatus: "requires-standard" }],
    } });
    const context = { sceneProxy: { invoke(method, payload) {
      calls.push({ method, payload });
      if (method.endsWith("PreviewProductAssemblyBinding")) {
        // The legacy bound host binds the declared local geometry definitions
        // into its internal descriptor; unknown fields remain rejected.
        const expectedKeys = [...new Set([...fixedAngleTemplate.parameters,
          ...fixedAngleTemplate.inputContract.geometryParameters].map((p) => p.key))].sort();
        if (JSON.stringify(Object.keys(payload.parameters).sort()) !== JSON.stringify(expectedKeys))
          throw Error("bound request contains undeclared product parameters");
      }
      if (method.endsWith("GetProductAssemblyBindings") && !deferReads) return Promise.resolve(nativeResult());
      return new Promise((resolve) => pending.push({ method, resolve }));
    } } };
    let searchValue = "连接工艺";
    const render = () => {
      const previousMount = document.querySelector("main");
      const restoreInteraction = previousMount ? capturePaneInteraction(previousMount) : () => {};
      const left = `<div class="test-search"><input type="search" data-test-search value="${searchValue}"></div><ol>${Array.from({ length: 70 }, (_, i) => `<li style="height:32px">产品连接 ${i + 1}</li>`).join("")}</ol>`;
      const right = `<section class="tube-connection-library-editor"><header class="tube-connection-library-editor-heading"><div><strong>孔槽连接</strong><span>真实产品 · 单件加工</span></div></header><div class="tube-assembly-work-mode"><button class="active">应用到产品</button><button>工艺示例</button></div><div class="tube-connection-library-editor-body">${binding.renderProductAssemblyBindings(view, input)}</div></section>`;
      const overlay = `<div class="tube-assembly-preview-pane"><header>工艺示例 · 中间画布不代表当前产品加工结果</header></div>`;
      if (!document.querySelector("main")) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas>${overlay}</div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, document.querySelector("main"), { left, right, overlay, suffix: "" })) throw new Error("未使用局部刷新");
      restoreInteraction();
    };
    const ops = { renderProject: render };
    render();
    await binding.ensureProductAssemblyBindings(context, view, ops);
    const initialAudit = mount => mount.querySelector("#tube-binding-whole-audit")?.textContent ?? "";
    const initialNodeAudit = mount => mount.querySelector("#tube-binding-node-audit")?.textContent ?? "";
    const firstPlanAudit = initialAudit(document.querySelector("main"));
    const firstPlanNodeAudit = initialNodeAudit(document.querySelector("main"));
    const change = async (kind, value, role = "") => binding.handleProductAssemblyBindingAction(context, view,
      `tube-designer-binding-${kind}`, { value, dataset: { tubeBindingRole: role } }, ops, getInput);
    await change("connection", "joint-ab");
    const autoAnchors = structuredClone(binding.productAssemblyBindingDraft(view, template).participants);
    await change("member", "a", "host"); await change("anchor", "side:top", "host");
    await change("station", "120", "host"); await change("member", "b", "branch"); await change("anchor", "start", "branch");
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const left = mount.querySelector(".cam-context-pane");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    const station = mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]');
    const manufacturingPlan = mount.querySelector("#tube-binding-manufacturing-plan");
    manufacturingPlan.open = true;
    let inputEvents = 0;
    station.addEventListener("input", () => inputEvents += 1);
    station.focus({ preventScroll: true }); station.setSelectionRange(1, 2);
    left.scrollTop = 150; right.scrollTop = 170;
    const check = change("preview");
    const checkLoading = { text: mount.querySelector("#tube-binding-candidate [role='status']")?.textContent,
      focus: document.activeElement === station, left: left.scrollTop, right: right.scrollTop,
      canvasSame: canvas === mount.querySelector("canvas") };
    const committedApproachFace = calls.at(-1).payload.participants[1].anchor.approachFace;
    left.scrollTop = 205; right.scrollTop = 220; station.setSelectionRange(0, 2);
    pending.shift().resolve(candidate()); await check;
    const candidateMeasured = mount.querySelector("#tube-binding-candidate-whole-audit")?.textContent;
    const candidateNodeMeasured = mount.querySelector("#tube-binding-candidate-node-audit")?.textContent;
    const asyncCheck = { same: mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]') === station,
      focus: document.activeElement === station, selection: [station.selectionStart, station.selectionEnd], left: left.scrollTop, right: right.scrollTop };
    station.dispatchEvent(new Event("input"));
    const nested = mount.querySelector(".tube-assembly-binding-workflow");
    nested.scrollTop = 90;
    const search = mount.querySelector("[data-test-search]");
    search.focus({ preventScroll: true }); search.setSelectionRange(1, 3);
    const secondCheck = change("preview");
    left.scrollTop = 250; right.scrollTop = 310; nested.scrollTop = 110;
    pending.shift().resolve(candidate({ status: "conflict", conflicts: [{}, {}], interfaceFitCertified: false },
      { status: "conflict", nodeCount: 3, measuredNodeCount: 1, conflictNodeCount: 2, interfaceFitCertified: false })); await secondCheck;
    const candidateConflict = mount.querySelector("#tube-binding-candidate-whole-audit")?.textContent;
    const candidateNodeConflict = mount.querySelector("#tube-binding-candidate-node-audit")?.textContent;
    const conflictCanApply = binding.productAssemblyCandidateCanApply(view, input);
    const nestedResult = { same: nested === mount.querySelector(".tube-assembly-binding-workflow"), top: nested.scrollTop,
      searchSame: search === mount.querySelector("[data-test-search]"), focus: document.activeElement === search,
      selection: [search.selectionStart, search.selectionEnd], left: left.scrollTop, right: right.scrollTop };

    // A late authoritative list refresh preserves in-progress input and scroll.
    deferReads = true;
    const reload = binding.ensureProductAssemblyBindings(context, view, ops, true);
    const refreshPending = { summary: mount.querySelector("#tube-binding-manufacturing-plan summary")?.textContent,
      audit: initialAudit(mount), nodeAudit: initialNodeAudit(mount), samePlan: manufacturingPlan === mount.querySelector("#tube-binding-manufacturing-plan"),
      sameStation: station === mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]'),
      focus: document.activeElement === search, left: left.scrollTop, right: right.scrollTop,
      canvasSame: canvas === mount.querySelector("canvas") };
    await change("station", "128.5", "host");
    station.focus({ preventScroll: true }); station.setSelectionRange(1, 4);
    left.scrollTop = 285; right.scrollTop = 245;
    pending.shift().resolve(nativeResult({ status: "conflict", conflicts: [{}], interfaceFitCertified: false },
      { status: "conflict", nodeCount: 1, measuredNodeCount: 0, conflictNodeCount: 1, interfaceFitCertified: false })); await reload;
    const planConflict = initialAudit(mount);
    const planNodeConflict = initialNodeAudit(mount);
    const refresh = { same: mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]') === station,
      focus: document.activeElement === station, selection: [station.selectionStart, station.selectionEnd], value: station.value,
      left: left.scrollTop, right: right.scrollTop, canvasSame: canvas === mount.querySelector("canvas"),
      planSame: manufacturingPlan === mount.querySelector("#tube-binding-manufacturing-plan"), planOpen: manufacturingPlan.open };

    const rawReload = binding.ensureProductAssemblyBindings(context, view, ops, true);
    station.value = "128.50"; station.setSelectionRange(3, 6);
    station.dispatchEvent(new Event("input", { bubbles: true }));
    pending.shift().resolve(nativeResult({ status: "unsupported", reason: "resource unavailable", interfaceFitCertified: false },
      { status: "unsupported", reason: "mapping unavailable", interfaceFitCertified: false })); await rawReload;
    const planUnsupported = initialAudit(mount);
    const planNodeUnsupported = initialNodeAudit(mount);
    const uncommittedText = { value: station.value, focus: document.activeElement === station,
      selection: [station.selectionStart, station.selectionEnd] };
    await change("station", station.value, "host");

    // A scene revision is rendered before the after-patch reader is invoked.
    // Undo/redo must not briefly replace the editor with a loading placeholder.
    view.scene.tubeDesigner.product.assemblyBindingsRevision = "undo-revision";
    render();
    const revisionReload = binding.ensureProductAssemblyBindings(context, view, ops);
    station.setSelectionRange(2, 4); left.scrollTop = 300; right.scrollTop = 270;
    pending.shift().resolve({ ...nativeResult(), bindings: [{ bindingId: "saved-1", templateId: "joint", templateName: "已保存的孔槽工艺",
      participants: [{ role: "host", memberEntityId: "a" }, { role: "branch", memberEntityId: "b" }], canRemove: true }] }); await revisionReload;
    await new Promise(requestAnimationFrame);
    const revisionRefresh = { same: station === mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]'),
      focus: document.activeElement === station, selection: [station.selectionStart, station.selectionEnd], left: left.scrollTop, right: right.scrollTop };

    // Conditional station removal must preserve a different, focused role.
    const branchAnchor = mount.querySelector('[data-cam-change-action="tube-designer-binding-anchor"][data-tube-binding-role="branch"]');
    branchAnchor.focus({ preventScroll: true });
    await change("anchor", "end", "host");
    const conditional = { gone: !mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]'),
      same: branchAnchor === mount.querySelector('[data-cam-change-action="tube-designer-binding-anchor"][data-tube-binding-role="branch"]'), focus: document.activeElement === branchAnchor };
    await change("anchor", "side:top", "host");
    const stale = change("preview");
    await change("station", "130", "host");
    pending.shift().resolve(candidate()); await stale;
    const staleBlocked = !binding.productAssemblyCandidateCanApply(view, input);
    const unsupported = change("preview");
    pending.shift().resolve({ ...candidate({ status: "unsupported", reason: "resource unavailable", interfaceFitCertified: false },
      { status: "unsupported", reason: "mapping unavailable", interfaceFitCertified: false }), canApply: false, supportStatus: "unsupported", checks: [{ status: "unsupported", detail: "本方案尚不支持真实制造件" }] }); await unsupported;
    const candidateUnsupported = mount.querySelector("#tube-binding-candidate-whole-audit")?.textContent;
    const candidateNodeUnsupported = mount.querySelector("#tube-binding-candidate-node-audit")?.textContent;
    const unsupportedBlocked = mount.querySelector('[data-cam-action="tube-designer-binding-apply"]').disabled;
    const last = change("preview"); pending.shift().resolve(candidate()); await last;
    search.focus({ preventScroll: true }); search.setSelectionRange(1, 3);
    left.scrollTop = 140; right.scrollTop = 175;
    const apply = change("apply");
    left.scrollTop = 190; right.scrollTop = 215; search.setSelectionRange(1, 4);
    const applyRequest = pending.shift();
    if (!applyRequest?.method.endsWith("ApplyProductAssemblyBinding")) throw Error("missing native apply");
    applyRequest.resolve({ applied: true, tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" },
      product: { assemblyBindingsRevision: "after-apply" }, parts: [
        { entityId: "part-a", sourceMemberId: "a", name: "已加工横梁", thumbnailGeometryResourceId: "cut-a", thumbnailGeometryResourceVersion: 2 },
        { entityId: "part-b", sourceMemberId: "b", name: "立杆", thumbnailGeometryResourceId: "uncut-b", thumbnailGeometryResourceVersion: 1 },
      ] } });
    for (let attempt = 0; attempt < 12 && !pending.some((item) => item.method.endsWith("GetProductAssemblyBindings")); attempt += 1)
      await new Promise(requestAnimationFrame);
    const refreshRequest = pending.shift();
    if (!refreshRequest?.method.endsWith("GetProductAssemblyBindings")) throw Error("missing native binding refresh");
    refreshRequest.resolve(nativeResult()); await apply;
    const applied = { exploded: view.tubeDesignerAssemblyLibrary.exploded,
      part: view.tubeDesignerAssemblyLibrary.productPartId,
      notice: mount.querySelector(".tube-assembly-binding-notice")?.textContent,
      sameCanvas: canvas === mount.querySelector("canvas"), sameSearch: search === mount.querySelector("[data-test-search]"),
      focus: document.activeElement === search, selection: [search.selectionStart, search.selectionEnd],
      left: left.scrollTop, right: right.scrollTop };
    const plainCheck = change("preview");
    const plainCandidate = candidate();
    delete plainCandidate.nodeFinalAudit;
    pending.shift().resolve({ ...plainCandidate, manufacturingEffect: "connection-only",
      geometryChanges: [], workflow: { blankParts: [], partOperations: [], assemblySteps: [], bom: [] } });
    await plainCheck;
    const connectionOnlyCandidate = mount.querySelector("#tube-binding-candidate")?.textContent || "";
    const connectionOnlyNodeMissing = !mount.querySelector("#tube-binding-candidate-node-audit");
    const plainApply = change("apply");
    const plainApplyRequest = pending.shift();
    if (!plainApplyRequest?.method.endsWith("ApplyProductAssemblyBinding"))
      throw Error("missing connection-only native apply");
    plainApplyRequest.resolve({ applied: true, tubeDesigner: { activeProductId: "product-1",
      generationRun: { entityId: "run-1" }, product: { assemblyBindingsRevision: "after-plain" },
      parts: [{ entityId: "part-a", sourceMemberId: "a", name: "原长横梁" },
        { entityId: "part-b", sourceMemberId: "b", name: "原长竖杆" }] } });
    for (let attempt = 0; attempt < 12 && !pending.some((item) => item.method.endsWith("GetProductAssemblyBindings")); attempt += 1)
      await new Promise(requestAnimationFrame);
    const plainRefresh = pending.shift();
    if (!plainRefresh?.method.endsWith("GetProductAssemblyBindings"))
      throw Error("missing connection-only binding refresh");
    plainRefresh.resolve(nativeResult()); await plainApply;
    const connectionOnlyNotice = mount.querySelector(".tube-assembly-binding-notice")?.textContent;
    return { autoAnchors, committedApproachFace, firstPlanAudit, firstPlanNodeAudit, checkLoading, candidateMeasured, candidateNodeMeasured, candidateConflict, candidateNodeConflict, conflictCanApply,
      refreshPending, planConflict, planNodeConflict, planUnsupported, planNodeUnsupported, candidateUnsupported, candidateNodeUnsupported, asyncCheck, nestedResult, refresh, uncommittedText, revisionRefresh, conditional, staleBlocked, unsupportedBlocked, inputEvents, errors: [],
      savedFirst: mount.querySelector(".tube-assembly-bindings > section").classList.contains("tube-assembly-binding-saved"), applied,
      connectionOnlyCandidate, connectionOnlyNodeMissing, connectionOnlyNotice };
  }, fixedAngleTemplate);
  assert.deepEqual(result.autoAnchors, { host: { memberEntityId: "a", anchorId: "side:right", station: "250",
    anchorBase: { kind: "side", face: "right", reference: "start", station: 250 } },
    branch: { memberEntityId: "b", anchorId: "start", station: "",
      anchorBase: { kind: "end", end: "start", approachFace: "left" } } });
  assert.equal(result.committedApproachFace, "left");
  assert.equal(result.firstPlanAudit, "非相邻构件无穿透");
  assert.equal(result.firstPlanNodeAudit, "节点复测：1/1 处无材料穿透");
  assert.deepEqual(result.checkLoading, { text: "正在检查加工方案与整件几何…", focus: true, left: 150, right: 170, canvasSame: true });
  assert.equal(result.candidateMeasured, "非相邻构件无穿透");
  assert.equal(result.candidateNodeMeasured, "节点复测：1/1 处无材料穿透");
  assert.equal(result.candidateConflict, "发现 2 处非相邻构件穿透");
  assert.equal(result.candidateNodeConflict, "节点复测：2 处材料穿透");
  assert.equal(result.conflictCanApply, true, "审计结果仅提示，不应擅自改动原生候选提交条件");
  assert.deepEqual(result.refreshPending, { summary: "当前下料汇总 · 0/1 处连接 · 待验证 · 检查中…",
    audit: "整件几何检查中…", nodeAudit: "节点复测中…", samePlan: true, sameStation: true, focus: true, left: 250, right: 310, canvasSame: true });
  assert.equal(result.planConflict, "发现 1 处非相邻构件穿透");
  assert.equal(result.planNodeConflict, "节点复测：1 处材料穿透");
  assert.equal(result.planUnsupported, "整件几何检查暂不可用");
  assert.equal(result.planNodeUnsupported, "节点复测暂不可用");
  assert.equal(result.candidateUnsupported, "整件几何检查暂不可用");
  assert.equal(result.candidateNodeUnsupported, "节点复测暂不可用");
  assert.deepEqual(result.asyncCheck, { same: true, focus: true, selection: [0, 2], left: 205, right: 220 });
  assert.deepEqual(result.nestedResult, { same: true, top: 110, searchSame: true, focus: true, selection: [1, 3], left: 250, right: 310 });
  assert.deepEqual(result.refresh, { same: true, focus: true, selection: [1, 4], value: "128.5", left: 285, right: 245, canvasSame: true,
    planSame: true, planOpen: true });
  assert.deepEqual(result.uncommittedText, { value: "128.50", focus: true, selection: [3, 6] });
  assert.deepEqual(result.revisionRefresh, { same: true, focus: true, selection: [2, 4], left: 300, right: 270 });
  assert.deepEqual(result.conditional, { gone: true, same: true, focus: true });
  assert.equal(result.inputEvents, 2);
  assert.equal(result.staleBlocked, true);
  assert.equal(result.unsupportedBlocked, true);
  assert.equal(result.savedFirst, true);
  assert.deepEqual(result.applied, { exploded: true, part: "part-a",
    notice: "单件加工已应用，正显示“已加工横梁”；装配位置待核对。",
    sameCanvas: true, sameSearch: true, focus: true, selection: [1, 4], left: 190, right: 215 });
  assert.ok(!result.connectionOnlyCandidate.includes("仅新增单件减料加工"));
  assert.equal(result.connectionOnlyNodeMissing, true);
  assert.match(result.connectionOnlyNotice, /连接已记录/);
  assert.deepEqual(errors, []);
  const topology = await page.evaluate(async () => {
    const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const members = [{ memberEntityId: "a", name: "横管" }, { memberEntityId: "b", name: "竖管" }];
    const corner = { key: "corner", label: "转角", participants: members,
      properties: { topology: "L", participantAnchors: [
        { itemKey: "a", anchor: { kind: "end", end: "end" } },
        { itemKey: "b", anchor: { kind: "end", end: "start" } }] } };
    const inline = { ...corner, key: "inline", label: "直连",
      properties: { ...corner.properties, topology: "straight" } };
    const tee = { ...corner, key: "tee", label: "支管",
      properties: { ...corner.properties, topology: "T" } };
    const view = { scene: { tubeDesigner: { activeProductId: "p", generationRun: { entityId: "r" }, product: {} } } };
    const state = binding.productAssemblyBindingsState(view);
    const identity = binding.productAssemblyBindingIdentity(view);
    Object.assign(state, { key: identity.key, productKey: identity.productKey, status: "ready",
      result: { members, connections: [corner, inline, tee], bindings: [],
        capabilities: { anchorKinds: ["end", "side"], templates: [
          { templateId: "angle", supported: true }, { templateId: "slot", supported: true }] } } });
    const template = (id, topology) => ({ id, version: "1", displayName: id,
      productBinding: { mode: "incremental-cut", compatibleProductTopologies: [{ topology }],
        anchors: { memberA: "end", memberB: "end" } },
      participants: [{ role: "memberA" }, { role: "memberB" }] });
    const angle = template("angle", "L");
    angle.productBinding.compatibleProductTopologies = [
      { topology: "L", when: { op: "ne", parameter: "jointAngle", value: 0 } },
      { topology: "straight", when: { op: "eq", parameter: "jointAngle", value: 0 } },
    ];
    const slot = template("slot", "straight");
    const options = (input) => [...new DOMParser().parseFromString(
      binding.renderProductAssemblyBindings(view, input), "text/html")
      .querySelectorAll('[data-cam-change-action="tube-designer-binding-connection"] option')]
      .map((item) => ({ value: item.value, label: item.textContent }));
    const angleOptions = options({ template: angle, parameters: { jointAngle: 90 }, processDrafts: {} });
    const angleZeroOptions = options({ template: angle, parameters: { jointAngle: 0 }, processDrafts: {} });
    const angleMissingOptions = options({ template: angle, parameters: {}, processDrafts: {} });
    const slotInput = { template: slot, parameters: {}, processDrafts: {} };
    const slotOptions = options(slotInput);
    binding.productAssemblyBindingDraft(view, slot).connectionKey = "corner";
    const staleOptions = options(slotInput);
    let nativeCalls = 0;
    await binding.handleProductAssemblyBindingAction({ sceneProxy: { invoke() { nativeCalls += 1; } } },
      view, "tube-designer-binding-preview", {}, {}, () => slotInput);
    return { angleOptions, angleZeroOptions, angleMissingOptions, slotOptions, staleOptions, nativeCalls,
      previewError: binding.productAssemblyBindingDraft(view, slot).previewError,
      canApply: binding.productAssemblyCandidateCanApply(view, slotInput),
      legacyCompatible: binding.productAssemblyTemplateMatchesTopology({ productBinding: {} }, {}, corner) };
  });
  assert.deepEqual(topology.angleOptions.map((item) => item.value), ["", "corner"]);
  assert.deepEqual(topology.angleZeroOptions.map((item) => item.value), ["", "inline"]);
  assert.deepEqual(topology.angleMissingOptions.map((item) => item.value), [""]);
  assert.deepEqual(topology.slotOptions.map((item) => item.value), ["", "inline"]);
  assert.deepEqual(topology.staleOptions.map((item) => item.value), ["", "inline", "corner"]);
  assert.match(topology.staleOptions.at(-1).label, /不适用/);
  assert.equal(topology.nativeCalls, 0);
  assert.match(topology.previewError, /成品连接形状不兼容/);
  assert.equal(topology.canApply, false);
  assert.equal(topology.legacyCompatible, false);
  const threeRole = await page.evaluate(async (stored) => {
    const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const connections = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
    const model = await import("/src/apps/tube-designer/webpage/finishedProductModel.mjs");
    const template = model.assemblyProcessWithInput(stored);
    const view = { activeAreaId: "assemblies", scene: { tubeDesigner: {
      activeProductId: "p3", generationRun: { entityId: "r3" }, product: {},
    } } };
    const members = stored.participants.map((participant, index) => ({
      memberEntityId: `member-${index}`, itemKey: `item-${index}`, name: participant.label,
      length: 300, manufacturingMappingStatus: "persisted",
    }));
    const connection = { key: "corner3", kind: "corner", participants: members,
      properties: { topology: stored.productBinding.compatibleProductTopologies[0].topology,
        participantAnchors: members.map((member, index) => ({ itemKey: member.itemKey,
          anchor: { kind: "end", end: index === 0 ? "end" : "start" } })) } };
    const result = { productEntityId: "p3", generationRunId: "r3", members,
      connections: [connection], bindings: [], capabilities: { anchorKinds: ["end", "side"],
        templates: [{ templateId: stored.id, supported: true }] } };
    const identity = binding.productAssemblyBindingIdentity(view);
    Object.assign(binding.productAssemblyBindingsState(view), { key: identity.key,
      productKey: identity.productKey, status: "ready", result });
    Object.assign(connections.productAssemblyConnectionsState(view), {
      key: connections.productAssemblyConnectionIdentity(view).key, status: "ready", result,
    });
    const draft = binding.productAssemblyBindingDraft(view, template);
    draft.connectionKey = connection.key;
    const fixture = document.createElement("section");
    fixture.innerHTML = connections.renderProductAssemblyConnections(view, connection.key)
      + binding.renderProductAssemblyBindings(view, { template,
        parameters: Object.fromEntries(stored.parameters.map((p) => [p.key, p.defaultValue])), processDrafts: {} });
    document.body.append(fixture);
    const fields = [...fixture.querySelectorAll(".tube-assembly-binding-role")];
    const answer = { roles: fields.map((field) => field.id), legends: fields.map((field) => field.querySelector("legend").textContent),
      members: fields.map((field) => field.querySelector('[data-cam-change-action="tube-designer-binding-member"]').value),
      anchors: fields.map((field) => field.querySelector('[data-cam-change-action="tube-designer-binding-anchor"]').value),
      prompt: fixture.querySelector("#tube-binding-selection > small").textContent,
      connectionText: fixture.querySelector("[data-tube-product-connections]").textContent,
      noTwoMemberText: !fixture.textContent.includes("两管") && !fixture.textContent.includes("两根管"),
    };
    fixture.remove();
    return answer;
  }, cornerTemplate);
  assert.deepEqual(threeRole.roles, ["tube-binding-role-memberA", "tube-binding-role-memberB", "tube-binding-role-memberC"]);
  assert.deepEqual(threeRole.members, ["member-0", "member-1", "member-2"]);
  assert.deepEqual(threeRole.anchors, ["end", "start", "start"]);
  assert.match(threeRole.legends[2], /^C · C 边 · C 边$/);
  assert.match(threeRole.prompt, /A、B、C 顺序分配/);
  assert.match(threeRole.connectionText, /A：A 边 \+ B：B 边 \+ C：C 边/);
  assert.equal(threeRole.noTwoMemberText, true);
  if (process.env.ICAX_ASSEMBLY_BINDING_SCREENSHOT) {
    const catalogueRoot = fileURLToPath(new URL("../../apps/tube-designer/templates/assembly/", import.meta.url));
    const templates = readdirSync(catalogueRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory())
      .map((entry) => resolve(catalogueRoot, entry.name, "assembly.json")).filter(existsSync)
      .map((file) => JSON.parse(readFileSync(file, "utf8")));
    await page.evaluate(async (templates) => {
      const connections = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
      const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
      const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
      const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: templates,
        scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: {} } } };
      const state = binding.productAssemblyBindingsState(view);
      const identity = binding.productAssemblyBindingIdentity(view);
      state.key = identity.key; state.productKey = identity.productKey; state.status = "ready";
      state.result = { productEntityId: "product-1", generationRunId: "run-1", bindings: [],
        members: [{ memberEntityId: "a", name: "中间横梁", length: 800 }, { memberEntityId: "b", name: "左侧竖杆", length: 1200 }],
        connections: [{ key: "joint-ab", participants: [{ memberEntityId: "a", name: "中间横梁" }, { memberEntityId: "b", name: "左侧竖杆" }], properties: { topology: "parallel" } }],
        capabilities: { anchorKinds: ["end", "side"], detail: "支持端部和侧面的新增减料加工", templates: [{ templateId: "mechanical-fastener", supported: true, detail: "当前可生成孔系加工，装配位置仍需核对。" }] } };
      view.tubeDesignerAssemblyProductConnections = { key: connections.productAssemblyConnectionIdentity(view).key, status: "ready", result: { productEntityId: "product-1", generationRunId: "run-1", connections: [] } };
      const template = library.selectedAssemblyTemplate(view);
      const input = { template, parameters: library.assemblyParameterValues(view, template), processDrafts: {} };
      const draft = binding.productAssemblyBindingDraft(view, template);
      draft.connectionKey = "joint-ab";
      draft.participants = { fastenedPart: { memberEntityId: "a", anchorId: "side:top", station: "120" }, basePart: { memberEntityId: "b", anchorId: "side:bottom", station: "320" } };
      draft.previewStatus = "ready"; draft.previewKey = binding.productAssemblyBindingInputKey(view, input);
      draft.preview = { canApply: true, supportStatus: "supported", candidateToken: "token", manufacturingEffect: "incremental-cut", assemblyFitStatus: "not-verified",
        checks: [{ status: "pass", detail: "两件真实构件的孔位位于有效长度内" }], workflow: {
          blankParts: [{ label: "横梁下料件", participantRoles: ["fastenedPart"] }, { label: "竖杆下料件", participantRoles: ["basePart"] }],
          partOperations: [{ label: "对穿螺栓孔系", participantRoles: ["fastenedPart", "basePart"] }],
          assemblySteps: [{ label: "穿入螺栓并放置垫片", kindLabel: "机械连接" }],
          bom: [{ label: "螺栓", quantity: 2, unit: "套", specificationStatus: "requires-standard" }],
        } };
      document.querySelector(".cam-context-pane").innerHTML = library.renderAssemblyLibraryLeftPane({}, view);
      document.querySelector(".cam-info-pane").innerHTML = library.renderAssemblyLibraryRightPane({}, view);
      const top = document.querySelector(".cam-viewport header");
      top.textContent = "孔系紧固 · 工艺示例（布局检查夹具未加载三维几何）";
    }, templates);
    await page.addStyleTag({ content: ".tube-assembly-binding-workflow{max-height:none;overflow:visible}.cam-viewport canvas{position:absolute;inset:0}.cam-viewport .tube-assembly-preview-pane{position:absolute;inset:0}.cam-viewport header{padding:16px;color:#dcebe9;font:12px sans-serif}" });
    const file = resolve(process.env.ICAX_ASSEMBLY_BINDING_SCREENSHOT);
    mkdirSync(dirname(file), { recursive: true });
    await page.screenshot({ path: file });
    await page.locator(".tube-connection-library-editor-body").evaluate((node) => { node.scrollTop = node.scrollHeight; });
    await page.screenshot({ path: file.replace(/\.png$/, "-result.png") });
  }
  console.log("TubeDesignerProductAssemblyBindings.browser: passed");
} finally { await browser.close(); }
