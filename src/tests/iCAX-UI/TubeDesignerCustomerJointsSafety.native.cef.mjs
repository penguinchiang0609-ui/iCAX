import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,readdirSync,writeFileSync} from 'node:fs';
import {join,resolve} from 'node:path';
const require=createRequire('C:/Users/pengu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/package.json');
const {chromium}=require('playwright');
const root=resolve(process.env.ICAX_CUSTOMER_JOINT_EVIDENCE||'output/tests/customer-joints-merge-20261023/accepted');
const browser=await chromium.connectOverCDP('http://127.0.0.1:19425');const page=browser.contexts()[0].pages().find(p=>p.url().includes('/AppShell/index.html'));
const report={businessMocked:false,checks:[]};
try{
 await page.reload();await page.waitForFunction(()=>window.__icaxWorkbench?.getState().projectId);
 await page.evaluate(async()=>{const {AppProxy}=await import('../../AppProxy/AppProxy.mjs');const app=await AppProxy.create(window.icax??window.icaxNativeBridge);await app.getState();const p=app.getProduct('icax.tube-designer');await p.getState();window.__safetyScene=p.getProject(window.__icaxWorkbench.getState().projectId).getMainScene();});
 const invoke=(method,payload)=>page.evaluate(({method,payload})=>window.__safetyScene.invoke('TubeDesigner.'+method,payload),{method,payload});
 const s=(await invoke('GeneratePreview',{templateId:'modular-guardrail',handrailWidth:50,handrailDepth:50})).tubeDesigner;
 const made=(await invoke('DisassembleSelected',{productEntityIds:[s.product.entityId]})).tubeDesigner;assert.equal(made.parts.length,4);
 await invoke('UpdateProductParameters',{productEntityId:s.product.entityId,parameters:{...s.product.parameters,sideLength1:3100}});
 const stale=join(root,'stale-rejected');mkdirSync(stale,{recursive:true});
 try{await invoke('ExportSelected',{partEntityIds:made.parts.map(p=>p.entityId),targetDirectory:stale});assert.fail('stale geometry exported');}catch(e){assert.match(e.message,/重新生成.*拆单/);report.staleError=e.message;}
 assert.equal(readdirSync(stale).length,0);report.checks.push('changed dimensions reject stale grouped geometry before files are written');
 await invoke('UpdateProductParameters',{productEntityId:s.product.entityId,parameters:s.product.parameters});
 await invoke('GeneratePreview',{templateId:'assembly-frame-lt'});
 const raw=(await invoke('List',{})).tubeDesigner.product;assert.equal(raw.templateId,'assembly-frame-lt');
 const both=(await invoke('DisassembleSelected',{productEntityIds:[s.product.entityId,raw.entityId]})).tubeDesigner;
 const rawParts=both.manufacturingGroups.find(g=>g.productEntityId===raw.entityId).parts;assert(rawParts.length);
 const blocked=join(root,'raw-rejected');mkdirSync(blocked,{recursive:true});
 try{await invoke('ExportSelected',{partEntityIds:rawParts.map(p=>p.entityId),targetDirectory:blocked});assert.fail('raw assembly exported');}catch(e){assert.match(e.message,/装配工艺尚未完成/);report.rawError=e.message;}
 assert.equal(readdirSync(blocked).length,0);report.checks.push('unexecuted raw assembly remains blocked after grouping change');
 const good=both.manufacturingGroups.find(g=>g.productEntityId===s.product.entityId).parts;
 const target=join(root,'selected-export');mkdirSync(target,{recursive:true});
 report.selected=await invoke('ExportSelected',{partEntityIds:good.slice(0,2).map(p=>p.entityId),targetDirectory:target});
 assert.equal(report.selected.exportedCount,2);assert.equal(report.selected.exportedGroups.length,1);assert.equal(report.selected.draftOnly,false);
 report.checks.push('two grouped types export without unrelated unfinished product');report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{writeFileSync(join(root,'safety-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
