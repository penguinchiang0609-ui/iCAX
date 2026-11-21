// Exercise the real replay UI with native-shaped observations and delayed updates.
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import {createRequire} from "node:module";
import {fileURLToPath} from "node:url";
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,"../../../..");
const require=createRequire(path.join(root,"src/tests/iCAX-UI/construction-test.cjs"));
const {chromium}=require("playwright");
const localSpaceLabels={largestFreeArea:"大块空余面积（largestFreeArea）",largeRegularFreeSpace:"大块空间规整评分（largeRegularFreeSpace）",
    fragmentFreeArea:"碎片空余面积（fragmentFreeArea）",freeBoundaryPerimeter:"空余边界总长（freeBoundaryPerimeter）",
    attachment:"贴合量（attachment）",freeArea:"空余面积（freeArea）",freeComponents:"空余区数量（freeComponents）",
    spaceEvaluationSeconds:"局部评价耗时（spaceEvaluationSeconds）",spaceCandidatesEvaluated:"局部评价候选数（spaceCandidatesEvaluated）",
    spaceCircleProxy:"圆形评分代理（spaceCircleProxy）",negativeLargeRegularFreeSpace:"负大块空间规整评分（negativeLargeRegularFreeSpace）",
    negativeAttachment:"负贴合量（negativeAttachment）"};
const browser=await chromium.launch({headless:true,channel:"msedge"});
try{
    const page=await browser.newPage({viewport:{width:1700,height:1000}});
    if(process.argv.includes("--deployed")||process.argv.includes("--deployed-trace")){
        const origin=process.env.MATERIALS_VIEWER_URL||"http://127.0.0.1:8769";
        const legacy=process.argv.includes("--deployed-trace");
        const task=process.argv.find(value=>value.startsWith(legacy?"--trace=":"--material-task="))?.split("=").slice(1).join("=")||"textile-0";
        const endpoint=`${origin}/api/construction/${legacy?"":"materials/"}${encodeURIComponent(task)}`;
        await page.goto(process.argv.includes("--default")?`${origin}/construction.html`:`${origin}/construction.html?${legacy?"trace":"materialTask"}=${encodeURIComponent(task)}`);
        const response=await page.request.get(endpoint);
        assert.equal(response.status(),200);const metadata=await response.json();
        await page.waitForFunction(id=>document.getElementById("task").value===id,legacy?task:`material::${task}`);
        let terminal=null,selection=null;const routeChecks=[],mirrorChecks=[];
        if(metadata.available){
            await page.waitForFunction(()=>document.getElementById("event-total").textContent!=="");
            if(process.argv.includes("--require-all-paths"))for(const route of metadata.routes.filter(row=>row.id!=="plane.initial"&&row.summary?.applicable)){
                assert.ok(route.terminal,`${route.id}: missing native route terminal`);
                assert.equal(route.construction?.status,"complete",`${route.id}: ${route.construction?.reason}`);
            }
            if(metadata.runInfo?.version)assert.ok((await page.locator("#run-summary").textContent()).includes(metadata.runInfo.version));
            else assert.match(await page.locator("#run-summary").textContent(),/运行版本：未记录/);
            const problemText=await page.locator("#problem-summary").textContent();
            assert.ok(problemText.includes(`边距：${metadata.index.problem.edgeMargin} mm`));
            assert.ok(problemText.includes(`零件间距：${metadata.index.problem.partGap} mm`));
            if(metadata.index.problem.parts.every(part=>part.allowMirror===undefined))assert.ok(problemText.includes(`未记录 ${metadata.index.problem.parts.length} 型`));
            const recordedRoutes=metadata.routes.filter(row=>row.terminal&&!row.terminalSnapshotTruncated);
            for(const route of process.argv.includes("--all-routes")?recordedRoutes:recordedRoutes.slice(0,1)){
                terminal=await (await page.request.get(`${endpoint}/events/${route.terminal.ordinal}`)).json();
                const routes=page.locator("#routes .route");
                for(let i=0;i<await routes.count();i++)if(await routes.nth(i).locator(".route-name").textContent()===route.id){await routes.nth(i).locator(".route-end").click();break;}
                await page.waitForFunction(ordinal=>document.getElementById("event-id").textContent.startsWith(`#${ordinal} ·`),route.terminal.ordinal);
                assert.equal(await page.locator("#boards svg").count(),terminal.snapshot?.layouts.length||0);
                assert.deepEqual(await page.locator("#ranks .measure span").allTextContents(),(terminal.event.rank||[]).map(row=>localSpaceLabels[row.name]||row.name));
                routeChecks.push({route:route.id,ordinal:terminal.ordinal,boards:terminal.snapshot?.layouts.length||0,
                    placed:terminal.snapshot?.layouts.reduce((sum,board)=>sum+board.placements.length,0)||0,rankNames:(terminal.event.rank||[]).map(row=>row.name),
                    viewModes:await page.locator("#boards svg").evaluateAll(nodes=>nodes.map(node=>({mode:node.dataset.viewMode,viewHeight:node.viewBox.baseVal.height,physicalScale:node.dataset.pixelsPerMm})))});
                if(route.construction?.status==="complete"){
                    const pathData=await (await page.request.get(`${endpoint}/events?view=construction&route=${encodeURIComponent(route.id)}&limit=100`)).json();
                    assert.ok(pathData.total>1,`${route.id}: no actual construction steps`);
                    await page.selectOption("#event-view","construction");
                    await page.waitForFunction(ordinal=>document.getElementById("event-id").textContent.startsWith(`#${ordinal} ·`),pathData.events[0].ordinal);
                    assert.equal(Number((await page.locator("#event-total").textContent()).replaceAll(",","")),pathData.total);
                    assert.ok(pathData.events.slice(0,-1).every(row=>["construction.path.commit","constructor.commit","fill.commit","envelope.skyline","competition.board-selection","sheet-template.copy"].includes(row.source)));
                    routeChecks.at(-1).constructionSteps=pathData.total;
                }
            }
            const final=metadata.decisions.findLast(event=>String(event.source||"").endsWith("final-selection"));
            if(final){
                await page.locator("#decisions button").filter({hasText:new RegExp(`^#${final.ordinal}$`)}).click();
                await page.waitForFunction(ordinal=>document.getElementById("event-id").textContent.startsWith(`#${ordinal} ·`),final.ordinal);
                selection=await (await page.request.get(`${endpoint}/events/${final.ordinal}`)).json();
                assert.match(await page.locator("#event-title").textContent(),/final-selection/);
                assert.equal(await page.locator("#boards svg").count(),selection.snapshot?.layouts.length||0);
                if(!metadata.index.finalSnapshotTruncated){
                    const required=metadata.index.problem.parts.reduce((sum,part)=>sum+part.quantity,0);
                    const placed=selection.event.layouts.reduce((sum,board)=>sum+board.placements.length,0);
                    const text=await page.locator("#completion-summary").textContent();
                    assert.ok(text.includes(`${placed} / ${required} 件`));
                    assert.ok(text.includes(placed<required?`未排 ${required-placed} 件`:"已排齐"));
                }
                for(const [boardIndex,board] of (selection.snapshot?.layouts||[]).entries()){
                    const drawings=page.locator("#boards svg").nth(boardIndex).locator(".original");
                    assert.equal(await drawings.count(),board.placements.length);
                    for(const [index,placed] of board.placements.entries()){
                        const pose=placed.pose||placed;if(pose.mirrored!==true)continue;
                        const part=metadata.index.problem.parts.find(part=>part.id===(placed.partID??placed.part));
                        assert.equal(part.allowMirror,true);
                        const node=drawings.nth(index);assert.equal(await node.getAttribute("data-mirrored"),"true");
                        const a=pose.angle*Math.PI/180,c=Math.cos(a),s=Math.sin(a);
                        const transform=([x,y])=>[pose.x+c*x+s*y,pose.y+s*x-c*y];
                        let expected,actual;
                        if(part.geometry.radius>0){expected=[transform(part.geometry.center)];actual=[[Number(await node.getAttribute("cx")),Number(await node.getAttribute("cy"))]];}
                        else{expected=[part.geometry.outer,...(part.geometry.holes||[])].flat().map(transform);
                            actual=[...(await node.getAttribute("d")).matchAll(/[ML]([^MLZ]+)/g)].map(match=>match[1].trim().split(/\s+/).map(Number));}
                        assert.equal(actual.length,expected.length);
                        actual.forEach((point,j)=>point.forEach((v,k)=>assert.ok(Math.abs(v-expected[j][k])<1e-7)));
                        mirrorChecks.push({boardIndex,partID:part.id,instance:placed.instance,verticesVerified:actual.length});
                    }
                }
            }
        }else{
            await page.waitForFunction(()=>document.getElementById("connection").textContent==="本任务暂无可用过程");
            assert.equal(await page.locator("#boards svg").count(),0);
            assert.match(await page.locator("#health").textContent(),/本次|本轮|过程/);
        }
        const evidence=process.argv.find(value=>value.startsWith("--evidence="))?.slice("--evidence=".length);
        if(evidence){await fs.mkdir(path.dirname(path.resolve(evidence)),{recursive:true});await page.screenshot({path:evidence.replace(/\.json$/,".png"),fullPage:true});
            await fs.writeFile(evidence,JSON.stringify({checkedAt:new Date().toISOString(),task,url:page.url(),available:metadata.available,binding:metadata.binding,
                indexedEvents:metadata.indexedEvents,routeCount:metadata.routes.length,routesVerified:routeChecks,index:metadata.index,terminalOrdinal:terminal?.ordinal,
                finalSelection:selection?.event,mirroredDrawingsVerified:mirrorChecks,runSummary:await page.locator("#run-summary").textContent(),problemSummary:await page.locator("#problem-summary").textContent(),completionSummary:await page.locator("#completion-summary").textContent(),health:await page.locator("#health").textContent()},null,2));}
        console.log(`Deployed material construction verified: ${task}, available=${metadata.available}, routes=${metadata.routes.length}, routesViewed=${routeChecks.length}, events=${metadata.indexedEvents}, selected=${selection?.ordinal??"not recorded"}`);
    }else{
    const board=(x,mirrored=false)=>({stockID:"s",stockInstance:0,width:10,height:1000000,placements:[{partID:"p",instance:0,pose:{x,y:1,angle:mirrored?90:0,mirrored}}]});
    const event=(ordinal,route,stage,source,layouts=[],reason="",rank=[])=>({ordinal,event:{route,scope:"original",stage,source,stateID:ordinal,layouts,reason,rank,resultStatus:0,members:[],geometries:[]}});
    let events=[event(1,"r1","begin","strategy.begin"),event(2,"r1","commit","native.commit",[board(1)]),
        event(3,"r1","complete","scheduler.route-result",[board(1)],"route r1",[{name:"unplaced",value:0},{name:"boards",value:1}]),
        event(4,"r2","begin","strategy.begin"),event(5,"r2","complete","scheduler.route-result",[board(3,true)],"route r2",[{name:"unplaced",value:0},{name:"boards",value:1}]),
        event(6,"plane.initial","selected","scheduler.final-selection",[board(1)],"selected=r1",[{name:"unplaced",value:0},{name:"boards",value:1}])];
    events[5].event.measures=[{name:"timeLimitSeconds",value:1e100},{name:"hugeNegative",value:-1e100},{name:"ordinaryDecimal",value:1234.25}];
    let available=true,truncated=false,generation=1,interruption=null,nativeCompleted=true,defaultTask="material::task-1",constructionMode=false;
    let waitMetadata=false,releaseMetadata,metadataStarted;
    const brief=value=>({...value.event,ordinal:value.ordinal,boards:value.event.layouts.length,placed:value.event.layouts.reduce((sum,b)=>sum+b.placements.length,0)});
    function metadata(material=true){
        if(material&&!available)return {available:false,materialTask:"task-1",binding:{reason:"本次任务尚未生成原生过程记录。"},index:{},routes:[],scopes:[],stages:{},indexedEvents:0,decisions:[]};
        return {available:true,interruption,runInfo:material?{version:"recorded-v52",metadataStatus:"bound",executableSha256:"2".repeat(64),timeSource:"run-started",recordedAt:"2026-11-11T13:00:31+08:00"}:{metadataStatus:"missing",timeSource:"index-modified",recordedAt:"2026-11-06T00:00:00Z"},binding:{indexPath:`catalog/raw/run-${generation}.trace-index.json`},index:{schemaVersion:2,format:"native-construction-jsonl",runCompleted:nativeCompleted&&!waitMetadata&&!interruption,truncated,
            eventsWritten:events.length,eventsOmitted:truncated?13:0,detailEventsOmitted:truncated?10:0,progressEventsOmitted:truncated?2:0,terminalSnapshotsTruncated:truncated?1:0,
            performanceEventsSeen:2,performanceEventsWritten:truncated?1:2,performanceEventsOmitted:truncated?1:0,performanceObservationTruncated:truncated,
            conditions:{timeLimitSeconds:1e100,attemptCap:8000000},problem:{edgeMargin:material ? .5 : 0,partGap:material ? .2 : 0,parts:[{id:"p",quantity:constructionMode?2:1,freeRotation:material,allowedAngles:[0],allowMirror:material?true:undefined,geometry:{outer:[[0,0],[2,0],[0,3]],holes:[[[.2,.2],[.4,.2],[.2,.4]]]}}],stocks:[{id:"s",width:10,height:1000000}]}},
            selectedRoute:constructionMode?"r1":undefined,
            routes:["r1","r2","plane.initial",...Array.from({length:24},(_,i)=>`extra-${i}`)].map(id=>({id,events:events.filter(e=>e.event.route===id).length,scopes:["original"],beginObserved:true,
                terminal:events.filter(e=>e.event.route===id&&e.event.source.startsWith("scheduler.")).map(brief).at(-1),terminalSnapshotTruncated:truncated&&id==="r2",eventsOmitted:truncated&&id==="r2"?12:0,
                summary:constructionMode?{required:2,placed:id==="r1"?2:1,selected:id==="r1",status:id==="r1"?"complete":"partial",routeElapsedSeconds:.25,metrics:{stockUtilization:.1,usedSectionUtilization:.6}}:undefined,
                construction:constructionMode?{status:id==="r1"?"complete":"unavailable",reason:id==="r1"?"真实原生父链已核对":"本路线提交记录缺失，不补造历史",branches:id==="r1"?[{id:"sibling",scope:"search-child",stockID:"s",stockInstance:0,steps:2,status:"complete"}]:[]}:undefined})),
            scopes:["original"],stages:{begin:2,commit:1,complete:2,selected:1},indexedEvents:events.length,decisions:events.filter(e=>e.event.source.startsWith("scheduler.")).map(brief),decisionCount:events.filter(e=>e.event.source.startsWith("scheduler.")).length,audits:[]};
    }
    await page.route("http://construction.test/**",async route=>{
        const url=new URL(route.request().url());
        if(url.pathname==="/api/construction/tasks")return route.fulfill({json:{tasks:[{id:"legacy"},{id:"material::task-1",label:"本轮素材 · Current",materialTask:"task-1"}],defaultTaskId:defaultTask,audits:[]}});
        if(url.pathname.startsWith("/api/construction/")){
            const pieces=url.pathname.split("/").filter(Boolean);
            const material=pieces[2]==="materials";
            const remainder=pieces.slice(material?4:3);
            if(!remainder.length){if(waitMetadata&&material){metadataStarted?.();await new Promise(resolve=>releaseMetadata=resolve);waitMetadata=false;}return route.fulfill({json:metadata(material)});}
            if(remainder[0]==="events"&&remainder.length===1){
                const pathOrdinals=url.searchParams.get("path")==="sibling"?[101,104]:[101,102,106];
                const filtered=url.searchParams.get("view")==="construction"?events.filter(value=>url.searchParams.get("route")==="r1"&&pathOrdinals.includes(value.ordinal)):
                    events.filter(value=>["route","scope","stage"].every(key=>!url.searchParams.has(key)||value.event[key]===url.searchParams.get(key)));
                const ordinal=Number(url.searchParams.get("ordinal"));const focus=ordinal?filtered.findIndex(value=>value.ordinal===ordinal):-1;
                const offset=focus>=0?Math.floor(focus/25)*25:Number(url.searchParams.get("offset")||0);
                return route.fulfill({json:{total:filtered.length,offset,limit:25,focusOffset:focus>=0?focus:null,nextOffset:null,events:filtered.slice(offset,offset+25).map(brief)}});
            }
            if(remainder[0]==="events"&&remainder.length===2){const value=events.find(e=>e.ordinal===Number(remainder[1]));return route.fulfill({json:{...value,snapshot:value.event.layouts.length?{ordinal:value.ordinal,mode:"event",layouts:value.event.layouts}:null}});}
        }
        const filename=path.basename(url.pathname);
        if(["construction.html","construction.js","construction.css","layoutGeometry.js"].includes(filename))return route.fulfill({body:await fs.readFile(path.join(here,filename)),contentType:filename.endsWith(".js")?"text/javascript":filename.endsWith(".css")?"text/css":"text/html"});
        return route.fulfill({status:404,body:"Not found"});
    });
    await page.goto("http://construction.test/construction.html");
    await page.waitForFunction(()=>document.getElementById("task").value==="material::task-1"&&document.getElementById("event-reason").textContent.includes("selected=r1"));
    assert.match(await page.locator("#run-summary").textContent(),/recorded-v52.*运行开始.*13:00:31（北京时间）.*元数据已绑定/);
    assert.match(await page.locator("#problem-summary").textContent(),/边距：0\.5 mm.*零件间距：0\.2 mm.*自由旋转许可.*允许 1 型/);
    assert.match(await page.locator("#completion-summary").textContent(),/最终选择：1 \/ 1 件 · 已排齐/);
    await page.goto("http://construction.test/construction.html?trace=legacy");
    await page.waitForFunction(()=>document.getElementById("task").value==="legacy"&&document.getElementById("event-reason").textContent.includes("selected=r1"));
    assert.match(await page.locator("#run-summary").textContent(),/运行版本：未记录.*时间来源：索引修改/);
    assert.match(await page.locator("#problem-summary").textContent(),/边距：0 mm.*零件间距：0 mm.*角度：0°.*镜像：允许 0 型 \/ 禁止 0 型 \/ 未记录 1 型/);
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("connection").textContent==="记录已读取");
    assert.equal(await page.locator("#task").inputValue(),"legacy");
    assert.equal(new URL(page.url()).searchParams.get("trace"),"legacy");
    await page.goto("http://construction.test/construction.html?materialTask=task-1");
    await page.waitForFunction(()=>document.getElementById("event-reason").textContent.includes("selected=r1"));
    assert.equal(await page.locator("#task").inputValue(),"material::task-1");
    assert.match(await page.locator("#decisions").textContent(),/selected=r1.*unplaced=0 → boards=1/);
    assert.match(await page.locator("#ranks").textContent(),/unplaced0boards1/);
    assert.match(await page.locator("#task-summary").textContent(),/上限 1E100 秒/);
    assert.doesNotMatch(await page.locator("#task-summary").textContent(),/无限|不限时/);
    const displayedMeasures=await page.locator("#measures .measure").evaluateAll(rows=>Object.fromEntries(rows.map(row=>[row.querySelector("span").textContent,row.querySelector("b").textContent])));
    assert.equal(displayedMeasures.timeLimitSeconds,"1E100");
    assert.equal(displayedMeasures.hugeNegative,"-1E100");
    assert.equal(displayedMeasures.ordinaryDecimal,"1,234.25");
    assert.equal(JSON.parse(await page.locator("#conditions").textContent()).conditions.timeLimitSeconds,1e100);
    assert.match(await page.locator("#health").textContent(),/构造性能累计：已记录 2 \/ 2 条，缺失 0 条/);
    assert.equal(JSON.parse(await page.locator("#event-json").textContent()).event.measures.find(row=>row.name==="timeLimitSeconds").value,1e100);
    await page.locator(".route").filter({has:page.locator(".route-name",{hasText:/^r2$/})}).locator(".route-end").click();
    await page.waitForFunction(()=>document.getElementById("event-reason").textContent.includes("route r2"));
    const r2Drawing=await page.locator("#boards .original").first().getAttribute("d");
    assert.match(r2Drawing,/M3 1/); // This route's layout, never the selected route's final layout.
    assert.equal(await page.locator("#boards .original").first().getAttribute("data-mirrored"),"true");
    const coordinates=[...r2Drawing.matchAll(/[ML]([^MLZ]+)/g)].map(match=>match[1].trim().split(/\s+/).map(Number));
    const expected=[[3,1],[3,3],[6,1],[3.2,1.2],[3.2,1.4],[3.4,1.2]];
    assert.equal(coordinates.length,expected.length);
    coordinates.forEach((point,index)=>point.forEach((value,axis)=>assert.ok(Math.abs(value-expected[index][axis])<1e-12)));
    const reflectedBounds=await page.evaluate(()=>PlaneLayoutGeometry.geometryBounds([{geometry:{outer:[[0,0],[2,0],[0,3]]},pose:{x:3,y:1,angle:90,mirrored:true}}]));
    for(const [name,value] of Object.entries({minX:3,minY:1,maxX:6,maxY:3}))assert.ok(Math.abs(reflectedBounds[name]-value)<1e-12);
    assert.ok(await page.locator("#boards svg").first().evaluate(svg=>svg.viewBox.baseVal.height)<10);
    assert.match(await page.locator("#boards .hint").textContent(),/坐标原点仍为 \(0, 0\).*显示裁切不代表占用段率或排样改善/);
    await page.selectOption("#construction-view-mode","full");
    assert.equal(await page.locator("#boards svg").first().evaluate(svg=>svg.viewBox.baseVal.height),1000000);
    assert.equal(await page.locator("#boards .original").first().getAttribute("d"),r2Drawing);
    assert.equal(await page.locator("#boards .original").first().getAttribute("vector-effect"),"non-scaling-stroke");
    await page.selectOption("#construction-view-mode","used");
    await page.locator("#all-routes").click();
    await page.waitForFunction(()=>document.getElementById("event-title").textContent.includes("strategy.begin"));
    assert.equal(await page.locator("#boards svg").count(),0);
    assert.match(await page.locator("#snapshot-status").textContent(),/无可精确关联的状态快照/);
    await page.locator(".route").filter({has:page.locator(".route-name",{hasText:/^r2$/})}).locator(".route-end").click();
    await page.waitForFunction(()=>document.getElementById("event-reason").textContent.includes("route r2"));
    const arrived=new Promise(resolve=>metadataStarted=resolve);waitMetadata=true;
    await page.evaluate(()=>document.getElementById("refresh").click());await arrived;
    const saved=await page.evaluate(()=>{const input=document.getElementById("overlay");input.focus();window.keptOverlay=input;window.keptSvg=document.querySelector("#boards svg");window.keptEvent=document.querySelector("#events .event-row");document.querySelector(".routes-panel").scrollTop=190;window.scrollTo(0,230);return {routes:document.querySelector(".routes-panel").scrollTop,y:window.scrollY};});
    events.push(event(7,"r2","score","native.score",[],"new actual observation",[{name:"boards",value:1}]));
    releaseMetadata();
    await page.waitForFunction(()=>document.getElementById("event-total").textContent==="3");
    const after=await page.evaluate(()=>({focus:document.activeElement===window.keptOverlay,svg:document.querySelector("#boards svg")===window.keptSvg,event:document.querySelector("#events .event-row")===window.keptEvent,routes:document.querySelector(".routes-panel").scrollTop,y:window.scrollY}));
    assert.equal(after.focus,true);assert.equal(after.svg,true);assert.equal(after.event,true);assert.equal(after.routes,saved.routes);assert.equal(after.y,saved.y);
    truncated=true;await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("health").textContent.includes("采样省略不代表算法失败"));
    assert.match(await page.locator("#health").textContent(),/细节 10 条、进展 2 条.*终局大快照省略 1 个/);
    assert.match(await page.locator("#health").textContent(),/构造性能累计：已记录 1 \/ 2 条，缺失 1 条/);
    const profileConditions=JSON.parse(await page.locator("#conditions").textContent());
    assert.equal(profileConditions.performanceEventsOmitted,1);
    assert.equal(profileConditions.performanceObservationTruncated,true);
    assert.match(await page.locator("#routes").textContent(),/终局布局过大，快照未完整记录/);
    generation++;await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("event-title").textContent.includes("strategy.begin"));
    assert.equal(await page.locator("#boards svg").count(),0); // A new invocation may reuse ordinals; never retain the previous invocation's SVG.
    available=false;await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("health").textContent.includes("本次任务尚未生成"));
    assert.equal(await page.locator("#boards svg").count(),0);assert.equal(await page.locator("#events .event-row").count(),0);
    await page.selectOption("#task","legacy");
    await page.waitForFunction(()=>document.getElementById("event-reason").textContent.includes("selected=r1"));
    assert.equal(await page.locator("#task").inputValue(),"legacy");
    assert.equal(new URL(page.url()).searchParams.get("trace"),"legacy");
    await page.reload();
    await page.waitForFunction(()=>document.getElementById("event-reason").textContent.includes("selected=r1"));
    assert.equal(await page.locator("#task").inputValue(),"legacy");
    events=[event(1,"r1","begin","strategy.begin")];generation++;nativeCompleted=false;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("connection").textContent==="记录未完成");
    assert.match(await page.locator("#task-summary").textContent(),/尚无完整终局记录/);
    assert.doesNotMatch(await page.locator("#task-summary").textContent(),/采集中|已中止/);
    const unfinishedConditions=JSON.parse(await page.locator("#conditions").textContent());
    assert.equal(unfinishedConditions.runCompleted,false);
    assert.equal(unfinishedConditions.interruption,null);
    events.push(event(2,"r1","score","native.score",[],"new observation without a process-status claim"));
    await page.waitForFunction(()=>document.getElementById("event-total").textContent==="2");
    assert.equal(await page.locator("#connection").textContent(),"记录未完成");
    assert.equal(await page.locator("#decisions button").count(),0);
    generation++;
    interruption={diagnosticOnly:true,evidenceFile:"legacy.interrupted.json",reason:"Owned diagnostic stopped for an equivalent fix",stoppedAt:"2026-11-09T21:36:38+08:00",completeResultProduced:false};
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("connection").textContent==="已中止，未生成完整结果");
    assert.match(await page.locator("#task-summary").textContent(),/已中止，未生成完整结果/);
    assert.doesNotMatch(await page.locator("#task-summary").textContent(),/采集中/);
    assert.match(await page.locator("#health").textContent(),/Owned diagnostic stopped for an equivalent fix.*不作为完整排样成绩/);
    assert.equal(await page.locator("#boards svg").count(),0);
    assert.equal(await page.locator("#decisions button").count(),0);
    assert.equal(await page.locator("#routes .route-end:visible").count(),0);
    assert.equal(await page.locator("#result-link").isHidden(),true);
    const interruptedConditions=JSON.parse(await page.locator("#conditions").textContent());
    assert.equal(interruptedConditions.runCompleted,false);
    assert.equal(interruptedConditions.interruption.evidenceFile,"legacy.interrupted.json");
    interruption=null;generation++;nativeCompleted=true;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("connection").textContent==="记录已读取");
    assert.doesNotMatch(await page.locator("#task-summary").textContent(),/已中止/);
    const phases=[["main-combination-array","主组合阵列"],["gap-combination-array","空隙组合阵列"],
        ["gap-single-nfp","单件 NFP 补空"],["next-blank-area","下一块空白区域"]];
    events=phases.flatMap(([reason],index)=>["begin","complete"].map((stage,offset)=>{
        const observation=event(index*2+offset+1,"r1",stage,"construction.phase",[],reason);
        // Older brief records may only contain the named reason.
        if(index||offset)observation.event.measures=[{name:"constructionPhase",value:index+1}];
        return observation;
    }));
    const committed=event(9,"r1","commit","fill.commit",[board(1)],"original-instances-committed");
    committed.event.measures=[{name:"constructionPhase",value:2},{name:"unitMembers",value:2},{name:"arrayCells",value:3}];
    const fallback=event(10,"r1","commit","constructor.commit",[board(1)],"original-instances-committed");
    fallback.event.measures=[{name:"constructionPhase",value:5}];
    const unknown=event(11,"r1","begin","construction.phase",[],"main-combination-array");
    unknown.event.measures=[{name:"constructionPhase",value:99}];
    const missing=event(12,"r1","begin","construction.phase",[],"unrecognized-phase");
    const mixed=event(13,"r1","commit","fill.commit",[board(1)],"original-instances-committed");
    mixed.event.measures=[{name:"constructionPhase",value:2},{name:"unitMembers",value:0}];
    const localMeasures=[{name:"largestFreeArea",value:28.28},{name:"largeRegularFreeSpace",value:25.75},
        {name:"fragmentFreeArea",value:0},{name:"freeBoundaryPerimeter",value:22.3},{name:"attachment",value:.5},
        {name:"freeArea",value:28.28},{name:"freeComponents",value:1},{name:"spaceEvaluationSeconds",value:.00125},
        {name:"spaceCandidatesEvaluated",value:8},{name:"spaceCircleProxy",value:0},{name:"futureSpaceMetric",value:.125}];
    const localRank=[{name:"negativeLargeRegularFreeSpace",value:-25.75},{name:"fragmentFreeArea",value:0},
        {name:"freeBoundaryPerimeter",value:22.3},{name:"negativeAttachment",value:-.5}];
    const localScore=event(14,"r1","score","placement.space-rank",[],"certified-current-space",localRank);
    localScore.event.measures=localMeasures;
    const localCommit=event(15,"r1","commit","fill.commit",[board(1)],"original-instances-committed",localRank);
    localCommit.event.measures=[{name:"constructionPhase",value:3},...localMeasures];
    events.push(committed,fallback,unknown,missing,mixed,localScore,localCommit);generation++;
    await page.evaluate(()=>document.getElementById("refresh").click());
    await page.waitForFunction(()=>document.getElementById("event-total").textContent==="15");
    const selectObservation=async ordinal=>{
        await page.locator(`#events .event-row[data-ordinal="${ordinal}"]`).click();
        await page.waitForFunction(value=>document.getElementById("event-id").textContent.startsWith(`#${value} ·`),ordinal);
    };
    const measureRows=()=>page.locator("#measures .measure").evaluateAll(rows=>Object.fromEntries(rows.map(row=>[row.querySelector("span").textContent,row.querySelector("b").textContent])));
    for(const [index,[reason,name]] of phases.entries())for(const offset of [0,1]){
        const ordinal=index*2+offset+1;
        assert.match(await page.locator(`#events .event-row[data-ordinal="${ordinal}"]`).textContent(),new RegExp(name));
        await selectObservation(ordinal);
        assert.match(await page.locator("#event-title").textContent(),new RegExp(`构造阶段 · ${name}`));
        assert.match(await page.locator("#event-reason").textContent(),new RegExp(name));
        const raw=JSON.parse(await page.locator("#event-json").textContent());
        assert.equal(raw.event.reason,reason);
        assert.deepEqual(raw.event.measures,events[ordinal-1].event.measures);
        if(index||offset)assert.equal((await measureRows())["构造阶段（constructionPhase）"],`${name}（${index+1}）`);
    }
    await selectObservation(9);
    assert.match(await page.locator("#event-title").textContent(),/提交 · fill.commit · 空隙组合阵列/);
    assert.match(await page.locator("#event-reason").textContent(),/原件实例已提交 · 构造阶段：空隙组合阵列 · 单元原件数：2 · 阵列单元数：3/);
    const commitMeasures=await measureRows();
    assert.equal(commitMeasures["构造阶段（constructionPhase）"],"空隙组合阵列（2）");
    assert.equal(commitMeasures["单元原件数（unitMembers）"],"2");
    assert.equal(commitMeasures["阵列单元数（arrayCells）"],"3");
    assert.deepEqual(JSON.parse(await page.locator("#event-json").textContent()).event,committed.event);
    await selectObservation(10);
    assert.match(await page.locator("#event-title").textContent(),/原件阵列回退/);
    assert.equal((await measureRows())["构造阶段（constructionPhase）"],"原件阵列回退（5）");
    assert.equal((await measureRows())["单元原件数（unitMembers）"],undefined);
    assert.equal((await measureRows())["阵列单元数（arrayCells）"],undefined);
    assert.doesNotMatch(await page.locator("#event-reason").textContent(),/单元原件数|阵列单元数/);
    await selectObservation(11);
    assert.match(await page.locator("#event-title").textContent(),/未知构造阶段（99）/);
    assert.equal((await measureRows())["构造阶段（constructionPhase）"],"未知构造阶段（99）");
    await selectObservation(12);
    assert.match(await page.locator("#event-title").textContent(),/未记录构造阶段/);
    assert.equal((await measureRows())["构造阶段（constructionPhase）"],undefined);
    await selectObservation(13);
    assert.equal((await measureRows())["单元原件数（unitMembers）"],"0");
    assert.equal((await measureRows())["阵列单元数（arrayCells）"],undefined);
    assert.doesNotMatch(await page.locator("#event-reason").textContent(),/阵列单元数/);
    const localValues={largestFreeArea:"28.28",largeRegularFreeSpace:"25.75",fragmentFreeArea:"0",freeBoundaryPerimeter:"22.3",
        attachment:"0.5",freeArea:"28.28",freeComponents:"1",spaceEvaluationSeconds:"0.00125",spaceCandidatesEvaluated:"8",spaceCircleProxy:"0"};
    for(const observation of [localScore,localCommit]){
        await selectObservation(observation.ordinal);
        if(observation===localScore){
            assert.match(await page.locator("#event-title").textContent(),/^评分 · 当前空间评价$/);
            assert.match(await page.locator(`#events .event-row[data-ordinal="${observation.ordinal}"]`).textContent(),/当前空间评价/);
        }
        const displayed=await measureRows();
        for(const [key,value] of Object.entries(localValues))assert.equal(displayed[localSpaceLabels[key]],value);
        assert.equal(displayed.futureSpaceMetric,"0.125");
        assert.deepEqual(await page.locator("#ranks .measure span").allTextContents(),localRank.map(row=>localSpaceLabels[row.name]));
        assert.deepEqual(await page.locator("#ranks .measure b").allTextContents(),["-25.75","0","22.3","-0.5"]);
        assert.deepEqual(JSON.parse(await page.locator("#event-json").textContent()).event,observation.event);
    }
    constructionMode=true;available=true;truncated=false;generation++;
    const pathBoard=(x,extra=null)=>{const value=board(x);if(extra!==null)value.placements.push({partID:"p",instance:1,pose:{x:extra,y:1,angle:0,mirrored:false}});return value;};
    events=[event(101,"r1","selected","construction.path.commit",[pathBoard(1)]),
        event(102,"r1","selected","construction.path.commit",[pathBoard(1,4)]),
        event(104,"r1","commit","constructor.commit",[pathBoard(1,7)],"independent sibling"),
        event(106,"r1","complete","scheduler.route-result",[pathBoard(1,4)]),
        event(107,"r1","selected","scheduler.final-selection",[pathBoard(1,4)]),
        event(110,"r2","complete","scheduler.route-result",[pathBoard(2)])];
    await page.goto("http://construction.test/construction.html?materialTask=task-1");
    await page.waitForFunction(()=>document.getElementById("event-id").textContent.startsWith("#101 ·"));
    assert.equal(await page.locator("#event-view").inputValue(),"construction");
    assert.equal(await page.locator("#scope").isDisabled(),true);
    assert.equal(await page.locator("#event-total").textContent(),"3");
    assert.match(await page.locator("#routes").textContent(),/最终选中.*已排 2 \/ 2 件.*整板 10% · 占用段 60%.*路线耗时 0\.25 秒/s);
    await page.locator("#next").click();
    await page.waitForFunction(()=>document.getElementById("event-id").textContent.startsWith("#102 ·"));
    assert.equal(await page.locator("#boards .original").count(),2);
    assert.equal(await page.locator('#events [data-ordinal="104"]').count(),0);
    await page.selectOption("#construction-path","sibling");
    await page.waitForFunction(()=>document.getElementById("event-id").textContent.startsWith("#101 ·")&&document.getElementById("event-total").textContent==="2");
    await page.locator("#next").click();
    await page.waitForFunction(()=>document.getElementById("event-id").textContent.startsWith("#104 ·"));
    assert.match(await page.locator("#construction-path-status").textContent(),/独立真实分支/);
    const refreshArrived=new Promise(resolve=>metadataStarted=resolve);waitMetadata=true;
    await page.locator("#refresh").click();await refreshArrived;
    const retained=await page.evaluate(()=>{const overlay=document.getElementById("overlay");overlay.focus();window.pathKeptOverlay=overlay;window.pathKeptSvg=document.querySelector("#boards svg");document.querySelector(".routes-panel").scrollTop=190;return document.querySelector(".routes-panel").scrollTop;});
    releaseMetadata();
    await page.waitForFunction(()=>document.getElementById("connection").textContent==="记录已读取");
    assert.equal(await page.locator("#construction-path").inputValue(),"sibling");
    assert.equal(await page.evaluate(()=>document.activeElement===window.pathKeptOverlay&&document.querySelector("#boards svg")===window.pathKeptSvg),true);
    assert.equal(await page.locator(".routes-panel").evaluate(node=>node.scrollTop),retained);
    await page.selectOption("#event-view","events");
    await page.waitForFunction(()=>document.getElementById("event-total").textContent==="5");
    assert.equal(await page.locator('#events [data-ordinal="104"]').count(),1);
    await page.selectOption("#event-view","construction");
    await page.locator(".route").filter({has:page.locator(".route-name",{hasText:/^r2$/})}).click();
    await page.waitForFunction(()=>document.getElementById("event-total").textContent==="0");
    assert.equal(await page.locator("#boards svg").count(),0);
    assert.match(await page.locator("#construction-path-status").textContent(),/提交记录缺失/);
    // A two-layout native mapping is one actual promotion/copy step. Render
    // its recorded target; retain both source snapshots in the original JSON.
    const copy=event(121,"r1","commit","sheet-template.copy",[pathBoard(1),pathBoard(1)]);
    copy.event.layouts[1].stockInstance=1;
    copy.event.measures=[{name:"sourceLayoutIndex",value:0},{name:"targetLayoutIndex",value:1}];
    events=[copy];generation++;
    await page.goto("http://construction.test/construction.html?materialTask=task-1&view=events");
    await page.waitForFunction(()=>document.getElementById("event-id").textContent.startsWith("#121 ·"));
    assert.equal(await page.locator("#boards svg").count(),1);
    assert.match(await page.locator("#boards .board-title").textContent(),/第2板/);
    assert.equal(JSON.parse(await page.locator("#event-json").textContent()).event.layouts.length,2);
    console.log("Construction display: real selected parent-chain paging, separate search branches, route utilization/time/selection, whole-board mapping, no fabricated missing history, preserved focus/scroll/canvas, legacy raw replay and score semantics passed");
    }
}finally{await browser.close();}
