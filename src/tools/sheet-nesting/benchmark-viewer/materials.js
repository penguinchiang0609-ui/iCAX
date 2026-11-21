(() => {
    "use strict";
    const $ = id => document.getElementById(id);
    const state = {mode:"tasks", page:0, size:50, total:0, selected:null, rows:[], revision:null,
        refreshing:false, listRequest:0, detailRequest:0, drawingSignature:null, layoutMode:"used", layoutZoom:100, lastBenchmark:{}};
    const fmt = (value, digits=0) => typeof value === "number" && Number.isFinite(value)
        ? value.toLocaleString("zh-CN", {maximumFractionDigits:digits}) : "—";
    const set = (id, value) => { const node=$(id); const text=String(value ?? "—"); if(node.textContent!==text)node.textContent=text; };
    const json = value => JSON.stringify(value ?? null, null, 2);
    const textValue = value => typeof value === "string" ? value : json(value);
    function element(tag, cls, text) {const node=document.createElement(tag);if(cls)node.className=cls;if(text!==undefined)node.textContent=text;return node;}
    function link(text, url) {const node=element("a","",text);node.href=url;return node;}
    const statusNames={ready:"可运行",runnable:"可运行",blocked:"输入受限",pending:"等待排样",queued:"等待排样",
        "not-run":"尚未排样","not-started":"尚未排样",reference:"参考资料",unsupported:"暂不支持",invalid:"几何检查未通过",
        "native-result":"已有原生结果",constructing:"初始解已排齐，策略比较中",finished:"已返回结果",completed:"已返回结果",complete:"已返回结果",error:"运行异常",failed:"运行失败",
        "missing-assets":"输入文件缺失","unsupported-geometry":"几何暂不支持","needs-review":"条件待核对","source-invalid":"原始轮廓无效",
        "process-timeout":"进程超时","binding-error":"结果绑定异常","stale-input":"输入已变更 · 旧结果未采用","stale-algorithm":"算法已更新，等待本次重跑",missing:"缺少素材",ok:"可读取"};
    const label = value => statusNames[value] || value || "未标记";
    function preparationLabel(task) {
        if(["ready","runnable"].includes(task.status)&&task.inputSource==="source-contours")return `原始轮廓可执行${task.missingDxfAssets?.length?" · DXF待按需导出":""}`;
        return label(task.status);
    }
    const pending = benchmark => !benchmark || [null,undefined,"","pending","queued","not-run","not-started","blocked"].includes(benchmark.status);
    function resultView(benchmark, requiredFallback) {
        if(pending(benchmark))return {actual:false,tone:"pending",text:benchmark?.status==="blocked"?"输入受限 · 尚未执行":"等待排样"};
        if(benchmark.status==="stale-algorithm")return {actual:false,tone:"pending",text:label(benchmark.status)};
        if(["binding-error","stale-input"].includes(benchmark.status))return {actual:false,tone:"invalid",text:label(benchmark.status)};
        if(["error","failed","process-timeout","execution-error","no-native-output","unreadable-native-output"].includes(benchmark.status))return {actual:false,tone:"invalid",text:label(benchmark.status)};
        const placed=benchmark.placed, required=benchmark.required??requiredFallback;
        const counts=Number.isInteger(placed)&&placed>=0&&Number.isInteger(required)&&required>=0;
        const remaining=counts?required-placed:null;
        const inconsistent=counts&&(remaining<0||(benchmark.complete===true&&remaining!==0));
        const completeness=inconsistent?"完整性字段与数量不一致":benchmark.complete===false?"未排齐":benchmark.complete===true?"已排齐":"完整性未提供 · 已排";
        const countText=`${fmt(placed)}/${fmt(required)}件`;
        const quantity=counts?`${fmt(placed)} / ${fmt(required)} 件`:"数量未提供";
        const prefix=benchmark.valid===false?"原生几何检查未通过 · ":"";
        const comparing=benchmark.status==="constructing";
        const timeLabel=comparing?(benchmark.timingScope==="live-current"?"已运行 ":"候选发布时 "):"";
        const text=prefix+`${comparing?label(benchmark.status):completeness} ${countText}${remaining!==null?` · 剩余${fmt(remaining)}件`:" · 剩余数量未提供"}${typeof benchmark.boards==="number"?` · ${fmt(benchmark.boards)}板`:""}${typeof benchmark.nativeWallSeconds==="number"?` · ${timeLabel}${fmt(benchmark.nativeWallSeconds,3)}s`:""}${benchmark.constructionTraceError?" · 过程采集异常":""}`;
        return {actual:true,tone:benchmark.valid===false||inconsistent?"invalid":benchmark.complete===false?"incomplete":"neutral",text,quantity,remaining};
    }
    function stockText(stocks, sourceStock) {
        if(!Array.isArray(stocks)||!stocks.length)return "无板材记录";
        const sources={"source-finite-sheet":"来源指定板材","user-default-3000x6000":"来源未指定，统一默认"};
        return stocks.map(stock=>{
            const width=stock.widthMm ?? stock.width, height=stock.heightMm ?? stock.height;
            const source=sourceStock?.kind==="roll"?"来源卷料：保留原幅宽，不限方向按1,000,000测试":sources[stock.source]||textValue(stock.source);
            return `${fmt(width,3)} × ${fmt(height,3)} mm · ${stock.unlimited ? "数量不限" : `${fmt(stock.quantity)} 张`}${stock.source||sourceStock?.kind==="roll"?`（${source}）`:""}`;
        }).join("；");
    }
    function unitsText(units) {
        if(!units)return "未标记";
        const scale=typeof units.scaleToMm==="number" ? ` · 1 单位 = ${fmt(units.scaleToMm,8)} mm` : "";
        return `${units.label || units.code || "单位未声明"}${scale}${units.assumed ? " · 按默认单位解释" : ""}`;
    }
    function curveNote(geometry) {
        if(!geometry||(!geometry.curveApproximation&&!(geometry.maxDeviationMm>0)))return "";
        const tolerance=geometry.maxDeviationMm>0?geometry.maxDeviationMm:.01;
        return `曲线导入公差 ${fmt(tolerance,4)} mm；近似弦不参与共边。`;
    }
    function rulesText(rules) {
        if(!rules)return "未提供规则";
        const margin=rules.edgeMarginMm ?? rules.edgeMargin;
        const gap=rules.partGapMm ?? rules.partGap;
        const common=rules.allowCommonEdges;
        const rotation=rules.freeRotation ?? rules.allowFreeRotation;
        const mirror=rules.allowMirror;
        return `边距 ${fmt(margin,4)} mm；间距 ${fmt(gap,4)} mm；${common===true?"允许共边":common===false?"不允许共边":"共边未声明"}；${rotation===true?"允许任意角度":rotation===false?"使用指定角度":"角度见原始字段"}；${mirror===true?"未限制的零件允许镜像":mirror===false?"默认禁止镜像，遵守零件约束":"镜像权限见本次原生输入"}`;
    }
    async function get(path) {
        const controller=new AbortController(), timer=setTimeout(()=>controller.abort(),15000);
        try {const response=await fetch(path,{cache:"no-store",signal:controller.signal});if(!response.ok)throw new Error(`读取失败（${response.status}）`);return await response.json();}
        finally{clearTimeout(timer);}
    }
    function keyed(parent, rows, key, create, update) {
        const known=new Map(Array.from(parent.children,child=>[child.dataset.key,child]));
        rows.forEach((row,index)=>{const id=String(key(row,index));let node=known.get(id);if(node)known.delete(id);else{node=create(row,index);node.dataset.key=id;}update(node,row,index);if(parent.children[index]!==node)parent.insertBefore(node,parent.children[index]||null);});
        for(const node of known.values())node.remove();
    }
    function updateCollections(collections) {
        const select=$("collection");
        keyed(select,["",...collections],value=>value,()=>document.createElement("option"),(node,value)=>{node.value=value;node.textContent=value||"全部来源";});
    }
    function updateSummary(data) {
        const summary=data.summary||{};
        for(const [id,key] of [["archive-count","archives"],["asset-count","assets"],["task-count","tasks"],["ready-count","ready"],["missing-count","missingAssets"],["executed-count","executed"]])set(id,fmt(summary[key]));
        updateCollections([...new Set([...(data.packages||[]),...(data.collections||[])])]);
        const defaults=data.defaults||{};
        const rules=defaults.rules||defaults;
        const defaultStock=defaults.stock||defaults.defaultStock||{};
        const width=defaultStock.widthMm ?? defaultStock.width ?? defaults.stockWidthMm;
        const height=defaultStock.heightMm ?? defaultStock.height ?? defaults.stockHeightMm;
        set("default-conditions",`无来源尺寸：${width&&height?`${fmt(width)} × ${fmt(height)}`:"3000 × 6000"} mm、数量不限；来源卷料保留原幅宽，不限方向按1,000,000测试；${rulesText(rules)}`);
        set("units-caption",`${fmt(summary.assumedUnitAssets)} 个 DXF 使用默认单位；${fmt(summary.assumedQuantityRows)} 行未指定数量、PK 默认 1 件；参考任务 ${fmt(summary.references)} 个${summary.sourceContourTasks?`；${fmt(summary.sourceContourTasks)} 个任务可用原始轮廓执行，原始轮廓未标单位时按 mm 解释；DXF待按需导出 ${fmt(summary.missingDxfTasks)} 项`:""}${summary.sourceInvalidTasks?`；原始轮廓无效 ${fmt(summary.sourceInvalidTasks)} 项`:""}`);
        const progress=data.progress||{};
        const activeTasks=Array.isArray(progress.currentTasks)?progress.currentTasks:progress.currentTask?[progress.currentTask]:[];
        const concurrency=progress.caseConcurrency??data.executionProfile?.caseConcurrency??1;
        const incomplete=progress.incomplete??0, invalid=progress.invalid??0, failed=progress.failed??0, observationFailed=progress.observationFailed??0;
        const needsFix=progress.status==="needs-fix"||(progress.status==="finished"&&(incomplete>0||invalid>0||failed>0||observationFailed>0));
        const unresolved=Number.isInteger(progress.unresolved)?`${fmt(progress.unresolved)} 项`:"任务";
        const fixCaption=`任务已执行完，仍有${unresolved}${observationFailed?"未排齐、不合法、运行或过程采集问题":"未排齐、不合法或运行错误"}，待修复。未排齐 ${fmt(incomplete)} 项 · 不合法 ${fmt(invalid)} 项 · 运行错误 ${fmt(failed)} 项${observationFailed?` · 过程采集异常 ${fmt(observationFailed)} 项`:""}；已返回 ${fmt(progress.completed)} / ${fmt(progress.total)}。`;
        set("run-progress",progress.status==="running"?`正在排样：${activeTasks.join("、")||"等待任务启动"} · 当前 ${fmt(activeTasks.length)} 项 / 最多 ${fmt(concurrency)} 项并行 · 本批 ${fmt(progress.completed)} / ${fmt(progress.total)} 已返回 · 每任务 ${fmt(progress.timeLimitSeconds,1)} s 上限、各 ${fmt(progress.workers)} 个构造线程${observationFailed?` · 过程采集异常 ${fmt(observationFailed)} 项`:""}`:needsFix?fixCaption:progress.status==="finished"?`任务已执行完，已返回 ${fmt(progress.completed)} / ${fmt(progress.total)}。排样完成以各任务同时合法且排齐为准。`:progress.status==="interrupted"?`本批已中断，已返回 ${fmt(progress.completed)} / ${fmt(progress.total)}；队列尚未执行完，现有单项结果保留。`:"素材索引已汇总；当前没有运行中的批次记录。");
        const generatedDate=data.generatedAt?new Date(data.generatedAt):null;
        const localGenerated=generatedDate&&!Number.isNaN(generatedDate.getTime())?new Intl.DateTimeFormat("zh-CN",{timeZone:"Asia/Shanghai",year:"numeric",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",second:"2-digit",hour12:false}).format(generatedDate):data.generatedAt;
        set("generated-at",data.generatedAt?`目录更新 ${localGenerated}（北京时间）`:"目录尚未生成");
        const archiveDocument={archives:data.archives||[],summary:data.sourceSummary||{},defaults:data.defaults||{}};
        set("archive-json",json(archiveDocument));
        set("connection",data.available?`已连接 · 本轮最终结果 ${fmt(summary.executed)} / ${fmt(summary.tasks)}${summary.constructing?` · 初始解比较中 ${fmt(summary.constructing)} 项`:""}`:"等待素材目录生成");
        return data.revision;
    }
    function tableHead() {
        const headings=state.mode==="tasks"?["任务 / 集合","规格 / 件数","板材 mm","准备状态","本轮结果 / 耗时"]:["DXF / 集中路径","单位","几何状态 / 零件","任务数","原始文件"];
        const row=element("tr");for(const heading of headings)row.append(element("th","",heading));$("list-head").replaceChildren(row);
    }
    function createRow(row) {
        const tr=element("tr","record-row");
        tr._cells=Array.from({length:5},()=>{const cell=element("td");tr.append(cell);return cell;});
        tr._button=element("button","row-title");tr._button.type="button";tr._button.addEventListener("click",()=>selectRecord(tr.dataset.key).catch(showError));
        tr._subtitle=element("span","row-subtitle");tr._cells[0].append(tr._button,tr._subtitle);
        tr.addEventListener("click",event=>{if(!event.target.closest("a,button"))selectRecord(tr.dataset.key).catch(showError);});
        return tr;
    }
    function updateRow(tr,row) {
        tr.classList.toggle("selected",state.selected===row.id);
        tr._button.textContent=row.label||row.name||row.id;
        tr._subtitle.textContent=state.mode==="tasks"?(row.collection||row.id):(row.relativePath||row.id);
        const cells=tr._cells;
        if(state.mode==="tasks") {
            cells[1].textContent=`${fmt(row.types)} 种 / ${fmt(row.required)} 件`;
            cells[2].textContent=stockText(row.stocks,row.sourceStock);
            cells[3].textContent=`${preparationLabel(row)}${row.missingAssets?.length&&row.status!=="source-invalid"?` · 缺 ${row.missingAssets.length} 项输入`:""}${row.assumedUnits?" · 默认单位":""}${row.assumedQuantities?` · ${row.assumedQuantities} 项默认数量`:""}`;
            const benchmark=row.benchmark||{};
            const result=resultView(benchmark,row.required);
            cells[4].textContent=result.text;cells[4].className=`result-cell ${result.tone}`;
        } else {
            cells[1].textContent=unitsText(row.units);
            cells[2].textContent=`${label(row.geometry?.status)} / ${fmt(row.geometry?.partCount)}`;
            cells[3].textContent=fmt(row.taskCount);
            let download=cells[4].firstElementChild;
            if(!download){download=link("下载 DXF",row.downloadUrl);cells[4].append(download);}else download.href=row.downloadUrl;
        }
    }
    function listQuery() {
        return new URLSearchParams({offset:String(state.page*state.size),limit:String(state.size),q:$("search").value,collection:$("collection").value,filter:$("scope").value});
    }
    async function loadList() {
        const request=++state.listRequest, mode=state.mode;
        const data=await get(`/api/materials/${mode}?${listQuery()}`);
        if(request!==state.listRequest||mode!==state.mode)return;
        state.total=data.total;state.rows=data.rows||[];
        const pages=Math.max(1,Math.ceil(state.total/state.size));
        if(state.page>=pages){state.page=pages-1;return loadList();}
        preserveInteraction(()=>keyed($("list-body"),state.rows,row=>row.id,createRow,updateRow));
        $("empty-list").hidden=state.rows.length>0;
        set("list-caption",`共 ${fmt(state.total)} ${state.mode==="tasks"?"个任务":"个 DXF"}`);
        set("page-caption",`${state.page+1} / ${pages}`);
        $("previous").disabled=state.page===0;$("next").disabled=state.page+1>=pages;
        if(!state.selected&&state.rows.length)await selectRecord(state.rows[0].id);
    }
    function issueText(issue) {return typeof issue==="string"?issue:json(issue);}
    function updateIssues(issues) {keyed($("task-issues"),issues,(_,index)=>index,()=>element("div"),(node,issue)=>node.textContent=issueText(issue));}
    function createBomRow() {
        const tr=element("tr");tr._cells=Array.from({length:4},()=>{const cell=element("td");tr.append(cell);return cell;});
        tr._title=element("span");tr._source=element("span","row-subtitle");tr._cells[0].append(tr._title,tr._source);
        return tr;
    }
    function updateTask(data) {
        const task=data.task, assets=new Map((data.assets||[]).map(asset=>[asset.id,asset]));
        set("detail-kind","排样任务");set("detail-title",task.label||task.id);set("detail-id",task.id);set("detail-status",preparationLabel(task));
        $("detail-status").classList.toggle("warn",!['ready','runnable'].includes(task.status));
        set("task-collection",task.collection);set("task-stocks",stockText(task.stocks,task.sourceStock));set("task-rules",rulesText(task.rules));set("task-source-stock",task.sourceStock?textValue(task.sourceStock):"来源未指定，采用统一默认板材");
        set("task-conditions-json",json({inputSource:task.inputSource,missingDxfAssets:task.missingDxfAssets,sourceProvenance:task.sourceProvenance,invalidSourceShapes:task.invalidSourceShapes,sourceStock:task.sourceStock,stocks:task.stocks,rules:task.rules,defaults:data.defaults}));
        updateIssues([...(task.issues||[]),...(task.invalidSourceShapes||[]).map(value=>`原始轮廓校验证据：${textValue(value)}`),...(data.resultError?[data.resultError]:[]),...(task.liveIssue?[task.liveIssue]:[])]);
        const parts=task.sourceQuantityRows||task.sourceParts||task.parts||[], knownQuantity=parts.every(part=>typeof part.quantity==="number"), native=task.parts||[];
        const expanded=parts.some(part=>(part.materialComponentCount||1)>1);
        set("bom-caption",expanded?`${fmt(parts.length)} 种原始来源 · ${knownQuantity?fmt(parts.reduce((total,part)=>total+part.quantity,0)):"待补"} 份；实际材料 ${fmt(native.length)} 种 / ${fmt(task.required)} 件`:`${fmt(parts.length)} 种 · ${knownQuantity?fmt(parts.reduce((total,part)=>total+part.quantity,0)):"数量待补"} 件`);
        keyed($("bom-body"),parts,(part,index)=>`${index}:${part.assetId}:${part.sourcePath}`,createBomRow,(node,part)=>{
            const asset=assets.get(part.assetId), component=part.materialComponentCount>1?` · 每份含 ${fmt(part.materialComponentCount)} 个材料分量`:Number.isInteger(part.componentIndex)&&part.componentCount>1?` · 轮廓 ${part.componentIndex+1}/${part.componentCount}`:"";
            const source=part.geometrySource, raw=source?.kind==="source-contour";
            const originalRef=part.name||part.requestedDxf||part.sourcePath||part.id||part.assetId||"未提供DXF";
            node._title.textContent=(asset?.name||(raw?`原始轮廓 ${source.shapeHash||originalRef}`:originalRef))+component;
            node._source.textContent=[part.sourcePath||part.requestedDxf,raw?`原始轮廓来源：${source.path||"见来源记录"} · shapeHash ${source.shapeHash||"—"} · SHA-256 ${source.sha256||"—"}`:"",raw?unitsText(source.units||{label:"未标单位，按 mm 解释",scaleToMm:1,assumed:true}):"",part.materialComponentCount>1?`原始 DXF 按 ${fmt(part.quantity)} 份导入，实际排 ${fmt(part.nativePieceQuantity)} 件`:'',curveNote(asset?.geometry||part.geometryImport)].filter(Boolean).join("；");node._cells[1].textContent=fmt(part.quantity)+(asset&&expanded?" 份 DXF":"");node._cells[2].textContent=part.quantitySource==="benchmark-default-one"?(asset&&expanded?"来源未指定数量，PK 默认 1 份 DXF":"来源未指定数量，PK 默认 1 件"):textValue(part.quantitySource||"数量来源未声明");node._cells[2].classList.toggle("assumption",part.quantitySource==="benchmark-default-one");
            const cell=node._cells[3];if(asset){let anchor=cell.firstElementChild;if(!anchor){anchor=link("下载 DXF",asset.downloadUrl);cell.replaceChildren(anchor);}else anchor.href=asset.downloadUrl;}else cell.textContent=raw?"DXF待按需导出":"DXF未提供";
        });
        const missingReferences=[...(task.status==="source-invalid"?[]:task.missingAssets||[]).map(value=>({kind:"input",value})),...(task.missingDxfAssets||[]).map(value=>({kind:"dxf",value}))];
        keyed($("missing-assets"),missingReferences,(_,index)=>index,()=>element("div","missing-path"),(node,row)=>node.textContent=`${row.kind==="dxf"?(task.status==="source-invalid"?"原包DXF未提供，原始轮廓需修复后再按需导出":"DXF待按需导出（不阻断原始轮廓排样）"):"输入缺失"}：${textValue(row.value)}`);
        updateBenchmark(task.benchmark||{status:"pending"},task.required);
        let process=$("task-construction");
        if(!process){process=element("p","hint");process.id="task-construction";$("result-caption").after(process);}
        let processLink=process.querySelector("a"),processStatus=process.querySelector("span");
        if(!processLink){processLink=link("查看本任务各策略构造过程","");processStatus=element("span");process.append(processLink,document.createTextNode(" · "),processStatus);}
        processLink.href="/construction.html?materialTask="+encodeURIComponent(task.id);
        process.className=task.benchmark?.constructionTraceError?"notice":"hint";
        processStatus.textContent=task.benchmark?.constructionTraceError?"过程采集异常："+task.benchmark.constructionTraceError:task.constructionTrace?.reason||"本次尚未生成原生过程记录；过程页不会使用其他任务或最终布局补造演化。";
    }
    function updateBenchmark(benchmark, requiredFallback) {
        const waiting=pending(benchmark), binding=["binding-error","stale-input","stale-algorithm"].includes(benchmark.status);
        const result=resultView(benchmark,requiredFallback), actual=result.actual;
        const comparing=benchmark.status==="constructing";
        set("result-caption",waiting?(benchmark.status==="blocked"?"输入条件受限，尚未执行排样。缺素材、参考图或当前不支持的几何均保留在目录中。":"尚未排样。素材准备状态不代表已完成排样；本页只显示实际返回的结果。"):binding?benchmark.reason:result.text+((benchmark.reason||benchmark.error)?`：${benchmark.reason||benchmark.error}`:""));
        $("result-caption").className=`result-caption ${result.tone}`;
        set("result-boards",actual?fmt(benchmark.boards):"—");
        set("result-quantity",actual?result.quantity:"—");
        set("result-remaining",actual&&result.remaining!==null?`${fmt(result.remaining)} 件`:"—");
        const utilization=benchmark.utilization;
        set("result-utilization",actual&&typeof utilization==="number"?`${fmt(utilization*100,2)}%`:"—");
        const roll=actual?benchmark.rollUsage:null;
        $("roll-metrics").hidden=!roll;$("result-roll-note").hidden=!roll;
        set("result-roll-length",roll&&Number.isFinite(roll.totalLength)?`${fmt(roll.totalLength,3)} mm`:"—");
        set("result-roll-utilization",roll&&Number.isFinite(roll.utilization)?`${fmt(roll.utilization*100,2)}%`:"—");
        set("result-roll-lengths",roll?roll.lengths.map((length,index)=>`第${index+1}板 ${fmt(length,3)} mm`).join("；"):"");
        set("result-roll-note",roll?`沿 ${roll.axis.toUpperCase()} 方向，从原坐标0计算至原件末端加原边距；固定幅宽保持原板尺寸，多板逐板计算后累加。${benchmark.complete!==true||benchmark.valid!==true?"当前结果未完整或不合法，以上仅为已排输出测量，不能作为整单成绩。":""}`:"");
        set("result-time",actual&&typeof benchmark.nativeWallSeconds==="number"?`${comparing?(benchmark.timingScope==="live-current"?"已运行 ":"发布时 "):""}${fmt(benchmark.nativeWallSeconds,3)} s`:"—");
        const firstComplete=benchmark.firstCompleteWallSeconds,bestFound=benchmark.bestFoundWallSeconds;
        const hasFirstComplete=Number.isFinite(firstComplete)&&firstComplete>=0;
        const hasBestFound=!comparing&&benchmark.complete===true&&benchmark.valid===true&&
            Number.isFinite(bestFound)&&bestFound>=0&&Number.isFinite(benchmark.nativeWallSeconds)&&bestFound<=benchmark.nativeWallSeconds;
        const hasMilestone=actual&&(hasFirstComplete||hasBestFound||Number.isFinite(benchmark.nativeWallSeconds)||Number.isFinite(benchmark.endToEndWallSeconds));
        $("result-milestones").hidden=!hasMilestone;
        set("result-milestones",hasMilestone?(comparing?`${hasFirstComplete?`首次排齐 ${fmt(firstComplete,3)} 秒（现场发布）`:"未记录首次排齐时间"}；${benchmark.timingScope==="live-current"?"当前已运行":"本候选发布时已运行"} ${fmt(benchmark.nativeWallSeconds,3)} 秒，策略比较中。`:`${hasFirstComplete?`首次排齐 ${fmt(firstComplete,3)} 秒`:"未记录首次排齐时间"}；${hasBestFound?`最佳方案找到 ${fmt(bestFound,3)} 秒（原生现场快照时间）`:"未记录最佳方案找到时间"}；整轮耗时 ${fmt(benchmark.nativeWallSeconds,3)} 秒${Number.isFinite(benchmark.endToEndWallSeconds)?`；进程总耗时 ${fmt(benchmark.endToEndWallSeconds,3)} 秒`:""}。首次排齐与最终布局质量分别判断。`):"");
        updateRemnants(actual?benchmark.remnants:null,actual,benchmark.complete===true&&benchmark.valid===true);
        set("result-check",comparing?"当前初始解已通过原生几何与完整性检查，其他构造策略仍在比较；此处为现场候选，还不是最终选择。":actual?`${benchmark.valid===true?"原生几何检查通过":benchmark.valid===false?"原生几何检查未通过":"几何结论未提供"}。${benchmark.complete===false?"当前未排齐；板数和利用率只对应已排零件，不是完成整单的成绩。":benchmark.valid===false?"当前布局未通过原生几何检查，不能作为有效排样成绩。":"几何合法性与是否排齐分别判断。"}`:waiting?"等待实际算法结果；不会从素材状态或参考布局推断成绩。":"本次记录没有可用的有效排样结论，详情见上方状态。");
        set("result-json",json(benchmark));
        renderLayouts(actual?benchmark:{});
    }
    function updateRemnants(remnants,actual,wholeJob) {
        $("result-remnants").hidden=!actual;
        const measured=remnants?.measured===true;
        const rectangles=measured&&Array.isArray(remnants.perLayout)?remnants.perLayout.map((value,index)=>({...value,index})).filter(value=>value.verified===true&&value.area>0&&[value.bounds?.x,value.bounds?.y,value.bounds?.width,value.bounds?.height].every(Number.isFinite)&&value.bounds.width>0&&value.bounds.height>0):[];
        set("result-remnant-status",!measured?"本候选尚未评估规则余料。":rectangles.length?`已评估，以下为通过原生间距与材料边界检查的矩形余料。${wholeJob?"":"当前整单未完成，余料仅对应已排输出。"}`:"已评估，未找到通过原生认证的规则矩形余料。");
        keyed($("result-remnant-rectangles"),rectangles,value=>value.index,()=>element("p"),(node,value)=>{
            const bounds=value.bounds;
            node.textContent=`第${value.index+1}板：${fmt(bounds.width,3)} × ${fmt(bounds.height,3)} mm；位置 (${fmt(bounds.x,3)}, ${fmt(bounds.y,3)}) mm；面积 ${fmt(value.area,3)} mm²。`;
        });
    }
    function svgElement(name, attributes={}) {const node=document.createElementNS("http://www.w3.org/2000/svg",name);for(const [key,value] of Object.entries(attributes))node.setAttribute(key,String(value));return node;}
    function preserveInteraction(update) {
        const active=document.activeElement;
        const selection=active instanceof HTMLInputElement&&typeof active.selectionStart==="number"?[active.selectionStart,active.selectionEnd,active.selectionDirection]:null;
        const positions=Array.from(document.querySelectorAll("#list-scroll,#detail-scroll,.bom-scroll,.board-scroll"),node=>({node,key:node.dataset.boardKey,x:node.scrollLeft,y:node.scrollTop}));
        const pageX=window.scrollX,pageY=window.scrollY;
        update();
        if(active?.isConnected&&document.activeElement!==active)active.focus({preventScroll:true});
        if(selection&&active.isConnected)active.setSelectionRange(...selection);
        for(const position of positions){const node=position.node.isConnected?position.node:Array.from(document.querySelectorAll(".board-scroll")).find(value=>value.dataset.boardKey===position.key);if(node){node.scrollLeft=position.x;node.scrollTop=position.y;}}
        if(window.scrollX!==pageX||window.scrollY!==pageY)window.scrollTo(pageX,pageY);
    }
    const {placementPose,occupiedBounds,displayViewport}=globalThis.PlaneLayoutGeometry;
    function layoutViewport(layout, originals) {
        return displayViewport(layout,occupiedBounds(layout,originals),state.layoutMode);
    }
    function renderLayouts(benchmark) {
        state.lastBenchmark=benchmark;
        const signature=JSON.stringify([benchmark.layouts,benchmark.problem,state.layoutMode,state.layoutZoom]);
        if(state.drawingSignature===signature)return;state.drawingSignature=signature;
        const root=$("result-layouts");root.replaceChildren();
        const layouts=benchmark.layouts||[];
        $("layout-controls").hidden=true;
        if(!layouts.length)return;
        const originals=new Map((benchmark.problem?.parts||[]).map(part=>[part.id||part.ID,part]));
        if(!originals.size){root.append(element("p","layout-note",`已记录 ${layouts.length} 块板的布局字段；当前记录未提供可复核原轮廓，图形待原生完整数据到达。`));return;}
        const viewports=layouts.map(layout=>layoutViewport(layout,originals));
        const commonScale=Math.min(1,550/Math.max(...viewports.map(view=>view.width||1)),600/Math.max(...viewports.map(view=>view.height||1)))*state.layoutZoom/100;
        $("layout-controls").hidden=false;$("layout-view-mode").value=state.layoutMode;$("layout-zoom").value=String(state.layoutZoom);set("layout-zoom-value",`${state.layoutZoom}%`);
        layouts.forEach((layout,index)=>{
            const width=layout.width, height=layout.height;
            if(!(width>0&&height>0))return;
            const view=viewports[index];
            const card=element("div","board-card");card.append(element("div","board-caption",`第 ${index+1} 板 · 原板 ${fmt(width,3)} × ${fmt(height,3)} mm · ${fmt(layout.placements?.length||0)} 件`));
            card.append(element("p","board-view-note",view.used?`已用区域加空白：X ${fmt(view.x,3)}–${fmt(view.x+view.width,3)}，Y ${fmt(view.y,3)}–${fmt(view.y+view.height,3)} mm。仅省略显示范围之外的空板；原板高度仍为 ${fmt(height,3)} mm，零件坐标不变。`:"显示整板，所有板保持相同物理比例。长板可切换“已用区域”查看零件。"));
            const scroll=element("div","board-scroll");scroll.dataset.boardKey=`${layout.stock||index}:${layout.instance??index}`;
            const svg=svgElement("svg",{viewBox:`${view.x} ${height-view.y-view.height} ${view.width} ${view.height}`,width:view.width*commonScale,height:view.height*commonScale,role:"img","aria-label":`第 ${index+1} 板真实排样结果`,"data-view-mode":view.used?"used":"full","data-pixels-per-mm":commonScale});
            const group=svgElement("g",{transform:`translate(0 ${height}) scale(1 -1)`});
            group.append(svgElement("rect",{x:0,y:0,width,height,fill:"white",stroke:"#8c9caf","stroke-width":1,"vector-effect":"non-scaling-stroke"}));
            (layout.placements||[]).forEach((placement,partIndex)=>{
                const part=originals.get(placement.part||placement.partId||placement.partID), geometry=part?.geometry;
                if(!geometry)return;
                const {x,y,angle,mirrored}=placementPose(placement);
                if(![x,y,angle].every(Number.isFinite))return;
                const attributes={transform:`translate(${x} ${y}) rotate(${angle}) scale(1 ${mirrored?-1:1})`,"data-mirrored":mirrored,fill:`hsl(${(partIndex*47)%360} 43% 80%)`,stroke:"#496580","stroke-width":.8,"vector-effect":"non-scaling-stroke","fill-rule":"evenodd"};
                let shape;
                if(geometry.radius>0)shape=svgElement("circle",{...attributes,cx:geometry.center?.[0]||0,cy:geometry.center?.[1]||0,r:geometry.radius});
                else {const rings=[geometry.outer,...(geometry.holes||[])].filter(ring=>Array.isArray(ring)&&ring.length>=3);const d=rings.map(ring=>`M ${ring.map(point=>`${point[0]} ${point[1]}`).join(" L ")} Z`).join(" ");shape=svgElement("path",{...attributes,d});}
                const title=svgElement("title");title.textContent=`${part.id||part.ID} / ${placement.instance??""}${mirrored?" · 镜像":""}`;shape.append(title);group.append(shape);
            });svg.append(group);scroll.append(svg);card.append(scroll);root.append(card);
        });
    }
    function updateAsset(data) {
        const asset=data.asset;
        set("detail-kind","原始 DXF");set("detail-title",asset.name||asset.id);set("detail-id",asset.id);set("detail-status",label(asset.geometry?.status));$("detail-status").classList.toggle("warn",asset.geometry?.status!=="ready");
        $("asset-download").href=asset.downloadUrl;set("asset-path",asset.relativePath);set("asset-units",unitsText(asset.units));set("asset-geometry",`${label(asset.geometry?.status)} · ${fmt(asset.geometry?.partCount)} 个零件${asset.geometry?.reason?`；${textValue(asset.geometry.reason)}`:""}`);
        if(curveNote(asset.geometry))set("asset-geometry",$("asset-geometry").textContent+`；${curveNote(asset.geometry)}`);
        set("asset-hash",asset.sha256);set("asset-sources",(asset.sourcePaths||[]).map(textValue).join("\n"));set("asset-json",json(asset));
        if(!$("asset-tasks").children.length)$("asset-tasks").textContent="";
        keyed($("asset-tasks"),data.tasks||[],task=>task.id,()=>{const button=element("button");button.type="button";button.addEventListener("click",()=>{setMode("tasks",false);selectRecord(button.dataset.key).catch(showError);loadList().catch(showError);});return button;},(node,task)=>node.textContent=`${task.label||task.id} · ${label(task.status)}`);
        if(!data.tasks?.length)$("asset-tasks").textContent="尚无任务关联；原文件仍保留在总目录。";
    }
    async function selectRecord(id) {
        state.selected=id;state.drawingSignature=null;
        for(const row of $("list-body").children)row.classList.toggle("selected",row.dataset.key===id);
        await loadDetail();
    }
    async function loadDetail() {
        if(!state.selected)return;
        const id=state.selected,mode=state.mode,request=++state.detailRequest;
        const data=await get(`/api/materials/${mode}/${encodeURIComponent(id)}`);
        if(request!==state.detailRequest||id!==state.selected||mode!==state.mode)return;
        $("detail-placeholder").hidden=true;$("task-detail").hidden=mode!=="tasks";$("asset-detail").hidden=mode!=="assets";
        preserveInteraction(()=>{if(mode==="tasks")updateTask(data);else updateAsset(data);});
    }
    function showError(error) {$("error").hidden=false;set("error",`${error.message||error}。已显示的数据继续保留。`);set("connection","读取暂时中断");}
    async function refresh(force=false) {
        if(state.refreshing)return;state.refreshing=true;
        try{const summary=await get("/api/materials/summary"), revision=updateSummary(summary);if(force||revision!==state.revision){state.revision=revision;await loadList();await loadDetail();}$("error").hidden=true;}
        catch(error){showError(error);}finally{state.refreshing=false;}
    }
    function setMode(mode,load=true) {
        if(state.mode===mode)return;state.mode=mode;state.page=0;state.selected=null;state.detailRequest++;state.drawingSignature=null;
        $("tasks-tab").setAttribute("aria-pressed",String(mode==="tasks"));$("assets-tab").setAttribute("aria-pressed",String(mode==="assets"));
        $("task-detail").hidden=true;$("asset-detail").hidden=true;$("detail-placeholder").hidden=false;tableHead();
        if(load)loadList().catch(showError);
    }
    $("tasks-tab").addEventListener("click",()=>setMode("tasks"));$("assets-tab").addEventListener("click",()=>setMode("assets"));
    let searchTimer;$("search").addEventListener("input",()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>{state.page=0;loadList().catch(showError);},180);});
    for(const id of ["collection","scope"])$(id).addEventListener("change",()=>{state.page=0;loadList().catch(showError);});
    $("page-size").addEventListener("change",()=>{state.size=Number($("page-size").value);state.page=0;loadList().catch(showError);});
    $("previous").addEventListener("click",()=>{state.page=Math.max(0,state.page-1);loadList().catch(showError);});
    $("next").addEventListener("click",()=>{state.page++;loadList().catch(showError);});
    $("refresh").addEventListener("click",()=>refresh(true));
    for(const [value,text] of [["source-contours","原始轮廓可执行"],["missing-dxf","DXF待按需导出"],["source-invalid","原始轮廓无效"]]){const option=element("option","",text);option.value=value;$("scope").append(option);}
    $("layout-view-mode").addEventListener("change",()=>{state.layoutMode=$("layout-view-mode").value;preserveInteraction(()=>renderLayouts(state.lastBenchmark));});
    $("layout-zoom").addEventListener("input",()=>{state.layoutZoom=Number($("layout-zoom").value);preserveInteraction(()=>renderLayouts(state.lastBenchmark));});
    tableHead();refresh(true);setInterval(()=>{if($("auto-refresh").checked)refresh();},4000);
})();
