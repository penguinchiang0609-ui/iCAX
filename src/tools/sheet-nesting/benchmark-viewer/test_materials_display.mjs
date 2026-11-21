// Real browser checks of partial/complete/error wording and unchanged source stock.
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import {createRequire} from "node:module";
import {fileURLToPath} from "node:url";

const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,"../../../..");
const require=createRequire(path.join(root,"src/tests/iCAX-UI/materials-test.cjs"));
const {chromium}=require("playwright");
const browser=await chromium.launch({headless:true,channel:"msedge"});
try {
    const page=await browser.newPage({viewport:{width:1600,height:1000}});
    if(process.argv.includes("--deployed-source-copies")){
        const base=process.env.MATERIALS_VIEWER_URL||"http://127.0.0.1:8769";
        const id=process.argv.find(value=>value.startsWith("--source-task="))?.slice("--source-task=".length)||"automotive_16_plus_mobile_equipment_20_DXF--4aa4df3b725977e4";
        const detail=await (await page.request.get(`${base}/api/materials/tasks/${id}`)).json();
        assert.equal(detail.task.sourceQuantityRows.length,1);const source=detail.task.sourceQuantityRows[0];
        assert.equal(source.quantity,1);assert.equal(source.materialComponentCount,7);assert.equal(source.nativePieceQuantity,7);
        assert.equal(detail.task.parts.length,7);assert.equal(detail.task.required,7);assert.equal(detail.assets.length,1);
        await page.goto(`${base}/materials.html`);await page.locator("#search").fill(id);
        await page.waitForFunction(id=>document.querySelector("#list-body tr")?.dataset.key===id,id);
        await page.locator("#list-body .row-title").first().click();
        await page.waitForFunction(id=>document.getElementById("detail-id").textContent===id,id);
        assert.equal(await page.locator("#bom-body tr").count(),1);assert.equal(await page.locator("#bom-body a").count(),1);
        assert.equal(await page.locator("#bom-body td").nth(1).textContent(),"1 份 DXF");
        assert.match(await page.locator("#bom-body").textContent(),/每份含 7 个材料分量.*原始 DXF 按 1 份导入，实际排 7 件/);
        assert.match(await page.locator("#bom-caption").textContent(),/1 份；实际材料 7 种 \/ 7 件/);
        const download=await page.locator("#bom-body a").getAttribute("href");assert.equal(download,detail.assets[0].downloadUrl);
        const bytes=await (await page.request.get(`${base}${download}`)).body();
        const {createHash}=await import("node:crypto");assert.equal(createHash("sha256").update(bytes).digest("hex"),detail.assets[0].sha256);
        await page.locator("#search").fill("automotive");await page.waitForTimeout(300);
        let detailRequests=0;page.on("request",request=>{if(new URL(request.url()).pathname===`/api/materials/tasks/${id}`)detailRequests++;});
        const saved=await page.evaluate(()=>{document.getElementById("task-conditions-json").parentElement.open=true;
            const input=document.getElementById("search");input.focus();input.setSelectionRange(1,4);
            window.keptSourceInput=input;window.keptSourceBom=document.querySelector("#bom-body tr");window.keptSourceDownload=document.querySelector("#bom-body a");window.keptSourceSvg=document.querySelector("#result-layouts svg");window.keptSourceRow=document.querySelector("#list-body tr");
            document.getElementById("list-scroll").scrollTop=140;document.getElementById("detail-scroll").scrollTop=120;
            return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop};});
        const beforeRequests=detailRequests;
        await page.waitForTimeout(5000);assert.ok(detailRequests>beforeRequests,"actual automatic refresh must re-read the selected source task");
        const after=await page.evaluate(()=>({focus:document.activeElement===window.keptSourceInput,selection:[window.keptSourceInput.selectionStart,window.keptSourceInput.selectionEnd],
            bom:document.querySelector("#bom-body tr")===window.keptSourceBom,download:document.querySelector("#bom-body a")===window.keptSourceDownload,
            svg:document.querySelector("#result-layouts svg")===window.keptSourceSvg,row:document.querySelector("#list-body tr")===window.keptSourceRow,
            list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop}));
        assert.equal(after.focus,true);assert.deepEqual(after.selection,[1,4]);assert.equal(after.bom,true);assert.equal(after.download,true);assert.equal(after.svg,true);assert.equal(after.row,true);assert.equal(after.list,saved.list);assert.equal(after.detail,saved.detail);
        const evidence=process.argv.find(value=>value.startsWith("--evidence="))?.slice("--evidence=".length);
        if(evidence){await fs.mkdir(path.dirname(path.resolve(evidence)),{recursive:true});await page.screenshot({path:evidence.replace(/\.json$/,".png"),fullPage:true});
            await fs.writeFile(evidence,JSON.stringify({checkedAt:new Date().toISOString(),task:id,inputSha256:detail.task.inputSha256,sourceQuantityRows:detail.task.sourceQuantityRows,
                nativePartIds:detail.task.parts.map(part=>part.id),nativeRequired:detail.task.required,downloadUrl:download,downloadSha256:detail.assets[0].sha256,
                renderedBom:await page.locator("#bom-body").textContent(),bomCaption:await page.locator("#bom-caption").textContent(),actualRefreshRequests:detailRequests,interaction:after},null,2));}
        console.log(`Deployed source copies verified: ${id}, one original DXF copy/download, 7 native components/pieces, unchanged original hash and preserved automatic-refresh interaction`);
    } else if(process.argv.includes("--deployed")){
        const base=process.env.MATERIALS_VIEWER_URL||"http://127.0.0.1:8769";
        await page.goto(`${base}/materials.html`);
        const summary=await (await page.request.get(`${base}/api/materials/summary`)).json();
        await page.waitForFunction(()=>document.getElementById("ready-count").textContent!=="—");
        assert.equal(await page.locator("#ready-count").textContent(),summary.summary.ready.toLocaleString("zh-CN"));
        const sources=await (await page.request.get(`${base}/api/materials/tasks?filter=source-contours&limit=1`)).json();
        assert.equal(sources.total,summary.summary.sourceContourTasks);
        const source=sources.rows[0];
        if(source){
            await page.selectOption("#scope","source-contours");
            await page.locator("#search").fill(source.id);
            await page.waitForFunction(id=>document.querySelector("#list-body tr")?.dataset.key===id,source.id);
            await page.locator("#list-body .row-title").first().click();
            await page.waitForFunction(id=>document.getElementById("detail-id").textContent===id,source.id);
            assert.match(await page.locator("#detail-status").textContent(),/原始轮廓可执行/);
            const sourceDetail=await (await page.request.get(`${base}/api/materials/tasks/${source.id}`)).json();
            const actualAssetIds=new Set(sourceDetail.assets.map(asset=>asset.id));
            assert.equal(await page.locator("#bom-body a").count(),sourceDetail.task.parts.filter(part=>actualAssetIds.has(part.assetId)).length);
            assert.match(await page.locator("#bom-body").textContent(),/DXF待按需导出/);
        }
        const invalidSources=await (await page.request.get(`${base}/api/materials/tasks?filter=source-invalid&limit=1`)).json();
        assert.equal(invalidSources.total,summary.summary.sourceInvalidTasks);
        if(invalidSources.rows.length){
            const invalid=invalidSources.rows[0];
            await page.selectOption("#scope","source-invalid");await page.locator("#search").fill(invalid.id);
            await page.waitForFunction(id=>document.querySelector("#list-body tr")?.dataset.key===id,invalid.id);
            await page.locator("#list-body .row-title").first().click();
            await page.waitForFunction(id=>document.getElementById("detail-id").textContent===id,invalid.id);
            assert.match(await page.locator("#detail-status").textContent(),/原始轮廓无效/);
            assert.match(await page.locator("#task-issues").textContent(),/原始轮廓校验证据/);
        }
        const tasks=await (await page.request.get(`${base}/api/materials/tasks?filter=executed&limit=200`)).json();
        const measured=tasks.rows.find(task=>task.benchmark.complete===true&&task.benchmark.valid===true);
        let detail=null;
        if(measured){
            await page.selectOption("#scope","");
            await page.locator("#search").fill(measured.id);
            await page.waitForFunction(id=>document.querySelector("#list-body tr")?.dataset.key===id,measured.id);
            await page.locator("#list-body .row-title").first().click();
            await page.waitForFunction(id=>document.getElementById("detail-id").textContent===id,measured.id);
            detail=await (await page.request.get(`${base}/api/materials/tasks/${measured.id}`)).json();
            if(detail.task.benchmark.remnants?.measured===true){
                await page.waitForFunction(()=>document.getElementById("result-remnant-status").textContent.startsWith("已评估"));
                assert.ok(await page.locator("#result-layouts svg").count()>0);
            }
        }
        const evidenceArg=process.argv.find(value=>value.startsWith("--evidence="));
        if(evidenceArg){
            const evidence=path.resolve(evidenceArg.slice("--evidence=".length));
            await fs.mkdir(path.dirname(evidence),{recursive:true});
            await page.screenshot({path:evidence.replace(/\.json$/,".png"),fullPage:true});
            await page.locator("#detail-scroll").evaluate(scroller=>{const caption=document.getElementById("result-caption");scroller.scrollTop+=caption.getBoundingClientRect().top-scroller.getBoundingClientRect().top-5;});
            await page.locator(".detail-panel").screenshot({path:evidence.replace(/\.json$/,".result.png")});
            await fs.writeFile(evidence,JSON.stringify({checkedAt:new Date().toISOString(),url:base,summary:summary.summary,sourceTask:source?.id,measuredTask:measured?.id,
                nativeRemnants:detail?.task.benchmark.remnants,firstCompleteWallSeconds:detail?.task.benchmark.firstCompleteWallSeconds,nativeWallSeconds:detail?.task.benchmark.nativeWallSeconds,
                endToEndWallSeconds:detail?.task.benchmark.endToEndWallSeconds,rollUsage:detail?.task.benchmark.rollUsage,actualRenderedRemnants:await page.locator("#result-remnants").textContent()},null,2));
        }
        console.log(`Deployed materials page verified: ${summary.summary.ready} ready, ${sources.total} source-contour tasks, no fabricated DXF link, actual ${measured?.id||"no measured result"} shown`);
    } else {
    let revision=1;
    const originalShape={radius:0,center:[0,0],outer:[[0,0],[13798.2,0],[13798.2,19984],[0,19984]],holes:[[[1000,1000],[2000,1000],[2000,2000],[1000,2000]]]};
    let benchmark={status:"finished",placed:13,required:26,complete:false,valid:true,boards:5,nativeWallSeconds:60.0327,utilization:.4,
        problem:{parts:[{id:"p",geometry:originalShape}]},
        layouts:[0,1].map(instance=>({stock:"stock",instance,width:17018,height:1000000,placements:[{part:"p",instance,x:.5,y:.5,angle:0}]}))};
    const task={id:"textile-6",label:"纺织任务 6",collection:"textile",status:"ready",types:1,required:26,
        parts:[],issues:[],missingAssets:[],sourceStock:{kind:"roll",width:17018,height:-1,units:"unitless"},
        stocks:[{widthMm:17018,heightMm:1000000,unlimited:true,quantity:null,source:"source-roll-test-length"}],
        rules:{edgeMarginMm:.5,partGapMm:.2,freeRotation:true,allowCommonEdges:true}};
    const profileTest=process.argv.includes("--profile");
    const liveTest=process.argv.includes("--live");
    const progressTest=process.argv.includes("--progress");
    const metricsTest=process.argv.includes("--metrics");
    const remnantsTest=process.argv.includes("--remnants");
    const traceErrorTest=process.argv.includes("--trace-error");
    const sourceCopiesTest=process.argv.includes("--source-copies");
    const mirrorTest=process.argv.includes("--mirror");
    let terminalProgress=progressTest?{status:"needs-fix",completed:13,total:13,failed:0,incomplete:1,invalid:0,unresolved:1}:null;
    const completedBenchmark={...benchmark,placed:26,complete:true,firstCompleteWallSeconds:.123};
    const asset={id:"curve-dxf",name:"B2.dxf",downloadUrl:"/api/materials/assets/curve-dxf/dxf",geometry:{status:"ready",curveApproximation:true,maxDeviationMm:.01}};
    if(sourceCopiesTest){
        task.required=7;task.types=7;task.parts=Array.from({length:7},(_,i)=>({id:`P0000_C00${i}`,assetId:asset.id,quantity:1}));
        task.sourceQuantityRows=[{id:"source",assetId:asset.id,sourcePath:"original/seven-components.dxf",quantity:1,quantitySource:"benchmark-default-one",materialComponentCount:7,nativePieceQuantity:7}];
        benchmark={...completedBenchmark,required:7,placed:7,boards:1,
            problem:{parts:task.parts.map(part=>({id:part.id,geometry:{outer:[[0,0],[2,0],[2,2],[0,2]],holes:[]}}))},
            layouts:[{stock:"stock",instance:0,width:17018,height:1000000,placements:task.parts.map((part,index)=>({part:part.id,instance:0,x:.5+index*2.2,y:.5,angle:0}))}]};
    }
    if(profileTest){
        task.parts=[0,1].map(componentIndex=>({assetId:asset.id,sourcePath:"source/B2.dxf",quantity:13,quantitySource:"source",componentIndex,componentCount:2}));
        benchmark={status:"stale-algorithm",reason:"算法已更新，等待本次重跑"};
    }
    if(mirrorTest){
        task.rules.allowMirror=true;
        benchmark={...completedBenchmark,problem:{parts:[{id:"p",allowMirror:true,geometry:originalShape}]},
            layouts:[{stock:"stock",instance:0,width:30000,height:1000000,
                placements:[{part:"p",instance:0,x:.5,y:.5,angle:90,mirrored:true}]}]};
    }
    if(liveTest)benchmark={...completedBenchmark,status:"constructing",live:true,timingScope:"live-current",firstCompleteWallSeconds:.5,nativeWallSeconds:5};
    if(remnantsTest)benchmark={...completedBenchmark,bestFoundWallSeconds:12.345,endToEndWallSeconds:61.2,remnants:{measured:true,lastSheetRegularArea:2400,lastSheetAreaUpperBound:999999,finalLayoutIndices:[0],perLayout:[{verified:true,area:2400,areaUpperBound:999999,resolution:64,bounds:{x:1,y:2,width:40,height:60}}]}};
    if(traceErrorTest){benchmark={...completedBenchmark,constructionTraceError:"native observer write failed"};terminalProgress={status:"needs-fix",completed:1,total:1,incomplete:0,invalid:0,failed:0,observationFailed:1,unresolved:1};}
    if(metricsTest)benchmark={...completedBenchmark,utilization:.00123,materialArea:1,sheetArea:1000,
        rollUsage:{axis:"y",lengths:[20484.5,20484.5],totalLength:40969,usedArea:100,utilization:.8765}};
    await page.route("http://materials.test/**",async route=>{
        const url=new URL(route.request().url());
        if(url.pathname==="/api/materials/summary")return route.fulfill({json:{revision,available:true,generatedAt:"2026-11-07T19:56:27Z",defaults:{...task.rules,stockWidthMm:3000,stockHeightMm:6000},summary:{archives:1,assets:1,tasks:60,ready:60,executed:["stale-algorithm","constructing"].includes(benchmark.status)?0:1,constructing:benchmark.status==="constructing"?1:0},progress:terminalProgress||(profileTest||liveTest?{status:"running",currentTasks:["textile-6","textile-7","textile-8"],caseConcurrency:3,workers:4,completed:1,total:60,timeLimitSeconds:60}:{}),archives:[],collections:["textile"]}});
        if(url.pathname==="/api/materials/tasks")return route.fulfill({json:{revision,total:60,rows:Array.from({length:50},(_,index)=>({...task,id:index?`other-${index}`:task.id,benchmark}))}});
        if(url.pathname.startsWith("/api/materials/tasks/"))return route.fulfill({json:{revision,task:{...task,benchmark},assets:profileTest||sourceCopiesTest?[asset]:[],defaults:task.rules}});
        const filename=path.basename(url.pathname);
        if(["materials.html","materials.js","materials.css","layoutGeometry.js"].includes(filename))return route.fulfill({body:await fs.readFile(path.join(here,filename)),contentType:filename.endsWith(".js")?"text/javascript":filename.endsWith(".css")?"text/css":"text/html"});
        return route.fulfill({status:404,body:"Not found"});
    });
    await page.goto("http://materials.test/materials.html");
    if(sourceCopiesTest){
        await page.waitForFunction(()=>document.querySelector("#bom-body tr")?.textContent.includes("每份含 7 个材料分量"));
        assert.equal(await page.locator("#bom-body tr").count(),1);
        assert.equal(await page.locator("#bom-body a").count(),1);
        assert.match(await page.locator("#bom-caption").textContent(),/1 份；实际材料 7 种 \/ 7 件/);
        assert.equal(await page.locator("#bom-body td").nth(1).textContent(),"1 份 DXF");
        assert.match(await page.locator("#bom-body").textContent(),/原始 DXF 按 1 份导入，实际排 7 件/);
        assert.equal(await page.locator("#result-layouts .board-scroll path").count(),7);
        const saved=await page.evaluate(()=>{const input=document.getElementById("search");input.focus();input.setSelectionRange(0,0);window.sourceInput=input;window.sourceSvg=document.querySelector("#result-layouts svg");window.sourceBom=document.querySelector("#bom-body tr");document.getElementById("list-scroll").scrollTop=140;document.getElementById("detail-scroll").scrollTop=120;return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop};});
        benchmark={...benchmark,nativeWallSeconds:60.1};revision++;
        await page.waitForFunction(()=>document.getElementById("result-time").textContent==="60.1 s",null,{timeout:12000});
        const after=await page.evaluate(()=>({focus:document.activeElement===window.sourceInput,bom:document.querySelector("#bom-body tr")===window.sourceBom,svg:document.querySelector("#result-layouts svg")===window.sourceSvg,list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop}));
        assert.equal(after.focus,true);assert.equal(after.bom,true);assert.equal(after.svg,true);assert.equal(after.list,saved.list);assert.equal(after.detail,saved.detail);
        console.log("Materials source quantity: one original DXF import/download, seven native components, true geometry and automatic-refresh focus/BOM/SVG/scroll preserved");
    } else if(traceErrorTest){
        await page.waitForFunction(()=>document.getElementById("run-progress").textContent.includes("过程采集异常 1 项"));
        assert.match(await page.locator("#result-caption").textContent(),/已排齐 26\/26件.*过程采集异常/);
        assert.match(await page.locator("#result-check").textContent(),/原生几何检查通过/);
        assert.match(await page.locator("#task-construction").textContent(),/过程采集异常：native observer write failed/);
        assert.equal(await page.locator("#task-construction a").getAttribute("href"),"/construction.html?materialTask=textile-6");
        assert.match(await page.locator("#run-progress").textContent(),/未排齐 0 项.*不合法 0 项.*运行错误 0 项.*过程采集异常 1 项/);
        console.log("Materials observation failure: actual complete/valid layout retained, observation error and affected task count displayed separately, bound material replay link retained");
    } else if(mirrorTest){
        await page.waitForFunction(()=>document.querySelector('#result-layouts path[data-mirrored="true"]'));
        const pose=await page.locator('#result-layouts path[data-mirrored="true"]').first().evaluate(node=>{
            const m=node.transform.baseVal.consolidate().matrix;
            return {matrix:[m.a,m.b,m.c,m.d,m.e,m.f],d:node.getAttribute("d")};
        });
        pose.matrix.forEach((value,index)=>assert.ok(Math.abs(value-[0,1,1,0,.5,.5][index])<1e-12));
        assert.match(pose.d,/1000 1000.*2000 1000.*2000 2000/);
        assert.match(await page.locator('#task-rules').textContent(),/未限制的零件允许镜像/);
        const bounds=await page.evaluate(geometry=>PlaneLayoutGeometry.geometryBounds([{geometry,pose:{x:.5,y:.5,angle:90,mirrored:true}}]),originalShape);
        for(const [name,value] of Object.entries({minX:.5,minY:.5,maxX:19984.5,maxY:13798.7}))assert.ok(Math.abs(bounds[name]-value)<1e-9);
        console.log("Materials mirror: actual SVG reflection before rotation, original hole path, world bounds and permissions passed");
    } else if(remnantsTest){
        await page.waitForFunction(()=>document.getElementById("result-remnant-rectangles").textContent.includes("40 × 60 mm"));
        assert.match(await page.locator("#result-remnant-status").textContent(),/已评估.*通过原生间距与材料边界检查/);
        assert.match(await page.locator("#result-remnant-rectangles").textContent(),/第1板：40 × 60 mm；位置 \(1, 2\) mm；面积 2,400 mm²/);
        assert.doesNotMatch(await page.locator("#result-remnant-rectangles").textContent(),/999,999/);
        assert.match(await page.locator("#result-milestones").textContent(),/首次排齐 0.123 秒；最佳方案找到 12.345 秒（原生现场快照时间）；整轮耗时 60.033 秒；进程总耗时 61.2 秒/);
        benchmark={...benchmark,remnants:{measured:true,perLayout:[{verified:false,area:2400,bounds:{x:1,y:2,width:40,height:60}}]}};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-remnant-status").textContent.includes("未找到通过原生认证"));
        assert.equal(await page.locator("#result-remnant-rectangles").textContent(),"");
        benchmark={...benchmark,remnants:{measured:false}};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-remnant-status").textContent.includes("尚未评估"));
        console.log("Materials native remnants: original verified rectangle, upper-bound exclusion, measured-empty/unmeasured distinction and end-to-end timing passed");
    } else if(metricsTest){
        await page.waitForFunction(()=>document.getElementById("result-roll-length").textContent.includes("40,969"));
        assert.match(await page.locator(".result-metrics").textContent(),/整板利用率/);
        assert.equal(await page.locator("#result-utilization").textContent(),"0.12%");
        assert.equal(await page.locator("#result-roll-utilization").textContent(),"87.65%");
        assert.match(await page.locator("#result-roll-lengths").textContent(),/第1板 20,484\.5 mm；第2板 20,484\.5 mm/);
        assert.match(await page.locator("#result-roll-note").textContent(),/从原坐标0计算至原件末端加原边距；固定幅宽保持原板尺寸，多板逐板计算后累加/);
        await page.selectOption("#layout-view-mode","full");
        await page.locator("#layout-zoom").evaluate(input=>{input.value="200";input.dispatchEvent(new Event("input",{bubbles:true}));});
        await page.locator("#search").fill("textile");await page.waitForTimeout(250);
        const saved=await page.evaluate(()=>{const input=document.getElementById("search");input.focus();input.setSelectionRange(1,4);window.metricsInput=input;document.getElementById("list-scroll").scrollTop=70;document.getElementById("detail-scroll").scrollTop=100;return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop};});
        benchmark={...benchmark,rollUsage:{...benchmark.rollUsage,utilization:.6543}};revision++;
        await page.waitForFunction(()=>document.getElementById("result-roll-utilization").textContent==="65.43%",null,{timeout:12000});
        const current=await page.evaluate(()=>({focus:document.activeElement===window.metricsInput,selection:[window.metricsInput.selectionStart,window.metricsInput.selectionEnd],list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop,mode:document.getElementById("layout-view-mode").value,zoom:document.getElementById("layout-zoom").value}));
        assert.equal(current.focus,true);assert.deepEqual(current.selection,[1,4]);assert.equal(current.list,saved.list);assert.equal(current.detail,saved.detail);assert.equal(current.mode,"full");assert.equal(current.zoom,"200");
        benchmark={...benchmark,complete:false,placed:13};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-roll-note").textContent.includes("不能作为整单成绩"));
        assert.match(await page.locator("#result-caption").textContent(),/未排齐 13\/26件/);
        task.sourceStock={kind:"sheet",width:17018,height:1000000};benchmark={...benchmark,rollUsage:null};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("roll-metrics").hidden);
        assert.equal(await page.locator("#roll-metrics").isVisible(),false);
        console.log("Materials PK metrics display: original sheet rate retained, roll length/segment rate, partial warning, finite-sheet exclusion and preserved automatic-refresh interaction passed");
    } else if(progressTest){
        await page.waitForFunction(()=>document.getElementById("run-progress").textContent.includes("待修复"));
        assert.match(await page.locator("#run-progress").textContent(),/任务已执行完，仍有1 项未排齐、不合法或运行错误，待修复.*未排齐 1 项.*不合法 0 项.*运行错误 0 项/);
        await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("未排齐 13/26件"));
        assert.match(await page.locator("#list-body tr").first().locator("td").nth(4).textContent(),/未排齐 13\/26件/);
        terminalProgress={...terminalProgress,incomplete:2,invalid:1,failed:1,unresolved:3};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("run-progress").textContent.includes("仍有3 项"));
        assert.match(await page.locator("#run-progress").textContent(),/未排齐 2 项.*不合法 1 项.*运行错误 1 项/);
        terminalProgress={...terminalProgress,status:"finished"};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        assert.match(await page.locator("#run-progress").textContent(),/待修复/);
        terminalProgress={...terminalProgress,incomplete:0,invalid:0,failed:0,unresolved:0};benchmark=completedBenchmark;revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("run-progress").textContent.includes("排样完成以各任务同时合法且排齐为准"));
        assert.doesNotMatch(await page.locator("#run-progress").textContent(),/全部达标|全面完成|质量达标/);
        terminalProgress={...terminalProgress,status:"interrupted",completed:13,total:531};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("run-progress").textContent.includes("本批已中断"));
        assert.match(await page.locator("#run-progress").textContent(),/已返回 13 \/ 531；队列尚未执行完/);
        console.log("Materials progress display: needs-fix counts, partial finished result, invalid/error categories, legacy terminal guard and interrupted queue wording passed");
    } else if(liveTest){
        await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("初始解已排齐，策略比较中"));
        assert.match(await page.locator("#list-body tr").first().locator("td").nth(4).textContent(),/初始解已排齐，策略比较中 26\/26件.*已运行 5s/);
        assert.equal(await page.locator("#executed-count").textContent(),"0");
        assert.match(await page.locator("#connection").textContent(),/初始解比较中 1 项/);
        assert.match(await page.locator("#result-milestones").textContent(),/首次排齐 0\.5 秒（现场发布）；当前已运行 5 秒，策略比较中/);
        assert.doesNotMatch(await page.locator("#result-milestones").textContent(),/最佳方案找到|整轮耗时/);
        assert.match(await page.locator("#result-check").textContent(),/现场候选，还不是最终选择/);
        assert.equal(await page.locator("#result-layouts svg").count(),2);
        await page.selectOption("#layout-view-mode","full");
        await page.locator("#layout-zoom").evaluate(input=>{input.value="200";input.dispatchEvent(new Event("input",{bubbles:true}));});
        await page.locator("#search").fill("textile");await page.waitForTimeout(250);
        const saved=await page.evaluate(()=>{const input=document.getElementById("search");input.focus();input.setSelectionRange(1,4);window.liveInput=input;document.getElementById("list-scroll").scrollTop=70;document.getElementById("detail-scroll").scrollTop=100;return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop};});
        benchmark={...benchmark,nativeWallSeconds:10};revision++;
        await page.waitForFunction(()=>document.getElementById("result-milestones").textContent.includes("当前已运行 10 秒"),null,{timeout:12000});
        const current=await page.evaluate(()=>({focused:document.activeElement===window.liveInput,selection:[window.liveInput.selectionStart,window.liveInput.selectionEnd],list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop,mode:document.getElementById("layout-view-mode").value,zoom:document.getElementById("layout-zoom").value}));
        assert.equal(current.focused,true);assert.deepEqual(current.selection,[1,4]);assert.equal(current.list,saved.list);assert.equal(current.detail,saved.detail);assert.equal(current.mode,"full");assert.equal(current.zoom,"200");
        benchmark={...completedBenchmark,status:"finished",nativeWallSeconds:60};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-milestones").textContent.includes("整轮耗时 60 秒"));
        assert.match(await page.locator("#result-milestones").textContent(),/未记录最佳方案找到时间/);
        assert.doesNotMatch(await page.locator("#result-caption").textContent(),/策略比较中/);
        assert.equal(await page.locator("#executed-count").textContent(),"1");
        benchmark={...completedBenchmark,status:"constructing",timingScope:"live-snapshot",nativeWallSeconds:3.75};revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-milestones").textContent.includes("本候选发布时已运行 3.75 秒"));
        assert.doesNotMatch(await page.locator("#result-milestones").textContent(),/当前已运行|最佳方案找到|整轮耗时/);
        console.log("Materials live display: immediate validated incumbent, active/final wording and counters, live/snapshot timing, real layout and automatic-refresh interaction passed");
    } else if(profileTest){
        await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("算法已更新，等待本次重跑"));
        assert.equal(await page.locator("#list-body tr").first().locator("td").nth(4).textContent(),"算法已更新，等待本次重跑");
        assert.equal(await page.locator("#result-boards").textContent(),"—");
        assert.equal(await page.locator("#executed-count").textContent(),"0");
        assert.match(await page.locator("#run-progress").textContent(),/textile-6、textile-7、textile-8.*当前 3 项 \/ 最多 3 项并行.*每任务 60 s 上限、各 4 个构造线程/);
        const names=await page.locator("#bom-body tr td:first-child").allTextContents();
        assert.match(names[0],/B2\.dxf · 轮廓 1\/2/);assert.match(names[1],/B2\.dxf · 轮廓 2\/2/);
        assert.match(names[0],/曲线导入公差 0\.01 mm；近似弦不参与共边/);
        benchmark=completedBenchmark;revision++;
        await page.evaluate(()=>document.getElementById("refresh").click());
        await page.waitForFunction(()=>document.getElementById("result-milestones").textContent.includes("首次排齐 0.123 秒"));
        assert.match(await page.locator("#result-milestones").textContent(),/首次排齐 0\.123 秒；未记录最佳方案找到时间；整轮耗时 60\.033 秒。首次排齐与最终布局质量分别判断/);
        await page.selectOption("#layout-view-mode","full");
        await page.locator("#layout-zoom").evaluate(input=>{input.value="200";input.dispatchEvent(new Event("input",{bubbles:true}));});
        await page.locator("#search").fill("textile");await page.waitForTimeout(250);
        const saved=await page.evaluate(()=>{const input=document.getElementById("search");input.focus();input.setSelectionRange(1,4);window.profileInput=input;document.getElementById("list-scroll").scrollTop=70;document.getElementById("detail-scroll").scrollTop=100;return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop};});
        benchmark={...benchmark,nativeWallSeconds:61.234};revision++;
        await page.waitForFunction(()=>document.getElementById("result-milestones").textContent.includes("整轮耗时 61.234 秒"),null,{timeout:12000});
        const current=await page.evaluate(()=>({same:document.activeElement===window.profileInput,selection:[window.profileInput.selectionStart,window.profileInput.selectionEnd],list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop,mode:document.getElementById("layout-view-mode").value,zoom:document.getElementById("layout-zoom").value}));
        assert.equal(current.same,true);assert.deepEqual(current.selection,[1,4]);assert.equal(current.list,saved.list);assert.equal(current.detail,saved.detail);assert.equal(current.mode,"full");assert.equal(current.zoom,"200");
        console.log("Materials profile display: stale algorithm, three cases with four workers each, first/final timings, component labels, curve tolerance and preserved automatic-refresh interaction passed");
    } else {
    const resultCell=page.locator("#list-body tr").first().locator("td").nth(4);
    await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("未排齐 13/26件"));
    assert.equal(await resultCell.textContent(),"未排齐 13/26件 · 剩余13件 · 5板 · 60.033s");
    assert.equal(await page.locator("#result-quantity").textContent(),"13 / 26 件");
    assert.equal(await page.locator("#result-remaining").textContent(),"13 件");
    assert.match(await page.locator("#result-check").textContent(),/原生几何检查通过.*当前未排齐/);
    assert.match(await page.locator("#task-stocks").textContent(),/17,018 × 1,000,000 mm.*来源卷料：保留原幅宽，不限方向按1,000,000测试/);
    assert.doesNotMatch(await page.locator("#task-stocks").textContent(),/来源未指定/);
    assert.match(await page.locator("#list-body tr").first().locator("td").nth(2).textContent(),/17,018 × 1,000,000 mm.*来源卷料：保留原幅宽/);
    assert.match(await page.locator("#default-conditions").textContent(),/无来源尺寸：3,000 × 6,000 mm/);
    assert.match(await page.locator("#task-source-stock").textContent(),/17018/);
    assert.match(await page.locator("#generated-at").textContent(),/2026\/11\/08 03:56:27.*北京时间/);

    const defaultDrawing=await page.locator("#result-layouts svg").first().evaluate(svg=>({height:Number(svg.getAttribute("height")),width:Number(svg.getAttribute("width")),
        viewHeight:svg.viewBox.baseVal.height,mode:svg.dataset.viewMode,stroke:svg.querySelector("path").getAttribute("stroke-width"),
        vector:svg.querySelector("path").getAttribute("vector-effect"),borderVector:svg.querySelector("rect").getAttribute("vector-effect"),
        d:svg.querySelector("path").getAttribute("d"),transform:svg.querySelector("path").getAttribute("transform")}));
    assert.equal(defaultDrawing.mode,"used");assert.ok(defaultDrawing.height<=600.001);assert.ok(defaultDrawing.width<=550.001);assert.ok(defaultDrawing.viewHeight<25000);
    assert.equal(defaultDrawing.stroke,"0.8");assert.equal(defaultDrawing.vector,"non-scaling-stroke");assert.equal(defaultDrawing.borderVector,"non-scaling-stroke");
    assert.match(defaultDrawing.d,/13798\.2 19984/);assert.equal(defaultDrawing.transform,"translate(0.5 0.5) rotate(0)");
    assert.match(await page.locator(".board-caption").first().textContent(),/原板 17,018 × 1,000,000 mm/);
    assert.match(await page.locator(".board-view-note").first().textContent(),/仅省略显示范围之外的空板；原板高度仍为 1,000,000 mm，零件坐标不变/);
    const scales=await page.locator("#result-layouts svg").evaluateAll(nodes=>nodes.map(node=>node.dataset.pixelsPerMm));assert.equal(new Set(scales).size,1);
    await page.selectOption("#layout-view-mode","full");
    assert.equal(await page.locator("#result-layouts svg").first().evaluate(svg=>svg.viewBox.baseVal.height),1000000);
    assert.ok(await page.locator("#result-layouts svg").first().evaluate(svg=>Number(svg.getAttribute("height")))<=600.001);
    await page.locator("#layout-zoom").evaluate(input=>{input.value="200";input.dispatchEvent(new Event("input",{bubbles:true}));});
    assert.equal(await page.locator("#layout-zoom-value").textContent(),"200%");
    assert.equal(await page.locator("#result-layouts path").first().getAttribute("stroke-width"),"0.8");

    // A new real result must refresh existing rows without replacing focused controls.
    await page.locator("#search").fill("textile");
    await page.waitForTimeout(250);
    const before=await page.evaluate(()=>{
        const input=document.getElementById("search");input.focus();input.setSelectionRange(1,4);
        document.getElementById("list-scroll").scrollTop=70;
        document.getElementById("detail-scroll").scrollTop=100;
        window.materialsFocusNode=input;
        document.querySelector(".board-scroll").scrollTop=80;
        window.materialsViewNode=document.getElementById("layout-view-mode");
        return {list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop,board:document.querySelector(".board-scroll").scrollTop};
    });
    benchmark={...benchmark,placed:26,complete:true,boards:7,layouts:benchmark.layouts.map((layout,index)=>index?layout:{...layout,placements:[{...layout.placements[0],x:.75}]})};revision++;
    // Let the real four-second automatic refresh deliver the changed layout.
    await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("已排齐 26/26件"),{timeout:12000});
    assert.equal(await resultCell.textContent(),"已排齐 26/26件 · 剩余0件 · 7板 · 60.033s");
    assert.equal(await page.locator("#result-remaining").textContent(),"0 件");
    const after=await page.evaluate(()=>({same:document.getElementById("search")===window.materialsFocusNode,focused:document.activeElement===window.materialsFocusNode,
        selection:[window.materialsFocusNode.selectionStart,window.materialsFocusNode.selectionEnd],list:document.getElementById("list-scroll").scrollTop,detail:document.getElementById("detail-scroll").scrollTop,
        board:document.querySelector(".board-scroll").scrollTop,mode:document.getElementById("layout-view-mode").value,zoom:document.getElementById("layout-zoom").value,
        sameViewNode:window.materialsViewNode===document.getElementById("layout-view-mode")}));
    assert.equal(after.same,true);assert.equal(after.focused,true);assert.deepEqual(after.selection,[1,4]);assert.equal(after.list,before.list);assert.equal(after.detail,before.detail);
    assert.equal(after.board,before.board);assert.equal(after.mode,"full");assert.equal(after.zoom,"200");assert.equal(after.sameViewNode,true);
    assert.equal(await page.locator("#result-layouts path").first().getAttribute("transform"),"translate(0.75 0.5) rotate(0)");

    benchmark={...benchmark,valid:false};revision++;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("原生几何检查未通过"));
    assert.match(await resultCell.textContent(),/^原生几何检查未通过/);
    assert.match(await page.locator("#result-caption").getAttribute("class"),/invalid/);
    benchmark={status:"error",error:"native process failed"};revision++;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("result-caption").textContent.includes("运行异常"));
    assert.equal(await resultCell.textContent(),"运行异常");
    assert.equal(await page.locator("#result-boards").textContent(),"—");
    benchmark={status:"pending",placed:26,complete:true,valid:true,boards:7};revision++;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("result-caption").textContent.startsWith("尚未排样"));
    assert.equal(await resultCell.textContent(),"等待排样");
    assert.equal(await page.locator("#result-quantity").textContent(),"—");
    console.log("Materials display: fixed pixel strokes, million-length used/full views, true coordinates, common scale, partial/complete states and automatic-refresh focus/selection/both-panel and board scroll checks passed");
    }
    }
} finally {
    await browser.close();
}
