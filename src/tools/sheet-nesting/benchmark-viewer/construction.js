(() => {
"use strict";
// Read-only comparison helpers also run without a DOM for data-contract checks.
function comparisonStable(value){
    if(Array.isArray(value))return "["+value.map(comparisonStable).join(",")+"]";
    if(value&&typeof value==="object")return "{"+Object.keys(value).sort().map(key=>JSON.stringify(key)+":"+comparisonStable(value[key])).join(",")+"}";
    return JSON.stringify(value);
}
function comparisonPairs(tasks,group,before,after){
    const pairs=new Map();
    for(const task of tasks){
        if(task.materialTask||typeof task.id!=="string")continue;
        for(const [side,version] of [["before",before],["after",after]]){
            const suffix="-"+group+"-"+version;
            if(!task.id.endsWith(suffix)||task.id.length===suffix.length)continue;
            const id=task.id.slice(0,-suffix.length);
            if(!pairs.has(id))pairs.set(id,{id});
            pairs.get(id)[side]=task;
        }
    }
    return [...pairs.values()].sort((a,b)=>a.id.localeCompare(b.id,"zh-CN",{numeric:true}));
}
function comparisonArea(geometry){
    if(!geometry)return NaN;
    if(geometry.radius>0)return Math.PI*geometry.radius*geometry.radius;
    const area=ring=>{
        if(!Array.isArray(ring)||ring.length<3)return NaN;
        const xy=p=>Array.isArray(p)?p:[p.x??p.X,p.y??p.Y],origin=xy(ring[0]);let sum=0;
        for(let i=1;i+1<ring.length;i++){
            const a=xy(ring[i]),b=xy(ring[i+1]);
            sum+=(a[0]-origin[0])*(b[1]-origin[1])-(a[1]-origin[1])*(b[0]-origin[0]);
        }
        return Math.abs(sum)*.5;
    };
    const rings=geometry.rings||[geometry.outer,...(geometry.holes||[])];
    return area(rings[0])-rings.slice(1).reduce((sum,ring)=>sum+area(ring),0);
}
function comparisonTop(geometry,placed){
    const pose=placed.transform||placed.pose||placed;
    const x=pose.x,y=pose.y,angle=pose.angle??pose.angleDegrees??0;
    if(!geometry||![x,y,angle].every(Number.isFinite))return NaN;
    const normalized=((angle%360)+360)%360,radians=normalized*Math.PI/180;
    const worldY=point=>{
        const px=Array.isArray(point)?point[0]:point.x??point.X;
        const sourceY=Array.isArray(point)?point[1]:point.y??point.Y,py=pose.mirrored===true?-sourceY:sourceY;
        if(normalized===0)return y+py;if(normalized===90)return y+px;
        if(normalized===180)return y-py;if(normalized===270)return y-px;
        return y+px*Math.sin(radians)+py*Math.cos(radians);
    };
    if(geometry.radius>0)return worldY(geometry.center||[0,0])+geometry.radius;
    const outer=geometry.outer||geometry.rings?.[0];
    return outer?.length?Math.max(...outer.map(worldY)):NaN;
}
function comparisonMetrics(problem,result){
    if(!problem?.parts?.length||!Array.isArray(result?.layouts))throw new Error("缺少原件或终局布局");
    const parts=new Map(problem.parts.map(part=>[part.id,part]));
    const required=problem.parts.reduce((sum,part)=>sum+part.quantity,0),margin=problem.edgeMargin;
    if(!Number.isFinite(required)||!Number.isFinite(margin))throw new Error("需求或边距字段不完整");
    const lengths=[];let material=0,sheetArea=0,sectionArea=0,placed=0;
    for(const board of result.layouts){
        const stockID=board.stock??board.stockID;
        const stock=problem.stocks?.find(value=>value.id===stockID);
        if(!stock||stock.width!==board.width||stock.height!==board.height||!(board.width>0&&board.height>0))throw new Error("终局原板尺寸与输入不一致");
        let top=-Infinity;
        for(const placement of board.placements||[]){
            const part=parts.get(placement.part??placement.partID??placement.partId);
            const area=comparisonArea(part?.geometry),high=comparisonTop(part?.geometry,placement);
            if(!(area>0)||!Number.isFinite(high))throw new Error("无法从原轮廓计算净面积和最终高度");
            material+=area;top=Math.max(top,high);++placed;
        }
        if(!Number.isFinite(top))throw new Error("终局包含空板");
        const length=Math.min(board.height,Math.max(0,top+margin));
        lengths.push(length);sheetArea+=board.width*board.height;sectionArea+=board.width*length;
    }
    if(result.placed!=null&&result.placed!==placed)throw new Error("原生件数与终局布局件数不一致");
    return {required,placed,boards:result.layouts.length,material,sheetArea,sectionArea,lengths,
        length: lengths.reduce((sum,value)=>sum+value,0),stockUtilization:sheetArea>0?material/sheetArea:0,
        sectionUtilization:sectionArea>0?material/sectionArea:0};
}
function comparisonBinding(run){
    const raw=run.raw||{},index=run.metadata?.index||{},settings=raw.settings||{};
    return {route:settings.isolatedConstructor||raw.strategy||null,settings,
        hash:index.inputSha256||raw.inputSha256||run.metadata?.binding?.inputSha256||null,
        problem:raw.problem,indexProblem:index.problem};
}
function comparisonMatch(before,after){
    if(!before?.finished||!after?.finished)return {ok:false,note:"待双方结束后核对"};
    const a=comparisonBinding(before),b=comparisonBinding(after);
    if(!a.problem||!b.problem||!a.indexProblem||!b.indexProblem)return {ok:false,note:"缺少输入约束记录"};
    if(comparisonStable(a.problem)!==comparisonStable(a.indexProblem)||comparisonStable(b.problem)!==comparisonStable(b.indexProblem))
        return {ok:false,note:"结果与过程索引的 problem 不一致"};
    if(comparisonStable(a.problem)!==comparisonStable(b.problem))return {ok:false,note:"problem 不一致，禁止比较"};
    if(a.route!=="plane.order-array"||b.route!==a.route)return {ok:false,note:"初构路由不一致或不是 plane.order-array"};
    for(const run of [before,after]){
        const routes=run.metadata?.routes||run.metadata?.index?.routes||[];
        if(!routes.some(route=>route.id===a.route))return {ok:false,note:"过程索引没有记录该初构路由"};
        const conditions=run.metadata.index.conditions||{},settings=run.raw.settings;
        if(conditions.timeLimitSeconds!=null&&conditions.timeLimitSeconds!==settings.timeLimitSeconds||
            conditions.attemptCap!=null&&conditions.attemptCap!==settings.maxPlacementAttempts)
            return {ok:false,note:"结果与过程索引的预算不一致"};
    }
    // Compare all recorded solver settings, including every budget and permission.
    if(comparisonStable(a.settings)!==comparisonStable(b.settings))return {ok:false,note:"执行条件或预算不一致"};
    if(!Number.isFinite(a.settings.timeLimitSeconds)||!Number.isFinite(a.settings.maxPlacementAttempts)||!Number.isFinite(a.settings.workers))
        return {ok:false,note:"缺少时间、尝试或 worker 条件"};
    if(a.hash&&b.hash&&a.hash!==b.hash)return {ok:false,note:"记录的输入 SHA256 不一致"};
    return {ok:true,note:"problem 逐字段一致；路由 / 预算一致",hashNote:a.hash&&b.hash?
        "记录 SHA256 一致；页面未读取源输入文件核验":"输入文件 SHA256 未由页面核验"};
}
function comparisonDelta(before,after,match){
    if(!match.ok||!before?.eligible||!after?.eligible||!before.metrics||!after.metrics)return null;
    const stock=after.metrics.stockUtilization-before.metrics.stockUtilization;
    const section=after.metrics.sectionUtilization-before.metrics.sectionUtilization;
    const primary=Math.abs(stock)>1e-10?stock:Math.abs(section)>1e-10?section:0;
    return {quality:primary>0?"改善":primary<0?"退步":"持平",stock,section,
        seconds:Number.isFinite(before.seconds)&&Number.isFinite(after.seconds)?after.seconds-before.seconds:null};
}
function comparisonTimes(raw,terminal){
    const recorded=value=>typeof value==="number"&&Number.isFinite(value)&&value>=0;
    const diagnostics=raw?.result?.diagnostics||[];
    const diagnostic=diagnostics.map(value=>String(value).match(/(?:^|\s)first-complete-wall-s=([+\-\d.eE]+)/)).find(Boolean);
    const first=recorded(raw?.firstCompleteWallSeconds)?raw.firstCompleteWallSeconds:
        diagnostic&&recorded(Number(diagnostic[1]))?Number(diagnostic[1]):null;
    const terminalWall=String(terminal?.source||"").endsWith("final-selection")?
        terminal.measures?.find(value=>value.name==="portfolioWallSeconds")?.value:null;
    const seconds=recorded(raw?.wholeJobWallSeconds)?raw.wholeJobWallSeconds:recorded(terminalWall)?terminalWall:
        recorded(raw?.result?.stats?.elapsedSeconds)?raw.result.stats.elapsedSeconds:null;
    return {first,seconds,timeSource:recorded(raw?.wholeJobWallSeconds)?"原生整任务 wholeJobWallSeconds":
        recorded(terminalWall)?"原生总调度终局 portfolioWallSeconds":"原生 result.stats.elapsedSeconds（无整任务墙钟记录）"};
}
globalThis.PlaneConstructionComparison=Object.freeze({stable:comparisonStable,pairs:comparisonPairs,area:comparisonArea,
    top:comparisonTop,metrics:comparisonMetrics,match:comparisonMatch,delta:comparisonDelta,times:comparisonTimes});
if(typeof document==="undefined")return;
const $ = id => document.getElementById(id);
const NS = "http://www.w3.org/2000/svg";
const {placementPose,geometryBounds,displayViewport}=globalThis.PlaneLayoutGeometry;
const stages = {begin:"开始",analysis:"分析",match:"匹配",array:"阵列",candidate:"候选",expand:"展开",backtrack:"回溯",complete:"完成记录",selected:"选中",stopped:"停止",generate:"生成",filter:"过滤",score:"评分",commit:"提交",budget:"预算",dispatch:"调度"};
const state = {task:"",route:null,scope:null,stage:null,metadata:null,page:null,position:0,ordinal:null,detail:null,playing:false,request:0,tasks:[],routeNodes:new Map(),busy:false,viewMode:"used",eventView:new URLSearchParams(location.search).get("view")==="events"?"events":"construction",path:"selected"};
const requestedMaterial=new URLSearchParams(location.search).get("materialTask");
const requestedTrace=new URLSearchParams(location.search).get("trace");
const requestedStart=new URLSearchParams(location.search).get("start")==="1";
const comparisonQuery=new URLSearchParams(location.search);
const requestedComparison=comparisonQuery.get("compare");
let playTimer, seekTimer;
const number = value => typeof value === "number" && Number.isFinite(value) ? value.toLocaleString("zh-CN",{
    maximumFractionDigits:7,notation:Math.abs(value)>=1e12?"scientific":"standard"
}) : value == null ? "无记录" : String(value);
const constructionPhases={1:"主组合阵列",2:"空隙组合阵列",3:"单件 NFP 补空",4:"下一块空白区域",5:"原件阵列回退",
    10:"延后补空：主阵列",11:"即时补空：主阵列",12:"随机重启：主阵列",20:"逐件 NFP",21:"扫描网格放置",22:"锁宽条带放置",23:"轮廓单元／阵列竞争",24:"矩形阵列"};
const constructionReasons={"main-combination-array":"主组合阵列","gap-combination-array":"空隙组合阵列",
    "gap-single-nfp":"单件 NFP 补空","next-blank-area":"下一块空白区域",
    "all-main-arrays-before-gap-fill":"先完成主阵列，再补空",
    "layered-array":"分层组合阵列 → 本层组合补空 → 单件补空",
    "deferred-array":"主阵列连续构造 → 板末补空",
    "immediate-array":"每批主阵列 → 当前批次内补空",
    "random-array-restart":"候选抽选 → 构造完整方案 → 重启比较",
    "nfp-all-specifications":"全规格逐件 NFP 比较",
    "nfp-contact":"首可行规格 NFP → 靠接评分",
    "nfp-directional-contact":"首可行规格 NFP → 方向靠接评分",
    "nfp-strip":"首件锁定条带 → 条带内逐件 NFP",
    "scanline":"扫描格点 → 原件检查 → 提交",
    "contour-array":"单件／自组合／轮廓阵列竞争",
    "bounded-array-search":"贪心保底 → 有限分支 → 单板选择",
    "rectangular-array":"矩形阵列构造"};
const constructionMeasureNames={constructionPhase:"构造阶段（constructionPhase）",unitMembers:"单元原件数（unitMembers）",arrayCells:"阵列单元数（arrayCells）",
    largestFreeArea:"大块空余面积（largestFreeArea）",largeRegularFreeSpace:"大块空间规整评分（largeRegularFreeSpace）",
    fragmentFreeArea:"碎片空余面积（fragmentFreeArea）",freeBoundaryPerimeter:"空余边界总长（freeBoundaryPerimeter）",
    attachment:"贴合量（attachment）",freeArea:"空余面积（freeArea）",freeComponents:"空余区数量（freeComponents）",
    spaceEvaluationSeconds:"局部评价耗时（spaceEvaluationSeconds）",spaceCandidatesEvaluated:"局部评价候选数（spaceCandidatesEvaluated）",
    spaceCircleProxy:"圆形评分代理（spaceCircleProxy）",negativeLargeRegularFreeSpace:"负大块空间规整评分（negativeLargeRegularFreeSpace）",
    negativeAttachment:"负贴合量（negativeAttachment）"};
function constructionPhase(event){
    const recorded=event.measures?.find(row=>row.name==="constructionPhase");
    if(recorded)return constructionPhases[recorded.value]||"未知构造阶段（"+number(recorded.value)+"）";
    return constructionReasons[event.reason]||(event.source==="construction.phase"?"未记录构造阶段":null);
}
function eventReason(event){
    let reason=constructionReasons[event.reason]||event.reason||"无理由记录";
    if(event.stage==="commit"){
        if(reason==="original-instances-committed")reason="原件实例已提交";
        const phase=constructionPhase(event);if(phase)reason+=" · 构造阶段："+phase;
        for(const [key,name] of [["unitMembers","单元原件数"],["arrayCells","阵列单元数"]]){
            const recorded=event.measures?.find(row=>row.name===key);
            if(recorded)reason+=" · "+name+"："+number(recorded.value);
        }
    }
    return reason;
}
function constructionMeasures(rows){
    return rows.map(row=>constructionMeasureNames[row.name]?{...row,name:constructionMeasureNames[row.name],
        value:row.name==="constructionPhase"?(constructionPhases[row.value]||"未知构造阶段")+"（"+number(row.value)+"）":row.value}:row);
}
const txt = (id,value) => { $(id).textContent = value; };
const label = (stage) => stages[stage] || stage || "未命名阶段";
async function get(url) {
    const response = await fetch(url,{cache:"no-store"});
    if(!response.ok){const error=new Error("记录读取失败（"+response.status+"）");error.status=response.status;throw error;}
    return response.json();
}
function base(){const task=state.tasks.find(row=>row.id===state.task);return "/api/construction/"+(task?.materialTask?"materials/"+encodeURIComponent(task.materialTask):encodeURIComponent(state.task));}
function preserveInteraction(update){
    const active=document.activeElement;
    const selected=active instanceof HTMLInputElement&&typeof active.selectionStart==="number"?[active.selectionStart,active.selectionEnd,active.selectionDirection]:null;
    const positions=Array.from(document.querySelectorAll(".routes-panel,#events,#boards,.board-viewport,#decisions,pre,.comparison-scroll"),node=>({node,x:node.scrollLeft,y:node.scrollTop}));
    const x=window.scrollX,y=window.scrollY;update();
    if(active?.isConnected&&document.activeElement!==active)active.focus({preventScroll:true});
    if(selected&&active.isConnected)active.setSelectionRange(...selected);
    for(const item of positions)if(item.node.isConnected){item.node.scrollLeft=item.x;item.node.scrollTop=item.y;}
    if(window.scrollX!==x||window.scrollY!==y)window.scrollTo(x,y);
}
function filters(){
    const params = new URLSearchParams();
    if(activeEventView()==="construction"){
        params.set("view","construction");params.set("path",state.path);
        if(state.route!==null)params.set("route",state.route);
    }else for(const key of ["route","scope","stage"]) if(state[key] !== null) params.set(key,state[key]);
    return params;
}
function activeEventView(){return state.eventView==="construction"&&state.metadata?.routes?.some(route=>route.construction)?"construction":"events";}
function chooseConstructionRoute(){
    const routes=state.metadata?.routes||[];
    return state.metadata?.selectedRoute??routes.find(row=>row.summary?.status==="complete"&&row.construction?.status==="complete")?.id??
        routes.find(row=>row.summary?.status==="complete")?.id??routes.find(row=>row.construction?.branches?.length)?.id??null;
}
function pathControls(){
    const mode=activeEventView();$("event-view").value=mode;
    $("scope").disabled=$("stage").disabled=mode==="construction";
    $("construction-path-control").hidden=mode!=="construction";
    const route=state.metadata?.routes?.find(row=>row.id===(state.route??state.metadata.selectedRoute));
    const paths=route?.construction;
    options($("construction-path"),[["selected","终局对应路径"],...(paths?.branches||[]).map(branch=>[branch.id,
        branch.scope+" · "+branch.stockID+" 第 "+(branch.stockInstance+1)+" 板 · "+branch.steps+" 次原生提交"+(branch.status!=="complete"?"（记录有缺口）":"")])],"请选择真实构造链");
    $("construction-path").value=state.path;
    const branch=paths?.branches?.find(row=>row.id===state.path);
    const running=route?.summary?.status==="running";
    const pathReason=state.path==="selected"?(running?"本路线仍在运行，尚无全任务终局。可选择已记录的真实分支。":paths?.reason):branch?.reason||"查看这一独立真实分支；它不等同于最终获胜分支。";
    txt("construction-path-status",mode==="construction"?pathReason||(state.route===null?"请在左侧选择策略；正在等待可回放的真实构造记录。":"等待本路线的实际过程关联记录。"):
        "全部原生事件包含搜索支线、重启、候选及调度；可按运行分支和阶段筛选。");
}
function options(select, rows, placeholder) {
    const signature = JSON.stringify(rows);
    if(select.dataset.signature === signature) return;
    if(document.activeElement===select) return;
    const previous=select.value;
    select.replaceChildren(new Option(placeholder,""));
    for(const [value,text] of rows) select.add(new Option(text,value));
    select.value=[...select.options].some(option=>option.value===previous)?previous:"";
    select.dataset.signature=signature;
}
function pause(){state.playing=false;clearTimeout(playTimer);txt("play","播放");}
function fail(error){txt("connection","读取异常");txt("health",String(error.message||error));pause();}
function auditLinks(rows){
    const host=$("audits"), signature=JSON.stringify(rows);
    if(host.dataset.signature===signature)return;
    host.replaceChildren();
    for(const row of rows){const a=document.createElement("a");a.href=row.url;a.target="_blank";a.rel="noopener";a.textContent=row.title;host.append(a);}
    host.dataset.signature=signature;
}
async function tasksRefresh(initial=false){
    const material=state.tasks.find(row=>row.id===state.task)?.materialTask||requestedMaterial;
    const data=await get("/api/construction/tasks"+(material?"?materialTask="+encodeURIComponent(material):""));
    state.tasks=data.tasks||[];
    options($("task"),state.tasks.map(row=>[row.id,row.label||row.id]),state.tasks.length?"请选择案例":"暂无原生记录");
    auditLinks(data.audits||[]);
    if(initial && !state.task && requestedMaterial){
        const requested=state.tasks.find(row=>row.materialTask===requestedMaterial);
        if(requested){$("task").value=requested.id;await changeTask();}
        else{txt("connection","任务不存在");txt("health","当前素材目录没有该任务，未切换到其他任务的过程记录。");}
    }
    else if(initial && !state.task && requestedTrace){
        const requested=state.tasks.find(row=>row.id===requestedTrace&&!row.materialTask);
        if(requested){$("task").value=requested.id;await changeTask();}
        else{txt("connection","记录不存在");txt("health","所指定的过程记录不存在，未切换到其他记录。");}
    }
    else if(initial && !state.task && state.tasks.length){
        const selected=state.tasks.find(row=>row.id===data.defaultTaskId);
        if(selected){$("task").value=selected.id;await changeTask();}
        else{txt("connection","请选择记录");txt("health","没有可确定时间的记录，请手动选择；未按文件名猜测最新运行。");}
    }
    else if(state.task){
        const previous=state.metadata?.indexedEvents;
        await metadataRefresh();
        if(state.metadata?.available!==false&&(previous!==state.metadata?.indexedEvents||!state.page)){
            $("scope").disabled=$("stage").disabled=$("all-routes").disabled=false;
            await loadPage(state.position,state.ordinal,true);
        }
    }
    else {txt("connection","等待记录");txt("health","尚无原生构造事件。原生采集完成前不生成过程图。");}
}
function routeFilterValue(route){return route===""?"（未命名/总调度）":route;}
function renderRoutes(){
    const host=$("routes");
    const routes=state.metadata.routes||[];
    txt("route-count",routes.length+" 个记录路由");
    const present=new Set();
    for(const route of routes){
        present.add(route.id);
        let row=state.routeNodes.get(route.id);
        if(!row){
            row=document.createElement("div");row.className="route";row.tabIndex=0;row.setAttribute("role","button");
            const title=document.createElement("span");title.className="route-name";
            const info=document.createElement("span");info.className="route-info";
            const terminal=document.createElement("button");terminal.className="route-end";terminal.textContent="终局";
            row.append(title,info,terminal);
            const choose=()=>changeRoute(route.id);
            row.addEventListener("click",choose);row.addEventListener("keydown",event=>{if(event.target===row&&(event.key==="Enter"||event.key===" ")){event.preventDefault();choose();}});
            terminal.addEventListener("click",async event=>{event.stopPropagation();try{await changeRoute(route.id,row.dataset.terminal?Number(row.dataset.terminal):null);}catch(error){fail(error);}});
            state.routeNodes.set(route.id,row);host.append(row);
        }
        row.querySelector(".route-name").textContent=routeFilterValue(route.id);
        const end=route.terminal;
        const required=(state.metadata.index.problem?.parts||[]).reduce((sum,part)=>sum+Number(part.quantity||0),0);
        const summary=route.summary;
        const percent=value=>Number.isFinite(value)?number(value*100)+"%":"未记录";
        const statusNames={"not-applicable":"不适用：需要全部原件为无孔矩形",complete:"已排齐",partial:"未排齐",cancelled:"已取消",invalid:"输入无效",failed:"运行失败","not-started":"未启动",running:"正在运行","no-terminal":"终局未记录"};
        row.querySelector(".route-info").textContent=summary?[statusNames[summary.status]||summary.status,summary.selected?"最终选中":"",
            summary.placed==null?"需求 "+number(summary.required)+" 件 · 全任务结果尚未返回":"已排 "+number(summary.placed)+" / "+number(summary.required)+" 件",
            "整板 "+percent(summary.metrics?.stockUtilization)+" · 占用段 "+percent(summary.metrics?.usedSectionUtilization),
            "路线耗时 "+(Number.isFinite(summary.routeElapsedSeconds)?number(summary.routeElapsedSeconds)+" 秒":"未记录"),
            route.construction?.status==="complete"?"终局构造链可回放":"可查看原生记录与已留存分支"].filter(Boolean).join("\n"):
            number(route.events)+" 条记录"+(route.eventsOmitted?" · 采样省略 "+number(route.eventsOmitted)+" 条":"")+" · "+(route.beginObserved?"有开始记录":"未记录开始")+" · "+(route.terminalSnapshotTruncated?"终局布局过大，快照未完整记录；评分与摘要见终局":end ? end.boards+" 板 / "+end.placed+" / "+required+" 件，"+(end.resultStatus===0?"排齐":"未排齐")+"，原生终局" : "无终局记录");
        row.dataset.selected=summary?.selected?"true":"false";
        row.querySelector(".route-end").hidden=!end;
        row.dataset.terminal=end?end.ordinal:"";
        row.classList.toggle("active",state.route===route.id);
    }
    for(const [id,row] of state.routeNodes)if(!present.has(id)){row.remove();state.routeNodes.delete(id);}
    $("all-routes").classList.toggle("active",state.route===null);
}
function scopeOptions(){
    const scopes=state.route===null ? state.metadata.scopes||[] : (state.metadata.routes.find(route=>route.id===state.route)?.scopes||[]);
    options($("scope"),scopes.map(scope=>[JSON.stringify(scope),scope||"（未命名分支）"]),"全部分支");
    $("scope").value=state.scope===null?"":JSON.stringify(state.scope);
}
function renderDecisions(){
    const metadata=state.metadata;
    const signature=JSON.stringify(metadata.decisions);
    if($("decisions").dataset.signature===signature)return;
    const existing=new Map(Array.from($("decisions").children,node=>[node.dataset.ordinal,node]));
    const present=new Set();
    txt("decision-count",(metadata.decisionCount||0)+" 条，显示最近 "+(metadata.decisions||[]).length+" 条");
    for(const item of metadata.decisions||[]){
        const key=String(item.ordinal);present.add(key);
        let row=existing.get(key);
        if(!row){row=document.createElement("div");row.className="decision";row.dataset.ordinal=key;const button=document.createElement("button");button.addEventListener("click",()=>jump(row._item).catch(fail));row.append(button,document.createElement("code"),document.createElement("span"));$("decisions").append(row);}
        row._item=item;row.children[0].textContent="#"+item.ordinal;
        row.children[1].textContent=(item.route||"总调度")+" · "+item.source;
        row.children[2].textContent=(item.reason||"无理由记录")+(item.rank?.length?"；"+item.rank.map(value=>value.name+"="+number(value.value)).join(" → "):"");
    }
    for(const [key,node] of existing)if(!present.has(key))node.remove();
    if(!metadata.decisions?.length){const p=document.createElement("p");p.className="empty";p.textContent="无原生调度选优记录";$("decisions").append(p);}
    $("decisions").dataset.signature=signature;
}
function renderRunSummary(metadata){
    const run=metadata.runInfo;
    const version=run?.version||"未记录",hash=run?.executableSha256;
    const recordedDate=run?.timeSource==="run-started"&&run?.recordedAt?new Date(run.recordedAt):null;
    const time=recordedDate&&!Number.isNaN(recordedDate.valueOf())?new Intl.DateTimeFormat("zh-CN",{timeZone:"Asia/Shanghai",year:"numeric",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",second:"2-digit",hour12:false}).format(recordedDate)+"（北京时间）":run?.recordedAt||"未记录";
    const source=run?.timeSource==="run-started"?"运行开始":"时间来源：索引修改";
    txt("run-summary","运行版本："+version+(hash?" · 程序 SHA256："+hash:"")+" · "+(run?source+" "+time:"运行时间未记录")+(run?.metadataStatus==="bound"?(run.metadataSource==="material-run"?" · 本轮运行签名与启动记录已绑定":" · 相邻运行元数据已绑定"):" · 运行元数据未记录或未绑定"));
    const problem=metadata.index?.problem;
    if(!problem){txt("problem-summary","本次实际输入尚未记录");txt("completion-summary","尚无最终选择结果");$("completion-summary").dataset.status="unknown";return;}
    const stocks=(problem.stocks||[]).map(stock=>number(stock.width)+" × "+number(stock.height)+" mm").join("；")||"未记录";
    const parts=problem.parts||[],angles=new Map(),mirror={allowed:0,forbidden:0,unknown:0};
    for(const part of parts){
        const angle=part.freeRotation===true?"自由旋转许可":part.freeRotation===false&&Array.isArray(part.allowedAngles)&&part.allowedAngles.length?part.allowedAngles.map(value=>number(value)+"°").join(" / "):"角度未记录";
        angles.set(angle,(angles.get(angle)||0)+1);
        mirror[part.allowMirror===true?"allowed":part.allowMirror===false?"forbidden":"unknown"]++;
    }
    const permissions=[...angles].map(([angle,count])=>angle+"（"+count+" 型）").join("；")||"未记录";
    txt("problem-summary","实际输入 · 大板："+stocks+" · 边距："+(Number.isFinite(problem.edgeMargin)?number(problem.edgeMargin)+" mm":"未记录")+" · 零件间距："+(Number.isFinite(problem.partGap)?number(problem.partGap)+" mm":"未记录")+" · 角度："+permissions+" · 镜像：允许 "+mirror.allowed+" 型 / 禁止 "+mirror.forbidden+" 型 / 未记录 "+mirror.unknown+" 型。自由旋转为许可，不表示穷举连续角。");
    const final=(metadata.decisions||[]).findLast(event=>String(event.source||"").endsWith("final-selection"));
    const required=parts.reduce((sum,part)=>sum+Number(part.quantity||0),0);
    if(final&&!metadata.index.finalSnapshotTruncated){
        const placed=final.placed,missing=Math.max(0,required-placed);
        const remaining=(final.remaining||[]).map(row=>(row.part??row.partID??"未知零件")+" × "+row.quantity).join("，");
        txt("completion-summary","最终选择："+placed+" / "+required+" 件 · "+(missing?"未排 "+missing+" 件"+(remaining?"（"+remaining+"）":""):"已排齐")+" · "+final.boards+" 板 · "+(final.route||"总调度"));
        $("completion-summary").dataset.status=missing?"incomplete":"complete";
    }else{
        txt("completion-summary",metadata.index.finalSnapshotTruncated?"最终布局快照未完整记录，件数请查看原生结果。":"尚无全局最终选择结果；采集运行结束不代表已排齐。");
        $("completion-summary").dataset.status="unknown";
    }
}
async function metadataRefresh(){
    const task=state.task;
    if(!task)return;
    const metadata=await get(base());
    if(task!==state.task)return;
    const previousIndex=state.metadata?.binding?.indexPath;
    if(previousIndex&&metadata.binding?.indexPath&&previousIndex!==metadata.binding.indexPath){
        state.request++;state.page=null;state.detail=null;state.ordinal=null;state.position=0;state.route=state.scope=state.stage=null;
        $("boards").replaceChildren();$("events").replaceChildren();$("ranks").replaceChildren();$("measures").replaceChildren();
        txt("snapshot-status","本任务已开始新的运行，正在读取本次实际状态。");
    }
    state.metadata=metadata;
    if(activeEventView()==="construction"&&state.route===null)state.route=chooseConstructionRoute();
    renderRunSummary(metadata);
    if(metadata.available===false){
        const recording=metadata.binding?.status==="recording";
        txt("connection",recording?"运行中，过程待收尾":"本任务暂无可用过程");txt("task-summary",metadata.materialTask||task);
        txt("health",metadata.binding?.reason||"本次任务尚未生成原生过程记录。不会根据最终布局或其他任务补造过程。");
        txt("conditions",JSON.stringify(metadata.binding||{},null,2));
        $("index-link").hidden=$("result-link").hidden=true;
        state.page=null;state.detail=null;state.ordinal=null;state.position=0;state.route=state.scope=state.stage=null;
        $("routes").replaceChildren();state.routeNodes.clear();$("events").replaceChildren();$("decisions").replaceChildren();$("boards").replaceChildren();$("ranks").replaceChildren();$("measures").replaceChildren();
        txt("event-title",recording?"运行中，完成后读取过程记录":"本任务尚无可用原生事件");txt("event-reason","等待实际记录；不会显示其他任务或最终布局作为演化。");txt("snapshot-status",recording?"最终索引待收尾":"无记录");
        $("scope").disabled=$("stage").disabled=$("all-routes").disabled=true;controls();return;
    }
    const index=metadata.index;
    const conditions=index.conditions||{};
    const interruption=metadata.interruption;
    const interrupted=interruption?.diagnosticOnly===true&&interruption.completeResultProduced===false;
    const runStatus=interrupted?"已中止，未生成完整结果":index.runCompleted?"采集运行结束":"尚无完整终局记录";
    txt("task-summary",(conditions.diagnosticRun?"诊断采集 · ":"")+"上限 "+number(conditions.timeLimitSeconds??index.wholeJobBudgetSeconds)+" 秒 / "+number(conditions.attemptCap??index.constructorAttemptCap)+" 次尝试 · "+number(metadata.indexedEvents)+" 条事件 · "+runStatus+" · "+(index.rotatedFrame?"旋转采集坐标系":"原采集坐标系"));
    const warnings=[];
    if(interrupted)warnings.push("诊断运行已中止，未生成完整结果。中止理由："+interruption.reason+"；停止时间："+interruption.stoppedAt+"。仅回放已记录事件，不作为完整排样成绩。");
    const terminalSnapshotsTruncated=index.terminalSnapshotsTruncated??metadata.routes.reduce((sum,route)=>sum+Number(route.terminalSnapshotsTruncated??Boolean(route.terminalSnapshotTruncated)),0);
    if(index.truncated||index.eventsOmitted)warnings.push("过程采用有限采样：省略 "+number(index.eventsOmitted||0)+" 条（细节 "+number(index.detailEventsOmitted||0)+" 条、进展 "+number(index.progressEventsOmitted||0)+" 条、组合摘要 "+number(index.summaryEventsOmitted||0)+" 条）；终局大快照省略 "+number(terminalSnapshotsTruncated)+" 个。采样省略不代表算法失败。");
    else warnings.push("索引未报告观察缺失；这不代表搜索已穷尽。");
    if(Number.isFinite(index.performanceEventsSeen))warnings.push("构造性能累计：已记录 "+number(index.performanceEventsWritten||0)+" / "+number(index.performanceEventsSeen)+" 条，缺失 "+number(index.performanceEventsOmitted||0)+" 条。各阶段耗时存在嵌套，不能相加当作总耗时。");
    if(index.observerFailed)warnings.push("原生观察写入异常，记录可能缺失。");
    if(index.finalSnapshotTruncated)warnings.push("最终选中布局的过程快照过大，未完整记录；完整最终布局仍保留在原生结果与素材任务页。");
    if(metadata.partialTail)warnings.push(interrupted?"末行未完整写入，仅显示此前完整记录。":"末行尚未写完，页面等待该行完整后读取。");
    if(metadata.invalidLines)warnings.push("有 "+metadata.invalidLines+" 行无法解析，不把它们显示成有效事件。");
    warnings.push("搜索限额截断与观察缺失分别显示；采集含记录开销，不与旧质量成绩比较。");
    txt("health",warnings.join(" "));
    txt("conditions",JSON.stringify({runInfo:metadata.runInfo,binding:metadata.binding,interruption,conditions,wholeJobBudgetSeconds:index.wholeJobBudgetSeconds,constructorAttemptCap:index.constructorAttemptCap,instrumentedTiming:index.instrumentedTiming,eventCap:index.eventCap,byteCap:index.byteCap,caps:index.caps,eventsWritten:index.eventsWritten,eventsOmitted:index.eventsOmitted,detailEventsOmitted:index.detailEventsOmitted,progressEventsOmitted:index.progressEventsOmitted,performanceEventsSeen:index.performanceEventsSeen,performanceEventsWritten:index.performanceEventsWritten,performanceEventsOmitted:index.performanceEventsOmitted,performanceObservationTruncated:index.performanceObservationTruncated,terminalSnapshotsTruncated,payloadsTruncated:index.payloadsTruncated,finalSelectionObserved:index.finalSelectionObserved,finalSnapshotTruncated:index.finalSnapshotTruncated,coreObservationTruncated:index.coreObservationTruncated,runCompleted:index.runCompleted,observerFailed:index.observerFailed},null,2));
    for(const [id,kind] of [["index-link","index"],["result-link","result"]]){$(id).href=base()+"/"+kind;$(id).hidden=interrupted&&kind==="result";$(id).target="_blank";}
    scopeOptions();
    options($("stage"),Object.entries(metadata.stages||{}).map(([stage,count])=>[stage,label(stage)+" ("+number(count)+")"]),"全部阶段");
    $("stage").value=state.stage||"";
    preserveInteraction(()=>{renderRoutes();renderDecisions();auditLinks(metadata.audits||[]);});
    pathControls();
    txt("connection",interrupted?runStatus:index.runCompleted?"记录已读取":"记录未完成");
}
async function changeTask(){
    pause();state.request++;state.task=$("task").value;state.route=state.scope=state.stage=null;state.path="selected";state.page=null;state.position=0;state.ordinal=null;state.detail=null;state.metadata=null;
    const selectedTask=state.tasks.find(row=>row.id===state.task),url=new URL(location.href);
    if(selectedTask?.materialTask){url.searchParams.set("materialTask",selectedTask.materialTask);url.searchParams.delete("trace");}
    else{url.searchParams.delete("materialTask");if(state.task)url.searchParams.set("trace",state.task);else url.searchParams.delete("trace");}
    history.replaceState(null,"",url);
    state.routeNodes.clear();$("routes").replaceChildren();
    txt("task-summary","正在读取所选任务的原生记录…");txt("event-title","正在读取所选任务…");txt("event-id","");
    txt("run-summary","正在读取运行版本与时间…");txt("problem-summary","正在读取实际输入…");txt("completion-summary","正在读取最终选择结果…");$("completion-summary").dataset.status="unknown";
    txt("snapshot-status","尚未载入所选任务的状态记录。");$("boards").replaceChildren(document.createTextNode("正在载入…"));
    $("ranks").replaceChildren();$("measures").replaceChildren();$("events").replaceChildren();$("decisions").replaceChildren();
    txt("event-reason","正在读取所选任务…");txt("conditions","正在读取…");controls();
    $("index-link").hidden=$("result-link").hidden=true;
    $("scope").disabled=$("stage").disabled=$("all-routes").disabled=true;
    if(!state.task)return;
    await metadataRefresh();
    if(state.metadata?.available===false)return;
    $("scope").disabled=$("stage").disabled=$("all-routes").disabled=false;
    const selected=(state.metadata.decisions||[]).findLast(event=>String(event.source||"").endsWith("final-selection"));
    if(activeEventView()==="construction"&&state.metadata.selectedRoute){state.route=state.metadata.selectedRoute;renderRoutes();}
    pathControls();
    await loadPage(0,activeEventView()==="construction"?null:requestedStart?null:selected?.ordinal??null);
}
async function changeRoute(route,ordinal=null){
    pause();state.route=route;state.scope=state.stage=null;state.path="selected";state.position=0;state.page=null;
    if(route===null||ordinal!==null)state.eventView="events";
    const selected=state.metadata?.routes?.find(row=>row.id===route);
    if(state.eventView==="construction"&&selected?.summary?.status==="running"&&selected.construction?.branches?.length)
        state.path=selected.construction.branches[0].id;
    $("scope").value=$("stage").value="";scopeOptions();renderRoutes();
    pathControls();
    await loadPage(0,ordinal);
}
async function jump(item){
    state.eventView="events";state.route=item.route??null;state.scope=state.stage=null;scopeOptions();$("stage").value="";renderRoutes();pathControls();
    await loadPage(0,item.ordinal);
}
async function loadPage(position,ordinal=null,preserveDetail=false){
    const request=++state.request, task=state.task;
    if(!task)return;
    const params=filters();params.set("offset",Math.floor(position/25)*25);params.set("limit","25");
    if(ordinal!==null)params.set("ordinal",ordinal);
    const data=await get(base()+"/events?"+params);
    if(request!==state.request||task!==state.task)return;
    state.page=data;
    state.position=data.focusOffset??Math.min(Math.max(0,position),Math.max(0,data.total-1));
    preserveInteraction(renderEventList);
    const item=data.events[state.position-data.offset];
    if(item){if(!preserveDetail||item.ordinal!==state.ordinal||!state.detail)await loadEvent(item.ordinal,request);}
    else {state.ordinal=null;state.detail=null;txt("event-title","当前筛选没有事件");txt("event-id","");txt("event-reason","无实际记录，页面不生成替代事件。");$("boards").replaceChildren();txt("snapshot-status","无记录");}
    controls();
}
function renderEventList(){
    const data=state.page;const host=$("events");
    const existing=new Map(Array.from(host.children,node=>[node.dataset.ordinal,node]));const present=new Set();
    for(const [local,item] of data.events.entries()){
        const key=String(item.ordinal);present.add(key);let button=existing.get(key);
        if(!button){button=document.createElement("button");button.className="event-row";button.dataset.ordinal=key;button.append(document.createElement("b"),document.createElement("small"));button.addEventListener("click",()=>{pause();seek(Number(button.dataset.position)).catch(fail);});host.append(button);}
        button.dataset.position=data.offset+local;
        button.children[0].textContent="#"+item.ordinal+" · "+label(item.stage)+" · "+eventSource(item);
        button.children[1].textContent=eventReason(item)+(item.searchTruncated?" · 搜索限额截断":"")+(item.truncated?" · 观察缺失":"");
    }
    for(const [key,node] of existing)if(!present.has(key))node.remove();
    txt("event-total",number(data.total));
}
function eventSource(event){
    if(event.source==="construction.path.commit")return "原生提交 · 选中路径保留记录";
    if(event.source==="construction.path.raw")return "本板实际选中构造";
    if(event.source==="sheet-template.copy")return "整板复用 · 实际复制一步";
    if(event.source==="competition.board-selection")return "单板竞争选中 · 原生实例映射";
    if(event.source==="strategy.construction-selection")return "实际获胜重启";
    if(event.source==="construction.controller")return "策略流程 · "+(constructionReasons[event.reason]||event.reason);
    if(event.source==="strip.locked-domain")return "首件确定的条带范围";
    if(event.source==="random-array.draw")return "当前阵列候选抽选";
    if(event.source==="construction.phase")return "构造阶段 · "+constructionPhase(event);
    if(event.source==="placement.space-rank")return "当前空间评价";
    const phase=event.stage==="commit"?constructionPhase(event):null;
    if(phase)return (event.source||"无来源名称")+" · "+phase;
    if(event.source!=="preparation.pair-summary")return event.source||"无来源名称";
    const part=state.metadata?.index?.problem?.parts?.[event.partIndex];
    return "同规格组合摘要 · "+(part?.id??("规格 "+event.partIndex));
}
async function seek(position){
    if(!state.page)return;
    position=Math.min(Math.max(0,position),Math.max(0,state.page.total-1));
    if(position<state.page.offset||position>=state.page.offset+state.page.events.length)return loadPage(position);
    state.position=position;const request=++state.request;
    const item=state.page.events[position-state.page.offset];if(item)await loadEvent(item.ordinal,request);
    controls();
}
function controls(){
    const total=state.page?.total||0;
    $("timeline").max=Math.max(0,total-1);$("timeline").value=state.position;
    txt("position",(total?number(state.position+1):"0")+" / "+number(total));
    $("previous").disabled=!total||state.position===0;$("next").disabled=!total||state.position>=total-1;$("play").disabled=!total;
    $("page-prev").disabled=!total||state.page.offset===0;$("page-next").disabled=!total||state.page.nextOffset===null;
    txt("page-label",total?(Math.floor(state.page.offset/25)+1)+" / "+Math.ceil(total/25):"0 / 0");
    for(const button of $("events").children)button.classList.toggle("active",Number(button.dataset.ordinal)===state.ordinal);
}
async function loadEvent(ordinal,request){
    const task=state.task;
    const detail=await get(base()+"/events/"+ordinal);
    if(task!==state.task||request!==state.request)return;
    state.ordinal=ordinal;state.detail=detail;
    const e=detail.event;
    txt("event-title",label(e.stage)+" · "+eventSource(e));
    txt("event-id","#"+ordinal+" · 状态 "+number(e.stateID)+" · 候选 "+number(e.candidateID));
    txt("event-reason",eventReason(e)+" · 路线 "+routeFilterValue(e.route||"")+" · 分支 "+(e.scope||"无名称")+(e.searchTruncated?" · 搜索受到限额截断":"")+(e.truncated?" · 观察记录有省略":"")+(e.accepted===false?" · 本观察标记为未接受":""));
    txt("snapshot-status",detail.snapshot?"显示原生状态快照 #"+detail.snapshot.ordinal+"；"+(detail.snapshot.mode==="event"?"当前事件直接记录":"以相同路线、分支和 stateID 精确关联")+"。橙色为本事件候选。":"无可精确关联的状态快照。仅显示事件实际记录的候选/几何；不会取最终布局补齐。");
    values($("ranks"),constructionMeasures(e.rank||[]),"没有评分记录");
    values($("measures"),[{name:"elapsedSeconds（原始事件字段）",value:e.elapsedSeconds},{name:"attempts",value:e.attempts},{name:"expandedStates",value:e.expandedStates},{name:"searchTruncated（搜索限额）",value:e.searchTruncated},{name:"truncated（观察缺失）",value:e.truncated},...constructionMeasures(e.measures||[])],"没有测量记录");
    txt("event-json",JSON.stringify(detail,null,2));draw();controls();
}
function values(host,rows,empty){
    host.replaceChildren();
    for(const row of rows){const line=document.createElement("div");line.className="measure";const name=document.createElement("span");name.textContent=row.name;const value=document.createElement("b");value.textContent=number(row.value);line.append(name,value);host.append(line);}
    if(!rows.length)host.textContent=empty;
}
function svg(name,attributes={}){const node=document.createElementNS(NS,name);for(const [key,value] of Object.entries(attributes))node.setAttribute(key,String(value));return node;}
function world(point,pose={}){
    const x=Array.isArray(point)?point[0]:point.x??point.X??0, sourceY=Array.isArray(point)?point[1]:point.y??point.Y??0;
    const y=pose.mirrored===true?-sourceY:sourceY;
    const angle=Number(pose.angle??pose.angleDegrees??0)*Math.PI/180;
    return [x*Math.cos(angle)-y*Math.sin(angle)+Number(pose.x||0),x*Math.sin(angle)+y*Math.cos(angle)+Number(pose.y||0)];
}
function shape(geometry,pose,cls,stroke){
    let node;
    if(geometry.radius>0){const center=world(geometry.center||[0,0],pose);node=svg("circle",{cx:center[0],cy:center[1],r:geometry.radius});}
    else{
        const rings=geometry.rings||[geometry.outer||[],...(geometry.holes||[])];
        const path=rings.filter(ring=>ring.length).map(ring=>ring.map((point,index)=>(index?"L":"M")+world(point,pose).join(" ")).join(" ")+" Z").join(" ");
        if(!path)return null;node=svg("path",{d:path,"fill-rule":"evenodd"});
    }
    node.setAttribute("class",cls);node.dataset.mirrored=String(pose?.mirrored===true);
    node.setAttribute("stroke-width",stroke);node.setAttribute("vector-effect","non-scaling-stroke");return node;
}
function draw(){
    const host=$("boards");host.replaceChildren();
    if(!state.detail||!state.metadata)return;
    const e=state.detail.event,problem=state.metadata.index.problem||{},parts=problem.parts||[];
    const byID=new Map(parts.map(part=>[part.id,part]));
    let recorded=state.detail.snapshot?.layouts||[];
    if(["competition.board-selection","sheet-template.copy"].includes(state.detail.event.source)){
        const target=state.detail.event.measures?.find(row=>row.name==="targetLayoutIndex")?.value;
        if(Number.isInteger(target)&&recorded[target])recorded=[recorded[target]];
    }
    let boards=recorded;
    let candidateOnly=false;
    if(!boards.length&&(e.members?.length||e.geometries?.length)){
        const stock=problem.stocks?.[e.stockIndex??0];
        const width=e.boardWidth>0?e.boardWidth:stock?.width;
        const height=e.boardHeight>0?e.boardHeight:stock?.height;
        if(width>0&&height>0){boards=[{stockID:stock?.id||"候选参考域",stockInstance:e.stockInstance||0,width,height,placements:[]}];candidateOnly=true;}
    }
    if(!boards.length){const p=document.createElement("p");p.className="empty";p.textContent="本事件没有可绘制的状态、候选或板材坐标记录。";host.append(p);return;}
    const matches=index=>candidateOnly||(e.boardIndex!=null?index===Number(e.boardIndex):boards.length===1);
    const views=boards.map((board,index)=>{
        const entries=(board.placements||[]).map(placed=>({geometry:byID.get(placed.part??placed.partID)?.geometry,pose:placementPose(placed)}));
        if($("overlay").checked&&matches(index)){
            for(const member of e.members||[])entries.push({geometry:parts[member.partIndex]?.geometry,pose:placementPose(member.pose||{x:0,y:0,angle:0})});
            if(!e.members?.length)for(const geometry of e.geometries||[])entries.push({geometry,pose:{x:0,y:0,angle:0}});
        }
        return displayViewport(board,geometryBounds(entries),state.viewMode,true);
    });
    const commonScale=Math.min(600/Math.max(...views.map(view=>view.width)),430/Math.max(...views.map(view=>view.height)));
    boards.forEach((board,index)=>{
        const view=views[index];
        const card=document.createElement("div");card.className="board";
        const heading=document.createElement("div");heading.className="board-title";
        heading.textContent=(board.stockID||board.stock||"板材")+" · 第"+(Number(board.stockInstance??board.instance??index)+1)+"板 · 原板 "+number(board.width)+" × "+number(board.height)+" · "+(board.placements||[]).length+" 件"+(candidateOnly?" · 参考板框，无状态快照":"");
        const legend=document.createElement("span");legend.className="legend";legend.textContent="实色：已记录布局 / 虚线：候选";heading.append(legend);
        const note=document.createElement("p");note.className="hint";note.style.padding="0 10px";
        note.textContent=view.used?`仅显示已记录区域加空白：X ${number(view.x)}–${number(view.x+view.width)}，Y ${number(view.y)}–${number(view.y+view.height)}。坐标原点仍为 (0, 0)，原板尺寸及零件坐标不变；显示裁切不代表占用段率或排样改善。`:"显示整板，原板尺寸及零件坐标不变。所有板使用相同物理比例。";
        const viewport=document.createElement("div");viewport.className="board-viewport";
        const stroke=1;
        const canvas=svg("svg",{viewBox:[view.x,board.height-view.y-view.height,view.width,view.height].join(" "),width:view.width*commonScale,height:view.height*commonScale,"data-view-mode":view.used?"used":"full","data-pixels-per-mm":commonScale,role:"img","aria-label":"原生状态和候选，板 "+(index+1)});
        Object.assign(canvas.style,{width:`${view.width*commonScale}px`,height:`${view.height*commonScale}px`,minHeight:"0",maxHeight:"none"});
        const group=svg("g",{transform:"translate(0 "+board.height+") scale(1 -1)"});
        group.append(svg("rect",{x:0,y:0,width:board.width,height:board.height,class:"sheet","stroke-width":stroke,"vector-effect":"non-scaling-stroke"}));
        const margin=problem.edgeMargin||0;
        if(margin>0)group.append(svg("rect",{x:margin,y:margin,width:Math.max(0,board.width-2*margin),height:Math.max(0,board.height-2*margin),class:"margin","stroke-width":stroke*.65,"vector-effect":"non-scaling-stroke"}));
        for(const placed of board.placements||[]){
            const part=byID.get(placed.part??placed.partID);if(!part)continue;
            const node=shape(part.geometry||{},placed.pose||placed,"original",stroke*.7);if(!node)continue;
            const title=svg("title");title.textContent=part.id+" · 实例 "+number(placed.instance)+" · "+JSON.stringify(placed.pose||placed);node.append(title);group.append(node);
        }
        const boardMatch=matches(index);
        if($("overlay").checked&&boardMatch){
            for(const member of e.members||[]){
                const part=parts[member.partIndex];if(!part)continue;
                const node=shape(part.geometry||{},member.pose||{},"candidate"+(e.accepted===false?" rejected":""),stroke*1.5);
                if(node){const title=svg("title");title.textContent="候选 "+part.id+" · "+JSON.stringify(member.pose);node.append(title);group.append(node);}
            }
            if(!e.members?.length)for(const geometry of e.geometries||[]){const node=shape(geometry,{},"candidate",stroke*1.3);if(node)group.append(node);}
        }
        canvas.append(group);viewport.append(canvas);card.append(heading,note,viewport);host.append(card);
    });
}
const comparisonState={known:new Map(),rows:new Map(),busy:false};
function comparisonSummary(run){
    const problem=run?.raw?.problem||run?.metadata?.index?.problem;
    if(!problem)return "参数待记录";
    const active=(problem.parts||[]).filter(part=>part.quantity>0),settings=run.raw?.settings;
    return "自由旋转 "+active.filter(part=>part.freeRotation===true).length+" / "+active.length+" 型；镜像 "+
        active.filter(part=>part.allowMirror===true).length+" / "+active.length+" 型；边距 "+number(problem.edgeMargin)+
        " mm，间距 "+number(problem.partGap)+" mm，共边 "+(problem.allowCommonEdges?"允许":"禁止")+
        (settings?"；上限 "+number(settings.timeLimitSeconds)+" 秒 / "+number(settings.maxPlacementAttempts)+" 次 / "+number(settings.workers)+" workers":"");
}
async function comparisonRead(task){
    if(!task)return {finished:false,error:"尚未发现这一侧记录"};
    const endpoint="/api/construction/"+encodeURIComponent(task.id);
    const run={task,endpoint,finished:false};
    const responses=await Promise.allSettled([get(endpoint),get(endpoint+"/result")]);
    if(responses[0].status==="fulfilled")run.metadata=responses[0].value;
    if(responses[1].status==="fulfilled")run.raw=responses[1].value;
    const errors=responses.filter(value=>value.status==="rejected").map(value=>value.reason?.message||String(value.reason));
    if(errors.length){
        const unfinished=run.metadata?.index?.runCompleted===false||
            run.metadata?.index?.runCompleted!==true&&task.runCompleted===false;
        const missingResult=responses[1].status==="rejected"&&responses[1].reason?.status===404;
        run.error=missingResult&&unfinished?(run.metadata?.interruption?"记录已中断，尚无正式终局":"构造中，尚无正式终局"):errors.join("；");
        return run;
    }
    const index=run.metadata.index,result=run.raw.result;
    if(run.metadata.available===false||index?.runCompleted!==true||run.raw.constructionObservation?.runCompleted===false){
        run.error=run.metadata.interruption?"记录已中断，尚无完整终局":"构造记录尚未结束，尚无正式终局";return run;
    }
    if(!result){run.error="缺少原生终局结果";return run;}
    run.finished=true;
    run.valid=result.valid===true&&run.raw.validation?.valid!==false;
    run.complete=result.complete===true&&run.raw.validation?.complete!==false;
    try{run.metrics=comparisonMetrics(run.raw.problem,result);}
    catch(error){run.error=error.message;run.finished=false;return run;}
    run.eligible=run.valid&&run.complete&&run.metrics.placed===run.metrics.required;
    const final=(run.metadata.decisions||[]).findLast(event=>String(event.source||"").endsWith("final-selection"));
    // The raw wrapper's clocks cover the whole job. Only a missing whole-job
    // clock needs a separate final scheduler event; strategy-local time is not used.
    let terminal;
    if(!(Number.isFinite(run.raw.wholeJobWallSeconds)&&run.raw.wholeJobWallSeconds>=0)&&final){
        try{terminal=(await get(endpoint+"/events/"+final.ordinal)).event;}
        catch(error){run.timeWarning="终局墙钟读取失败；"+error.message;}
    }
    Object.assign(run,comparisonTimes(run.raw,terminal));
    return run;
}
function comparisonCell(cell,lines){
    const signature=JSON.stringify(lines);
    if(cell.dataset.content===signature)return;
    cell.replaceChildren();
    for(const line of lines){const block=document.createElement("div");block.className="comparison-value";
        if(typeof line==="string")block.textContent=line;
        else{block.textContent=line.text;block.title=line.title||"";if(line.className)block.classList.add(line.className);}
        cell.append(block);
    }
    cell.dataset.content=signature;
}
function comparisonRender(pair){
    let row=comparisonState.rows.get(pair.id);
    if(!row){
        row=document.createElement("tr");row.dataset.case=pair.id;
        for(let i=0;i<11;i++)row.append(document.createElement(i===0?"th":"td"));
        row.children[0].scope="row";
        for(const [side,label] of [["before","前"],["after","后"]]){
            const block=document.createElement("div");block.className="comparison-links";block.dataset.side=side;
            const name=document.createElement("span");name.textContent=label+"：";block.append(name);
            for(const [kind,title] of [["final","终局"],["start","过程"],["raw","JSON"]]){
                const a=document.createElement("a");a.dataset.kind=kind;a.textContent=title;a.target="_blank";a.rel="noopener";block.append(a);
            }
            row.children[10].append(block);
        }
        comparisonState.rows.set(pair.id,row);
    }
    const a=pair.runs?.before,b=pair.runs?.after,match=comparisonMatch(a,b),delta=comparisonDelta(a,b,match);
    row.dataset.quality=delta?.quality||"待完成";
    comparisonCell(row.children[0],[pair.id]);
    const problem=a?.raw?.problem||b?.raw?.problem||a?.metadata?.index?.problem||b?.metadata?.index?.problem;
    const stockText=(problem?.stocks||[]).map(stock=>number(stock.width)+" × "+number(stock.height)+" mm").join("；");
    comparisonCell(row.children[1],[{text:match.note,title:[comparisonSummary(a),comparisonSummary(b)].join("\n")},
        stockText||"板材待记录",match.hashNote||"尚未核对输入文件 SHA256"]);
    const metric=(run,read)=>run?.finished&&run.metrics?read(run):"待完成";
    const two=(cell,read)=>comparisonCell(row.children[cell],[{text:"前："+metric(a,read),title:a?.error||comparisonSummary(a)},
        {text:"后："+metric(b,read),title:b?.error||comparisonSummary(b)}]);
    const percent=value=>(value*100).toFixed(3)+"%";
    const demand=run=>{
        if(run?.finished&&run.metrics)return run.metrics.placed+" / "+run.metrics.required+" · "+(run.complete?"排齐":"未排齐")+" · 原生"+(run.valid?"核验通过":"核验未通过");
        const parts=run?.metadata?.index?.problem?.parts||run?.raw?.problem?.parts;
        return parts?"需求 "+number(parts.reduce((sum,part)=>sum+part.quantity,0))+" / 待完成":"需求待记录 / 待完成";
    };
    comparisonCell(row.children[2],[{text:"前："+demand(a),title:a?.error||""},{text:"后："+demand(b),title:b?.error||""}]);
    two(3,run=>number(run.metrics.boards));two(4,run=>percent(run.metrics.stockUtilization));two(5,run=>percent(run.metrics.sectionUtilization));
    const fixed=(value,digits)=>value.toLocaleString("zh-CN",{minimumFractionDigits:digits,maximumFractionDigits:digits});
    two(6,run=>(run.metrics.lengths.length>1?"合计 ":"")+fixed(run.metrics.length,2)+" mm"+
        (run.metrics.lengths.length>1?"（"+run.metrics.lengths.length+" 板）":""));
    row.children[6].title=[a,b].map((run,i)=>(i?"后：":"前：")+(run?.metrics?.lengths||[]).map((value,j)=>"第"+(j+1)+"板 "+String(value)+" mm").join("；")).join("\n");
    two(7,run=>Number.isFinite(run.first)?fixed(run.first,3)+" 秒":"无记录");
    row.children[7].title=[a,b].map((run,i)=>(i?"后：":"前：")+String(run?.first??"无记录")+" 秒；首次完整不代表最终质量").join("\n");
    two(8,run=>Number.isFinite(run.seconds)?fixed(run.seconds,3)+" 秒":"无记录");
    row.children[8].title=[a,b].map((run,i)=>(i?"后：":"前：")+String(run?.seconds??"无记录")+" 秒；"+(run?.timeSource||"待完整记录")+(run?.timeWarning?"；"+run.timeWarning:"")).join("\n");
    const signed=value=>(value>0?"+":"")+fixed(value,3);
    comparisonCell(row.children[9],delta?[{text:delta.quality,className:"comparison-quality"},
        "整板 "+signed(delta.stock*100)+" 个百分点","使用段 "+signed(delta.section*100)+" 个百分点",
        delta.seconds==null?"耗时差无记录":"耗时 "+signed(delta.seconds)+" 秒（后 − 前）"]:
        ["待完成 / 不比较",...[["前",a],["后",b]].filter(([,run])=>run?.error||run?.finished&&!run.eligible)
            .map(([name,run])=>name+"："+(run.error||"已结束，但原生未同时核验通过并排齐"))]);
    for(const [side,task] of [["before",pair.before],["after",pair.after]]){
        const block=row.children[10].querySelector('[data-side="'+side+'"]');
        for(const link of block.querySelectorAll("a")){
            link.hidden=!task;
            if(task)link.href=link.dataset.kind==="raw"?"/api/construction/"+encodeURIComponent(task.id)+"/result":
                "/construction.html?trace="+encodeURIComponent(task.id)+(link.dataset.kind==="start"?"&start=1":"&final=1");
        }
        block.title=task?.id||"尚未发现这一侧记录";
    }
    return row;
}
async function comparisonRefresh(){
    if(comparisonState.busy)return;
    comparisonState.busy=true;$("refresh").setAttribute("aria-busy","true");txt("connection","正在刷新对比…");
    try{
        const data=await get("/api/construction/tasks");
        const before=comparisonQuery.get("before")||"v34-array",after=comparisonQuery.get("after")||"v35-array";
        if(before===after)throw new Error("前后版本必须不同");
        for(const found of comparisonPairs(data.tasks||[],requestedComparison,before,after)){
            const existing=comparisonState.known.get(found.id)||{id:found.id};
            Object.assign(existing,found);comparisonState.known.set(found.id,existing);
        }
        const pairs=[...comparisonState.known.values()].sort((a,b)=>a.id.localeCompare(b.id,"zh-CN",{numeric:true}));
        await Promise.all(pairs.map(async pair=>{
            const runs=await Promise.all([comparisonRead(pair.before),comparisonRead(pair.after)]);
            pair.runs={before:runs[0],after:runs[1]};
        }));
        preserveInteraction(()=>{
            const host=$("comparison-rows");
            pairs.forEach((pair,i)=>{const row=comparisonRender(pair);if(host.children[i]!==row)host.insertBefore(row,host.children[i]||null);});
            const finished=pairs.filter(pair=>pair.runs.before.finished&&pair.runs.after.finished).length;
            txt("comparison-status",pairs.length?"已发现 "+pairs.length+" 个案例；双方均结束 "+finished+" 个，待完成 "+(pairs.length-finished)+
                " 个。按需点击“刷新记录”读取新结果；本页不启动排样，也不把未结束记录计作改善。":"尚未发现匹配的原生记录。页面等待真实记录，不补造案例或结果；可稍后刷新。");
            const run=pairs.flatMap(pair=>[pair.runs.before,pair.runs.after]).find(value=>value.raw?.problem||value.metadata?.index?.problem);
            txt("comparison-conditions",run?"已读取记录示例："+comparisonSummary(run)+"。逐案例条件见表内悬停说明；自由旋转是许可，不表示穷举连续角。":"实际参数尚未记录；自由旋转是许可，不表示穷举连续角。");
            txt("connection","对比已刷新 · "+new Date().toLocaleTimeString("zh-CN"));
        });
    }catch(error){txt("connection","对比读取失败");txt("comparison-status",error.message+"。待完成；未据此计算改善。请刷新重试。");
        for(const pair of comparisonState.known.values()){pair.runs={before:{finished:false,error:error.message},after:{finished:false,error:error.message}};comparisonRender(pair);}
    }finally{comparisonState.busy=false;$("refresh").removeAttribute("aria-busy");}
}
function comparisonInitialize(){
    $("replay-main").hidden=true;$("comparison-main").hidden=false;
    document.title="初始排样 · 前后对比";txt("page-title","初始排样 · 前后对比");
    txt("comparison-versions","对比组："+requestedComparison+" · 限制前 "+(comparisonQuery.get("before")||"v34-array")+
        " → 限制后 "+(comparisonQuery.get("after")||"v35-array"));
    $("refresh").addEventListener("click",comparisonRefresh);comparisonRefresh();
}
async function playTick(){
    if(!state.playing)return;
    if(!state.page||state.position>=state.page.total-1){pause();return;}
    try{await seek(state.position+1);}catch(error){fail(error);return;}
    if(state.playing)playTimer=setTimeout(playTick,Number($("speed").value));
}
if(requestedComparison){comparisonInitialize();return;}
const viewLabel=document.createElement("label"),viewSelect=document.createElement("select");viewSelect.id="construction-view-mode";
viewLabel.append(document.createTextNode("显示范围 "),viewSelect);viewSelect.add(new Option("已记录区域 + 空白","used"));viewSelect.add(new Option("整板","full"));
document.querySelector(".filters").append(viewLabel);viewSelect.addEventListener("change",()=>{state.viewMode=viewSelect.value;preserveInteraction(draw);});
$("task").addEventListener("change",()=>changeTask().catch(fail));
$("event-view").addEventListener("change",()=>{
    pause();state.eventView=$("event-view").value;state.path="selected";state.scope=state.stage=null;
    if(state.eventView==="construction"&&state.route===null)state.route=state.metadata?.selectedRoute??null;
    scopeOptions();$("stage").value="";pathControls();renderRoutes();
    const url=new URL(location.href);url.searchParams.set("view",state.eventView);history.replaceState(null,"",url);
    loadPage(0).catch(fail);
});
$("construction-path").addEventListener("change",()=>{pause();state.path=$("construction-path").value||"selected";pathControls();loadPage(0).catch(fail);});
$("all-routes").addEventListener("click",()=>changeRoute(null).catch(fail));
for(const key of ["scope","stage"])$(key).addEventListener("change",()=>{pause();state[key]=$(key).value?(key==="scope"?JSON.parse($(key).value):$(key).value):null;loadPage(0).catch(fail);});
$("overlay").addEventListener("change",draw);
$("previous").addEventListener("click",()=>{pause();seek(state.position-1).catch(fail);});
$("next").addEventListener("click",()=>{pause();seek(state.position+1).catch(fail);});
$("page-prev").addEventListener("click",()=>{pause();loadPage(Math.max(0,state.page.offset-25)).catch(fail);});
$("page-next").addEventListener("click",()=>{pause();loadPage(state.page.nextOffset||0).catch(fail);});
$("timeline").addEventListener("input",()=>{pause();clearTimeout(seekTimer);const position=Number($("timeline").value);seekTimer=setTimeout(()=>seek(position).catch(fail),90);});
$("play").addEventListener("click",()=>{if(state.playing){pause();return;}state.playing=true;txt("play","暂停");playTick();});
$("refresh").addEventListener("click",async()=>{try{await tasksRefresh();if(state.task&&state.page)await loadPage(state.position,state.ordinal,true);}catch(error){fail(error);}});
tasksRefresh(true).catch(fail);
setInterval(()=>{if(!document.hidden&&!state.busy){state.busy=true;tasksRefresh(false).catch(fail).finally(()=>{state.busy=false;});}},5000);
})();
