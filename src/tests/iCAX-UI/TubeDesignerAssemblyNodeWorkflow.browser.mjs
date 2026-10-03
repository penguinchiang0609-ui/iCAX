import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1320, height: 820 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const source = fileURLToPath(new URL("../../", import.meta.url));
  const template = (id) => JSON.parse(readFileSync(resolve(source,
    `apps/tube-designer/templates/assembly/${id}/assembly.json`), "utf8"));
  const realTemplates = {
    paired: template("wrap-a-over-b"),
    integrated: template("node-v-notch-integrated"),
    edgeArc: template("node-edge-arc-integrated"),
    miter: template("two-end-end-angle"),
    tee: template("t-profile-insert"),
  };
  await page.route("http://assembly-node.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT && pathname.startsWith("/src/apps/tube-designer/") ? resolve(process.env.ICAX_ASSEMBLY_RUNTIME_ROOT, "apps/tube-designer", pathname.slice("/src/apps/tube-designer/".length)) : file, "utf8") });
  });
  await page.goto("http://assembly-node.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `body{margin:0}.cam-workbench{display:grid;grid-template-columns:310px minmax(0,1fr) 390px;height:780px}.cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}.cam-viewport{position:relative;background:#13252d}.tube-connection-library-list{max-height:165px}.tube-connection-library-editor-body{height:180px;overflow:auto}` });
  const result = await page.evaluate(async ({ paired, integrated, edgeArc, miter, tee }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const binding = await import("/src/apps/tube-designer/webpage/productAssemblyBindings.mjs");
    const connections = await import("/src/apps/tube-designer/webpage/productAssemblyConnections.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const filler = Array.from({ length: 7 }, (_, index) => ({ ...paired, id: `filler-${index}`, displayName: `其他工艺 ${index + 1}` }));
    const threePart = { ...paired, id: "three-part", displayName: "三管工艺", participants: [...paired.participants, { role: "memberC", label: "第三管" }] };
    const node = { key: "corner-1", kind: "corner", properties: { topology: "L" },
      nodeGeometry: { nodeGeometryStatus: "verified", axisAngleDeg: 90 }, participants: [
      { memberEntityId: "member-a", itemKey: "frame.left", name: "左边框", manufacturingMappingStatus: "persisted" },
      { memberEntityId: "member-b", itemKey: "frame.top", name: "上边框", manufacturingMappingStatus: "persisted" },
    ] };
    const teeNode = { key: "tee-1", kind: "tee", nodeGeometry: { nodeGeometryStatus: "verified", axisAngleDeg: 90 }, properties: { topology: "T", participantAnchors: [
      { itemKey: "frame.left", anchor: { kind: "side", face: "top", reference: "start", station: 100 } },
      { itemKey: "frame.top", anchor: { kind: "end", end: "start" } },
    ] }, participants: node.participants };
    const nativeResult = () => ({ productEntityId: "product-1", generationRunId: "run-1", sourceRevision: "1",
      members: [{ memberEntityId: "member-a", name: "左边框", length: 420 }, { memberEntityId: "member-b", name: "上边框", length: 360 }],
      bindings: [], connections: [node, teeNode], capabilities: { anchorKinds: ["end", "side"], templates: [
        { templateId: paired.id, supported: true }, { templateId: tee.id, supported: true },
        { templateId: integrated.id, supported: false },
      ] } });
    const view = { activeAreaId: "assemblies", scene: { tubeDesigner: { activeProductId: "product-1", generationRun: { entityId: "run-1" }, product: {} } },
      tubeDesignerAssemblyTemplates: [paired, integrated, edgeArc, miter, tee, ...filler, threePart],
      tubeDesignerAssemblyLibrary: { selectedId: paired.id, workMode: "product", workModeUserSelected: true },
      tubeDesignerAssemblyProductConnections: { key: "product-1/run-1/false/false", status: "ready",
        result: { productEntityId: "product-1", generationRunId: "run-1", source: "committed-product-model", modelOutdated: false, connections: [node, teeNode] } },
      tubeDesignerProductAssemblyBindings: { key: "product-1/run-1/false/", productKey: "product-1/run-1", status: "ready", result: nativeResult(),
        drafts: {}, mutation: null, notice: "", epoch: 0 } };
    view.tubeDesignerAssemblyProductConnections.key = connections.productAssemblyConnectionIdentity(view).key;
    view.tubeDesignerProductAssemblyBindings.key = binding.productAssemblyBindingIdentity(view).key;
    const pending = [];
    const context = { sceneProxy: { invoke(method) {
      if (method !== "TubeDesigner.GetProductAssemblyBindings") throw new Error(`unexpected ${method}`);
      return new Promise((resolve) => pending.push(resolve));
    } } };
    const render = () => {
      const left = library.renderAssemblyLibraryLeftPane(context, view);
      const right = library.renderAssemblyLibraryRightPane(context, view);
      const mount = document.querySelector("main");
      if (!mount) {
        document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane">${left}</aside><div class="cam-viewport"><canvas></canvas></div><aside class="cam-info-pane">${right}</aside></div></main>`;
        patch.rememberLibraryDom(view, document.querySelector("main"), "");
      } else if (!patch.patchLibraryDom(view, mount, { left, right, overlay: "", suffix: "" })) throw new Error("节点编辑没有走局部更新");
    };
    const ops = { renderProject: render };
    render();
    const mount = document.querySelector("main");
    const canvas = mount.querySelector("canvas");
    const firstStage = { prompt: mount.querySelector(".cam-info-pane")?.textContent.includes("请在左侧选择连接节点"),
      cardsHidden: !mount.querySelector("[data-tube-assembly-id]") };
    const nodeButton = mount.querySelector('[data-tube-connection-key="corner-1"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select-connection", nodeButton, ops);
    const nodeStage = { labels: mount.querySelector(".tube-assembly-product-connections")?.textContent.includes("A：左边框 + B：上边框"),
      paired: !!mount.querySelector('[data-tube-assembly-id="wrap-a-over-b"]'),
      duplicateAbsent: !mount.querySelector('[data-tube-assembly-id="wrap-b-over-a"]'),
      miter: !!mount.querySelector('[data-tube-assembly-id="two-end-end-angle"]'),
      integrated: mount.querySelector('[data-tube-assembly-id="node-v-notch-integrated"]')?.textContent.includes("需一体下料生成"),
      edgeArc: mount.querySelector('[data-tube-assembly-id="node-edge-arc-integrated"]')?.textContent.includes("需一体下料生成"),
      threePartHidden: !mount.querySelector('[data-tube-assembly-id="three-part"]'),
      noTemplateChosen: mount.querySelector(".tube-connection-library-editor-heading")?.textContent.includes("选择装配工艺")
        && !mount.querySelector("#tube-binding-role-memberA") };
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select",
      mount.querySelector('[data-tube-assembly-id="node-v-notch-integrated"]'), ops);
    const integratedBlocked = mount.querySelector(".cam-info-pane")?.textContent.includes("当前产品已拆为两件，暂不能应用")
      && !mount.querySelector('[data-cam-action="tube-designer-binding-preview"]');
    const templateButton = mount.querySelector('[data-tube-assembly-id="wrap-a-over-b"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select", templateButton, ops);
    const selected = { roles: mount.querySelector("#tube-binding-role-memberA")?.textContent.includes("主件")
        && mount.querySelector("#tube-binding-role-memberA")?.textContent.includes("左边框")
        && mount.querySelector("#tube-binding-role-memberB")?.textContent.includes("支件")
        && mount.querySelector("#tube-binding-role-memberB")?.textContent.includes("上边框"),
      autoAssigned: binding.productAssemblyBindingDraft(view, paired).participants.memberA.memberEntityId === "member-a"
        && binding.productAssemblyBindingDraft(view, paired).participants.memberB.memberEntityId === "member-b" };
    const selectMember = async (role, memberId) => {
      const field = mount.querySelector(`[data-cam-change-action="tube-designer-binding-member"][data-tube-binding-role="${role}"]`);
      field.value = memberId;
      await library.handleAssemblyLibraryAction(context, view, "tube-designer-binding-member", field, ops);
    };
    await selectMember("memberA", "member-b");
    await selectMember("memberB", "member-a");
    const swappedRoles = binding.productAssemblyBindingDraft(view, paired).participants.memberA.memberEntityId === "member-b"
      && binding.productAssemblyBindingDraft(view, paired).participants.memberB.memberEntityId === "member-a";
    await selectMember("memberA", "member-a");
    await selectMember("memberB", "member-b");
    const endOnly = [...mount.querySelector('[data-cam-change-action="tube-designer-binding-anchor"][data-tube-binding-role="memberA"]').options]
      .every((option) => !option.value.startsWith("side:"))
      && !mount.querySelector('[data-cam-change-action="tube-designer-binding-station"]');
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select-connection",
      mount.querySelector('[data-tube-connection-key="tee-1"]'), ops);
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select",
      mount.querySelector('[data-tube-assembly-id="t-profile-insert"]'), ops);
    const hostAnchor = mount.querySelector('[data-cam-change-action="tube-designer-binding-anchor"][data-tube-binding-role="host"]');
    const sideOnly = [...hostAnchor.options].some((option) => option.value === "side:top")
      && [...hostAnchor.options].every((option) => !["start", "end"].includes(option.value));
    const station = mount.querySelector('[data-cam-change-action="tube-designer-binding-station"][data-tube-binding-role="host"]');
    station.value = "120";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-binding-station", station, ops);
    const textInput = mount.querySelector('[data-cam-change-action="tube-designer-binding-station"][data-tube-binding-role="host"]');
    const leftScroll = mount.querySelector(".tube-connection-library-list");
    const rightScroll = mount.querySelector(".tube-connection-library-editor-body");
    let inputEvents = 0;
    textInput.addEventListener("input", () => inputEvents += 1);
    textInput.focus({ preventScroll: true }); textInput.setSelectionRange(1, 3);
    leftScroll.scrollTop = 90; rightScroll.scrollTop = 120;
    const refresh = binding.ensureProductAssemblyBindings(context, view, ops, true);
    leftScroll.scrollTop = 125; rightScroll.scrollTop = 160; textInput.setSelectionRange(0, 3);
    pending.shift()(nativeResult()); await refresh;
    textInput.dispatchEvent(new Event("input"));
    const asyncStable = { canvas: canvas === mount.querySelector("canvas"), input: textInput === mount.querySelector('[data-cam-change-action="tube-designer-binding-station"][data-tube-binding-role="host"]'),
      focus: document.activeElement === textInput, selection: [textInput.selectionStart, textInput.selectionEnd],
      left: leftScroll.scrollTop, right: rightScroll.scrollTop, inputEvents };
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select-connection",
      mount.querySelector('[data-tube-connection-key="corner-1"]'), ops);
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-select",
      mount.querySelector('[data-tube-assembly-id="wrap-a-over-b"]'), ops);
    const roleField = mount.querySelector('[data-cam-change-action="tube-designer-binding-member"][data-tube-binding-role="memberA"]');
    const maleFemale = mount.querySelector('[data-tube-assembly-parameter="maleFemale"]');
    const beforeCondition = !mount.querySelector('[data-tube-assembly-parameter="pairCount"]');
    roleField.focus({ preventScroll: true });
    leftScroll.scrollTop = 125; rightScroll.scrollTop = 160;
    maleFemale.checked = true;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", maleFemale, ops);
    const groups = mount.querySelector('[data-tube-assembly-parameter="pairCount"]');
    const conditional = { beforeCondition, appeared: !!groups, canvas: canvas === mount.querySelector("canvas"),
      role: roleField === mount.querySelector('[data-cam-change-action="tube-designer-binding-member"][data-tube-binding-role="memberA"]'),
      focus: document.activeElement === roleField, left: leftScroll.scrollTop, right: rightScroll.scrollTop };
    groups.value = "four";
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", groups, ops);
    const fourGroups = library.assemblyParameterValues(view, paired).pairCount === "four";
    const maleFemaleAfter = mount.querySelector('[data-tube-assembly-parameter="maleFemale"]');
    groups.closest("details").open = true;
    groups.focus({ preventScroll: true });
    const focusedConditional = document.activeElement === groups;
    maleFemaleAfter.checked = false;
    await library.handleAssemblyLibraryAction(context, view, "tube-designer-assembly-parameter-change", maleFemaleAfter, ops);
    return { firstStage, nodeStage, integratedBlocked, swappedRoles, selected, endOnly, sideOnly, asyncStable, conditional, fourGroups,
      removed: !mount.querySelector('[data-tube-assembly-parameter="pairCount"]'),
      focusNotStolen: document.activeElement !== roleField && document.activeElement !== maleFemaleAfter, focusedConditional,
      storedDraft: library.assemblyParameterValues(view, paired).pairCount === "four" };
  }, realTemplates);
  assert.deepEqual(result.firstStage, { prompt: true, cardsHidden: true });
  assert.deepEqual(result.nodeStage, { labels: true, paired: true, duplicateAbsent: true, miter: true,
    integrated: true, edgeArc: true, threePartHidden: true, noTemplateChosen: true });
  assert.equal(result.integratedBlocked, true);
  assert.equal(result.swappedRoles, true, "同一包接模板可交换主件和支件的实际构件");
  assert.deepEqual(result.selected, { roles: true, autoAssigned: true });
  assert.equal(result.endOnly, true, "端部包接模板只提供声明的端部位置");
  assert.equal(result.sideOnly, true, "端中连接模板只提供声明的主管侧面位置");
  assert.deepEqual(result.asyncStable, { canvas: true, input: true, focus: true, selection: [0, 3],
    left: 125, right: 160, inputEvents: 1 });
  assert.equal(result.conditional.beforeCondition, true);
  assert.equal(result.conditional.appeared, true);
  assert.equal(result.conditional.canvas, true);
  assert.equal(result.conditional.role, true);
  assert.equal(result.conditional.focus, true);
  assert.ok(result.conditional.left > 0 && result.conditional.right > 0);
  assert.equal(result.fourGroups, true);
  assert.equal(result.removed, true);
  assert.equal(result.focusedConditional, true);
  assert.equal(result.focusNotStolen, true);
  assert.equal(result.storedDraft, true, "关闭高级公母选项时保留组数草稿");
  assert.deepEqual(errors, []);
  console.log("TubeDesignerAssemblyNodeWorkflow.browser: passed");
} finally { await browser.close(); }
