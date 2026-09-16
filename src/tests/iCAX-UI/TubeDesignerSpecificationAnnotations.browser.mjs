// Real WebGL regression for the 防盗窗 specification annotation layer.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});

try {
  const page = await browser.newPage({ viewport: { width: 960, height: 720 } });
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const root = fileURLToPath(new URL("../../", import.meta.url));
  await page.route("http://specification-annotations.test/**", (route) => {
    const pathname = decodeURIComponent(new URL(route.request().url()).pathname);
    if (pathname === "/") {
      return route.fulfill({
        contentType: "text/html",
        body: "<!doctype html><html><head></head><body></body></html>",
      });
    }
    const path = resolve(root, pathname.replace(/^\/src\//, ""));
    if (!pathname.startsWith("/src/")
        || !path.startsWith(root.replace(/[\\/]$/, "") + sep)
        || !/\.(mjs|js)$/.test(path)) return route.abort();
    return route.fulfill({ contentType: "text/javascript", body: readFileSync(path, "utf8") });
  });
  await page.goto("http://specification-annotations.test/");

  const result = await page.evaluate(async () => {
    const { createThreeViewport } = await import(
      "/src/iCAX-UI/SDK/Viewport/threeViewport.mjs"
    );
    document.body.innerHTML = '<div id="host" style="width:960px;height:720px"></div>';
    const viewport = createThreeViewport({ continuousRender: false });
    viewport.mount(document.querySelector("#host"));
    viewport.camera.position.set(1500, -2400, 1500);
    viewport.camera.lookAt(600, 0, 900);

    const annotation = {
      id: "width",
      parameter: "width",
      start: [0, 0, 0],
      end: [1200, 0, 0],
      offset: [0, 0, -80],
      label: "正面宽度 1,200 mm",
      oldValue: 1200,
      newValue: 1200,
      cleanLabel: "正面宽度 1,200 mm",
      changedLabel: "正面宽度 1,200 mm",
    };
    viewport.setSpecificationAnnotations([annotation]);
    viewport.renderer.render(viewport.scene, viewport.camera);
    const greenLabel = document.querySelector(".icax-three-specification-label");
    const greenTrigger = greenLabel.querySelector(".icax-three-specification-trigger");
    const greenRect = greenLabel.getBoundingClientRect();
    const greenCenter = {
      x: (greenRect.left + greenRect.right) / 2,
      y: (greenRect.top + greenRect.bottom) / 2,
    };
    let editActivations = 0;
    viewport.specificationLabelLayer.addEventListener("click", (event) => {
      if (event.target?.matches?.(".icax-three-specification-trigger")) editActivations += 1;
    });
    viewport.renderer.domElement.dispatchEvent(new MouseEvent("click", {
      button: 0, clientX: greenCenter.x, clientY: greenCenter.y,
      bubbles: true, cancelable: true,
    }));
    const activationsAfterSingleClick = editActivations;
    viewport.renderer.domElement.dispatchEvent(new MouseEvent("dblclick", {
      button: 0, clientX: greenCenter.x, clientY: greenCenter.y,
      bubbles: true, cancelable: true,
    }));
    const green = {
      count: viewport.getDebugState().specificationAnnotationCount,
      lineColor: viewport.specificationContent.children[0].material.color.getHex(),
      pending: greenLabel.classList.contains("is-pending"),
      text: greenLabel.textContent,
      action: greenTrigger.dataset.camAction,
      parameter: greenTrigger.dataset.tubeDesignerParameterKey,
      pointerTargetIsCanvas: document.elementFromPoint(greenCenter.x, greenCenter.y)
        === viewport.renderer.domElement,
      activationsAfterSingleClick,
      activationsAfterDoubleClick: editActivations,
    };

    const editingAnnotation = {
      ...annotation,
      editing: true,
      editor: { type: "number", value: 1200, min: 300, max: 6000, step: 1 },
    };
    viewport.setSpecificationAnnotations([editingAnnotation]);
    const focused = viewport.focusSpecificationAnnotationEditor("width");
    const editor = document.querySelector(".icax-three-specification-input");
    const editing = {
      focused,
      active: document.activeElement === editor,
      lineColor: viewport.specificationContent.children[0].material.color.getHex(),
      redEditor: document.querySelector(".icax-three-specification-label")?.classList.contains("is-editing")
        && getComputedStyle(document.querySelector(".icax-three-specification-label")).borderColor === "rgb(255, 119, 112)",
      pointerInteractive: getComputedStyle(
        document.querySelector(".icax-three-specification-label"),
      ).pointerEvents === "auto",
      type: editor.type,
      value: editor.value,
      min: editor.min,
      max: editor.max,
      step: editor.step,
      action: editor.dataset.camChangeAction,
      parameter: editor.dataset.tubeDesignerParameter,
    };

    editor.value = "1350";
    viewport.setSpecificationAnnotations([editingAnnotation]);
    const unchangedEditor = document.querySelector(".icax-three-specification-input");
    viewport.setSpecificationAnnotations([{
      ...editingAnnotation,
      offset: [0, 0, -90],
    }]);
    const restoredEditor = document.querySelector(".icax-three-specification-input");
    const changedNodeRestoredFocus = document.activeElement === restoredEditor;
    let committedChanges = 0;
    viewport.specificationLabelLayer.addEventListener("change", (event) => {
      committedChanges += 1;
      viewport.setSpecificationAnnotations([{
        ...annotation,
        newValue: Number(event.target.value),
        changedLabel: `正面宽度 1,200 mm → ${event.target.value} mm`,
      }]);
    });
    const enterCanceled = !restoredEditor.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Enter",
      bubbles: true,
      cancelable: true,
    }));
    await new Promise((resolve) => setTimeout(resolve, 0));
    const interaction = {
      unchangedNodePreserved: unchangedEditor === editor,
      changedNodeRestoredValue: restoredEditor.value,
      changedNodeRestoredFocus,
      enterCanceled,
      enterBlurred: document.activeElement !== restoredEditor,
      editorClosed: !document.querySelector(".icax-three-specification-input"),
      normalTriggerRestored: Boolean(document.querySelector(".icax-three-specification-trigger")),
      committedChanges,
    };

    viewport.setSpecificationAnnotations([{
      ...annotation,
      label: "正面宽度 1,200 mm → 1,350 mm",
      oldValue: 1200,
      newValue: 1350,
      changedLabel: "正面宽度 1,200 mm → 1,350 mm",
    }]);
    viewport.renderer.render(viewport.scene, viewport.camera);
    const redLabel = document.querySelector(".icax-three-specification-label");
    const red = {
      count: viewport.getDebugState().specificationAnnotationCount,
      lineColor: viewport.specificationContent.children[0].material.color.getHex(),
      pending: redLabel.classList.contains("is-pending"),
      text: redLabel.textContent,
    };

    viewport.setSpecificationAnnotations([{
      ...editingAnnotation,
      id: "other-product:width",
      oldValue: 1200,
      newValue: 1200,
      cleanLabel: "正面宽度 1,200 mm",
      changedLabel: "正面宽度 1,200 mm",
    }]);
    const otherInstanceEditor = document.querySelector(".icax-three-specification-input");
    const instanceIsolation = {
      value: otherInstanceEditor.value,
      pending: otherInstanceEditor.closest(".icax-three-specification-label").classList.contains("is-pending"),
    };

    viewport.clearSpecificationAnnotations();
    const cleared = {
      count: viewport.getDebugState().specificationAnnotationCount,
      labels: document.querySelectorAll(".icax-three-specification-label").length,
      lines: viewport.specificationContent.children.length,
    };
    viewport.dispose();
    return { green, editing, interaction, red, instanceIsolation, cleared };
  });

  await page.evaluate(async () => {
    const { createThreeViewport } = await import(
      "/src/iCAX-UI/SDK/Viewport/threeViewport.mjs"
    );
    document.body.innerHTML = '<div id="keyboard-host" style="width:960px;height:720px"></div>';
    const viewport = createThreeViewport({ continuousRender: false });
    viewport.mount(document.querySelector("#keyboard-host"));
    const annotation = {
      id: "keyboard-width",
      parameter: "width",
      start: [0, 0, 0],
      end: [1200, 0, 0],
      offset: [0, 0, -80],
      label: "正面宽度 1,200 mm",
      oldValue: 1200,
      newValue: 1200,
      cleanLabel: "正面宽度 1,200 mm",
      changedLabel: "正面宽度 1,200 mm",
      editing: true,
      editor: { type: "number", value: 1200, min: 300, max: 6000, step: 1 },
    };
    viewport.setSpecificationAnnotations([annotation]);
    viewport.focusSpecificationAnnotationEditor("width");
    const commits = [];
    viewport.specificationLabelLayer.addEventListener("change", (event) => {
      commits.push(event.target.value);
      viewport.setSpecificationAnnotations([{
        ...annotation,
        editing: false,
        newValue: Number(event.target.value),
        changedLabel: `正面宽度 1,200 mm → ${event.target.value} mm`,
      }]);
    });
    globalThis.__annotationKeyboardTest = { viewport, annotation, commits };
  });
  const liveEditor = page.locator('[data-tube-designer-scene-specification-input="width"]');
  await liveEditor.fill("13");
  await page.evaluate(() => {
    const state = globalThis.__annotationKeyboardTest;
    const editorState = state.viewport.captureSpecificationAnnotationEditorState({
      suspendCommit: true,
    });
    document.body.innerHTML = '<div id="keyboard-host-restored" style="width:960px;height:720px"></div>';
    state.viewport.mount(document.querySelector("#keyboard-host-restored"));
    state.viewport.restoreSpecificationAnnotationEditorState(editorState);
    state.viewport.setSpecificationAnnotations([{
      ...state.annotation,
      offset: [0, 0, -95],
    }]);
  });
  const refreshedEditor = page.locator('[data-tube-designer-scene-specification-input="width"]');
  assert.equal(await refreshedEditor.inputValue(), "13");
  assert.equal(await refreshedEditor.evaluate((element) => document.activeElement === element), true);
  await refreshedEditor.pressSequentially("50", { delay: 25 });
  await refreshedEditor.press("Enter");
  await page.waitForTimeout(0);
  await page.evaluate(() => {
    const state = globalThis.__annotationKeyboardTest;
    state.viewport.setSpecificationAnnotations([{
      ...state.annotation,
      editing: false,
      newValue: 1350,
      changedLabel: "正面宽度 1,200 mm → 1,350 mm",
      editor: { ...state.annotation.editor, value: 1350 },
    }]);
  });
  const keyboard = await page.evaluate(() => {
    const state = globalThis.__annotationKeyboardTest;
    const input = document.querySelector('[data-tube-designer-scene-specification-input="width"]');
    const trigger = document.querySelector('.icax-three-specification-trigger');
    const result = {
      editorClosed: input == null,
      triggerRestored: Boolean(trigger),
      focusedEditor: document.activeElement?.matches?.('[data-tube-designer-scene-specification-input]') === true,
      commits: [...state.commits],
    };
    state.viewport.dispose();
    delete globalThis.__annotationKeyboardTest;
    return result;
  });

  assert.deepEqual(result.green, {
    count: 1,
    lineColor: 0x27c27a,
    pending: false,
    text: "正面宽度 1,200 mm",
    action: "tube-designer-edit-scene-specification",
    parameter: "width",
    pointerTargetIsCanvas: true,
    activationsAfterSingleClick: 0,
    activationsAfterDoubleClick: 1,
  });
  assert.deepEqual(result.editing, {
    focused: true,
    active: true,
    lineColor: 0xef5b55,
    redEditor: true,
    pointerInteractive: true,
    type: "number",
    value: "1200",
    min: "300",
    max: "6000",
    step: "1",
    action: "tube-designer-scene-specification-change",
    parameter: "width",
  });
  assert.deepEqual(result.interaction, {
    unchangedNodePreserved: true,
    changedNodeRestoredValue: "1350",
    changedNodeRestoredFocus: true,
    enterCanceled: true,
    enterBlurred: true,
    editorClosed: true,
    normalTriggerRestored: true,
    committedChanges: 1,
  });
  assert.deepEqual(result.red, {
    count: 1,
    lineColor: 0xef5b55,
    pending: true,
    text: "正面宽度 1,200 mm → 1,350 mm",
  });
  assert.deepEqual(result.instanceIsolation, { value: "1200", pending: false });
  assert.deepEqual(result.cleared, { count: 0, labels: 0, lines: 0 });
  assert.deepEqual(keyboard, {
    editorClosed: true,
    triggerRestored: true,
    focusedEditor: false,
    commits: ["1350"],
  });
  assert.deepEqual(errors, []);
  console.log("PASS 防盗窗规格标注真实 WebGL 颜色与生命周期");
} finally {
  await browser.close();
}
