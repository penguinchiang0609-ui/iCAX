// Actual AppShell controls and DOM; application transport and the two small
// product modules are fixtures for the public bottom-dock extension contract.
import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const root = fileURLToPath(new URL("../../../", import.meta.url));
const output = resolve(root, "output/tests/app-shell-bottom-dock");mkdirSync(output, { recursive: true });
const { chromium } = await import(process.env.ICAX_PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({ headless: true, channel: process.env.ICAX_BROWSER_CHANNEL || "chrome" });
const report = { pass: false, checks: [], errors: [], scope: "Actual AppShell footer, tabs, resize and project selection; native transport and product contents are controlled fixtures, not CAD or desktop CEF acceptance." };
const page = await browser.newPage({ viewport: { width: 1280, height: 850 } });
page.on("pageerror", error => report.errors.push(error.message));
try {
  await page.route("http://bottom-dock.test/**", route => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/src/iCAX-UI/SDK/runtime.mjs") return route.fulfill({ contentType: "text/javascript", body: "export async function connectApplication(){return __dockFixture.app;}" });
    if (path.startsWith("/fixture/")) {
      const tabbed = path.includes("tabbed");
      return route.fulfill({ contentType: "text/javascript", body: `
        export function mountProject(context) {
          __dockFixture.contexts.set(context.project.projectId,context);
          if(!context.mount.firstElementChild)context.mount.innerHTML='<div data-test-workspace>Fixture workspace</div>';
        }
        ${tabbed ? `
        export function getBottomDockDefinition(){return {panels:[{id:'command',title:'命令'}],defaultTabId:'command',initialHeight:96,minHeight:72};}
        export function mountBottomDockPanel(context,id){
          __dockFixture.panelContexts.set(context.project.projectId,context);
          if(!context.mount.firstElementChild){
            const field=document.createElement('input');field.dataset.testCommand='';
            field.value=__dockFixture.drafts.get(context.project.projectId)||'';
            field.addEventListener('input',()=>__dockFixture.drafts.set(context.project.projectId,field.value));
            context.mount.append(field);
          }
        }` : ""}
      ` });
    }
    const file = resolve(root, "." + path);
    if (!file.startsWith(resolve(root, "src") + sep)) return route.abort();
    try {
      let body = readFileSync(file, "utf8");if (path.endsWith("index.html")) body = body.replace(/<script type="module"[^>]*><\/script>/, "");
      return route.fulfill({ contentType: path.endsWith(".html") ? "text/html" : path.endsWith(".css") ? "text/css" : "text/javascript", body });
    } catch { return route.abort(); }
  });
  await page.goto("http://bottom-dock.test/src/iCAX-UI/SDK/AppShell/index.html");
  await page.evaluate(async () => {
    const fixture = globalThis.__dockFixture = { contexts: new Map(), panelContexts: new Map(), drafts: new Map(), sequence: 0 };
    const states = ["legacy", "tabbed"].map(id => ({ productId: "fixture." + id, productName: id, frontendEntry: "/fixture/" + id + ".mjs", catalogs: [], projectFile: { fileExtensions: ["fixture"] } }));
    const proxies = new Map(states.map(state => [state.productId, {
      productId: state.productId, state, projects: new Map(), async getState() { return state; }, getProject(id) { return this.projects.get(id); },
      async openProjectCatalog(path, options = {}) {
        const projectId = state.productId + "-" + (++fixture.sequence);
        const scene = { sceneId: projectId + "-scene" }, sceneProxy = { sceneId: scene.sceneId, state: scene, async getState() { return scene; } };
        const project = { projectId, projectName: options.projectName ?? projectId, projectPath: path, mainScene: scene };
        const projectProxy = { projectId, state: project, getMainScene() { return sceneProxy; }, async getState() { return project; } };
        this.projects.set(projectId, projectProxy);const catalog = { catalogId: projectId + "-catalog", mainProject: project };
        state.catalogs.push(catalog);return { projectProxy, sceneProxy, catalog };
      },
    }]));
    fixture.app = { products: new Map(), bridge: {}, async getState() { return { products: states }; }, getProduct(id) { return this.products.get(id); }, async startProduct(id) { const proxy = proxies.get(id);this.products.set(id, proxy);return proxy; } };
    await import("/src/iCAX-UI/SDK/AppShell/app/bootstrap.mjs");
  });
  await page.waitForFunction(() => __icaxAppShell.getState().startupPhase === "ready");
  const create = async (productId, projectName) => {
    await page.evaluate(options => __icaxAppShell.createProject(options), { productId, projectName, projectPath: "" });
    await page.waitForSelector("[data-test-workspace]");return page.evaluate(() => __icaxAppShell.getState().activeProjectId);
  };
  const select = async id => {
    await page.locator(`[data-action="select-open-project"][data-project-id="${id}"]`).click();
    await page.waitForFunction(id => __icaxAppShell.getState().activeProjectId === id && !__icaxAppShell.getState().pendingCount, id);
  };
  const height = () => page.locator(".bottom-dock").evaluate(node => node.getBoundingClientRect().height);
  const resize = async delta => {
    const box = await page.locator("[data-bottom-dock-resize]").boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + 4);await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2, box.y + 4 - delta);await page.mouse.up();
  };
  const legacyA = await create("fixture.legacy", "Legacy A");
  assert.equal(await page.locator("[data-bottom-dock-tab]").count(), 0);assert.equal(await page.locator(".bottom-title").textContent(), "日志");assert.equal(await height(), 64);
  await resize(70);const legacyHeight = await height();assert.equal(legacyHeight, 134);
  await page.evaluate(id => { const context = __dockFixture.contexts.get(id);for(let i=0;i<60;i++)context.actions.log("info","Legacy log " + i);globalThis.__legacyLogNode=document.querySelector(".log-list");__legacyLogNode.scrollTop=75; }, legacyA);
  await page.locator('[data-action="open-start-center"]').click();await page.locator('[data-action="close-start-center"]').click();
  assert.deepEqual(await page.locator(".log-list").evaluate(node => [node === __legacyLogNode,node.scrollTop]), [true,75]);assert.equal(await height(), legacyHeight);
  report.checks.push("A product without the hook retains its single legacy log, resize height, log node and scroll through real shell renders");

  const tabbedA = await create("fixture.tabbed", "Tabbed A");assert.equal(await height(), 96);assert.equal(await page.locator('[data-bottom-dock-tab="command"]').getAttribute("aria-selected"), "true");
  await resize(40);const tabbedHeight = await height();assert.equal(tabbedHeight,136);
  await page.locator("[data-test-command]").fill("first project draft");
  await page.locator('[data-bottom-dock-tab="logs"]').click();
  await select(legacyA);assert.equal(await page.locator("[data-bottom-dock-tab]").count(),0);assert.equal(await height(),legacyHeight);assert.equal(await page.locator("[data-test-command]").count(),0);
  assert.equal(await page.evaluate(id => __dockFixture.panelContexts.get(id).bottomDock.getPanelMount("command"),tabbedA),null);
  assert.equal(await page.evaluate(id => __dockFixture.panelContexts.get(id).bottomDock.selectTab("command"),tabbedA),false);
  report.checks.push("Switching from a tabbed product restores legacy layout and height; an inactive product context cannot access or switch its panel");

  await select(tabbedA);assert.equal(await height(),tabbedHeight);assert.equal(await page.locator('[data-bottom-dock-tab="logs"]').getAttribute("aria-selected"),"true");
  await page.locator('[data-bottom-dock-tab="command"]').click();assert.equal(await page.locator("[data-test-command]").inputValue(),"first project draft");
  const tabbedB = await create("fixture.tabbed","Tabbed B");assert.equal(await height(),96);assert.equal(await page.locator('[data-bottom-dock-tab="command"]').getAttribute("aria-selected"),"true");assert.equal(await page.locator("[data-test-command]").inputValue(),"");
  await select(tabbedA);assert.equal(await height(),tabbedHeight);assert.equal(await page.locator("[data-test-command]").inputValue(),"first project draft");
  report.checks.push("Independent scenes retain their own dock height, chosen tab and product-owned command draft without leaking into another project");
  assert.deepEqual(report.errors,[]);report.pass=true;report.projects={legacyA,tabbedA,tabbedB};await page.screenshot({path:resolve(output,"bottom-dock.png")});
} catch(error) { report.error=error.stack??String(error);await page.screenshot({path:resolve(output,"failure.png")}).catch(()=>{});throw error; }
finally { writeFileSync(resolve(output,"report.json"),JSON.stringify(report,null,2));await browser.close(); }
console.log(JSON.stringify({pass:report.pass,checks:report.checks.length,output}));
