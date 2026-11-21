(() => {
    "use strict";

    const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
    const STAGE_NAMES = {
        begin: "输入", analysis: "特征分析", match: "配对", array: "阵列",
        candidate: "候选试放", expand: "展开状态", backtrack: "回退",
        complete: "完整候选", selected: "选中结果", stopped: "停止记录"
    };
    const GROUP_NAMES = { input: "输入", analysis: "特征分析", match: "配对", array: "阵列", search: "搜索", result: "结果" };
    const REASONS = {
        "independent-single-sheet-construction": "从空白单板开始独立构造。",
        "retained-original-material-safe-candidate": "本次试放通过原件几何安全检查，已保留为搜索候选。",
        "nominal-group-pair-guard": "组内原件未通过几何安全检查，本次试放被拒绝。",
        "nominal-existing-pair-guard": "与已放原件的几何安全检查未通过，本次试放被拒绝。",
        "original-sheet-guard": "原始板界检查未通过，本次试放被拒绝。",
        "original-result-validated": "该完整候选已通过原始问题校验。",
        "selected-search-state-for-expansion": "搜索选择此状态继续展开。",
        "complete-candidate-limit": "完整候选数量已达到本轮上限。",
        "retained-children-exhausted": "此状态保留的后继候选已遍历完成。",
        "shared-quality-selected-original-result": "共享质量比较选中了这份原件排样结果。",
        "independent-single-piece-greedy-construction": "从空白单板开始逐件贪心排样，每次确认放置一个原始零件。",
        "pair-features-propose-single-piece-contacts": "记录两个原件的接触参照，用于计算下一个单件的相对位置与角度。",
        "single-piece-candidate-safe": "这个单件位姿通过了原件几何安全检查，等待本步候选比较。",
        "original-inside-sheet-guard": "这个单件位姿未通过原始板界安全检查。",
        "single-piece-existing-material-conflict": "这个单件位姿与已放原件的安全检查未通过。",
        "greedy-committed-one-original-piece": "本步已确认放置一个原始零件，原件保持独立。",
        "greedy-committed-with-next-fit-unresolved": "本步已确认放置一个原始零件。下一待排件的可放性仍待后续查询。",
        "greedy-kept-immediate-choice-after-lookahead-no-fit": "本步已确认放置一个原始零件，采用当前评分最高的安全位姿。有限前瞻查询未找到下一待排件的可用位置。",
        "greedy-next-required-part-no-fit": "本次前瞻查询未找到下一待排原件的合法位姿。",
        "greedy-selected-original-single-piece-result": "记录逐件贪心排样的实际原件终态。",
        "greedy-no-more-feasible-single-piece-candidates": "本轮已遍历的候选中，继续放置单件的位置已耗尽。",
        "search-budget": "本轮时间或尝试预算已用完。",
        "memory-limit": "本轮保留内存预算已用完。",
        "cancelled": "本轮排样已收到取消请求。"
    };
    const model = {
        bundle: null, trace: null, events: [], index: 0, timer: null,
        nodes: new Map(), stateCache: new Map(), analysis: new Map(), stateEvents: new Map(),
        listPage: 0, focusedMember: null, finalView: false, analysisDisplay: null, greedy: false,
        playbackMode: "events", playbackIndices: []
    };
    const byId = id => document.getElementById(id);

    function groupOf(stage) {
        if (stage === "begin") return "input";
        if (["analysis", "match", "array"].includes(stage)) return stage;
        if (["selected", "stopped"].includes(stage)) return "result";
        return "search";
    }

    function stageName(stage) {
        if (model.greedy && stage === "match") return "接触参照";
        if (model.greedy && stage === "expand") return "放置确认";
        return STAGE_NAMES[stage] || stage;
    }

    function number(value, digits = 4) {
        return Number.isFinite(value) ? value.toLocaleString("zh-CN", { maximumFractionDigits: digits }) : "—";
    }

    function measuredArea(value) {
        return Number.isFinite(value) && value >= 0 ? number(value) : "未测量";
    }

    function point(value) {
        if (!Array.isArray(value) || value.length !== 2 || !value.every(Number.isFinite)) {
            throw new Error("记录含有无效二维坐标。");
        }
        return value;
    }

    function memberKey(member, index) {
        return `${member.partIndex}/${member.instance === null ? `proposal-${index}` : member.instance}`;
    }

    function reasonText(event) {
        if (REASONS[event.reason]) return REASONS[event.reason];
        if (event.stage === "analysis" && event.reason?.startsWith("carriers=")) {
            return "已记录该原件的基体、凹特征、凸特征及接口分析。";
        }
        if (!event.reason && event.stage === "match") return "已记录本次配对候选的原始成员与位姿。";
        if (!event.reason && event.stage === "array") return "已记录本次真实生成的周期阵列候选。";
        return event.reason || "此事件未附带原因文字。";
    }

    function applyDisplayDefault(event) {
        const analysis = event.stage === "analysis";
        if (model.analysisDisplay === analysis) return;
        for (const id of ["show-carrier", "show-recess", "show-protrusion", "show-mouths"]) {
            byId(id).checked = analysis;
        }
        model.analysisDisplay = analysis;
    }

    function pause() {
        if (model.timer !== null) clearInterval(model.timer);
        model.timer = null;
        byId("play").textContent = "播放";
    }

    function configurePlayback(mode) {
        model.playbackMode = model.greedy ? mode : "events";
        model.playbackIndices = model.events.flatMap((event, index) =>
            model.playbackMode === "events" || event.stage === "begin" || event.stage === "expand" ? [index] : []);
        byId("playback-mode-control").hidden = !model.greedy;
        byId("playback-mode").value = model.playbackMode;
        byId("timeline").max = String(Math.max(0, model.playbackIndices.length - 1));
        byId("timeline").disabled = model.playbackIndices.length === 0;
    }

    function navigate(delta) {
        const position = model.playbackIndices.indexOf(model.index);
        const index = model.playbackIndices[position + delta];
        if (index !== undefined) go(index);
    }

    function showError(error) {
        pause();
        byId("error").hidden = false;
        byId("error").textContent = error instanceof Error ? error.message : String(error);
    }

    function loadDocument(documentValue, label) {
        pause();
        const bundle = documentValue.trace ? documentValue : { trace: documentValue };
        const trace = bundle.trace;
        if (trace.schemaVersion !== 1 || !Array.isArray(trace.events) || !Array.isArray(trace.problem?.parts)) {
            throw new Error("需要schemaVersion为1的原生构造过程文件。");
        }
        model.bundle = bundle;
        model.trace = trace;
        model.events = trace.events;
        model.greedy = trace.events.some(event => event.reason === "independent-single-piece-greedy-construction");
        model.index = 0;
        model.finalView = false;
        model.focusedMember = null;
        model.analysisDisplay = null;
        model.nodes.clear(); model.stateCache.clear(); model.analysis.clear(); model.stateEvents.clear();
        model.nodes.set(1, { parentStateID: 0, members: [], full: true });
        model.events.forEach((event, index) => {
            if (event.stage === "analysis" && Number.isInteger(event.partIndex)) model.analysis.set(event.partIndex, event);
            if (event.stage === "candidate" && event.accepted && event.stateID) {
                model.nodes.set(event.stateID, { ...event, full: false });
                if (!model.stateEvents.has(event.stateID)) model.stateEvents.set(event.stateID, index);
            }
            if (event.stage === "selected" && event.stateID) {
                model.nodes.set(event.stateID, { ...event, full: true });
                if (!model.stateEvents.has(event.stateID)) model.stateEvents.set(event.stateID, index);
            }
        });
        byId("error").hidden = true;
        configurePlayback(model.greedy ? "placements" : "events");
        byId("final-result").disabled = !bundle.result && !model.events.some(event => event.stage === "selected");
        const demand = trace.problem.parts.reduce((total, part) => total + part.quantity, 0);
        const seconds = bundle.meta?.requestedSeconds ?? bundle.result?.secondsPerConstructor;
        const mode = model.greedy ? " · 逐件贪心 · 每次确认1件 · 原件独立" : "";
        byId("run-summary").textContent = `${label || "原生过程"} · ${trace.problem.parts.length}种 / ${demand}件${mode} · 过程记录已开启${Number.isFinite(seconds) ? ` · 演示构造预算${seconds}秒` : ""}`;
        const meta = trace.meta || {};
        const notices = [];
        if (meta.truncated || meta.coreTruncated || meta.eventLimited || meta.byteLimited) {
            notices.push(`过程已截断，记录${model.events.length}条，丢弃${meta.eventsDropped ?? "未记录"}条。终态若携带完整Members可单独查看。`);
        }
        if (meta.observerFailed) notices.push("原生观察回调报告异常，界面显示已实际写入的记录。");
        if (trace.rotatedFrame) notices.push("本记录使用原生旋转工作坐标系。");
        if (!bundle.audit) notices.push("当前文件尚未附带独立输入与原材审计。");
        byId("notice").hidden = notices.length === 0;
        byId("notice").textContent = notices.join(" ");
        byId("evidence-json").textContent = JSON.stringify({
            schemaVersion: trace.schemaVersion, caps: trace.caps, meta: trace.meta,
            resultFile: trace.resultFile, generated: bundle.meta, audit: bundle.audit
        }, null, 2);
        const stages = byId("stages"); stages.replaceChildren();
        for (const [group, name] of Object.entries(GROUP_NAMES)) {
            const count = model.events.filter(event => groupOf(event.stage) === group).length;
            const button = document.createElement("button");
            const groupName = model.greedy && group === "match" ? "接触参照" : name;
            button.textContent = `${groupName} ${count}`;
            button.dataset.group = group;
            button.disabled = count === 0;
            button.addEventListener("click", () => go(model.events.findIndex(event => groupOf(event.stage) === group)));
            stages.append(button);
        }
        if (model.events.length) {
            applyDisplayDefault(model.events[0]);
            render();
        }
        else {
            byId("step-label").textContent = "文件未记录事件";
            byId("event-list").textContent = "没有可播放的过程事件。";
        }
    }

    function stateAt(stateID) {
        if (!stateID) return { members: [], warning: "此事件未携带可恢复的状态编号。" };
        if (model.stateCache.has(stateID)) return model.stateCache.get(stateID);
        const chain = [], visited = new Set();
        let cursor = stateID;
        let result = { members: [], warning: "" };
        while (cursor) {
            if (model.stateCache.has(cursor)) { result = model.stateCache.get(cursor); break; }
            if (visited.has(cursor)) { result = { members: [], warning: "记录中的父状态存在循环。" }; break; }
            visited.add(cursor);
            const node = model.nodes.get(cursor);
            if (!node) { result = { members: [], warning: `父状态${cursor}未被记录，当前图只含可恢复的成员。` }; break; }
            chain.push(node);
            if (node.full) { result = { members: [], warning: "" }; break; }
            cursor = node.parentStateID;
        }
        for (const node of chain.reverse()) {
            result = {
                ...result, ...node,
                members: node.full ? [...(node.members || [])] : [...result.members, ...(node.members || [])],
                warning: result.warning
            };
            if (node.stateID) model.stateCache.set(node.stateID, result);
        }
        model.stateCache.set(stateID, result);
        return result;
    }

    function selectedRawScene() {
        const raw = model.bundle.result;
        const result = raw?.runs?.[0]?.selected;
        if (!result) return null;
        const board = result.layouts?.[0];
        const parts = model.trace.problem.parts;
        return {
            members: board ? board.placements.map(pose => ({
                partIndex: parts.findIndex(part => part.id === pose.part), instance: pose.instance,
                pose: { x: pose.x, y: pose.y, angle: pose.angle }, group: pose.group
            })) : [],
            proposed: [], boardWidth: board?.width || model.trace.problem.stocks[0]?.width,
            boardHeight: board?.height || model.trace.problem.stocks[0]?.height,
            warning: "独立最终输出文件视图，时间线仍保留实际记录的截止位置。", result
        };
    }

    function sceneFor(event) {
        if (model.finalView) return selectedRawScene();
        if (event.stage === "analysis") {
            return { members: [{ partIndex: event.partIndex, instance: null, pose: { x: 0, y: 0, angle: 0 } }],
                proposed: [], local: true,
                warning: event.analysisTruncated ? "当前原件的特征分析未完整覆盖，图中显示已保留的分析内容。过程事件是否丢失另由记录限额判断。" : "" };
        }
        if (["match", "array"].includes(event.stage)) {
            return { members: event.members || [], proposed: [], local: true,
                boardWidth: event.width, boardHeight: event.height, warning: "" };
        }
        if (event.stage === "selected") {
            return { ...event, members: event.members || [], proposed: [], warning: "" };
        }
        if (event.stage === "candidate" && !event.accepted) {
            return { ...stateAt(event.parentStateID), ...event,
                members: stateAt(event.parentStateID).members, proposed: event.members || [],
                warning: stateAt(event.parentStateID).warning };
        }
        const state = stateAt(event.stateID);
        return { ...state, ...event, members: state.members, proposed: [], warning: state.warning };
    }

    function svgElement(name, attributes = {}) {
        const node = document.createElementNS(SVG_NAMESPACE, name);
        for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
        return node;
    }

    function pathNode(rings, attributes) {
        const commands = rings.filter(ring => ring.length).map(ring =>
            ring.map((value, index) => `${index ? "L" : "M"}${point(value)[0]} ${point(value)[1]}`).join(" ") + " Z").join(" ");
        return svgElement("path", { d: commands, "fill-rule": "evenodd", "vector-effect": "non-scaling-stroke", ...attributes });
    }

    function geometryNode(geometry, attributes) {
        if (geometry.radius > 0) {
            const center = point(geometry.center);
            return svgElement("circle", { cx: center[0], cy: center[1], r: geometry.radius,
                "vector-effect": "non-scaling-stroke", ...attributes });
        }
        return pathNode(geometry.rings || [geometry.outer || [], ...(geometry.holes || [])], attributes);
    }

    function worldPoint(local, pose) {
        const angle = pose.angle * Math.PI / 180;
        const quarter = { 0: [1,0], 90: [0,1], 180: [-1,0], 270: [0,-1] }[((pose.angle % 360) + 360) % 360];
        const cosine = quarter ? quarter[0] : Math.cos(angle);
        const sine = quarter ? quarter[1] : Math.sin(angle);
        return [local[0] * cosine - local[1] * sine + pose.x, local[0] * sine + local[1] * cosine + pose.y];
    }

    function measureMembers(members) {
        const points = [];
        let materialArea = 0;
        let hasCircle = false;
        const ringArea = ring => {
            if (!ring.length) return 0;
            const origin = ring[0];
            let twiceArea = 0;
            for (let index = 0; index < ring.length; index++) {
                const a = ring[index], b = ring[(index + 1) % ring.length];
                twiceArea += (a[0] - origin[0]) * (b[1] - origin[1]) - (a[1] - origin[1]) * (b[0] - origin[0]);
            }
            return Math.abs(twiceArea) / 2;
        };
        for (const member of members) {
            const geometry = model.trace.problem.parts[member.partIndex]?.geometry;
            if (!geometry) continue;
            if (geometry.radius > 0) {
                hasCircle = true;
                materialArea += Math.PI * geometry.radius * geometry.radius;
            } else {
                materialArea += ringArea(geometry.outer) - geometry.holes.reduce((sum,ring) => sum + ringArea(ring),0);
                for (const value of geometry.outer) points.push(worldPoint(value,member.pose));
            }
        }
        if (hasCircle || points.length < 3) return { materialArea, obbArea: null, density: null };
        points.sort((a,b) => a[0] - b[0] || a[1] - b[1]);
        const unique = points.filter((value,index) => !index || value[0] !== points[index-1][0] || value[1] !== points[index-1][1]);
        const cross = (o,a,b) => (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0]);
        const lower = [], upper = [];
        for (const value of unique) {
            while (lower.length > 1 && cross(lower[lower.length-2],lower[lower.length-1],value) <= 0) lower.pop();
            lower.push(value);
        }
        for (const value of [...unique].reverse()) {
            while (upper.length > 1 && cross(upper[upper.length-2],upper[upper.length-1],value) <= 0) upper.pop();
            upper.push(value);
        }
        const hull = [...lower.slice(0,-1),...upper.slice(0,-1)];
        let obbArea = Infinity;
        for (let index = 0; index < hull.length; index++) {
            const a = hull[index], b = hull[(index+1)%hull.length];
            const length = Math.hypot(b[0]-a[0],b[1]-a[1]);
            if (!length) continue;
            const ux = (b[0]-a[0])/length, uy = (b[1]-a[1])/length;
            let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
            for (const value of hull) {
                const x = (value[0]-a[0])*ux + (value[1]-a[1])*uy;
                const y = -(value[0]-a[0])*uy + (value[1]-a[1])*ux;
                minX = Math.min(minX,x); maxX = Math.max(maxX,x);
                minY = Math.min(minY,y); maxY = Math.max(maxY,y);
            }
            obbArea = Math.min(obbArea,(maxX-minX)*(maxY-minY));
        }
        return { materialArea, obbArea, density: obbArea > 0 ? materialArea/obbArea : null };
    }

    function updateCarrierChoices(event, scene) {
        const first = scene.members.find((member, index) => !model.focusedMember || memberKey(member, index) === model.focusedMember);
        const partIndex = event.stage === "analysis" ? event.partIndex : first?.partIndex;
        const analysis = model.analysis.get(partIndex);
        const carriers = new Map();
        for (const geometry of analysis?.geometries || []) {
            if (geometry.role === "carrier") carriers.set(geometry.carrierIndex, geometry.carrierKind);
        }
        const select = byId("carrier-choice");
        const signature = JSON.stringify([...carriers]);
        if (select.dataset.signature !== signature) {
            const previous = select.value;
            select.replaceChildren();
            for (const [index, kind] of carriers) {
                const option = document.createElement("option"); option.value = String(index);
                option.textContent = `${index} · ${{"convex-hull":"凸包", "outer-rectangle":"外矩形", "core-rectangle":"核心条带"}[kind] || kind}`;
                select.append(option);
            }
            if (!carriers.size) { const option = document.createElement("option"); option.value = "0"; option.textContent = "未记录基体"; select.append(option); }
            if ([...select.options].some(option => option.value === previous)) select.value = previous;
            select.dataset.signature = signature;
        }
    }

    function draw(event, scene) {
        const canvas = byId("canvas"); canvas.replaceChildren();
        if (!scene) { canvas.textContent = "最终输出尚未载入。"; return; }
        updateCarrierChoices(event, scene);
        const svg = svgElement("svg", { role: "img", "aria-label": "原生几何与真实位姿" });
        const layer = svgElement("g", { transform: "scale(1,-1)" }); svg.append(layer);
        const bounds = [];
        const frameWidth = scene.boardWidth || model.trace.problem.stocks[0]?.width;
        const frameHeight = scene.boardHeight || model.trace.problem.stocks[0]?.height;
        if ((!scene.local || event.stage !== "analysis") && frameWidth > 0 && frameHeight > 0) {
            layer.append(svgElement("rect", { x: 0, y: 0, width: frameWidth, height: frameHeight,
                fill: "#fff", stroke: "#7d91a7", "stroke-width": 1, "vector-effect": "non-scaling-stroke" }));
            bounds.push([0,0], [frameWidth,frameHeight]);
        }
        const carrierIndex = Number(byId("carrier-choice").value);
        const paintMember = (member, index, rejected) => {
            const part = model.trace.problem.parts[member.partIndex];
            if (!part || !member.pose) return;
            const pose = member.pose;
            if (![pose.x, pose.y, pose.angle].every(Number.isFinite)) throw new Error("成员位姿含无效数值。");
            const group = svgElement("g", { transform: `translate(${pose.x} ${pose.y}) rotate(${pose.angle})` });
            const key = memberKey(member, index);
            const focused = model.focusedMember === key;
            const overlays = scene.local || focused;
            const color = `hsl(${(member.partIndex * 137.508) % 360} 49% 66%)`;
            const source = part.geometry;
            if (source.radius > 0) {
                const center = point(source.center);
                for (const x of [-1,1]) for (const y of [-1,1]) bounds.push(worldPoint([center[0]+x*source.radius,center[1]+y*source.radius],pose));
            } else for (const value of source.outer) bounds.push(worldPoint(point(value), pose));
            if (byId("show-original").checked) {
                const shape = geometryNode(source, { fill: rejected ? "#e99b91" : color,
                    "fill-opacity": rejected ? .32 : .82, stroke: rejected ? "#b54237" : "#344c65",
                    "stroke-width": focused ? 2 : .8, "stroke-dasharray": rejected ? "5 3" : "none", class: "source-material" });
                const title = svgElement("title"); title.textContent = `${part.id}${member.instance === null ? " · 尚未分配实例" : ` #${member.instance}`} · ${number(pose.angle)}°`;
                shape.append(title);
                shape.addEventListener("click", () => { model.focusedMember = focused ? null : key; renderDrawing(); });
                group.append(shape);
            }
            if (overlays) {
                const analysis = model.analysis.get(member.partIndex);
                for (const geometry of analysis?.geometries || []) {
                    if (geometry.role === "original" || geometry.carrierIndex !== carrierIndex) continue;
                    const visible = {
                        carrier: byId("show-carrier").checked,
                        recess: byId("show-recess").checked,
                        protrusion: byId("show-protrusion").checked
                    }[geometry.role];
                    if (!visible) continue;
                    const styles = {
                        carrier: { fill: "none", stroke: "#697b8b", "stroke-dasharray": "5 3", "stroke-width": 1.3 },
                        recess: { fill: "#ed9a40", "fill-opacity": .35, stroke: "#b96c19", "stroke-width": .8 },
                        protrusion: { fill: "#45a590", "fill-opacity": .35, stroke: "#247e6b", "stroke-width": .8 }
                    }[geometry.role];
                    group.append(geometryNode(geometry, styles));
                    for (const ring of geometry.rings || []) for (const value of ring) bounds.push(worldPoint(point(value),pose));
                }
                if (byId("show-mouths").checked) for (const mouth of analysis?.mouths || []) {
                    if (mouth.carrierIndex !== carrierIndex) continue;
                    const start = point(mouth.start), end = point(mouth.end), into = point(mouth.into);
                    group.append(svgElement("line", { x1:start[0],y1:start[1],x2:end[0],y2:end[1],
                        stroke:"#6545a3","stroke-width":2,"vector-effect":"non-scaling-stroke" }));
                    const middle = [(start[0]+end[0])/2,(start[1]+end[1])/2];
                    const length = Math.min(mouth.depth || mouth.width / 5, mouth.width / 3);
                    group.append(svgElement("line", { x1:middle[0],y1:middle[1],x2:middle[0]+into[0]*length,y2:middle[1]+into[1]*length,
                        stroke:"#6545a3","stroke-width":1.2,"vector-effect":"non-scaling-stroke" }));
                }
            }
            layer.append(group);
        };
        scene.members.forEach((member,index) => paintMember(member,index,false));
        (scene.proposed || []).forEach((member,index) => paintMember(member,index,true));
        if (!bounds.length) bounds.push([0,0], [frameWidth || 10,frameHeight || 10]);
        let left = Infinity, bottom = Infinity, right = -Infinity, top = -Infinity;
        for (const [x,y] of bounds) {
            left = Math.min(left,x);
            right = Math.max(right,x);
            bottom = Math.min(bottom,y);
            top = Math.max(top,y);
        }
        const padding = Math.max(right-left,top-bottom,1) * .055;
        svg.setAttribute("viewBox", `${left-padding} ${-top-padding} ${right-left+padding*2} ${top-bottom+padding*2}`);
        canvas.append(svg);
        byId("state-warning").hidden = !scene.warning;
        byId("state-warning").textContent = scene.warning || "";
    }

    function eventTitle(event) {
        if (event.stage === "analysis") {
            const suffix = event.analysisTruncated ? " · 分析受限" : "";
            return `${model.trace.problem.parts[event.partIndex]?.id || event.partIndex} · 特征分析${suffix}`;
        }
        if (["match","array"].includes(event.stage)) return `${stageName(event.stage)} #${event.proposalID} · ${(event.members || []).length}件`;
        if (event.stage === "candidate") return `试放 #${event.candidateID} · ${event.accepted ? "通过守门" : "拒绝"}`;
        return `${stageName(event.stage)} · 状态${event.stateID || "—"}`;
    }

    function renderList(event) {
        const group = groupOf(event.stage);
        const query = byId("event-search").value.trim().toLowerCase();
        byId("rejected-only").disabled = group !== "search";
        const rejectedOnly = group === "search" && byId("rejected-only").checked;
        const candidates = [];
        model.events.forEach((item,index) => {
            if (groupOf(item.stage) !== group) return;
            if (rejectedOnly && !(item.stage === "candidate" && !item.accepted)) return;
            if (query && !`${eventTitle(item)} ${item.reason || ""} ${item.proposalID} ${item.stateID}`.toLowerCase().includes(query)) return;
            candidates.push(index);
        });
        const selectedPosition = candidates.indexOf(model.index);
        if (selectedPosition >= 0) model.listPage = Math.floor(selectedPosition / 100);
        const page = Math.min(model.listPage,Math.max(0,Math.ceil(candidates.length/100)-1));
        const visible = candidates.slice(page*100,page*100+100);
        const list = byId("event-list"); const previousScroll = list.scrollTop; list.replaceChildren();
        for (const index of visible) {
            const item = model.events[index]; const button = document.createElement("button");
            button.className = `event-row${index===model.index ? " active" : ""}${item.stage==="candidate" ? item.accepted ? " accepted" : " rejected" : ""}`;
            button.setAttribute("aria-current",index===model.index ? "step" : "false");
            button.textContent = `${item.step} · ${eventTitle(item)}`;
            const detail = document.createElement("small");
            detail.textContent = ["match","array"].includes(item.stage)
                ? `OBB节省 ${number(item.saving)} · 利用率 ${number(item.fill*100,2)}%`
                : reasonText(item);
            button.append(detail); button.addEventListener("click",() => go(index)); list.append(button);
        }
        if (!visible.length) { const text = document.createElement("p"); text.className="empty"; text.textContent="本阶段没有符合筛选的记录。"; list.append(text); }
        list.scrollTop = previousScroll;
        byId("list-title").textContent = GROUP_NAMES[group];
        byId("list-count").textContent = `${candidates.length}条`;
        byId("list-range").textContent = candidates.length ? `${page*100+1}–${page*100+visible.length} / ${candidates.length}` : "0";
        byId("list-before").disabled = page === 0;
        byId("list-after").disabled = (page+1)*100 >= candidates.length;
        byId("list-before").onclick = () => { const index=candidates[Math.max(0,page-1)*100]; if(index!==undefined)go(index); };
        byId("list-after").onclick = () => { const index=candidates[(page+1)*100]; if(index!==undefined)go(index); };
    }

    function renderMetrics(event, scene) {
        const pairs = [];
        if (event.stage === "analysis") {
            const part = model.trace.problem.parts[event.partIndex];
            pairs.push(["原件",part?.id || "—"],["原需求",part?.quantity ?? "—"],
                ["已记录基体",(event.geometries || []).filter(geometry => geometry.role === "carrier").length],
                ["凹／凸区域",`${(event.geometries || []).filter(geometry => geometry.role === "recess").length} / ${(event.geometries || []).filter(geometry => geometry.role === "protrusion").length}`]);
        } else if (["match","array"].includes(event.stage)) {
            const prefix = model.greedy && event.stage === "match" ? "参照" : "组合";
            pairs.push(["OBB绝对节省面积",number(event.saving)],[`${prefix}OBB利用率`,`${number(event.fill*100,2)}%`],
                [`${prefix}OBB面积`,event.footprintArea > 0 ? number(event.footprintArea) : "未测量"],["原成员数",scene.members.length],
                ["候选宽 × 高",`${number(event.width)} × ${number(event.height)}`],["Proposal",event.proposalID]);
        } else {
            pairs.push(["状态／父状态",`${event.stateID || "—"} / ${event.parentStateID || "—"}`],
                ["已记录放置数",scene.members.length],["候选试放编号",event.candidateID || "—"],
                ["AABB占用面积",number(scene.result?.right && scene.result?.top ? scene.result.right*scene.result.top : event.envelopeArea)],
                ["认证规整余料",measuredArea(scene.result?.regularRemnant ?? event.regularRemnantArea)]);
            if (event.stage === "candidate") pairs.push(["本次守门",event.accepted ? "通过" : "拒绝"],["新增包络面积",number(event.increment)]);
            if (event.stage === "selected" || model.finalView || model.greedy && event.stage === "expand") {
                const demand = model.trace.problem.parts.reduce((sum,part) => sum + part.quantity,0);
                const computed = measureMembers(scene.members);
                pairs.push(["已放／原需求",`${scene.members.length} / ${demand}`],["尚余件数",demand-scene.members.length],
                    ["OBB面积（原位姿计算）",number(computed.obbArea)],
                    ["实料／OBB（计算）",computed.density === null ? "未测量" : `${number(computed.density*100,2)}%`],
                    ["原件材料面积（计算）",number(computed.materialArea)]);
            }
            if (model.greedy && event.stage === "expand") {
                pairs.push(["本步确认新增",1],["原件组合标记",scene.members.every(member => member.group === 0) ? "全部为0" : "含非0标记"]);
            }
        }
        pairs.push(["实际累计时间",`${number(event.elapsedSeconds,3)} 秒`],["累计尝试",event.attempts ?? "—"],["已展开状态",event.expandedStates ?? "—"]);
        const metrics = byId("metrics"); metrics.replaceChildren();
        for (const [label,value] of pairs) {
            const item=document.createElement("div");item.className="metric";
            const title=document.createElement("dt");title.textContent=label;
            const content=document.createElement("dd");content.textContent=String(value);
            item.append(title,content);metrics.append(item);
        }
    }

    function renderDrawing() {
        const event = model.events[model.index]; if (!event) return;
        const scene = sceneFor(event);
        draw(event,scene);
        renderMetrics(event,scene);
    }

    function render() {
        const event = model.events[model.index]; if (!event) return;
        const position = model.playbackIndices.indexOf(model.index);
        const demand = model.trace.problem.parts.reduce((sum, part) => sum + part.quantity, 0);
        byId("timeline").value = String(position);
        byId("step-label").textContent = model.playbackMode === "placements"
            ? `已确认 ${position} / ${demand}件 · 原生Step ${event.step}`
            : `${model.index+1} / ${model.events.length}条 · 原生Step ${event.step}`;
        byId("stage-label").textContent = model.finalView ? "独立最终输出" : stageName(event.stage);
        byId("drawing-title").textContent = model.finalView ? "最终原始结果" : eventTitle(event);
        byId("drawing-caption").textContent = event.stage === "analysis" ? "原始局部坐标，基体和残差来自真实分析记录。"
            : model.greedy && event.stage === "match" ? "原件接触参照的归一化相对位姿，用于计算下一单件的候选。"
            : model.greedy && event.stage === "expand" ? "板材绝对坐标。本步确认新增1个原始零件，原件保持独立。"
            : ["match","array"].includes(event.stage) ? "归一化候选坐标，原成员尚未分配实例。"
            : "板材绝对坐标。点击原件查看该实例的基体与凹凸特征。";
        for (const button of byId("stages").children) button.classList.toggle("active",button.dataset.group===groupOf(event.stage));
        byId("previous").disabled = position <= 0;
        byId("next").disabled = position >= model.playbackIndices.length - 1;
        byId("reason").textContent = reasonText(event);
        byId("event-json").textContent = JSON.stringify(event,null,2);
        const path = byId("parent-path");path.replaceChildren();
        let stateID = event.stage === "candidate" && !event.accepted ? event.parentStateID : event.stateID;
        const visited = new Set();
        while (stateID && !visited.has(stateID) && visited.size<18) {
            visited.add(stateID);
            const button=document.createElement("button");button.textContent=`状态${stateID}`;
            const index=model.stateEvents.get(stateID);button.disabled=index===undefined;
            if(index!==undefined)button.addEventListener("click",()=>go(index));
            path.append(button);
            stateID=model.nodes.get(stateID)?.parentStateID;
        }
        renderList(event);renderDrawing();
    }

    function go(index) {
        if (!model.events.length || index < 0) return;
        model.index=Math.max(0,Math.min(index,model.events.length-1));
        if (!model.playbackIndices.includes(model.index)) configurePlayback("events");
        model.finalView=false;model.focusedMember=null;
        applyDisplayDefault(model.events[model.index]);
        try { render(); } catch(error) { showError(error); }
    }

    byId("first").addEventListener("click", () => go(model.playbackIndices[0]));
    byId("previous").addEventListener("click", () => navigate(-1));
    byId("next").addEventListener("click", () => navigate(1));
    byId("last").addEventListener("click", () => go(model.playbackIndices.at(-1)));
    byId("timeline").addEventListener("input", event => go(model.playbackIndices[Number(event.target.value)]));
    byId("playback-mode").addEventListener("change", event => {
        pause();
        configurePlayback(event.target.value);
        const index = model.playbackIndices.findLast(value => value <= model.index);
        go(index ?? model.playbackIndices[0]);
    });
    byId("play").addEventListener("click", () => {
        if (model.timer !== null) {
            pause();
            return;
        }
        if (!model.events.length) return;
        if (model.index === model.playbackIndices.at(-1)) go(model.playbackIndices[0]);
        byId("play").textContent = "暂停";
        model.timer = setInterval(() => {
            if (model.index === model.playbackIndices.at(-1)) {
                pause();
                return;
            }
            navigate(1);
        }, 1000 / Number(byId("speed").value));
    });
    byId("speed").addEventListener("change", pause);
    for (const id of ["show-original", "show-carrier", "show-recess", "show-protrusion", "show-mouths", "carrier-choice"]) {
        byId(id).addEventListener("change", () => {
            try {
                renderDrawing();
            } catch (error) {
                showError(error);
            }
        });
    }
    for (const id of ["event-search", "rejected-only"]) {
        byId(id).addEventListener("input", () => {
            model.listPage = 0;
            if (model.events.length) renderList(model.events[model.index]);
        });
    }
    byId("final-result").addEventListener("click", () => {
        pause();
        const index = model.events.findLastIndex(event => event.stage === "selected");
        if (index >= 0) {
            go(index);
        } else if (model.bundle.result) {
            model.finalView = true;
            render();
        }
    });
    byId("file-input").addEventListener("change", async event => {
        const file = event.target.files[0];
        if (!file) return;
        try {
            if (file.size > 128 * 1024 * 1024) throw new Error("过程文件超过128MiB读取上限。");
            loadDocument(JSON.parse(await file.text()), file.name);
        } catch (error) {
            showError(error);
        }
    });
    document.addEventListener("keydown", event => {
        if (["INPUT", "SELECT", "TEXTAREA", "BUTTON"].includes(document.activeElement?.tagName)) return;
        if (event.code === "ArrowRight") {
            event.preventDefault();
            navigate(1);
        }
        if (event.code === "ArrowLeft") {
            event.preventDefault();
            navigate(-1);
        }
        if (event.code === "Space") {
            event.preventDefault();
            byId("play").click();
        }
    });
    try {
        const embedded = JSON.parse(byId("trace-data").textContent);
        if (embedded) loadDocument(embedded, embedded.meta?.caseName || "内嵌原生过程");
    } catch (error) {
        showError(error);
    }
})();
