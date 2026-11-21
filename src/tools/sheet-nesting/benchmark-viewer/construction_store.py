"""Read-only byte-offset access to native construction observations."""
from __future__ import annotations

from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict, OrderedDict
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import hashlib
import math
from pathlib import Path
import re
import threading

from materials_store import MaterialsStore, _metrics_module
from json_file_io import read_json_bytes

SAFE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\Z")
AUDITS = {
    "non-nfp": "non-nfp-source-audit.md",
    "remaining": "remaining-source-audit.md",
    "scheduler": "scheduler-audit.md",
    "nfp": "nfp-source-audit.md",
}


def identifier(value):
    if not SAFE_ID.fullmatch(value) or ".." in value:
        raise ValueError("无效的记录标识。")
    return value


def read_json(path):
    return json.loads(read_json_bytes(path).decode("utf-8-sig"))


def placement_key(placed):
    pose = placed.get("pose", placed.get("transform", placed))
    values = (pose.get("x"), pose.get("y"), pose.get("angle", pose.get("angleDegrees", 0)))
    if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in values):
        raise ValueError("原生位姿字段缺失。")
    return (placed.get("part", placed.get("partID", placed.get("partId"))), placed.get("instance"),
            *values, pose.get("mirrored", False), placed.get("group", 0), placed.get("gapFiller", False))


def board_key(board):
    return (board.get("stockID", board.get("stock")), board.get("stockInstance", 0),
            board.get("width"), board.get("height"),
            tuple(sorted((placement_key(value) for value in board.get("placements", [])), key=repr)))


def layout_metrics(problem, layouts):
    """Measure represented originals at recorded poses, without validating or changing a layout."""
    try:
        parts = {part["id"]: part["geometry"] for part in problem["parts"]}
        area = sheet = section = 0.
        margin = problem["edgeMargin"]
        for board in layouts:
            width, height = board["width"], board["height"]
            if not width > 0 or not height > 0:
                raise ValueError("板尺寸无效。")
            top = 0.
            for placed in board.get("placements", []):
                geometry = parts[placed.get("part", placed.get("partID", placed.get("partId")))]
                area += _metrics_module.geometry_area(geometry)
                top = max(top, _metrics_module.geometry_bounds(geometry, placed)[3])
            sheet += width * height
            section += width * min(height, max(0., top + margin))
        return dict(stockUtilization=area/sheet if sheet else None,
                    usedSectionUtilization=area/section if section else None, materialArea=area,
                    sheetArea=sheet, usedSectionArea=section, source="recorded-original-geometry")
    except (KeyError, TypeError, ValueError, ZeroDivisionError):
        return dict(stockUtilization=None, usedSectionUtilization=None, source="unavailable")


@dataclass(slots=True)
class Record:
    ordinal: int
    offset: int
    size: int
    route: str
    scope: str
    stage: str
    state: int
    snapshot: bool


class EventFile:
    def __init__(self, path):
        self.path = path
        self.lock = threading.RLock()
        self.reset()

    def reset(self):
        self.records = []
        self.ordinals = {}
        self.by_route = defaultdict(list)
        self.by_scope = defaultdict(list)
        self.by_stage = defaultdict(list)
        self.states = defaultdict(list)
        self.finals = {}
        self.route_results = {}
        self.applicability = {}
        self.commits = defaultdict(list)
        self.raw_boards = defaultdict(list)
        self.final_selection = None
        self.path_cache = {}
        self.chain_cache = {}
        self.read_cache = OrderedDict()
        self.read_cache_bytes = 0
        self.links = defaultdict(list)
        self.winning_scopes = {}
        self.decisions = []
        self.offset = 0
        self.signature = None
        self.identity = None
        self.invalid_lines = 0
        self.partial_tail = False
        self.selection_cache = {}

    def refresh(self):
        with self.lock:
            if not self.path.exists():
                return
            stat = self.path.stat()
            signature = (stat.st_mtime_ns, stat.st_size)
            if self.signature == signature:
                return
            identity = (stat.st_dev, stat.st_ino)
            if self.identity != identity or stat.st_size < self.offset or (self.signature and stat.st_size == self.signature[1]):
                self.reset()
            self.identity = identity
            self.partial_tail = False
            with self.path.open("rb") as stream:
                stream.seek(self.offset)
                # Index the byte snapshot observed at entry. A busy native
                # writer must not make a read request chase new appends forever.
                while stream.tell() < stat.st_size:
                    start = stream.tell()
                    line = stream.readline(stat.st_size-start)
                    if not line:
                        break
                    if not line.endswith(b"\n"):
                        self.partial_tail = True
                        break
                    self.offset = stream.tell()
                    try:
                        value = json.loads(line)
                        event = value["event"]
                        ordinal = int(value["ordinal"])
                        if ordinal in self.ordinals or (self.records and ordinal <= self.records[-1].ordinal):
                            raise ValueError("non-increasing observation ordinal")
                        route, scope, stage = (str(event.get(name, "")) for name in ("route", "scope", "stage"))
                        state = int(event.get("stateID") or 0)
                        layouts = event.get("layouts") or []
                        snapshot = bool(layouts)
                        record = Record(ordinal, start, len(line), route, scope, stage, state, snapshot)
                        index = len(self.records)
                        self.records.append(record)
                        self.ordinals[ordinal] = index
                        self.by_route[route].append(index)
                        self.by_scope[scope].append(index)
                        self.by_stage[stage].append(index)
                        if snapshot and state:
                            self.states[(route, scope, state)].append(index)
                        source = str(event.get("source", ""))
                        if (stage == "commit" or source == "construction.path.commit") and snapshot and state:
                            self.commits[(route, scope, state)].append(index)
                        if source in ("board.raw-construction", "construction.path.raw") and snapshot:
                            self.raw_boards[route].append(index)
                        if source in ("competition.board-selection", "sheet-template.copy") and snapshot:
                            self.links[route].append(index)
                        if source == "strategy.construction-selection" and snapshot:
                            self.winning_scopes[route] = index
                        if source == "scheduler.applicability":
                            self.applicability[route] = index
                        if source.endswith("final-selection") and source.startswith("scheduler."):
                            self.final_selection = index
                        elif source in ("strategy.result", "scheduler.route-result", "scheduler.serial-route-result",
                                        "scheduler.portfolio-route-result", "scheduler.not-started", "scheduler.serial-not-started"):
                            self.route_results[route] = index
                        terminal = source in ("strategy.result", "scheduler.route-result", "scheduler.serial-route-result",
                            "scheduler.portfolio-route-result", "scheduler.final-selection", "scheduler.serial-final-selection",
                            "scheduler.portfolio-final-selection", "scheduler.not-started", "scheduler.serial-not-started")
                        if terminal:
                            self.finals[route] = index
                        if snapshot or terminal or source == "scheduler.applicability":
                            self.remember(index, value)
                        if source.startswith("scheduler.") and stage != "dispatch":
                            self.decisions.append(index)
                    except (ValueError, TypeError, KeyError, UnicodeError):
                        self.invalid_lines += 1
            self.signature = signature
            self.selection_cache.clear()

    def remember(self, index, value):
        if index in self.read_cache:
            self.read_cache.move_to_end(index)
            return value
        self.read_cache[index] = value
        self.read_cache_bytes += self.records[index].size
        while self.read_cache and (len(self.read_cache) > 2048 or self.read_cache_bytes > 16*1024*1024):
            removed, _value = self.read_cache.popitem(last=False)
            self.read_cache_bytes -= self.records[removed].size
        return value

    def read(self, index):
        if index in self.read_cache:
            value = self.read_cache[index]
            self.read_cache.move_to_end(index)
            return value
        record = self.records[index]
        with self.path.open("rb") as stream:
            stream.seek(record.offset)
            return self.remember(index, json.loads(stream.read(record.size)))

    def select(self, route=None, scope=None, stage=None):
        key = (route, scope, stage)
        if key in self.selection_cache:
            return self.selection_cache[key]
        constraints = [(value, table) for value, table in (
            (route, self.by_route), (scope, self.by_scope), (stage, self.by_stage)) if value is not None]
        indices = min((table.get(value, []) for value, table in constraints), key=len) if constraints else range(len(self.records))
        result = [i for i in indices if
                  (route is None or self.records[i].route == route) and
                  (scope is None or self.records[i].scope == scope) and
                  (stage is None or self.records[i].stage == stage)]
        if len(self.selection_cache) >= 32:
            self.selection_cache.clear()
        self.selection_cache[key] = result
        return result

    @staticmethod
    def brief(value):
        event = value["event"]
        layouts = event.get("layouts") or []
        return {"ordinal": value["ordinal"], **{key: event.get(key) for key in (
            "route", "scope", "stage", "source", "reason", "accepted", "candidateID",
            "proposalID", "stateID", "parentStateID", "step", "elapsedSeconds",
            "attempts", "searchTruncated", "truncated", "resultStatus", "partIndex")},
            "boards": len(layouts), "placed": sum(len(board.get("placements", [])) for board in layouts),
            "members": len(event.get("members") or []), "rank": event.get("rank") or [],
            "remaining": event.get("remaining") or []}

    def chain(self, raw_index, board, problem):
        raw = self.records[raw_index]
        current, before, expected = raw.state, raw_index, board_key(board)
        result, seen = [], set()
        parts = [part.get("id") for part in problem.get("parts", [])]
        child = None
        while current:
            if current in seen:
                return result[::-1], "父状态形成循环，未连成有效构造链。"
            seen.add(current)
            candidates = self.commits.get((raw.route, raw.scope, current), [])
            position = bisect_left(candidates, before)-1
            if position < 0:
                return result[::-1], "真实父提交未记录或被采样省略。"
            index = candidates[position]
            event = self.read(index)["event"]
            layouts = event.get("layouts") or []
            if len(layouts) != 1:
                return result[::-1], "提交没有完整单板快照。"
            recorded = layouts[0]
            if board_key(recorded) != expected:
                return result[::-1], "父链快照与已记录子状态不一致。"
            placements = {placement_key(value) for value in recorded.get("placements", [])}
            members = event.get("members") or []
            if not members:
                return result[::-1], "提交的原件增量缺失。"
            added = set()
            for member in members:
                part_index = member.get("partIndex")
                if not isinstance(part_index, int) or not 0 <= part_index < len(parts):
                    return result[::-1], "原件增量的来源身份未记录。"
                matches = [value for value in placements if value[:2] == (parts[part_index], member.get("instance")) and
                           value[2:6] == placement_key(dict(member, part=parts[part_index]))[2:6] and
                           value[6] == member.get("group", 0)]
                if len(matches) != 1:
                    return result[::-1], "原件增量与提交快照坐标不一致。"
                added.add(matches[0])
            if len(added) != len(members):
                return result[::-1], "原件增量包含重复身份。"
            if child is not None and child != placements:
                return result[::-1], "子提交改变了已有原件。"
            result.append(index)
            remaining = placements-added
            current = int(event.get("parentStateID") or 0)
            if not current:
                if remaining:
                    return result[::-1], "构造起始布局未记录，不能补造先前步骤。"
                return result[::-1], None
            child = remaining
            expected = (*board_key(recorded)[:4], tuple(sorted(remaining, key=repr)))
            before = index
        return [], "该原生板没有可追溯的提交状态。"

    def construction(self, route, problem):
        terminal = self.route_results.get(route)
        part_ids = tuple(part.get("id") for part in problem.get("parts", []))
        cache_key = (route, part_ids, terminal, len(self.raw_boards.get(route, [])),
                     len(self.links.get(route, [])), self.winning_scopes.get(route))
        if cache_key in self.path_cache:
            return self.path_cache[cache_key]
        final_boards = self.read(terminal)["event"].get("layouts", []) if terminal is not None else []
        branches_by_state = {}
        for raw_index in self.raw_boards.get(route, []):
            if terminal is not None and raw_index > terminal:
                continue
            value = self.read(raw_index)
            for bi, board in enumerate(value["event"].get("layouts", [])):
                try:
                    chain_key = (raw_index, bi, part_ids)
                    if chain_key not in self.chain_cache:
                        self.chain_cache[chain_key] = self.chain(raw_index, board, problem)
                    steps, error = self.chain_cache[chain_key]
                    key = board_key(board)
                except (ValueError, TypeError, KeyError):
                    steps, error, key = [], "原生状态字段不完整，不能证明构造链。", None
                branch = dict(id=f"{value['ordinal']}-{bi}", scope=self.records[raw_index].scope,
                                     stockID=board.get("stockID", board.get("stock")), stockInstance=board.get("stockInstance", 0),
                                     rawOrdinal=value["ordinal"], steps=len(steps), status="partial" if error else "complete",
                                     reason=error, indices=steps, key=key)
                identity = (branch["scope"], self.records[raw_index].state, key)
                branch["id"] = "state-"+hashlib.sha256(repr(identity).encode("utf-8")).hexdigest()[:20]
                previous = branches_by_state.get(identity)
                if previous is None or value["event"].get("source") == "construction.path.raw":
                    branches_by_state[identity] = branch
        branches = list(branches_by_state.values())
        hint = None
        winner = self.winning_scopes.get(route)
        if winner is not None:
            selected = self.read(winner)["event"]
            try:
                if sorted(map(board_key, selected.get("layouts", [])), key=repr) == sorted(map(board_key, final_boards), key=repr):
                    hint = selected.get("scope")
            except (TypeError, ValueError):
                pass

        def resolve(board, before, scope=None, depth=0):
            if depth > 64:
                return [], "missing"
            key = board_key(board)
            # Explicit native promotion/copy records are the only authority
            # for changing instance/group identities between two snapshots.
            for li in reversed(self.links.get(route, [])):
                if li >= before:
                    continue
                event = self.read(li)["event"]
                values = event.get("layouts") or []
                measures = {row["name"]: row["value"] for row in event.get("measures", [])}
                si, ti = measures.get("sourceLayoutIndex"), measures.get("targetLayoutIndex")
                if si != 0 or ti != 1 or len(values) != 2 or board_key(values[1]) != key:
                    continue
                source, target = values
                if source.get("width") != target.get("width") or source.get("height") != target.get("height"):
                    continue
                a, b = source.get("placements", []), target.get("placements", [])
                if len(a) != len(b) or not all(placement_key(x)[0] == placement_key(y)[0] and
                    placement_key(x)[2:6] == placement_key(y)[2:6] for x, y in zip(a, b)):
                    continue
                source_hint = event.get("scope") if event.get("source") == "competition.board-selection" else None
                steps, status = resolve(source, li, source_hint, depth+1)
                if status == "complete":
                    return steps+[li], status
            matches = [branch for branch in branches if branch["key"] == key and branch["status"] == "complete" and
                       branch["rawOrdinal"] < self.records[before].ordinal and (scope is None or branch["scope"] == scope)]
            if len(matches) == 1:
                return matches[0]["indices"], "complete"
            return [], "ambiguous" if len(matches) > 1 else "missing"

        selected, missing, ambiguous = [], 0, False
        for board in final_boards:
            try:
                steps, status = resolve(board, terminal, hint)
            except (TypeError, ValueError):
                steps, status = [], "missing"
            if status == "complete":
                selected.extend(steps)
            elif status == "ambiguous":
                ambiguous = True
            else:
                missing += 1
        status = "complete" if final_boards and not missing and not ambiguous else "ambiguous" if ambiguous else "unavailable"
        reason = ("按真实原生提交父链回放；每块原生 raw 板与路线终局逐件坐标和身份一致。" if status == "complete" else
                  "多个真实分支得到相同终局，但原生记录未明确获胜分支；可选择各分支查看。" if ambiguous else
                  "终局缺少可证明的原生提交父链；可能有采样、复用或实例重编号，保留终局与其他真实分支。")
        indices = sorted(set(selected)) if status == "complete" else []
        if terminal is not None and status == "complete":
            indices.append(terminal)
        value = dict(status=status, reason=reason, steps=len(indices), missingBoards=missing,
                     indices=indices, branches=branches)
        self.path_cache[cache_key] = value
        return value

    def page(self, route=None, scope=None, stage=None, offset=0, limit=25, ordinal=None,
             view=None, path="selected", problem=None):
        self.refresh()
        with self.lock:
            path_info = None
            if view == "construction":
                if route is None and self.final_selection is not None:
                    route = self.records[self.final_selection].route
                path_info = self.construction(route, problem or {})
                selected = path_info if path == "selected" else next((branch for branch in path_info["branches"] if branch["id"] == path), None)
                indices = selected["indices"] if selected else []
            else:
                indices = self.select(route, scope, stage)
            focus = None
            if ordinal is not None and ordinal in self.ordinals:
                candidate = bisect_left(indices, self.ordinals[ordinal])
                if candidate < len(indices) and indices[candidate] == self.ordinals[ordinal]:
                    focus = candidate
                    offset = (candidate // limit) * limit
            selected = indices[offset:offset+limit]
            return {"total": len(indices), "offset": offset, "limit": limit,
                    "focusOffset": focus,
                    "nextOffset": offset+len(selected) if offset+len(selected) < len(indices) else None,
                    "events": [self.brief(self.read(i)) for i in selected],
                    "indexedEvents": len(self.records), "invalidLines": self.invalid_lines,
                    "partialTail": self.partial_tail,
                    "constructionStatus": path_info["status"] if path_info else None,
                    "constructionReason": path_info["reason"] if path_info else None}

    def detail(self, ordinal):
        self.refresh()
        with self.lock:
            index = self.ordinals[ordinal]
            value = self.read(index)
            event = value["event"]
            record = self.records[index]
            snapshot = None
            if record.snapshot:
                snapshot = {"ordinal": ordinal, "mode": "event", "layouts": event["layouts"]}
            elif record.state:
                candidates = self.states.get((record.route, record.scope, record.state), [])
                position = bisect_right(candidates, index) - 1
                if position >= 0:
                    source = self.read(candidates[position])
                    snapshot = {"ordinal": source["ordinal"], "mode": "same-route-scope-state",
                                "layouts": source["event"]["layouts"]}
            return {**value, "snapshot": snapshot,
                    "snapshotStatus": "recorded" if snapshot else "no-matching-recorded-state",
                    "observationOnly": True}


class ConstructionStore:
    def __init__(self, workspace_root, trace_root=None, materials=None):
        self.workspace_root = Path(workspace_root).resolve()
        self.trace_root = Path(trace_root or self.workspace_root / "output/research/plane-nesting/20261108/construction-traces").resolve()
        if not self.trace_root.is_relative_to(self.workspace_root):
            raise ValueError("过程目录不在工作区内。")
        self.audit_root = self.workspace_root / "output/research/plane-nesting/20261108/construction-audit"
        self.files = {}
        self.lock = threading.RLock()
        self.materials = materials or MaterialsStore(self.workspace_root)

    def local(self, filename):
        if not isinstance(filename, str) or not filename:
            raise ValueError("缺少记录文件名。")
        path = (self.trace_root / filename).resolve()
        if not path.is_relative_to(self.trace_root) or path.parent != self.trace_root:
            raise ValueError("记录文件不在固定过程目录内。")
        return path

    def index(self, task, material=False):
        if material:
            binding = self.materials.construction_trace(task)
            if not binding.get("available"):
                raise FileNotFoundError(binding["reason"])
            path = self.materials._raw_record_path(binding["indexPath"], ".trace-index.json")
            value = self.materials.files.read(path)
        else:
            task = identifier(task)
            value = read_json(self.local(task + ".trace-index.json"))
        if not isinstance(value, dict):
            raise ValueError("原生过程索引尚未完整写入。")
        if value.get("format") != "native-construction-jsonl" or value.get("schemaVersion") != 2:
            raise ValueError("不支持的原生过程记录格式。")
        if material:
            raw = self.materials._raw_record_path(binding["rawPath"], ".json")
            if value.get("resultFile") != raw.name or value.get("eventsFile") != raw.with_suffix(".construction.jsonl").name:
                raise ValueError("原生过程索引的事件或结果不是本次任务输出。")
            if any(key in value and value[key] != binding[key] for key in ("inputSha256", "executableSha256")):
                raise ValueError("原生过程索引版本与本次任务不一致。")
        return value

    def events(self, task, material=False):
        index = self.index(task, material)
        if material:
            binding = self.materials.construction_trace(task)
            relative = Path(binding["indexPath"]).with_name(index["eventsFile"]).as_posix()
            path = self.materials._raw_record_path(relative, ".construction.jsonl")
        else:
            path = self.local(index["eventsFile"])
        if path.suffix != ".jsonl":
            raise ValueError("不是原生事件文件。")
        with self.lock:
            if path not in self.files:
                self.files[path] = EventFile(path)
            events = self.files[path]
        events.refresh()
        return index, events

    def interruption(self, task, index, material=False):
        if index.get("runCompleted") is True:
            return None
        if material:
            binding = self.materials.construction_trace(task)
            path = self.materials._raw_record_path(binding["indexPath"], ".trace-index.json")
        else:
            path = self.local(identifier(task) + ".trace-index.json")
        evidence = path.with_name(path.name[:-len(".trace-index.json")] + ".interrupted.json").resolve()
        if evidence.parent != path.parent:
            return None
        try:
            value = read_json(evidence)
        except (OSError, ValueError, UnicodeError):
            return None
        if not isinstance(value, dict) or value.get("completeResultProduced") is not False:
            return None
        if not all(isinstance(value.get(key), str) and value[key].strip() for key in ("reason", "stoppedAt")):
            return None
        return {"evidenceFile": evidence.name, "diagnosticOnly": True, "reason": value["reason"],
                "stoppedAt": value["stoppedAt"], "completeResultProduced": False}

    def run_info(self, path, index):
        """Bind adjacent run metadata to its explicit invocation; never infer a version from a name."""
        modified = path.stat().st_mtime
        info = dict(metadataStatus="missing", version=None, caseId=None, executableSha256=None,
                    startedAt=None, finishedAt=None, timeSource="index-modified", timeUnix=modified,
                    recordedAt=datetime.fromtimestamp(modified, timezone.utc).isoformat())
        sidecar = path.with_name(path.name[:-len(".trace-index.json")] + ".run.json")
        try:
            run = read_json(sidecar)
        except (OSError, ValueError, UnicodeError):
            return info
        info["metadataStatus"] = "unbound"
        if not isinstance(run, dict):
            return info
        arguments = run.get("arguments")
        try:
            if not isinstance(arguments, list) or len(arguments) < 3 or arguments[0] != "--initial":
                return info
            trace = arguments[arguments.index("--trace") + 1]
            if (Path(trace).resolve() != path.resolve() or
                    Path(arguments[2]).resolve() != path.with_name(index["resultFile"]).resolve() or
                    Path(arguments[1]).resolve() != Path(run["inputPath"]).resolve()):
                return info
            for key in ("inputSha256", "executableSha256"):
                if not isinstance(run.get(key), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", run[key]):
                    return info
                if key in index and index[key] != run[key]:
                    return info
            started = next((run[key] for key in ("startedAt", "startedAtUtc", "nativeProcessStartedUtc")
                            if isinstance(run.get(key), str) and run[key]), None)
            if started is None:
                return info
            stamp = datetime.fromisoformat(started.replace("Z", "+00:00"))
            if stamp.tzinfo is None:
                return info
        except (KeyError, IndexError, TypeError, ValueError, OSError):
            return info
        return dict(info, metadataStatus="bound", metadataFile=sidecar.name,
                    caseId=run.get("caseId") or run.get("taskId"),
                    version=run.get("version") or run.get("comparisonVersion"),
                    executableSha256=run["executableSha256"], inputSha256=run["inputSha256"],
                    startedAt=started, finishedAt=run.get("finishedAt") or run.get("completedAtUtc") or run.get("nativeProcessFinishedUtc"),
                    timeSource="run-started", recordedAt=started, timeUnix=stamp.timestamp())

    @staticmethod
    def task_label(task, info):
        version = info.get("version") or ("程序 " + info["executableSha256"][:12] if info.get("executableSha256") else "版本未记录")
        source = "运行开始" if info["timeSource"] == "run-started" else "索引修改时间"
        return f"{info.get('caseId') or task} · {version} · {source} {info['recordedAt']}"

    def material_run_info(self, task, binding, path, index):
        info = self.run_info(path, index)
        if info["metadataStatus"] == "bound":
            return info
        live = self.materials.files.read(self.materials.catalog_root / "live" / (task + ".json"), {})
        if not isinstance(live, dict) or live.get("taskId") != task or live.get("runSignature") != binding.get("runSignature") or live.get("rawPath") != binding.get("rawPath"):
            return info
        descriptor = live.get("nativeConstructionTrace") or {}
        if descriptor.get("indexPath") != binding.get("indexPath") or descriptor.get("runSignature") != binding.get("runSignature"):
            return info
        started = live.get("startedAtUnix")
        if not isinstance(started, (int, float)) or not math.isfinite(started) or started <= 0:
            return info
        try:
            recorded = datetime.fromtimestamp(started, timezone.utc).isoformat()
        except (ValueError, OverflowError, OSError):
            return info
        profile = self.materials.catalog.get("executionProfile") or {}
        version = profile.get("version") if isinstance(profile.get("version"), str) else None
        return dict(info, metadataStatus="bound", metadataSource="material-run", caseId=task, version=version,
                    executableSha256=binding.get("executableSha256"), inputSha256=binding.get("inputSha256"),
                    metadataFile="catalog/live/"+task+".json", startedAt=recorded, recordedAt=recorded,
                    timeSource="run-started", timeUnix=started)

    def tasks(self, material_task=None):
        rows = []
        for path in sorted(self.trace_root.glob("*.trace-index.json")):
            task = path.name[:-len(".trace-index.json")]
            try:
                index = self.index(task)
                run = self.run_info(path, index)
                rows.append({"id": task, "runInfo": run, "label": self.task_label(task, run), **{key: index.get(key) for key in (
                    "runCompleted", "eventsWritten", "eventsOmitted", "observerFailed", "truncated",
                    "instrumentedTiming", "rotatedFrame", "conditions")},
                    "routeCount": len(index.get("routes") or [])})
            except (OSError, ValueError, TypeError):
                continue
        self.materials.refresh()
        with self.materials.lock:
            for task in self.materials.tasks_by_id.values():
                binding = self.materials._construction_trace(task)
                if binding["status"] == "missing" and task["id"] != material_task:
                    continue
                run = None
                if binding.get("available"):
                    try:
                        path = self.materials._raw_record_path(binding["indexPath"], ".trace-index.json")
                        run = self.material_run_info(task["id"], binding, path, self.index(task["id"], True))
                    except (OSError, ValueError, TypeError, KeyError):
                        pass
                rows.append(dict(id="material::"+task["id"], label="本轮素材 · "+str(task.get("label") or task["id"]),
                                 materialTask=task["id"], available=binding.get("available"), status=binding["status"], runInfo=run))
        dated = [row for row in rows if row.get("runInfo")]
        dated.sort(key=lambda row: (row["runInfo"]["metadataStatus"] == "bound", row["runInfo"]["timeUnix"]), reverse=True)
        default = dated[0]["id"] if dated else None
        return {"tasks": rows, "defaultTaskId": default, "readOnly": True, "audits": self.audits(),
                "note": "记录实际现有构造，用于审查，未宣称修好排样质量；采集耗时含记录开销。"}

    def audits(self):
        return [{"id": key, "title": filename, "url": "/api/construction/audits/" + key}
                for key, filename in AUDITS.items() if (self.audit_root / filename).is_file()]

    def metadata(self, task, material=False):
        binding = self.materials.construction_trace(task) if material else None
        if binding is not None and not binding.get("available"):
            return dict(id=task, materialTask=task, available=False, binding=binding, index={}, routes=[], stages={},
                        scopes=[], indexedEvents=0, decisions=[], decisionCount=0, audits=self.audits(), observationOnly=True)
        try:
            index, events = self.events(task, material)
        except (OSError, ValueError, TypeError, KeyError) as error:
            if not material:
                raise
            binding = dict(binding, available=False, status="binding-error", reason="本次原生过程索引尚不可用："+str(error))
            return dict(id=task, materialTask=task, available=False, binding=binding, index={}, routes=[], stages={},
                        scopes=[], indexedEvents=0, decisions=[], decisionCount=0, audits=self.audits(), observationOnly=True)
        with events.lock:
            routes = []
            selected_route = events.records[events.final_selection].route if events.final_selection is not None else None
            required = sum(part.get("quantity", 0) for part in index.get("problem", {}).get("parts", []))
            ids = list(dict.fromkeys([str(row.get("id", "")) for row in index.get("routes", [])] + list(events.by_route)))
            for route in ids:
                terminal = events.route_results.get(route)
                final = events.read(terminal)["event"] if terminal is not None else None
                measures = {row["name"]: row["value"] for row in (final or {}).get("measures", [])}
                applicability = events.read(events.applicability[route])["event"] if route in events.applicability else None
                not_applicable = applicability is not None and applicability.get("accepted") is False
                layouts = (final or {}).get("layouts") or []
                truncated = bool((final or {}).get("truncated"))
                placed = measures.get("placed", sum(len(board.get("placements", [])) for board in layouts) if final and not truncated else None)
                construction = events.construction(route, index.get("problem", {}))
                public_construction = {key: value for key, value in construction.items() if key not in ("indices", "branches")}
                public_construction["branches"] = [{key: value for key, value in branch.items() if key not in ("indices", "key")}
                                                    for branch in construction["branches"]]
                status = ("not-applicable" if not_applicable else
                          "not-started" if final and str(final.get("source", "")).endswith("not-started") else
                          {0: "complete", 1: "partial", 2: "cancelled", 3: "invalid", 4: "failed"}.get((final or {}).get("resultStatus"), "no-terminal" if index.get("runCompleted") else "running"))
                summary = dict(required=required, placed=placed, selected=route == selected_route,
                               status=status, applicable=not not_applicable,
                               reason=applicability.get("reason") if not_applicable else (final or {}).get("reason"),
                               routeElapsedSeconds=measures.get("routeElapsedSeconds"),
                               metrics=layout_metrics(index.get("problem", {}), layouts) if layouts and not truncated else
                                       dict(stockUtilization=None, usedSectionUtilization=None, source="unavailable"))
                observed = next((row for row in index.get("routes", []) if str(row.get("id", "")) == route), {})
                routes.append({"id": route, "summary": summary, "construction": public_construction,
                               "events": len(events.by_route.get(route, [])),
                               "eventsSeen": observed.get("eventsSeen"), "eventsOmitted": observed.get("eventsOmitted"),
                               "terminalSnapshotTruncated": observed.get("terminalSnapshotTruncated", bool(observed.get("terminalSnapshotsTruncated"))),
                               "terminalSnapshotsTruncated": observed.get("terminalSnapshotsTruncated", int(bool(observed.get("terminalSnapshotTruncated")))),
                               "beginObserved": any(events.records[i].stage == "begin" for i in events.by_route.get(route, [])),
                               "scopes": list(dict.fromkeys(events.records[i].scope for i in events.by_route.get(route, []))),
                               "terminal": events.brief(events.read(terminal)) if terminal is not None else None})
            return {"id": task, "available": True, "materialTask": task if material else None, "binding": binding, "index": index, "routes": routes,
                    "selectedRoute": selected_route,
                    "runInfo": self.material_run_info(task, binding, self.materials._raw_record_path(binding["indexPath"], ".trace-index.json"), index) if material
                               else self.run_info(self.local(task + ".trace-index.json"), index),
                    "interruption": self.interruption(task, index, material),
                    "stages": dict(Counter({key: len(value) for key, value in events.by_stage.items()})),
                    "scopes": list(events.by_scope), "indexedEvents": len(events.records),
                    "invalidLines": events.invalid_lines, "partialTail": events.partial_tail,
                    "decisions": [events.brief(events.read(i)) for i in events.decisions[-50:]],
                    "decisionCount": len(events.decisions), "audits": self.audits(), "observationOnly": True}

    def page(self, task, material=False, **kwargs):
        index, events = self.events(task, material)
        return events.page(problem=index.get("problem", {}), **kwargs)

    def detail(self, task, ordinal, material=False):
        _index, events = self.events(task, material)
        return events.detail(ordinal)

    def audit(self, key):
        return (self.audit_root / AUDITS[key]).read_bytes()

    def evidence(self, task, kind, material=False):
        index = self.index(task, material)
        if kind not in ("index", "result"):
            raise KeyError(kind)
        if material:
            binding = self.materials.construction_trace(task)
            path = self.materials._raw_record_path(binding["indexPath"] if kind == "index" else binding["rawPath"], ".json")
        else:
            path = self.local(task+".trace-index.json") if kind == "index" else self.local(index["resultFile"])
        if path.suffix != ".json":
            raise ValueError("不是过程结果文件。")
        return path.read_bytes()

