// Isolated browser regression for the shared ViewCube. No native scene is needed.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const source = readFileSync(new URL("../../apps/_shared/workbench/viewport/viewCube.mjs", import.meta.url), "utf8");
const styles = readFileSync(new URL("../../apps/_shared/workbench/styles/laser3dcam.css", import.meta.url), "utf8");
const browser = await chromium.launch({
  headless: true,
  channel: process.env.ICAX_BROWSER_CHANNEL || "msedge",
});

try {
  const page = await browser.newPage();
  page.on("pageerror", (error) => { throw error; });
  await page.route("http://viewcube.test/**", (route) => {
    if (new URL(route.request().url()).pathname === "/viewCube.mjs") {
      return route.fulfill({ contentType: "text/javascript", body: source });
    }
    if (new URL(route.request().url()).pathname === "/laser3dcam.css") {
      return route.fulfill({ contentType: "text/css", body: styles });
    }
    return route.fulfill({ contentType: "text/html", body: `
      <link rel="stylesheet" href="/laser3dcam.css">
      <div id="scroll" style="height:80px;overflow:auto">
        <div style="height:120px"></div><div id="mount" style="position:relative;height:116px"></div>
      </div>
      <input id="outside" aria-label="其他输入框">` });
  });
  await page.goto("http://viewcube.test/");
  await page.evaluate(async () => {
    const cube = await import("/viewCube.mjs");
    const mount = document.querySelector("#mount");
    mount.innerHTML = cube.renderViewCube();
    const state = { direction: { x: -1, y: 1, z: -1 }, up: { x: 0, y: 0, z: 1 } };
    const calls = [];
    const view = { viewport: {
      getViewCubeState: () => state,
      setStandardView: (name) => calls.push(name),
    } };
    let clicks = 0;
    mount.addEventListener("click", (event) => {
      if (event.target.closest('[data-cam-action="view-standard"]')) clicks++;
    });
    cube.attachViewCube(view, mount);
    window.fixture = { cube, view, state, calls, get clicks() { return clicks; } };
  });

  const front = page.getByRole("button", { name: "front 视图", exact: true });
  assert.equal(await front.count(), 1, "SVG piece must be exposed as a named button");
  assert.equal(await front.getAttribute("tabindex"), "0");
  await front.focus();
  await page.keyboard.press("Enter");
  await page.keyboard.press("Space");
  await page.keyboard.press("ArrowRight");
  assert.deepEqual(await page.evaluate(() => window.fixture.calls), ["front", "front"]);
  assert.equal(await page.evaluate(() => window.fixture.clicks), 0, "keyboard activation must not fake a click");

  await front.evaluate((piece) => piece.dispatchEvent(new MouseEvent("click", { bubbles: true })));
  assert.equal(await page.evaluate(() => window.fixture.clicks), 1, "mouse clicks must keep bubbling to the existing route");
  assert.equal(await page.evaluate(() => window.fixture.calls.length), 2);

  await page.evaluate(() => {
    const { state } = window.fixture;
    const focused = document.activeElement;
    document.querySelector("#scroll").scrollTop = 70;
    window.fixture.previousFocusedPiece = focused;
    state.direction.x = -1.2;
  });
  await page.waitForFunction(() => document.activeElement !== window.fixture.previousFocusedPiece);
  assert.deepEqual(await page.evaluate(() => ({
    view: document.activeElement?.dataset.camView,
    scrollTop: document.querySelector("#scroll").scrollTop,
  })), { view: "front", scrollTop: 70 }, "redraw must restore the same focused piece without scrolling");

  await page.evaluate(() => { window.fixture.state.direction.y = -1.3; });
  await page.waitForFunction(() => !document.querySelector('[data-cam-view="front"]'));
  assert.equal(await page.evaluate(() => !!document.activeElement?.closest?.('[data-cam-viewcube] [role="button"]')),
    true, "focus must stay on an available cube button when the previous piece disappears");

  await page.locator("#outside").focus();
  await page.evaluate(() => {
    window.fixture.state.direction.y = 1.3;
  });
  await page.waitForFunction(() => !!document.querySelector('[data-cam-view="front"]'));
  assert.equal(await page.evaluate(() => document.activeElement?.id), "outside", "redraw must not take focus from another control");

  await page.evaluate(() => {
    const { cube, view } = window.fixture;
    cube.stopViewCubeAnimation(view);
    cube.attachViewCube(view, document.querySelector("#mount"));
  });
  await page.getByRole("button", { name: "front 视图", exact: true }).focus();
  await page.keyboard.press("Enter");
  assert.deepEqual(await page.evaluate(() => window.fixture.calls), ["front", "front", "front"],
    "reattaching must not leave duplicate keyboard listeners");

  await page.evaluate(() => Object.assign(window.fixture.state.direction, { x: 0, y: 1, z: 0 }));
  await page.waitForFunction(() => {
    const cube = document.querySelector("[data-cam-viewcube]");
    return cube?.style.getPropertyValue("--viewcube-yaw") === "0.0deg"
      && cube?.style.getPropertyValue("--viewcube-pitch") === "0.0deg";
  });
  await page.getByRole("button", { name: "front 视图", exact: true }).click();
  assert.deepEqual(await page.evaluate(() => {
    const piece = document.activeElement;
    return {
      view: piece?.dataset.camView,
      outlineStyle: getComputedStyle(piece).outlineStyle,
    };
  }), { view: "front", outlineStyle: "none" },
  "mouse activation must not leave the browser's native black-and-white focus ring");

  await page.locator("#outside").focus();
  await page.keyboard.press("Shift+Tab");
  assert.deepEqual(await page.evaluate(() => {
    const piece = document.activeElement;
    const surface = piece?.querySelector(".cam-viewcube-surface");
    return {
      cubePiece: !!piece?.matches('.cam-viewcube-piece[role="button"]'),
      focusVisible: piece?.matches(":focus-visible"),
      outlineStyle: getComputedStyle(piece).outlineStyle,
      stroke: surface ? getComputedStyle(surface).stroke : null,
      strokeWidth: surface ? getComputedStyle(surface).strokeWidth : null,
    };
  }), { cubePiece: true, focusVisible: true, outlineStyle: "none", stroke: "rgb(36, 84, 93)", strokeWidth: "1.8px" },
  "keyboard focus must remain visible through a fine cube-face border");
} finally {
  await browser.close();
}
