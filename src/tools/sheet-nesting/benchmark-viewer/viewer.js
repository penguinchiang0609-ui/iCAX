(() => {
    "use strict";
    const $ = id => document.getElementById(id);
    const NS = "http://www.w3.org/2000/svg";
    const PAGE_SIZE = 80;
    const state = { rows: [], byIndex: new Map(), nodes: new Map(), revision: null, summaryRevision: null,
        summary: null, page: 0, selected: null, detail: null, detailSignature: null, drawingSignature: null,
        request: 0, tab: "layout", zoom: 100, scale: 1, refreshing: false, lastExecuted: null, error: false };
    const groups = { public: "公开同条件", conditional: "有条件参考", "no-target": "无直接目标" };
    const routes = {
        "plane.array-search-multi": "多规格递归阵列", "plane.nfp-initial": "NFP 多评分构造",
        "plane.array-1": "数量评分 · 延后补隙", "plane.array-2": "几何评分 · 延后补隙",
        "plane.array-3": "阵列与扫描线逐板竞争", "plane.array-4": "数量评分 · 即时补隙",
        "plane.array-5": "几何评分 · 即时补隙", "plane.array-2-source": "43 评分 · 延后补隙",
        "plane.array-5-source": "43 评分 · 即时补隙", "plane.array-first": "历史首项阵列",
        "plane.array-search-first": "单规格递归阵列", "plane.array-reduce": "减行减列构造",
        "plane.array-random": "随机多次构造", "plane.mix": "多策略逐板混合",
        "plane.scanline": "离散扫描线", "plane.nfp-closest": "NFP 纯靠接",
        "plane.nfp-directional": "NFP 主方向混合", "plane.nfp-strip": "首件定宽长条",
        "plane.nfp-array": "NFP 分区阵列", "plane.nfp-multi-array": "NFP 主尾联合阵列",
        "plane.rect-block-search": "矩形块递归组合", "plane.rect-contact": "矩形接触评分",
        "plane.single-product": "每板单品阵列", "plane.single-column": "同宽单列",
        "plane.four-block": "四块正交阵列", "plane.order-ultra": "几何排序·轮廓NFP",
        "plane.order-array": "几何排序·数量阵列"
    };
    const fmt = (value, digits = 2) => Number.isFinite(Number(value)) && value !== null && value !== ""
        ? Number(value).toLocaleString("zh-CN", { maximumFractionDigits: digits }) : "—";
    const text = (id, value) => { const node = $(id); const next = String(value ?? "—"); if (node.textContent !== next) node.textContent = next; };
    const nativeDone = row => row && row.inputStatus !== "blocked" && row.executionStatus && !["pending", "blocked"].includes(row.executionStatus);
    const reviewed = row => row && row.geometryValid !== null && row.geometryValid !== undefined;
    const complete = row => row?.certifiedComplete === true;
    function badge(node, label, tone = "neutral") { node.textContent = label; node.className = `badge ${tone}`; }
    function shortName(row) { return row.title || String(row.id || "未知案例").split(":").slice(1).join(":") || row.id; }
    function statusOf(row) {
        if (!row) return ["尚未选择", "neutral"];
        if (row.inputStatus === "blocked") return ["输入受限", "warn"];
        if (!nativeDone(row)) return ["等待计算", "neutral"];
        if (row.executionStatus === "process-timeout") return ["进程超时", "bad"];
        if (["no-native-output", "execution-error", "unreadable-native-output"].includes(row.executionStatus)) return ["运行或输出异常", "bad"];
        if (row.geometryValid === false || row.errors?.length) return ["核验未通过", "bad"];
        if (!reviewed(row)) return ["已计算 · 待核验", "warn"];
        if (!complete(row)) return ["合法 · 未排齐", "warn"];
        return ["几何核验通过 · 排齐", "neutral"];
    }
    function qualityOf(row) {
        if (row?.inputStatus === "blocked") return ["—", "neutral"];
        if (row?.comparisonGroup === "no-target" || row?.metric === null) return ["无可比目标", "neutral"];
        if (!nativeDone(row) || !reviewed(row) || row.qualityReached === null || row.qualityReached === undefined) return ["待核定", "neutral"];
        if (!complete(row)) return ["未排齐", "warn"];
        const metric = row.metric === "boards" ? "板数" : row.metric === "strip-length" ? "占长" : "标量";
        if (row.qualityReached) return [`${metric}满足参考`, "neutral"];
        return [`${metric}未满足参考`, "warn"];
    }
    async function getJSON(path) {
        const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 15000);
        try {
            const response = await fetch(path, { cache: "no-store", signal: controller.signal });
            if (!response.ok) throw new Error(`读取数据失败（${response.status}）`);
            return await response.json();
        } finally { clearTimeout(timer); }
    }
    function updateOverview(payload) {
        state.summary = payload;
        const score = payload.score || payload.summary || payload;
        const execution = payload.execution || {};
        const live = payload.live || {};
        const counts = score.counts || {};
        const total = score.runnableConditions ?? execution.ready ?? 1436;
        const done = live.executed ?? execution.executed ?? counts.executed ?? 0;
        const checked = live.reviewed ?? counts.reviewed ?? 0;
        const valid = live.valid ?? live.geometryValid ?? counts.geometryValid ?? 0;
        const full = live.complete ?? live.certifiedComplete ?? counts.certifiedComplete ?? 0;
        const pending = Math.max(0, total - done);
        const percent = total ? done / total * 100 : 0;
        text("executed-count", fmt(done, 0)); text("runnable-count", `/ ${fmt(total, 0)}`);
        text("progress-percent", `${fmt(percent, 1)}%`); $("progress-bar").style.width = `${Math.min(100, percent)}%`;
        text("progress-caption", `待计算 ${fmt(pending, 0)} · 输入受限 ${fmt(score.blockedConditions ?? live.blocked ?? 25, 0)}`);
        text("valid-count", fmt(valid, 0)); text("review-caption", `已核验 ${fmt(checked, 0)} · 待核验 ${fmt(Math.max(0, done - checked), 0)}`);
        text("complete-count", fmt(full, 0)); text("complete-caption", `已核验未排齐 ${fmt(Math.max(0, valid - full), 0)} · 排齐不代表布局好`);
        const minute = live.endToEndWithinMinute ?? counts.endToEndWithinMinute;
        text("minute-count", fmt(minute, 0));
        text("minute-caption", `进程超时 ${fmt(live.processTimeout ?? counts.processTimeout ?? 0, 0)} · 运行异常 ${fmt(live.processOrNativeFailure ?? counts.processOrNativeFailure ?? 0, 0)}`);
        const publicGroup = score.comparisonGroups?.public || {};
        const conditional = score.comparisonGroups?.conditional || {};
        text("public-target-count", `${fmt(publicGroup.qualityReached ?? 0, 0)} / ${fmt(publicGroup.reviewedComparableTargetConditions ?? publicGroup.reviewed ?? 0, 0)}`);
        text("public-target-caption", `标量满足参考 / 已核验可比 · 总 ${fmt(score.publicComparableConditions ?? 268, 0)} 项`);
        text("conditional-target-count", `${fmt(conditional.qualityReached ?? 0, 0)} / ${fmt(conditional.reviewedComparableTargetConditions ?? conditional.reviewed ?? 0, 0)}`);
        text("conditional-target-caption", `标量满足参考 / 已核验有参考 · 总 ${fmt(score.conditionalNumericReferenceConditions ?? 1043, 0)} 项`);
        text("unrated-count", `${fmt(score.noDirectComparableTargetConditions ?? 125, 0)} / ${fmt(score.blockedConditions ?? 25, 0)}`);
        const finished = score.completed === true;
        const stopped = String(score.status || "").startsWith("stopped");
        badge($("run-state"), stopped ? "批测已停止：构造实现审查中" : finished ? "执行与核验完成" : "批测进行中", "neutral");
        text("run-configuration", `${execution.caseConcurrency ?? 3} 个案例并行 · 每例最多 ${execution.internalWorkersPerCase ?? 4} 个构造线程`);
        const stamp = score.updatedAtUtc || execution.updatedAtUtc;
        if (stamp) text("last-update", `数据 ${new Date(stamp).toLocaleTimeString("zh-CN", { hour12: false })}`);
        if (state.lastExecuted !== null && done > state.lastExecuted) {
            text("new-results", `新增 ${done - state.lastExecuted} 例`);
            clearTimeout(state.newTimer); state.newTimer = setTimeout(() => text("new-results", ""), 9000);
        }
        state.lastExecuted = done;
        const g = payload.g025 || {};
        if (g.medianWallSeconds !== undefined) {
            text("g025-caption", `2440 × 1220 mm · 边距 0.5 · 间距 0.2 · 三次独立实测，另计于全量队列`);
            text("g025-result", `5 板 / 36 件 · 中位 ${fmt(g.medianWallSeconds, 2)} 秒 →`);
        }
    }
    function matches(row) {
        const query = $("search").value.trim().toLocaleLowerCase();
        if (query && !`${row.id} ${row.title || ""} ${row.source} ${row.index}`.toLocaleLowerCase().includes(query)) return false;
        const source = $("source-filter").value;
        if (source !== "all" && row.source !== source) return false;
        const group = $("group-filter").value;
        if (group === "blocked" ? row.inputStatus !== "blocked" : group !== "all" && row.comparisonGroup !== group) return false;
        switch ($("status-filter").value) {
        case "executed": return !!nativeDone(row);
        case "complete": return complete(row);
        case "quality-met": return row.qualityReached === true;
        case "incomplete": return reviewed(row) && row.geometryValid === true && !complete(row);
        case "quality-missed": return complete(row) && row.qualityReached === false;
        case "invalid": return row.geometryValid === false || !!row.errors?.length || ["no-native-output", "execution-error", "unreadable-native-output", "process-timeout"].includes(row.executionStatus);
        case "pending": return row.inputStatus !== "blocked" && !nativeDone(row);
        case "blocked": return row.inputStatus === "blocked";
        case "over-minute": return nativeDone(row) && row.endToEndWithinMinute === false;
        default: return true;
        }
    }
    function completionTime(row) { return Number(row.completedAt) || Date.parse(row.completedAt || "") || 0; }
    function visibleRows() {
        const rows = state.rows.filter(matches); const order = $("sort-order").value;
        rows.sort((a, b) => {
            if (order === "latest") return completionTime(b) - completionTime(a) || (b.index || 0) - (a.index || 0);
            if (order === "slowest") return (b.wholeProcessWallSeconds ?? -1) - (a.wholeProcessWallSeconds ?? -1) || a.index - b.index;
            if (order === "boards") return (b.boards ?? -1) - (a.boards ?? -1) || a.index - b.index;
            if (order === "source") return String(a.source).localeCompare(String(b.source), "zh-CN") || a.index - b.index;
            return a.index - b.index;
        });
        return rows;
    }
    function makeRow() {
        const tr = document.createElement("tr"); tr.className = "case-row"; tr.tabIndex = 0;
        for (let i = 0; i < 5; ++i) tr.appendChild(document.createElement("td"));
        const name = document.createElement("span"), subtitle = document.createElement("span");
        name.className = "case-name"; subtitle.className = "case-subtitle";
        tr.children[0].append(name, subtitle);
        tr.addEventListener("click", () => selectCase(Number(tr.dataset.index)));
        tr.addEventListener("keydown", event => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); selectCase(Number(tr.dataset.index)); } });
        return tr;
    }
    function setCell(node, value, tone = "neutral") { if (node.textContent !== String(value)) node.textContent = value; node.className = `cell-${tone}`; }
    function patchRow(tr, row) {
        tr.dataset.index = row.index; tr.classList.toggle("selected", state.selected === row.index);
        tr.setAttribute("aria-selected", String(state.selected === row.index));
        const name = shortName(row); const title = tr.children[0].children[0], subtitle = tr.children[0].children[1];
        if (title.textContent !== name) title.textContent = name;
        const sub = `${String(row.index).padStart(4, "0")} · ${row.source} · ${row.inputStatus === "blocked" ? "输入受限" : groups[row.comparisonGroup] || "待核定"}`;
        if (subtitle.textContent !== sub) subtitle.textContent = sub;
        const placed = row.placed ?? row.nativePlaced;
        const placedLabel = row.inputStatus === "blocked" ? "—" : Number.isFinite(placed) ? `${fmt(placed, 0)}/${fmt(row.required, 0)}` : "待计算";
        setCell(tr.children[1], placedLabel, complete(row) ? "neutral" : reviewed(row) ? "warn" : "neutral");
        const [quality, tone] = qualityOf(row);
        setCell(tr.children[2], quality.replace("目标", "").replace("参考", ""), row.comparisonGroup === "conditional" && tone === "good" ? "condition" : tone);
        tr.children[2].title = quality;
        setCell(tr.children[3], row.boards ?? "—", "neutral");
        setCell(tr.children[4], Number.isFinite(row.wholeProcessWallSeconds) ? `${fmt(row.wholeProcessWallSeconds, 1)}s` : "—", row.endToEndWithinMinute === false ? "bad" : "neutral");
        tr.title = `${row.id}\n${statusOf(row)[0]} · ${quality}`;
    }
    function updateList(resetPage = false) {
        if (resetPage) state.page = 0;
        const rows = visibleRows(); const pages = Math.max(1, Math.ceil(rows.length / PAGE_SIZE));
        state.page = Math.min(state.page, pages - 1);
        const shown = rows.slice(state.page * PAGE_SIZE, (state.page + 1) * PAGE_SIZE);
        const container = $("case-scroll"), tbody = $("case-rows"); const oldScroll = container.scrollTop;
        const focused = document.activeElement; const focusedIndex = focused?.classList.contains("case-row") ? focused.dataset.index : null;
        const wanted = new Set(shown.map(row => row.index));
        for (const [index, tr] of state.nodes) if (!wanted.has(index)) { tr.remove(); state.nodes.delete(index); }
        shown.forEach((row, position) => {
            let tr = state.nodes.get(row.index);
            if (!tr) { tr = makeRow(); state.nodes.set(row.index, tr); }
            patchRow(tr, row);
            if (tbody.children[position] !== tr) tbody.insertBefore(tr, tbody.children[position] || null);
        });
        container.scrollTop = resetPage ? 0 : oldScroll;
        if (focusedIndex && document.activeElement !== focused) state.nodes.get(Number(focusedIndex))?.focus({ preventScroll: true });
        $("empty-list").hidden = rows.length !== 0;
        text("list-caption", `${fmt(rows.length, 0)} 个符合筛选 · 共 ${fmt(state.rows.length, 0)} 个独立工况`);
        text("page-caption", rows.length ? `${state.page * PAGE_SIZE + 1}–${Math.min((state.page + 1) * PAGE_SIZE, rows.length)} / ${fmt(rows.length, 0)}` : "0 个案例");
        $("previous-page").disabled = state.page === 0; $("next-page").disabled = state.page >= pages - 1;
    }
    function addSources() {
        const select = $("source-filter"); const existing = new Set(Array.from(select.options, option => option.value));
        [...new Set(state.rows.map(row => row.source).filter(Boolean))].sort((a, b) => a.localeCompare(b, "zh-CN")).forEach(source => {
            if (existing.has(source)) return;
            const option = document.createElement("option"); option.value = source; option.textContent = source; select.append(option);
        });
    }
    function rowSignature(row) { return JSON.stringify([row?.executionStatus, row?.reviewStatus, row?.rawSha256, row?.reviewSha256, row?.qualityReached, row?.geometryValid, row?.nativeWallSeconds, row?.complete]); }
    async function refresh(force = false) {
        if (state.refreshing || !force && !$("auto-refresh").checked) return;
        state.refreshing = true; $("refresh").disabled = true;
        try {
            const payload = await getJSON("/api/summary"); updateOverview(payload);
            const revision = String(payload.revision || `${payload.score?.updatedAtUtc || payload.updatedAtUtc || ""}|${payload.execution?.updatedAtUtc || ""}`);
            if (force || !state.rows.length || revision !== state.summaryRevision) {
                const data = await getJSON("/api/cases"); const rows = data.rows || data.cases || [];
                if (String(data.revision) !== state.revision || !state.rows.length || force) {
                    state.rows = rows; state.byIndex = new Map(rows.map(row => [Number(row.index), row]));
                    state.revision = String(data.revision); addSources(); updateList();
                    if (typeof state.selected === "number") {
                        const row = state.byIndex.get(state.selected);
                        if (row && rowSignature(row) !== state.detailSignature) await selectCase(state.selected, true);
                    }
                }
                state.summaryRevision = revision;
            }
            state.error = false; $("connection-error").hidden = true; $("live-dot").className = "live-dot connected";
            text("live-status", $("auto-refresh").checked ? "实时连接" : "自动更新已暂停");
            if (state.selected === null) await selectCase("g025");
        } catch (error) {
            state.error = true; $("connection-error").hidden = false;
            text("connection-error", `暂时无法读取最新数据，已保留当前画面。${error.name === "AbortError" ? "本次连接超时。" : error.message} 将自动重试。`);
            $("live-dot").className = "live-dot error"; text("live-status", "等待重连");
        } finally { state.refreshing = false; $("refresh").disabled = false; }
    }
    async function selectCase(index, updating = false) {
        const changed = state.selected !== index; state.selected = index;
        const row = index === "g025" ? null : state.byIndex.get(index);
        if (row) { patchDetailMetrics(row); state.detailSignature = rowSignature(row); }
        updateList();
        const request = ++state.request; const panel = document.querySelector(".detail-panel");
        if (changed) {
            panel.classList.add("loading"); state.drawingSignature = null; state.detail = null;
            drawingPlaceholder("正在载入所选案例的原始轮廓与最终坐标…", true);
            text("drawing-caption", "正在读取所选案例，请稍候。");
            $("remaining-section").hidden = true;
            if (state.tab === "raw") text("raw-json", "正在载入所选案例…");
        }
        try {
            const detail = await getJSON(index === "g025" ? "/api/g025" : `/api/cases/${index}`);
            if (request !== state.request || state.selected !== index) return;
            state.detail = detail;
            renderDetail(detail, changed);
            if (changed) $("detail-scroll").scrollTop = 0;
            if (!updating && changed && window.innerWidth < 850) panel.scrollIntoView({ behavior: "smooth", block: "start" });
        } catch (error) {
            if (request !== state.request) return;
            $("detail-notice").hidden = false; text("detail-notice", `此案例数据暂时无法读取：${error.message}。稍后刷新会重试。`);
            if (changed) {
                drawingPlaceholder("所选案例读取失败，稍后刷新会重试。");
                text("drawing-caption", "尚未载入所选案例的排样图。");
            }
            state.detailSignature = null;
        } finally { if (request === state.request) panel.classList.remove("loading"); }
    }
    function detailRow(detail) {
        const row = { ...(detail.case || {}) }; const result = detail.result;
        if (result) {
            row.boards ??= result.layouts?.length; row.placed ??= result.placed; row.required ??= detail.problem?.parts?.reduce((sum, part) => sum + part.quantity, 0);
            row.nativeComplete ??= result.complete; row.nativeValid ??= result.valid;
        }
        if (state.selected === "g025") {
            const verification = detail.verification || state.summary?.g025 || {};
            row.id = "G025 5板重复结果"; row.title = row.id; row.source = "三次独立实测 · 中位结果";
            row.inputStatus = "ready"; row.comparisonGroup = "public"; row.metric = "boards"; row.target = { boards: 5 };
            row.executionStatus = result ? "native-result" : "pending";
            row.geometryValid = verification.independentExactRationalAuditAllPassed === true;
            row.certifiedComplete = row.geometryValid && result?.complete === true;
            row.qualityReached = row.certifiedComplete && result.layouts.length === 5;
            row.nativeWallSeconds ??= verification.medianWallSeconds;
            row.auditTolerance = { geometryMm: 0 }; row.auditMethod = "exact rational quarter-turn original material and clearance";
            row.selectedRoute ??= detail.execution?.selectedRoute; row.eligibleMethods = verification.eligible; row.startedMethods = verification.medianStarted;
        }
        return row;
    }
    function patchDetailMetrics(row) {
        text("detail-source", `${row.source || "本轮案例"}${row.index ? ` · ${String(row.index).padStart(4, "0")}` : ""}`);
        text("detail-title", shortName(row)); badge($("detail-status"), ...statusOf(row));
        const placed = row.placed ?? row.nativePlaced;
        text("detail-placed", `${fmt(placed, 0)} / ${fmt(row.required, 0)}`);
        text("detail-boards", `${fmt(row.boards, 0)} / ${Number.isFinite(row.target?.boards) ? fmt(row.target.boards, 0) : "—"}`);
        text("detail-seconds", Number.isFinite(row.nativeWallSeconds) ? `${fmt(row.nativeWallSeconds, 3)} 秒` : "—");
        const quality = qualityOf(row); text("detail-quality", quality[0]); $("detail-quality").style.color = quality[1] === "good" ? "#20815d" : quality[1] === "bad" ? "#ad5750" : "#7e8ca0";
    }
    function translateQualification(row) {
        if (row.comparisonGroup === "public") return "公开同条件目标；采用对应数据源的比较精度。";
        if (row.comparisonGroup === "no-target") return row.targetKind === "paper-F-K" ? "原来源提供 F/K 标量，未作为普通利用率或整数板数比较。" : "未提供可直接比较的质量目标，仍核验完整性与合法性。";
        const reason = row.qualification?.reason;
        if (reason?.includes("primary-paper angle domain is unconfirmed")) return "几何、数量及库存沿用原输入；原论文旋转条件尚未核定。";
        return reason || "仅作有条件数值参考，未计作公开同条件胜负。";
    }
    function renderDetail(detail, changed) {
        const row = detailRow(detail); patchDetailMetrics(row);
        const notices = [];
        if (state.selected === "g025") notices.push("此处是三次专项实测的中位结果；全量队列中的 G025 仍按其正式批次进度单独计数。");
        if (row.inputStatus === "blocked") notices.push(...(row.blockedReasons || ["原始条件或当前输入入口不完整，本轮未执行。"])) ;
        else if (row.comparisonGroup === "conditional") notices.push(translateQualification(row));
        else if (nativeDone(row) && !reviewed(row)) notices.push("原生计算已完成，独立核验和质量计分正在补入。");
        if (row.geometryValid === false || row.errors?.length) notices.push(...(row.errors || ["最终输出未通过核验。"]));
        $("detail-notice").hidden = !notices.length; text("detail-notice", notices.join(" "));
        const rawSignature = detail.execution?.rawSha256 || row.rawSha256 || (detail.result ? `${row.id}|${row.nativeWallSeconds}` : `pending:${row.id}`);
        if (changed || rawSignature !== state.drawingSignature) {
            renderBoards(detail.problem, detail.result, row);
            renderParts(detail.problem || detail.inputConditions, detail.result);
            state.drawingSignature = rawSignature;
        }
        renderFacts(detail, row);
        if (state.tab === "raw") renderRaw();
    }
    function svgElement(name, attributes = {}) {
        const node = document.createElementNS(NS, name);
        for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
        return node;
    }
    function world(point, pose) {
        const angle = ((Number(pose.angle) % 360) + 360) % 360; const [x, sourceY] = point;
        const y = pose.mirrored === true ? -sourceY : sourceY; let px, py;
        if (angle === 0) { px = x; py = y; } else if (angle === 90) { px = -y; py = x; }
        else if (angle === 180) { px = -x; py = -y; } else if (angle === 270) { px = y; py = -x; }
        else { const a = angle * Math.PI / 180; px = Math.cos(a) * x - Math.sin(a) * y; py = Math.sin(a) * x + Math.cos(a) * y; }
        return [px + Number(pose.x), py + Number(pose.y)];
    }
    function ringArea(ring) {
        return Math.abs(ring.reduce((sum, point, index) => { const next = ring[(index + 1) % ring.length]; return sum + point[0] * next[1] - point[1] * next[0]; }, 0)) / 2;
    }
    function materialArea(part) { const g = part.geometry || {}; return g.radius > 0 ? Math.PI * g.radius ** 2 : ringArea(g.outer || []) - (g.holes || []).reduce((sum, ring) => sum + ringArea(ring), 0); }
    function drawingPlaceholder(copy, loading = false) {
        const empty = document.createElement("div"); empty.className = "empty drawing-empty";
        if (loading) empty.setAttribute("role", "status");
        const symbol = document.createElement("span"); symbol.textContent = "▱";
        const paragraph = document.createElement("p"); paragraph.textContent = copy;
        empty.append(symbol, paragraph); $("boards").replaceChildren(empty);
    }
    function renderBoards(problem, result, row) {
        const host = $("boards"); host.replaceChildren();
        if (!problem || !result || !Array.isArray(result.layouts) || !result.layouts.length) {
            const empty = document.createElement("div"); empty.className = "empty drawing-empty";
            const symbol = document.createElement("span"); symbol.textContent = "▱";
            const copy = document.createElement("p"); copy.textContent = row.inputStatus === "blocked" ? "输入条件尚不能完整回放，保留原因与原参数。" : nativeDone(row) ? "本轮没有返回可绘制的板布局，请查看核验与原始字段。" : "此案例仍在队列中，计算完成后自动显示对应板图。";
            empty.append(symbol, copy); host.append(empty);
            text("drawing-caption", "可在“参数与核验”查看已有输入信息。"); $("remaining-section").hidden = true; return;
        }
        const parts = new Map(problem.parts.map((part, index) => [part.id, { ...part, color: `hsl(${Math.round(index * 137.508 % 360)} 43% 78%)` }]));
        const maxWidth = Math.max(...result.layouts.map(board => board.width + .008 * Math.max(board.width, board.height)));
        const available = Math.max(280, $("detail-scroll").clientWidth - 70);
        state.scale = Math.min(available, 1100) / maxWidth;
        let material = 0, stockArea = 0;
        for (const [boardIndex, board] of result.layouts.entries()) {
            const card = document.createElement("article"); card.className = "board-card";
            const heading = document.createElement("div"); heading.className = "board-heading";
            const title = document.createElement("b"); title.textContent = `板 ${String(boardIndex + 1).padStart(2, "0")}`;
            const description = document.createElement("span"); description.textContent = `${fmt(board.width, 3)} × ${fmt(board.height, 3)} mm · ${fmt(board.placements.length, 0)} 件`;
            const area = board.placements.reduce((sum, pose) => sum + (parts.has(pose.part) ? materialArea(parts.get(pose.part)) : 0), 0);
            const usage = document.createElement("span"); usage.className = "board-utilization"; usage.textContent = `整板 ${fmt(area / (board.width * board.height) * 100, 2)}%`;
            heading.append(title, description, usage);
            const viewport = document.createElement("div"); viewport.className = "board-viewport";
            const pad = Math.max(board.width, board.height) * .004;
            const svg = svgElement("svg", { viewBox: `${-pad} ${-pad} ${board.width + 2 * pad} ${board.height + 2 * pad}`, role: "img", "aria-label": `第${boardIndex + 1}板，${board.placements.length}个原件` });
            svg.dataset.physicalWidth = board.width + 2 * pad;
            const group = svgElement("g", { transform: `translate(0 ${board.height}) scale(1 -1)` });
            const stroke = 1 / state.scale;
            group.append(svgElement("rect", { x: 0, y: 0, width: board.width, height: board.height, class: "board-outline", "stroke-width": stroke }));
            const margin = problem.edgeMargin || 0;
            if (margin > 0 && board.width > 2 * margin && board.height > 2 * margin) group.append(svgElement("rect", { x: margin, y: margin, width: board.width - 2 * margin, height: board.height - 2 * margin, class: "margin-outline", "stroke-width": stroke * .7, "stroke-dasharray": `${5 / state.scale} ${4 / state.scale}` }));
            for (const pose of board.placements) {
                const part = parts.get(pose.part); if (!part) continue;
                const g = part.geometry; let shape, center;
                if (g.radius > 0) { center = world(g.center || [0, 0], pose); shape = svgElement("circle", { cx: center[0], cy: center[1], r: g.radius }); }
                else {
                    const rings = [g.outer || [], ...(g.holes || [])].map(ring => ring.map(point => world(point, pose)));
                    const d = rings.filter(ring => ring.length).map(ring => `M${ring.map(point => `${point[0]},${point[1]}`).join("L")}Z`).join("");
                    shape = svgElement("path", { d, "fill-rule": "evenodd" });
                    const ring = rings[0] || []; if (!ring.length) continue;
                    center = [(Math.min(...ring.map(point => point[0])) + Math.max(...ring.map(point => point[0]))) / 2, (Math.min(...ring.map(point => point[1])) + Math.max(...ring.map(point => point[1]))) / 2];
                }
                shape.setAttribute("fill", part.color); shape.setAttribute("class", "part-shape"); shape.setAttribute("stroke-width", stroke * .85);
                const tooltip = svgElement("title"); tooltip.textContent = `${part.id} · 实例 ${pose.instance + 1} · ${fmt(pose.angle, 4)}°\n位置 ${fmt(pose.x, 6)}, ${fmt(pose.y, 6)}`; shape.append(tooltip); group.append(shape);
                const label = svgElement("text", { x: center[0], y: -center[1], transform: "scale(1 -1)", "text-anchor": "middle", "dominant-baseline": "middle", class: "part-label", "font-size": 10 / state.scale, style: `stroke-width:${3 / state.scale}px` });
                label.textContent = part.id; group.append(label);
            }
            svg.append(group); viewport.append(svg); card.append(heading, viewport); host.append(card);
            material += area; stockArea += board.width * board.height;
        }
        text("drawing-caption", `${result.layouts.length} 张板 · 实材整板利用率 ${fmt(stockArea ? material / stockArea * 100 : 0, 2)}% · 间距 ${fmt(problem.partGap, 4)} mm，边距 ${fmt(problem.edgeMargin, 4)} mm。各板采用相同物理比例。`);
        applyZoom(); applyLabels();
        const remaining = result.remaining || []; $("remaining-section").hidden = !remaining.length;
        const list = $("remaining-list"); list.replaceChildren();
        remaining.forEach(item => { const chip = document.createElement("span"); chip.className = "remaining-chip"; chip.textContent = `${item.part} × ${fmt(item.quantity, 0)}`; list.append(chip); });
    }
    function applyZoom() {
        state.zoom = Number($("zoom").value); text("zoom-value", `${state.zoom}%`);
        document.querySelectorAll(".board-viewport svg").forEach(svg => { svg.style.width = `${Number(svg.dataset.physicalWidth) * state.scale * state.zoom / 100}px`; });
    }
    function fitWidth() {
        const drawings = [...document.querySelectorAll(".board-viewport svg")];
        if (drawings.length) {
            const width = Math.max(...drawings.map(svg => Number(svg.dataset.physicalWidth)));
            state.scale = Math.min(Math.max(280, $("detail-scroll").clientWidth - 70), 1100) / width;
            drawings.forEach(svg => {
                svg.querySelectorAll(".board-outline").forEach(node => node.setAttribute("stroke-width", 1 / state.scale));
                svg.querySelectorAll(".margin-outline").forEach(node => { node.setAttribute("stroke-width", .7 / state.scale); node.setAttribute("stroke-dasharray", `${5 / state.scale} ${4 / state.scale}`); });
                svg.querySelectorAll(".part-shape").forEach(node => node.setAttribute("stroke-width", .85 / state.scale));
                svg.querySelectorAll(".part-label").forEach(node => { node.setAttribute("font-size", 10 / state.scale); node.style.strokeWidth = `${3 / state.scale}px`; });
            });
        }
        $("zoom").value = "100"; applyZoom();
    }
    function applyLabels() { $("boards").classList.toggle("hide-labels", !$("show-labels").checked); }
    function patchFacts(pairs) {
        const dl = $("case-facts"); const previous = new Map(Array.from(dl.children).filter(node => node.tagName === "DT").map(node => [node.dataset.key, node]));
        const wanted = new Set();
        for (const [key, value] of pairs) {
            if (value === null || value === undefined || value === "") continue; wanted.add(key);
            let dt = previous.get(key), dd;
            if (!dt) { dt = document.createElement("dt"); dt.dataset.key = key; dt.textContent = key; dd = document.createElement("dd"); dl.append(dt, dd); } else dd = dt.nextElementSibling;
            const content = String(value); if (dd.textContent !== content) dd.textContent = content;
        }
        for (const [key, dt] of previous) if (!wanted.has(key)) { dt.nextElementSibling.remove(); dt.remove(); }
    }
    function renderFacts(detail, row) {
        const problem = detail.problem || detail.inputConditions || {}; const stock = problem.stocks || detail.originalConditions?.stocks || [];
        const angles = [...new Set((problem.parts || []).map(part => part.freeRotation ? "自由旋转" : (part.allowedAngles || []).map(angle => `${fmt(angle, 3)}°`).join(" / ") || "0°"))];
        const tolerance = row.auditTolerance?.geometryMm ?? detail.review?.tolerance?.geometryMm ?? detail.review?.geometricComparisonTolerance;
        const method = row.auditMethod || detail.review?.method || "";
        let audit = reviewed(row) ? tolerance === 0 ? "零几何容差；原件有理数核验" : tolerance !== undefined ? `原始容差 ${tolerance} mm` : "独立原件核验" : "等待独立核验";
        if (detail.review?.quarterTurnOutput === false) audit = "浮点三角位姿重建后按有理数检查；不使用几何容差";
        if (method.toLowerCase().includes("circle")) audit += "；解析圆距离检查";
        const target = row.target || {};
        patchFacts([
            ["案例标识", row.id], ["比较范围", groups[row.comparisonGroup] || "未核定"], ["目标适用条件", translateQualification(row)],
            ["原始板材", stock.map(item => `${item.id || "板材"}：${fmt(item.width, 4)} × ${fmt(item.height, 4)} mm，库存 ${fmt(item.quantity, 0)}`).join("； ")],
            ["零件需求", `${fmt((problem.parts || []).length, 0)} 种 / ${fmt(row.required, 0)} 件`],
            ["原件间距", problem.partGap !== undefined ? `${fmt(problem.partGap, 6)} mm` : "未声明"], ["板边预留", problem.edgeMargin !== undefined ? `${fmt(problem.edgeMargin, 6)} mm` : "未声明"],
            ["允许角度", angles.length > 4 ? `${angles.length} 组不同角域，见原件规格` : angles.join("； ")],
            ["参考标量", row.metric === "boards" || target.kind === "boards" ? Number.isFinite(target.boards) ? `${target.boards} 板` : "未提供参考板数" : row.metric === "strip-length" || target.kind === "strip-utilization" ? `占长 ≤ ${fmt(target.length, 8)} mm` : target.kind === "utilization" ? `整板利用率 ≥ ${fmt(target.utilization * 100, 4)}%` : "无直接可比目标"],
            ["实际占长", Number.isFinite(row.occupiedLength) ? `${fmt(row.occupiedLength, 10)} mm` : null], ["精确占长", row.occupiedLengthExact],
            ["严格数值关系", ({ matched: "相同", better: "更优", "not-reached": "未达到", incomplete: "原上限内未排齐" })[row.numericRelation] || row.numericRelation],
            ["报告精度关系", ({ matched: "与公开报告值相同", better: "优于公开报告值", "not-reached": "未达到公开报告值" })[row.reportedPrecisionRelation] || null],
            ["完整性 / 合法性", `${statusOf(row)[0]} · ${qualityOf(row)[0]}`], ["核验方式", audit],
            ["实际获胜方法", row.selectedRoute ? `${routes[row.selectedRoute] || row.selectedRoute}（${row.selectedRoute}）` : null],
            ["初解参赛", row.eligibleMethods !== undefined ? `${row.eligibleMethods} 种适用 / ${row.startedMethods ?? "—"} 种实际启动` : null],
            ["原生计算耗时", Number.isFinite(row.nativeWallSeconds) ? `${fmt(row.nativeWallSeconds, 7)} 秒` : null],
            ["整个进程耗时", Number.isFinite(row.wholeProcessWallSeconds) ? `${fmt(row.wholeProcessWallSeconds, 7)} 秒` : null],
            ["一分钟结论", row.endToEndWithinMinute === true ? "整个进程在 60 秒内返回" : row.endToEndWithinMinute === false ? "整个进程超过 60 秒" : "尚未计定"],
            ["停止原因", row.targetStop ? "达到材料面积板数下界，成功提前停止" : row.deadlineStop ? "达到共享时间截止点" : nativeDone(row) ? "本轮已分配构造结束" : "等待执行"],
            ["输入受限原因", row.blockedReasons?.join("； ")]
        ]);
        text("review-json", JSON.stringify(detail.review || { status: "pending", explanation: "尚未生成本轮独立核验记录。" }, null, 2));
    }
    function renderParts(problem, result) {
        const host = $("part-list"); host.replaceChildren();
        if (!Array.isArray(problem?.parts)) { const p = document.createElement("p"); p.textContent = "当前入口尚未提供可完整回放的原件参数。"; host.append(p); return; }
        const table = document.createElement("table"); table.className = "part-data-table";
        const head = document.createElement("thead"), heading = document.createElement("tr");
        for (const label of ["原件", "需求", "几何", "材料", "允许角度"]) { const th = document.createElement("th"); th.textContent = label; heading.append(th); } head.append(heading); table.append(head);
        const body = document.createElement("tbody");
        for (const part of problem.parts) {
            const g = part.geometry || {}; const tr = document.createElement("tr");
            const kind = g.radius > 0 ? `圆 Ø${fmt(g.radius * 2, 4)}` : g.outer ? `${g.outer.length} 顶点${g.holes?.length ? ` / ${g.holes.length} 孔` : ""}` : "见原输入";
            for (const value of [part.id, part.quantity, kind, part.material || "—", part.freeRotation ? "自由旋转" : (part.allowedAngles || []).map(angle => `${fmt(angle, 3)}°`).join(" / ") || "0°"]) { const td = document.createElement("td"); td.textContent = value; tr.append(td); } body.append(tr);
        }
        table.append(body); host.append(table);
    }
    function renderRaw() { text("raw-json", state.detail ? JSON.stringify(state.detail, null, 2) : "尚未选择案例。"); }
    function activateTab(tab) {
        state.tab = tab;
        document.querySelectorAll("[data-tab]").forEach(button => { const selected = button.dataset.tab === tab; button.classList.toggle("active", selected); button.setAttribute("aria-selected", String(selected)); });
        for (const name of ["layout", "facts", "raw"]) $(`tab-${name}`).hidden = name !== tab;
        if (tab === "raw") renderRaw();
    }
    $("search").addEventListener("input", () => updateList(true));
    for (const id of ["source-filter", "group-filter", "status-filter", "sort-order"]) $(id).addEventListener("change", () => updateList(true));
    $("previous-page").addEventListener("click", () => { --state.page; updateList(); $("case-scroll").scrollTop = 0; });
    $("next-page").addEventListener("click", () => { ++state.page; updateList(); $("case-scroll").scrollTop = 0; });
    $("g025-card").addEventListener("click", () => selectCase("g025"));
    $("refresh").addEventListener("click", () => refresh(true));
    $("auto-refresh").addEventListener("change", () => { text("live-status", $("auto-refresh").checked ? "实时连接" : "自动更新已暂停"); if ($("auto-refresh").checked) refresh(true); });
    $("zoom").addEventListener("input", applyZoom); $("show-labels").addEventListener("change", applyLabels);
    $("reset-zoom").addEventListener("click", fitWidth);
    document.querySelectorAll("[data-tab]").forEach(button => button.addEventListener("click", () => activateTab(button.dataset.tab)));
    $("download-json").addEventListener("click", () => {
        if (!state.detail) return; const blob = new Blob([JSON.stringify(state.detail, null, 2)], { type: "application/json;charset=utf-8" });
        const url = URL.createObjectURL(blob); const link = document.createElement("a"); link.href = url; link.download = `${state.selected === "g025" ? "G025-专项" : `case-${state.selected}`}.json`; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
    });
    refresh(true); setInterval(() => refresh(), 4000);
})();
