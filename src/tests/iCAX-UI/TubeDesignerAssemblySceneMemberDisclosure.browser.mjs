import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";

const source = fileURLToPath(new URL("../../", import.meta.url));
const template = JSON.parse(readFileSync(new URL(
  "../../apps/tube-designer/templates/assembly/two-end-end-angle/assembly.json", import.meta.url), "utf8"));
const alternate = { ...template, id: "alternate-end-angle", displayName: "另一种端切工艺" };
const profile = {
  id: "rect", name: "方管", profileForm: "parametric",
  defaultParameters: { width: 60, depth: 40, note: "初始" },
  descriptor: { parameters: [
    { key: "width", displayName: "宽", valueType: "number", defaultValue: 60, min: 1 },
    { key: "depth", displayName: "高", valueType: "number", defaultValue: 40, min: 1 },
    { key: "note", displayName: "标记", valueType: "string", defaultValue: "初始" },
  ] },
  previewProfile: {
    name: "rect-60", parameters: { width: 60, depth: 40 },
    contours: [{ kind: "roundedRectangle", center: [0, 0], width: 60, height: 40, radius: 3 }],
    parameterDiagram: { schemaVersion: 1, annotations: [
      { parameter: "width", kind: "linear", from: [-30, 20], to: [30, 20], axis: "x", side: "top" },
      { parameter: "depth", kind: "linear", from: [-30, -20], to: [-30, 20], axis: "y", side: "left" },
    ] },
  },
};

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 650 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("http://assembly-member-disclosure.test/**", (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<body></body>" });
    const file = resolve(source, pathname.replace(/^\/src\//, ""));
    if (!file.startsWith(source.replace(/[\\/]$/, "") + sep)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(file, "utf8") });
  });
  await page.goto("http://assembly-member-disclosure.test/");
  await page.addStyleTag({ content: tubeDesignerCss + `
    body{margin:0}.cam-workbench{display:grid;grid-template-columns:240px minmax(0,1fr) 340px;height:650px}
    .cam-context-pane,.cam-info-pane{min-width:0;min-height:0;overflow:hidden}
    .cam-viewport{min-width:0;position:relative;background:#13252d}
    .left-scroll{height:120px;overflow:auto}.tube-connection-library-editor-body{height:180px;overflow:auto}
  ` });
  const result = await page.evaluate(async ({ template, alternate, profile }) => {
    const library = await import("/src/apps/tube-designer/webpage/assemblyLibrary.mjs");
    const patch = await import("/src/apps/tube-designer/webpage/libraryDomPatch.mjs");
    const profileDiagram = await import("/src/apps/tube-designer/webpage/profileParameterDiagram.mjs");
    const view = { activeAreaId: "assemblies", tubeDesignerAssemblyTemplates: [template, alternate],
      tubeDesignerSystemProfiles: [profile], tubeDesignerAssemblyLibrary: { selectedId: template.id,
        workMode: "example", workModeUserSelected: true } };
    const evaluations = [];
    const context = { sceneProxy: { invoke(method, payload) {
      if (method !== "TubeDesigner.EvaluateProfilePackage") throw new Error(`unexpected ${method}`);
      return new Promise((resolve) => evaluations.push({ payload, resolve }));
    } } };
    document.body.innerHTML = `<main><div class="cam-workbench"><aside class="cam-context-pane"></aside>
      <div class="cam-viewport"><canvas id="stable"></canvas></div><aside class="cam-info-pane"></aside></div></main>`;
    const mount = document.querySelector("main");
    context.mount = mount;
    const render = () => {
      const right = library.renderAssemblyLibraryRightPane(context, view);
      if (!patch.patchLibraryDom(view, mount, { left: `<div class="left-scroll"><div style="height:500px">左侧</div></div>`,
        right, overlay: "", suffix: "" })) {
        mount.querySelector(".cam-context-pane").innerHTML = `<div class="left-scroll"><div style="height:500px">左侧</div></div>`;
        mount.querySelector(".cam-info-pane").innerHTML = right;
        patch.rememberLibraryDom(view, mount, "");
      }
      library.bindAssemblyParameterDiagrams(mount);
      profileDiagram.bindProfileParameterDiagrams(mount);
    };
    view.tubeDesignerAssemblyLibraryRenderProject = render;
    const snapshot = (width) => ({ ...profile.previewProfile, name: `rect-${width}`,
      parameters: { width, depth: 40 },
      contours: [{ kind: "roundedRectangle", center: [0, 0], width, height: 40, radius: 3 }] });
    render();
    const card = (role) => mount.querySelector(`details[data-finished-span="${role}"]`);
    const initiallyClosed = !card("armA").open && !card("armB").open;
    const armBeforeAngle = card("armB").compareDocumentPosition(
      mount.querySelector('[data-finished-parameter="angle"]')) & Node.DOCUMENT_POSITION_FOLLOWING;
    const collapsedInputHidden = card("armA").querySelector("input").getClientRects().length === 0;
    card("armA").querySelector("summary").click();
    await new Promise((resolve) => setTimeout(resolve, 0));
    const firstEvaluation = evaluations.length === 1 && evaluations[0].payload.parameters.width === 60;
    const initialDiagram = !!card("armA").querySelector("svg.td-profile-parameter-svg")
      && card("armA").querySelectorAll("[data-profile-annotation-key]").length === 2;
    const initialSummary = card("armA").querySelector("summary").textContent.includes("260 mm");
    const canvas = mount.querySelector("canvas");
    let canvasClicks = 0;
    canvas.addEventListener("click", () => { canvasClicks += 1; });
    const note = card("armA").querySelector('[data-finished-profile-parameter="note"]');
    note.focus({ preventScroll: true });
    note.setSelectionRange(1, 2);
    const left = mount.querySelector(".left-scroll");
    const right = mount.querySelector(".tube-connection-library-editor-body");
    left.scrollTop = 45;
    right.scrollTop = 65;
    const width = card("armA").querySelector('[data-finished-profile-parameter="width"]');
    width.value = "72";
    await library.handleAssemblyLibraryAction(context, view,
      "tube-designer-finished-profile-parameter-change", width, { renderProject: render });
    await Promise.resolve();
    right.scrollTop = Math.min(right.scrollTop + 20, right.scrollHeight);
    const scrolledBeforeResponse = right.scrollTop;
    const newEvaluation = evaluations.length === 2 && evaluations[1].payload.parameters.width === 72;
    evaluations[1].resolve({ profile: snapshot(72) });
    for (let i = 0; i < 10 && !card("armA").querySelector("svg[aria-label='rect-72参数尺寸示意图']"); i += 1)
      await new Promise((resolve) => setTimeout(resolve, 0));
    evaluations[0].resolve({ profile: snapshot(60) });
    await new Promise((resolve) => setTimeout(resolve, 0));
    canvas.click();
    const freshDiagram = !!card("armA").querySelector("svg[aria-label='rect-72参数尺寸示意图']")
      && card("armA").querySelector('[data-profile-annotation-key="width"]')?.getAttribute("aria-label").includes("72");
    const preserved = card("armA").open && !card("armB").open
      && card("armA").querySelector('[data-finished-profile-parameter="note"]') === note
      && document.activeElement === note && note.selectionStart === 1 && note.selectionEnd === 2
      && mount.querySelector("canvas") === canvas && canvasClicks === 1
      && left.scrollTop === 45 && right.scrollTop === scrolledBeforeResponse;
    view.tubeDesignerAssemblyLibrary.selectedId = alternate.id;
    render();
    const alternateSharesDisclosure = card("armA").open && !card("armB").open;
    view.tubeDesignerAssemblyLibrary.selectedId = template.id;
    render();
    const restoredAfterSwitch = card("armA").open && !card("armB").open;
    return { initiallyClosed, armBeforeAngle: !!armBeforeAngle, collapsedInputHidden,
      firstEvaluation, initialDiagram, initialSummary, newEvaluation, freshDiagram,
      preserved, alternateSharesDisclosure, restoredAfterSwitch };
  }, { template, alternate, profile });
  assert.deepEqual(errors, []);
  for (const [key, value] of Object.entries(result)) assert.equal(value, true, key);
  console.log("assembly scene member disclosure Edge regression passed");
} finally {
  await browser.close();
}
