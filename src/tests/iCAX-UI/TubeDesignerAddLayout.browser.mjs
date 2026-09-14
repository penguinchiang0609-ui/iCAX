// Run with Playwright installed, or set ICAX_PLAYWRIGHT_MODULE to its module URL.
// No application connection: exercise the shipped HTML/CSS in an isolated browser.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { renderDesignerAddDialog } from "../../apps/tube-designer/webpage/designerViews.mjs";
import { tubeDesignerCss } from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
import { catalogText } from "../../apps/tube-designer/webpage/productCatalog.mjs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const templates = ["single"].map((kind) => {
  const raw = JSON.parse(readFileSync(new URL(`../../apps/tube-designer/templates/product/${kind}_face_security_window/template.json`, import.meta.url)));
  const groups = raw.groups.map((g) => ({ ...g, displayName: catalogText(g.displayName) }));
  return { ...raw, available: true, name: catalogText(raw.displayName), groups,
    parameters: raw.parameters.map((p) => ({ ...p, displayName: catalogText(p.displayName),
      type: { enum: "select", string: "text" }[p.valueType] || p.valueType,
      groupKey: p.group, group: groups.find((g) => g.key === p.group)?.displayName,
      options: p.choices?.map((c) => ({ ...c, label: catalogText(c.displayName) })),
    })),
  };
});
const browser = await chromium.launch({ headless: true, ...(process.env.ICAX_BROWSER_CHANNEL ? { channel: process.env.ICAX_BROWSER_CHANNEL } : {}) });
try {
  const page = await browser.newPage();
  page.on("requestfailed", (request) => console.error("Failed module request:", request.url(), request.failure()?.errorText));
  // Serve imports only from this repository's src tree; never attach to the user's app.
  const sourceRoot = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://tube-designer.test/**", async (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") return route.fulfill({ contentType: "text/html", body: "<!doctype html><body></body>" });
    const path = resolve(sourceRoot, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/") || !path.startsWith(sourceRoot.replace(/[\\/]$/, "") + sep) || !/\.m?js$/.test(path)) return route.abort();
    await route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://tube-designer.test/");
  async function assertNoOverlap() {
    const problems = await page.evaluate(() => {
      const issues = [];
      const visible = (el) => el.getClientRects().length && el.getBoundingClientRect().height > 0;
      // The editor tabs are intentionally sticky over the scrolling form; test
      // ordinary parameter containers rather than flagging that overlay.
      for (const parent of document.querySelectorAll(".tube-designer-subsection-list, .tube-designer-config-subsection-content")) {
        if (!visible(parent)) continue;
        const children = [...parent.children].filter(visible);
        for (let i = 1; i < children.length; i++) {
          if (children[i].getBoundingClientRect().top < children[i - 1].getBoundingClientRect().bottom - 1)
            issues.push(`Overlapping siblings in ${parent.className}`);
        }
      }
      const pane = document.querySelector(".tube-designer-config-parameters");
      if (pane.scrollWidth > pane.clientWidth + 1) issues.push("Horizontal overflow");
      for (const details of pane.querySelectorAll("details[open]")) {
        if (!visible(details)) continue;
        const last = details.lastElementChild;
        if (visible(last) && last.getBoundingClientRect().bottom > details.getBoundingClientRect().bottom + 1)
          issues.push("Expanded content escapes its section");
      }
      return issues;
    });
    assert.deepEqual(problems, []);
  }
  for (const viewport of [{ width: 1600, height: 1000 }, { width: 1024, height: 768 }]) {
    await page.setViewportSize(viewport);
    for (const template of templates) {
      const html = renderDesignerAddDialog({ templates }, {
        tubeDesignerAddTemplateId: template.id, tubeDesignerAddInstanceName: "布局检查",
        tubeDesignerExpandedTemplateGroupIds: ["template-path:防盗窗", "template-path:防盗窗/常用款式", "template-path:防盗窗/局部围护（需现场封闭）"],
      });
      await page.setContent(`<style>*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif}${tubeDesignerCss}</style><div class="tube-designer-workspace">${html}</div>`);
      await assertNoOverlap();
      assert.equal(await page.locator(".tube-designer-review-disclosure").count(), 0);
      const roots = page.locator('[data-tube-designer-add-form] .tube-designer-add-structure-fields > .tube-designer-config-subsection');
      assert.equal(await roots.count(), 1);
      await page.locator('.tube-designer-config-parameters details').evaluateAll((nodes) => nodes.forEach((node) => { node.open = true; }));
      await assertNoOverlap();
      assert.equal(await page.locator('.tube-designer-profile-field').count(), 0);
      await roots.nth(0).locator(":scope > summary").click();
      await assertNoOverlap();
      await roots.nth(0).locator(":scope > summary").click();
      await assertNoOverlap();
      const reachable = await page.locator('.tube-designer-config-parameters').evaluate((pane) => {
        pane.scrollTop = pane.scrollHeight;
        return pane.lastElementChild.getBoundingClientRect().bottom <= pane.getBoundingClientRect().bottom + 1;
      });
      assert.ok(reachable, "Last section must be reachable by scrolling");
    }
  }
  await page.setViewportSize({ width: 1600, height: 1000 });
  const interaction = await page.evaluate(async ({ templates, css }) => {
    const { handleDesignerAreaAction } = await import("/src/apps/tube-designer/webpage/designerActions.mjs");
    const { renderDesignerAddDialog, renderDesignerRightPane } = await import("/src/apps/tube-designer/webpage/designerViews.mjs");
    const { bindProductParameterDiagrams } = await import("/src/apps/tube-designer/webpage/productParameterDiagram.mjs");
    const template = templates.find((t) => t.id === "single-face-security-window");
    const view = { scene: { tubeDesigner: { templates } }, tubeDesignerAddTemplateId: template.id, tubeDesignerAddDialogOpen: true };
    document.body.innerHTML = `<style>*{box-sizing:border-box}body{margin:0}${css}</style><div class="tube-designer-workspace"></div>`;
    const mount = document.querySelector(".tube-designer-workspace");
    const context = { mount };
    const ops = { renderProject() { mount.innerHTML = renderDesignerAddDialog(view.scene.tubeDesigner, view); bindProductParameterDiagrams(mount); } };
    ops.renderProject();
    if (mount.querySelector('[data-cam-change-action="tube-designer-profile-selection-change"]')) throw new Error("Add dialog must not expose material/profile selection");
    const field = (key) => mount.querySelector(`[data-tube-designer-parameter="${key}"]`);
    const group = (key) => mount.querySelector(`details[data-tube-designer-parameter-group="${key}"]`);
    for (const node of mount.querySelectorAll("details")) node.open = true;
    let parameterScroller = mount.querySelector(".tube-designer-config-parameters");
    const initialDiagramLabel = mount.querySelector(".tube-designer-product-structure-svg")?.getAttribute("aria-label");
    const faceType = field("faceType");
    faceType.scrollIntoView({ block: "center" });
    const faceTypeTop = faceType.getBoundingClientRect().top;
    faceType.focus({ preventScroll: true });
    faceType.value = "three";
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", faceType, ops);
    await new Promise(queueMicrotask);
    const threeFaceDiagramLabel = mount.querySelector(".tube-designer-product-structure-svg")?.getAttribute("aria-label");
    const faceTypeFocus = document.activeElement === field("faceType");
    const beforeOpeningFace = mount.querySelector(".product-diagram-door")?.getAttribute("points");
    const openingFace = field("accessDoorFace3");
    openingFace.focus({ preventScroll: true });
    openingFace.value = "left";
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", openingFace, ops);
    await new Promise(queueMicrotask);
    const afterOpeningFace = field("accessDoorFace3");
    const diagramInteraction = {
      initialDiagramLabel,
      threeFaceDiagramLabel,
      focus: faceTypeFocus,
      scrollAnchorStable: Math.abs(field("faceType").getBoundingClientRect().top - faceTypeTop) <= 1,
      sideFaces: mount.querySelectorAll(".product-diagram-face-side").length,
      leftDimension: !!mount.querySelector('[data-tube-designer-parameter-key="leftWidth"]'),
      rightDimension: !!mount.querySelector('[data-tube-designer-parameter-key="rightWidth"]'),
      openingFaceFocus: document.activeElement === afterOpeningFace,
      openingFaceHighlighted: afterOpeningFace.classList.contains("is-active")
        && !!mount.querySelector('[data-product-diagram-parameter~="accessDoorFace3"].is-active'),
      openingFaceMoved: beforeOpeningFace !== mount.querySelector(".product-diagram-door")?.getAttribute("points"),
      // Fine dimensions are intentionally not rendered in the style picker;
      // selecting a side opening must still prepare a feasible value for the
      // subsequent native preview/Confirm request.
      openingSurfaceSized: Number(view.tubeDesignerAddDraft?.leftWidth) >= 1200,
    };
    // Fine-grained dimensions, profiles and process options belong to the
    // instance editor, not the style-selection dialog.
    view.tubeDesignerAddDialogOpen = false;
    view.scene.tubeDesigner.product = { entityId: "layout-test-product", name: "测试实例", templateId: template.id, parameters: view.tubeDesignerAddDraft };
    ops.renderProject = () => { mount.innerHTML = renderDesignerRightPane(view.scene.tubeDesigner, view, context); bindProductParameterDiagrams(mount); };
    ops.renderProject();
    parameterScroller = mount.querySelector("[data-tube-designer-parameter-scroll]");
    for (const node of mount.querySelectorAll("details")) node.open = true;
    const horizontal = field("horizontalCount");
    parameterScroller.scrollTop = 0;
    for (const node of mount.querySelectorAll("details")) node.open = true;
    horizontal.scrollIntoView({ block: "center" });
    parameterScroller.scrollTop += 80;
    const stickyReferenceBottom = mount.querySelector(".tube-designer-parameter-header").getBoundingClientRect().bottom;
    const stickyDiagram = mount.querySelector("[data-tube-designer-product-diagram]");
    const stickyTop = stickyDiagram.getBoundingClientRect().top;
    const lineCount = () => mount.querySelectorAll('[data-product-diagram-parameter~="horizontalCount"] .product-diagram-grid').length;
    const beforeHorizontalLines = lineCount();
    horizontal.focus({ preventScroll: true });
    horizontal.value = "6";
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", horizontal, ops);
    await new Promise(queueMicrotask);
    const afterHorizontal = field("horizontalCount");
    diagramInteraction.sticky = stickyTop >= stickyReferenceBottom - 1
      && mount.querySelector("[data-tube-designer-product-diagram]").getBoundingClientRect().top >= stickyReferenceBottom - 1;
    diagramInteraction.horizontalLinesChange = [beforeHorizontalLines, lineCount()];
    diagramInteraction.horizontalFocus = document.activeElement === afterHorizontal;
    diagramInteraction.horizontalHighlighted = afterHorizontal.classList.contains("is-active")
      && !!mount.querySelector('[data-product-diagram-parameter~="horizontalCount"].is-active');
    const horizontalProfileWidth = field("horizontalWidth");
    horizontalProfileWidth.focus({ preventScroll: true });
    await new Promise(queueMicrotask);
    diagramInteraction.horizontalProfileHighlighted = horizontalProfileWidth.classList.contains("is-active")
      && !!mount.querySelector('[data-product-diagram-profile-role="horizontal"].is-active');
    const verticalProfileType = mount.querySelector('[data-product-parameter-key="verticalProfileType"]');
    verticalProfileType.focus({ preventScroll: true });
    await new Promise(queueMicrotask);
    diagramInteraction.verticalProfileHighlighted = verticalProfileType.classList.contains("is-active")
      && !!mount.querySelector('[data-product-diagram-profile-role="vertical"].is-active');
    diagramInteraction.installationHidden = !mount.querySelector('[data-tube-designer-parameter="installationMode"]');
    group("section:materials").open = false;
    mount.querySelector("[data-tube-designer-product-detail]").open = false;
    const target = field("doorFrameJoinType");
    target.scrollIntoView({ block: "center" });
    target.focus({ preventScroll: true });
    const before = target.getBoundingClientRect().top;
    target.value = "v_groove_90:sharp_v";
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", target, ops);
    await new Promise(queueMicrotask);
    const after = field("doorFrameJoinType");
    const result = { focus: document.activeElement === after, offset: Math.abs(after.getBoundingClientRect().top - before),
      processOpen: group("section:process").open, profileClosed: !group("section:materials").open,
      reviewClosed: !mount.querySelector("[data-tube-designer-product-detail]").open, groovePresent: !!field("vGrooveKFactor") };
    group("group:groove_process").open = true;
    for (const value of ["miter_45", "v_groove_90:sharp_v"]) {
      const input = field("doorFrameJoinType"); input.focus(); input.value = value;
      await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", input, ops);
    }
    result.hiddenGroupRestored = group("group:groove_process").open;
    const code = field("productCode"); code.focus(); code.value = "DEMO-12345"; code.setSelectionRange(2, 6);
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", code, ops);
    result.textSelection = [field("productCode").selectionStart, field("productCode").selectionEnd];
    result.textFocus = document.activeElement === field("productCode");
    // The existing-instance editor must retain the same interaction state.
    group("section:materials").open = false;
    const edit = field("productCode"); edit.focus(); edit.value = "EDIT-12345"; edit.setSelectionRange(1, 4);
    await handleDesignerAreaAction(context, view, "tube-designer-parameter-change", edit, ops);
    await new Promise(queueMicrotask);
    result.editFocus = document.activeElement === field("productCode");
    result.editSelection = [field("productCode").selectionStart, field("productCode").selectionEnd];
    result.editGroups = group("section:process").open && !group("section:materials").open;
    // Restore the add dialog for the optional visual snapshot.
    mount.innerHTML = renderDesignerAddDialog(view.scene.tubeDesigner, view);
    result.diagramInteraction = diagramInteraction;
    return result;
  }, { templates, css: tubeDesignerCss });
  assert.ok(interaction.focus && interaction.textFocus);
  assert.deepEqual(interaction.diagramInteraction, {
    initialDiagramLabel: "单面防盗窗结构与尺寸示意",
    threeFaceDiagramLabel: "三面防盗窗结构与尺寸示意",
    focus: true,
    scrollAnchorStable: true,
    sideFaces: 2,
    leftDimension: true,
    rightDimension: true,
    openingFaceFocus: true,
    openingFaceHighlighted: true,
    openingFaceMoved: true,
    openingSurfaceSized: true,
    sticky: true,
    horizontalLinesChange: [4, 6],
    horizontalFocus: true,
    horizontalHighlighted: true,
    horizontalProfileHighlighted: true,
    verticalProfileHighlighted: true,
    installationHidden: true,
  });
  assert.ok(interaction.offset <= 1, JSON.stringify(interaction));
  assert.ok(interaction.processOpen && interaction.profileClosed && interaction.reviewClosed);
  assert.ok(interaction.groovePresent && interaction.hiddenGroupRestored);
  assert.deepEqual(interaction.textSelection, [2, 6]);
  assert.ok(interaction.editFocus && interaction.editGroups);
  assert.deepEqual(interaction.editSelection, [1, 4]);
  if (process.env.ICAX_LAYOUT_SCREENSHOT) {
    await page.setViewportSize({ width: 1600, height: 1000 });
    await page.locator('.tube-designer-config-parameters details').evaluateAll((nodes) => nodes.forEach((node) => { node.open = node.dataset.tubeDesignerGroupDepth === "0"; }));
    await page.locator('.tube-designer-config-parameters').evaluate((pane) => { pane.scrollTop = 0; });
    await page.screenshot({ path: process.env.ICAX_LAYOUT_SCREENSHOT });
  }
  console.log("PASS add dialog: merged security-window template × 2 sizes; four parameter classes, focus, text selection, scroll anchoring and hidden-group state");
} finally {
  await browser.close();
}
