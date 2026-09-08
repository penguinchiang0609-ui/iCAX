// Isolated rendering test: no connection to the user's project or desktop.
import assert from "node:assert/strict";
import {readFileSync,readdirSync,mkdirSync} from "node:fs";
import {resolve,sep} from "node:path";
import {fileURLToPath} from "node:url";
import {tubeDesignerCss} from "../../apps/tube-designer/webpage/styles/tubeDesigner.css.mjs";
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||"playwright");
const root=fileURLToPath(new URL("../../",import.meta.url));
const toolsRoot=new URL("../../apps/tube-designer/templates/_shared/punch-tools/",import.meta.url);
const tools=readdirSync(toolsRoot,{withFileTypes:true}).filter(item=>item.isDirectory()).map(item=>JSON.parse(readFileSync(new URL(item.name+"/tool.json",toolsRoot))))
  .map(t=>({...t,digest:"browser-test",defaultParameters:Object.fromEntries(t.parameters.map(p=>[p.key,p.defaultValue]))}));
const browser=await chromium.launch({headless:true,...(process.env.ICAX_BROWSER_CHANNEL?{channel:process.env.ICAX_BROWSER_CHANNEL}:{})});
try {
  const page=await browser.newPage();
  const errors=[];page.on("pageerror",e=>errors.push(e.message));
  await page.route("http://punch.test/**",route=>{
    const path=new URL(route.request().url()).pathname;
    if(path==="/")return route.fulfill({contentType:"text/html",body:"<!doctype html><body><div id='app'></div></body>"});
    const file=resolve(root,path.replace(/^\/src\//,""));
    if(!path.startsWith("/src/")||!file.startsWith(root.replace(/[\\/]$/,"")+sep)||!file.endsWith(".mjs"))return route.abort();
    return route.fulfill({contentType:"text/javascript",body:readFileSync(file,"utf8")});
  });
  await page.goto("http://punch.test/");
  await page.addStyleTag({content:"*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif}"+tubeDesignerCss});
  await page.evaluate(async tools=>{
    const wizard=await import("/src/apps/tube-designer/webpage/punchWizard.mjs");
    const part={entityId:"fixture",name:"端面刀具测试",length:500,profile:{kind:"rect",width:40,depth:20}};
    const view={pending:false,tubeDesignerPunchWizard:wizard.createPunchWizardState(part)};
    wizard.installPunchCatalogue(view.tubeDesignerPunchWizard,{tools});
    window.test={part,view,wizard};
    window.draw=()=>{document.querySelector("#app").innerHTML=wizard.renderPunchWizardDialog(part,view);};
    document.addEventListener("change",event=>{wizard.updatePunchWizardField(view,event.target);window.draw();});
    document.addEventListener("click",event=>{
      const button=event.target.closest("[data-cam-action]");
      if(!button)return;
      const name=button.dataset.camAction.replace("tube-designer-punch-","");
      if(name==="add")wizard.addPunchWizardFeature(view,part);
      else if(["edit","copy","toggle","remove","remove-end","undo","redo"].includes(name))wizard.editPunchWizardFeature(view,name,button.dataset.tubeDesignerPunchIndex);
      window.draw();
    });
    window.draw();
  },tools);
  for(const size of [{width:1600,height:1000},{width:1024,height:768}]) {
    await page.setViewportSize(size);
    await page.locator('[data-tube-designer-punch-field=tool]:not([data-tube-designer-punch-end])').selectOption("diamond-12");
    await page.locator('[data-cam-action=tube-designer-punch-add]').click();
    await page.locator('[data-tube-designer-punch-end=start][data-tube-designer-punch-field=tool]').selectOption("end-convex");
    await page.locator('[data-tube-designer-punch-end=start][data-tube-designer-punch-parameter=diameter]').fill("60");
    await page.locator('[data-tube-designer-punch-end=start][data-tube-designer-punch-parameter=diameter]').press("Tab");
    assert.equal(await page.evaluate(()=>window.test.view.tubeDesignerPunchWizard.ends.start.toolParameters.diameter),60);
    const dimensions=await page.evaluate(()=>{
      const dialog=document.querySelector(".tube-designer-punch-dialog").getBoundingClientRect();
      const footer=document.querySelector(".tube-designer-punch-footer").getBoundingClientRect();
      return {dialog:{x:dialog.x,y:dialog.y,right:dialog.right,bottom:dialog.bottom},footerBottom:footer.bottom,
        overflow:[...document.querySelectorAll(".tube-designer-punch-editor,.tube-designer-punch-preview-pane,.tube-designer-punch-field-grid")].filter(e=>e.scrollWidth>e.clientWidth+2).map(e=>e.className)};
    });
    assert.deepEqual(dimensions.overflow,[]);
    assert.ok(dimensions.dialog.x>=0&&dimensions.dialog.y>=0&&dimensions.dialog.right<=size.width&&dimensions.dialog.bottom<=size.height);
    assert.ok(dimensions.footerBottom<=size.height);
    assert.equal(await page.locator(".tube-designer-punch-schematic").count(),0);
    if(process.env.ICAX_ARTIFACT_DIR){
      mkdirSync(process.env.ICAX_ARTIFACT_DIR,{recursive:true});
      await page.screenshot({path:resolve(process.env.ICAX_ARTIFACT_DIR,"punch-"+size.width+".png")});
    }
  }
  await page.evaluate(()=>{
    const {wizard,view}=window.test,s=view.tubeDesignerPunchWizard;
    for(const item of [...s.features,s.ends.start]){
      item.frozenTool={schema:"icax.frozen-punch-tool",schemaVersion:1,ref:structuredClone(item.toolRef),
        parameters:structuredClone(item.toolParameters),geometry:{mode:"profile",contours:[{kind:"circle",radius:6}]},instance:structuredClone(item)};
      item.frozenCut={url:"fixture-only-embedded-cutter",version:1};
    }
    wizard.installPunchCatalogue(s,{tools:[]});window.draw();
  });
  const cards=page.locator('.tube-designer-punch-feature-list article');
  assert.ok(await cards.count()>0);
  for(const card of await cards.all()) {
    assert.deepEqual(await card.locator('button').allTextContents(),['删除']);
    assert.match(await card.textContent(),/退化定式 · 只读/);
  }
  assert.equal(await page.locator('[data-tube-designer-punch-end=start][data-tube-designer-punch-field=trim]').isDisabled(),true);
  assert.equal(await page.locator('[data-tube-designer-punch-end=start][data-tube-designer-punch-field=tool]').isDisabled(),true);
  await page.locator('[data-cam-action=tube-designer-punch-remove-end][data-tube-designer-punch-index=start]').click();
  assert.equal(await page.evaluate(()=>window.test.view.tubeDesignerPunchWizard.ends.start.type),'keep');
  const count=await cards.count();await cards.first().getByRole('button',{name:'删除',exact:true}).click();
  assert.equal(await cards.count(),count-1);
  assert.deepEqual(errors,[]);
  console.log("Dynamic fields, two responsive layouts and delete-only degraded side/end nodes passed.");
} finally {await browser.close();}
