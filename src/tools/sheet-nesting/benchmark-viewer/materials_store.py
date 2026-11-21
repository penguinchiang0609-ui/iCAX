"""Read-only catalogue, source DXF and incremental benchmark result access."""
from __future__ import annotations

from collections import Counter
from importlib.util import module_from_spec, spec_from_file_location
import json
import math
from pathlib import Path, PurePosixPath
import re
import threading
import time
from json_file_io import read_json_bytes

_metrics_spec = spec_from_file_location("plane_materials_metrics", Path(__file__).resolve().parents[1] / "plane_materials_metrics.py")
_metrics_module = module_from_spec(_metrics_spec)
_metrics_spec.loader.exec_module(_metrics_module)
calculate_metrics = _metrics_module.calculate_metrics
source_quantity_rows = _metrics_module.source_quantity_rows


SAFE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,191}\Z")
CSV_FILES = {"dxf_inventory.csv", "task_parts.csv", "tasks.csv", "pk_results.csv"}
OUTDATED_RESULTS = {"binding-error", "stale-input", "stale-algorithm"}


def identifier(value):
    if not SAFE_ID.fullmatch(value) or ".." in value:
        raise ValueError("无效的素材或任务标识。")
    return value


class RecordFiles:
    """Retain the last complete document during atomic/partial producer writes."""
    def __init__(self):
        self.values = {}

    def read(self, path, default=None):
        path = Path(path)
        previous = self.values.get(path)
        try:
            stat = path.stat()
            signature = stat.st_mtime_ns, stat.st_size
            if previous and previous[0] == signature:
                return previous[1]
            value = json.loads(read_json_bytes(path).decode("utf-8-sig"))
            self.values[path] = signature, value
            return value
        except (OSError, ValueError, UnicodeError):
            return previous[1] if previous else default

    def signature(self, path):
        previous = self.values.get(Path(path))
        return previous[0] if previous else None


def is_reference(task):
    return task.get("status") == "reference" or task.get("kind") in {"reference", "nest-reference", "reference-nest", "drawing-reference"} or "reference" in str(task.get("kind", ""))


def assumed_units(asset):
    return bool((asset.get("units") or {}).get("assumed"))


def is_ready(task):
    return task.get("status") in {"ready", "runnable"} and not task.get("missingAssets") and not is_reference(task)


def has_result(task):
    return (task.get("benchmark") or {}).get("status", "pending") not in {"pending", "queued", "not-run", "not-started", "blocked", None, ""}


class MaterialsStore:
    def __init__(self, workspace_root, files=None):
        self.workspace_root = Path(workspace_root).resolve()
        self.plane_root = (self.workspace_root / "TestData" / "Plane").resolve()
        if not self.plane_root.is_relative_to(self.workspace_root):
            raise ValueError("素材目录不在工作区内。")
        self.catalog_root = self.plane_root / "catalog"
        self.files = files or RecordFiles()
        self.lock = threading.RLock()
        self.last_scan = 0
        self.revision = 0
        self.signature = None
        self.catalog = {}
        self.tasks_by_id = {}
        self.assets_by_id = {}
        self.asset_tasks = {}
        self.result_errors = {}
        self.metrics_cache = {}
        self.refresh(force=True)

    def refresh(self, force=False):
        with self.lock:
            if not force and time.monotonic() - self.last_scan < .8:
                return
            self.last_scan = time.monotonic()
            catalog_path = self.catalog_root / "catalog.json"
            catalog = self.files.read(catalog_path, {})
            if not isinstance(catalog, dict):
                raise ValueError("素材目录必须是 JSON 对象。")
            assets = catalog.get("assets", [])
            tasks = catalog.get("tasks", [])
            if not isinstance(assets, list) or not isinstance(tasks, list):
                raise ValueError("素材与任务目录结构无效。")
            signatures = [self.files.signature(catalog_path)]
            self.progress = self.files.read(self.catalog_root / "progress.json", {})
            signatures.append(self.files.signature(self.catalog_root / "progress.json"))
            overlays = {}
            for path in sorted((self.catalog_root / "results").glob("*.json")):
                if not SAFE_ID.fullmatch(path.stem) or ".." in path.stem:
                    continue
                value = self.files.read(path)
                if isinstance(value, dict):
                    overlays[path.stem] = value
                signatures.append((path.name, self.files.signature(path)))
            live_records = {}
            for path in sorted((self.catalog_root / "live").glob("*.json")):
                if not SAFE_ID.fullmatch(path.stem) or ".." in path.stem:
                    continue
                descriptor = self.files.read(path)
                signatures.append(("live", path.name, self.files.signature(path)))
                if not isinstance(descriptor, dict) or descriptor.get("active") is not True:
                    continue
                record = {"descriptor": descriptor, "payload": None}
                try:
                    incumbent_path = self.incumbent_file(descriptor.get("incumbentPath", ""))
                    record["payload"] = self.files.read(incumbent_path)
                    record["incumbentSignature"] = self.files.signature(incumbent_path)
                    signatures.append(("incumbent", str(incumbent_path), self.files.signature(incumbent_path)))
                    if isinstance(record["payload"], dict):
                        signatures.append(("live-clock", path.stem, int(time.time())))
                except (ValueError, OSError) as error:
                    record["error"] = str(error)
                live_records[path.stem] = record
            signature = repr(signatures)
            if signature == self.signature:
                return
            task_ids = [row.get("id") for row in tasks]
            asset_ids = [row.get("id") for row in assets]
            if len(set(task_ids)) != len(tasks) or len(set(asset_ids)) != len(assets):
                raise ValueError("素材目录存在重复标识。")
            if any(not isinstance(value, str) or not SAFE_ID.fullmatch(value) or ".." in value for value in task_ids + asset_ids):
                raise ValueError("素材目录含无效标识。")
            self.catalog = catalog
            self.assets_by_id = {row["id"]: row for row in assets}
            self.tasks_by_id = {}
            self.asset_tasks = {row["id"]: [] for row in assets}
            self.result_errors = {}
            for original in tasks:
                task = dict(original)
                task.setdefault("benchmark", {"status": "pending"})
                overlay = overlays.get(task["id"])
                if overlay:
                    bound_id = overlay.get("taskId", overlay.get("id"))
                    expected_hash = task.get("inputSha256")
                    benchmark = overlay.get("benchmark")
                    run_signature = overlay.get("runSignature")
                    if not isinstance(run_signature, dict):
                        run_signature = {}
                    actual_hash = overlay.get("inputSha256", run_signature.get("inputSha256", benchmark.get("inputSha256") if isinstance(benchmark, dict) else None))
                    hash_matches = not expected_hash or actual_hash == expected_hash
                    expected_algorithm = (catalog.get("executionProfile") or {}).get("executableSha256")
                    actual_algorithm = run_signature.get("executableSha256") or (benchmark.get("executableSha256") if isinstance(benchmark, dict) else None)
                    if bound_id != task["id"] or not hash_matches or not isinstance(benchmark, dict):
                        self.result_errors[task["id"]] = "结果记录与本任务标识或输入版本不一致，未采用该记录。"
                        task["benchmark"] = {"status": "stale-input" if bound_id == task["id"] and not hash_matches else "binding-error", "reason": self.result_errors[task["id"]]}
                    elif expected_algorithm and actual_algorithm != expected_algorithm:
                        self.result_errors[task["id"]] = "算法已更新，等待本次重跑"
                        task["benchmark"] = {"status": "stale-algorithm", "reason": self.result_errors[task["id"]]}
                    else:
                        task["benchmark"] = overlay["benchmark"]
                        task["resultFile"] = True
                final_result = task.get("resultFile") and task["benchmark"].get("status") not in {"pending", "queued", "constructing"}
                if not final_result and task["id"] in live_records:
                    live, error = self.live_benchmark(task, live_records[task["id"]])
                    if live is not None:
                        task["benchmark"] = live
                        task["liveResult"] = True
                        self.result_errors.pop(task["id"], None)
                    elif error:
                        task["liveIssue"] = error
                if has_result(task) and task["benchmark"].get("status") not in OUTDATED_RESULTS and not all(
                        field in task["benchmark"] for field in ("materialArea", "sheetArea", "rollUsage")):
                    source_signature = ("live", live_records[task["id"]].get("incumbentSignature")) if task.get("liveResult") else (
                        "final", self.files.signature(self.catalog_root / "results" / (task["id"] + ".json")))
                    metric_signature = (self.files.signature(catalog_path), source_signature)
                    cached_metrics = self.metrics_cache.get(task["id"])
                    if cached_metrics is None or cached_metrics[0] != metric_signature:
                        cached_metrics = metric_signature, calculate_metrics(task, task["benchmark"])
                        self.metrics_cache[task["id"]] = cached_metrics
                    task["benchmark"] = dict(task["benchmark"])
                    task["benchmark"].update(cached_metrics[1])
                self.tasks_by_id[task["id"]] = task
                for part in task.get("parts", []):
                    asset_id = part.get("assetId")
                    if asset_id in self.asset_tasks:
                        self.asset_tasks[asset_id].append(task["id"])
            self.signature = signature
            self.revision += 1

    def summary(self):
        self.refresh()
        with self.lock:
            tasks = list(self.tasks_by_id.values())
            assets = list(self.assets_by_id.values())
            measured = [task for task in tasks if has_result(task) and task["benchmark"].get("status") not in OUTDATED_RESULTS | {"constructing"}]
            complete = [task for task in measured if task["benchmark"].get("valid") is True and task["benchmark"].get("complete") is True]
            return dict(revision=self.revision, available=bool(self.catalog), generatedAt=self.catalog.get("generatedAt"),
                        defaults=self.catalog.get("defaults", {}), sourceSummary=self.catalog.get("summary", {}),
                        archives=self.catalog.get("archives", []),
                        progress=self.progress,
                        executionProfile=self.catalog.get("executionProfile", {}),
                        collections=sorted({str(task.get("collection")) for task in tasks if task.get("collection")}),
                        packages=sorted({str(archive.get("directory")) for archive in self.catalog.get("archives", []) if isinstance(archive, dict) and archive.get("directory")}),
                        summary=dict(assets=len(assets), tasks=len(tasks), archives=len(self.catalog.get("archives", [])),
                                     ready=sum(is_ready(row) for row in tasks),
                                     missingAssets=sum(bool(row.get("missingAssets")) and row.get("status") != "source-invalid" for row in tasks),
                                     sourceContourTasks=sum(is_ready(row) and row.get("inputSource") == "source-contours" for row in tasks),
                                     missingDxfTasks=sum(bool(row.get("missingDxfAssets")) for row in tasks),
                                     sourceInvalidTasks=sum(row.get("status") == "source-invalid" for row in tasks),
                                     references=sum(is_reference(row) for row in tasks),
                                     assumedUnitAssets=sum(assumed_units(row) for row in assets),
                                     executed=len(measured), complete=len(complete),
                                     constructing=sum(task["benchmark"].get("status") == "constructing" for task in tasks),
                                     pending=sum(is_ready(row) and (not has_result(row) or row["benchmark"].get("status") in OUTDATED_RESULTS) for row in tasks), bindingErrors=len(self.result_errors),
                                     assumedQuantityRows=sum(part.get("quantitySource") == "benchmark-default-one" for task in tasks for part in task.get("parts", [])),
                                     statuses=dict(Counter(row.get("status", "unknown") for row in tasks))))

    @staticmethod
    def _source_package(source):
        return str(source).replace("\\", "/").split("/")[0]

    def _asset_in_collection(self, asset, collection):
        return collection in asset.get("collections", []) or any(self._source_package(source) == collection for source in asset.get("sourcePaths", []))

    def _task_in_collection(self, task, collection):
        return collection in {task.get("collection"), task.get("archive"), task.get("archiveId"), self._source_package(task.get("sourcePath", ""))} or any(
            self._source_package(part.get("sourcePath", "")) == collection or self._source_package((part.get("geometrySource") or {}).get("path", "")) == collection
            or self._asset_in_collection(self.assets_by_id.get(part.get("assetId"), {}), collection)
            for part in task.get("parts", []))

    def _part_assumed_units(self, part):
        geometry_source = part.get("geometrySource") or {}
        if geometry_source.get("kind") == "source-contour":
            units = geometry_source.get("units")
            return bool(units.get("assumed", False)) if isinstance(units, dict) else not (isinstance(units, str) and units.casefold() in {"mm", "millimetres", "millimeters"})
        return assumed_units(self.assets_by_id.get(part.get("assetId"), {}))

    def _task_matches(self, task, collection="", filter="", q=""):
        if collection and not self._task_in_collection(task, collection):
            return False
        if filter == "ready" and not is_ready(task):
            return False
        if filter == "missing" and (not task.get("missingAssets") or task.get("status") == "source-invalid"):
            return False
        if filter == "missing-dxf" and not task.get("missingDxfAssets"):
            return False
        if filter == "source-contours" and (not is_ready(task) or task.get("inputSource") != "source-contours"):
            return False
        if filter == "source-invalid" and task.get("status") != "source-invalid":
            return False
        if filter == "import-blocked" and task.get("status") not in {"unsupported-geometry", "needs-review"}:
            return False
        if filter == "reference" and not is_reference(task):
            return False
        if filter == "assumed-units" and not any(self._part_assumed_units(row) for row in task.get("parts", [])):
            return False
        if filter == "pending" and (not is_ready(task) or (has_result(task) and task["benchmark"].get("status") not in OUTDATED_RESULTS)):
            return False
        if filter == "executed" and (not has_result(task) or task["benchmark"].get("status") in OUTDATED_RESULTS):
            return False
        if q and q.casefold() not in " ".join(str(task.get(k, "")) for k in ("id", "label", "collection", "kind")).casefold():
            return False
        return True

    @staticmethod
    def pagination(rows, offset, limit):
        if type(offset) is not int or offset < 0 or type(limit) is not int or not 1 <= limit <= 200:
            raise ValueError("分页范围无效；每页限 1–200 条。")
        return dict(total=len(rows), offset=offset, limit=limit, rows=rows[offset:offset + limit])

    def task_summary(self, task):
        row = {key: task.get(key) for key in ("id", "label", "collection", "kind", "status", "stocks", "sourceStock", "issues", "missingAssets", "inputSource", "missingDxfAssets", "invalidSourceShapes")}
        quantities = [part.get("quantity") for part in task.get("parts", [])]
        row.update(types=len(quantities), required=sum(quantities) if all(type(value) is int and value >= 0 for value in quantities) else None,
                   assumedUnits=any(self._part_assumed_units(part) for part in task.get("parts", [])),
                   assumedQuantities=sum(part.get("quantitySource") == "benchmark-default-one" for part in task.get("parts", [])),
                   benchmark={key: (task.get("benchmark") or {}).get(key) for key in
                              ("status", "nativeWallSeconds", "boards", "placed", "required", "complete", "valid", "utilization", "reason",
                               "live", "timingScope", "snapshotWallSeconds", "firstCompleteWallSeconds", "materialArea", "sheetArea", "rollUsage", "constructionTraceError")})
        return row

    def tasks(self, offset=0, limit=50, **filters):
        self.refresh()
        with self.lock:
            rows = [self.task_summary(task) for task in self.tasks_by_id.values() if self._task_matches(task, **filters)]
            return dict(revision=self.revision, **self.pagination(rows, offset, limit))

    def incumbent_file(self, relative):
        relative = str(relative).replace("\\", "/")
        parts = PurePosixPath(relative).parts
        if not parts or PurePosixPath(relative).is_absolute() or ".." in parts:
            raise ValueError("直播候选路径无效。")
        path = (self.plane_root / relative).resolve()
        raw_root = (self.catalog_root / "raw").resolve()
        if not raw_root.is_relative_to(self.plane_root) or not path.is_relative_to(raw_root) or not path.name.endswith(".incumbent.json"):
            raise ValueError("直播候选只允许 catalog/raw 内的 .incumbent.json 文件。")
        return path

    def live_benchmark(self, task, record):
        if record.get("error"):
            return None, record["error"]
        descriptor = record["descriptor"]
        signature = descriptor.get("runSignature") or {}
        if not isinstance(signature, dict) or descriptor.get("taskId") != task["id"]:
            return None, "直播记录与当前任务标识不一致，未采用。"
        expected_input = task.get("inputSha256")
        expected_algorithm = (self.catalog.get("executionProfile") or {}).get("executableSha256")
        if not expected_input or not expected_algorithm:
            return None, "直播缺少本轮输入或算法版本，暂不采用。"
        if signature.get("inputSha256") != expected_input:
            return None, "直播输入版本与当前任务不一致，未采用。"
        if signature.get("executableSha256") != expected_algorithm:
            return None, "直播算法版本与本轮程序不一致，未采用。"
        payload = record.get("payload")
        if not isinstance(payload, dict):
            return None, None
        result = payload.get("result")
        if payload.get("live") is not True or not isinstance(result, dict) or result.get("valid") is not True or result.get("complete") is not True:
            return None, "直播候选尚未通过原生几何与完整性检查，未作为初始完整解发布。"
        parts = task.get("parts", [])
        required = task.get("required", sum(part.get("quantity", 0) for part in parts))
        if type(result.get("placed")) is not int or result["placed"] != required:
            return None, "直播候选的原件数量与当前任务不一致，未采用。"
        layouts = result.get("layouts")
        if not isinstance(layouts, list) or not isinstance(payload.get("problem"), dict):
            return None, "直播候选缺少原始轮廓或布局，未采用。"
        if result.get("boards", len(layouts)) != len(layouts) or any(not isinstance(board, dict) or any(
                not isinstance(board.get(field), (int, float)) or not math.isfinite(board[field]) or board[field] <= 0
                for field in ("width", "height")) for board in layouts):
            return None, "直播候选板材记录无效，未采用。"
        snapshot_seconds = payload.get("wholeJobWallSeconds")
        if not isinstance(snapshot_seconds, (int, float)) or not math.isfinite(snapshot_seconds) or snapshot_seconds < 0:
            return None, "直播候选的原生时间无效，未采用。"
        started = descriptor.get("startedAtUnix")
        running_clock = isinstance(started, (int, float)) and math.isfinite(started) and started > 0
        elapsed = max(snapshot_seconds, time.time() - started) if running_clock else snapshot_seconds
        validation = payload.get("validation")
        area = validation.get("materialArea") if isinstance(validation, dict) else None
        sheet_area = sum(board.get("width", 0) * board.get("height", 0) for board in layouts)
        utilization = area / sheet_area if isinstance(area, (int, float)) and math.isfinite(area) and sheet_area > 0 else None
        return dict(status="constructing", live=True, valid=True, complete=True, placed=result["placed"], required=required,
                    boards=result.get("boards", len(layouts)), layouts=layouts, problem=payload["problem"],
                    diagnostics=result.get("diagnostics", []), nativeWallSeconds=elapsed, snapshotWallSeconds=snapshot_seconds,
                    timingScope="live-current" if running_clock else "live-snapshot", utilization=utilization,
                    materialArea=area, materialAreaSource="native-validation" if isinstance(area, (int, float)) else None,
                    remnants=result.get("remnants"),
                    firstCompleteWallSeconds=payload.get("firstCompleteWallSeconds"), settings=payload.get("settings", {}),
                    inputSha256=expected_input, executableSha256=expected_algorithm), None

    def task(self, task_id):
        self.refresh()
        with self.lock:
            task = self.tasks_by_id[identifier(task_id)]
            task = dict(task, constructionTrace=self._construction_trace(task),sourceQuantityRows=source_quantity_rows(task))
            asset_ids = {part.get("assetId") for part in task.get("parts", [])}
            return dict(revision=self.revision, task=task, defaults=self.catalog.get("defaults", {}),
                        assets=[self.asset_view(self.assets_by_id[asset_id]) for asset_id in asset_ids if asset_id in self.assets_by_id],
                        resultError=self.result_errors.get(task_id))

    def construction_trace(self, task_id):
        self.refresh()
        with self.lock:
            return self._construction_trace(self.tasks_by_id[identifier(task_id)])

    def _construction_trace(self, task):
        """Bind observations to this exact native invocation, without reading layouts."""
        missing = dict(available=False, status="missing", reason="本次任务尚未生成原生过程记录。", taskId=task["id"])
        live = self.files.read(self.catalog_root / "live" / (task["id"] + ".json"), {})
        final = self.files.read(self.catalog_root / "results" / (task["id"] + ".json"), {})
        active = isinstance(live, dict) and live.get("active") is True
        record = live if active else final
        if not isinstance(record, dict):
            return missing
        value = record if active else record.get("benchmark", {})
        descriptor = value.get("nativeConstructionTrace") if isinstance(value, dict) else None
        observation_error = value.get("constructionTraceError") if isinstance(value, dict) else None
        if not isinstance(descriptor, dict) and not observation_error:
            return missing
        signature = record.get("runSignature")
        profile = self.catalog.get("executionProfile") or {}
        expected_input, expected_exe = task.get("inputSha256"), profile.get("executableSha256")
        def rejected(reason):
            return dict(missing, status="binding-error", reason=reason)
        if record.get("taskId") != task["id"] or not isinstance(signature, dict):
            return rejected("过程记录与本任务标识或运行条件不一致，未采用。")
        if not expected_input or not expected_exe or signature.get("inputSha256") != expected_input or signature.get("executableSha256") != expected_exe:
            return rejected("过程记录与本轮输入或算法版本不一致，未采用。")
        if any(key in profile and signature.get(key) != profile[key] for key in ("timeLimitSeconds", "maxPlacementAttempts", "workers", "caseConcurrency", "constructionTraceFormat")):
            return rejected("过程记录与本轮运行条件不一致，未采用。")
        if not isinstance(descriptor, dict):
            return dict(missing, status="observation-error", reason="本次原生过程采集异常："+str(observation_error))
        if descriptor.get("inputSha256") != expected_input or descriptor.get("executableSha256") != expected_exe or descriptor.get("runSignature") != signature:
            return rejected("过程记录的输入、算法或运行绑定不一致，未采用。")
        try:
            index_path = self._raw_record_path(descriptor.get("indexPath"), ".trace-index.json")
            raw_path = self._raw_record_path(value.get("rawPath"), ".json")
            if index_path != raw_path.with_suffix(".trace-index.json"):
                raise ValueError("过程索引不是本次原生输出的邻接文件。")
        except (ValueError, TypeError) as error:
            return rejected(str(error))
        # Even stat/link operations can race with native atomic replacement on
        # Windows. Do not touch the producer index until its live run ends.
        available = not active and index_path.is_file()
        return dict(available=available, status="recording" if active else "finished", taskId=task["id"],
                    reason="运行中，完成后读取过程记录。" if active else "本任务的原生过程记录已绑定。",
                    indexPath=index_path.relative_to(self.plane_root).as_posix(), rawPath=raw_path.relative_to(self.plane_root).as_posix(),
                    inputSha256=expected_input, executableSha256=expected_exe, runSignature=signature)

    def _raw_record_path(self, relative, suffix):
        if not isinstance(relative, str) or not relative:
            raise ValueError("过程记录路径缺失。")
        relative = relative.replace("\\", "/")
        pieces = PurePosixPath(relative)
        if pieces.is_absolute() or ".." in pieces.parts or ":" in relative:
            raise ValueError("过程记录路径无效。")
        candidate = self.plane_root / relative
        # Resolve the parent for containment, never an actively replaced file.
        path = candidate.parent.resolve() / candidate.name
        raw_root = (self.catalog_root / "raw").resolve()
        if not raw_root.is_relative_to(self.plane_root) or not path.is_relative_to(raw_root) or path.parent != raw_root or not path.name.endswith(suffix):
            raise ValueError("过程记录必须位于本次原生输出目录。")
        return path

    def asset_view(self, asset):
        row = dict(asset)
        row["taskCount"] = len(set(self.asset_tasks.get(asset["id"], [])))
        row["downloadUrl"] = "/api/materials/assets/" + asset["id"] + "/dxf"
        return row

    def assets(self, offset=0, limit=50, collection="", filter="", q=""):
        self.refresh()
        with self.lock:
            relevant = None
            if filter in {"ready", "missing", "missing-dxf", "source-contours", "source-invalid", "import-blocked", "reference", "pending", "executed"}:
                relevant = {part.get("assetId") for task in self.tasks_by_id.values()
                            if self._task_matches(task, collection=collection, filter=filter)
                            for part in task.get("parts", [])}
            rows = []
            for asset in self.assets_by_id.values():
                if relevant is not None and asset["id"] not in relevant:
                    continue
                if collection and relevant is None and not self._asset_in_collection(asset, collection):
                    continue
                if filter == "assumed-units" and not assumed_units(asset):
                    continue
                if q and q.casefold() not in " ".join(str(asset.get(k, "")) for k in ("id", "name", "relativePath", "sourcePaths")).casefold():
                    continue
                rows.append(self.asset_view(asset))
            return dict(revision=self.revision, **self.pagination(rows, offset, limit))

    def asset(self, asset_id):
        self.refresh()
        with self.lock:
            asset = self.assets_by_id[identifier(asset_id)]
            return dict(revision=self.revision, asset=self.asset_view(asset),
                        tasks=[self.task_summary(self.tasks_by_id[task_id]) for task_id in dict.fromkeys(self.asset_tasks.get(asset_id, []))])

    def dxf_file(self, asset_id):
        self.refresh()
        with self.lock:
            asset = self.assets_by_id[identifier(asset_id)]
            relative = str(asset.get("relativePath", "")).replace("\\", "/")
            parts = PurePosixPath(relative).parts
            if not parts or parts[0].casefold() != "dxf" or ".." in parts or PurePosixPath(relative).is_absolute():
                raise ValueError("DXF 下载只允许集中 DXF 目录中的文件。")
            path = (self.plane_root / relative).resolve()
            if not path.is_relative_to((self.plane_root / "DXF").resolve()) or path.suffix.casefold() != ".dxf":
                raise ValueError("DXF 下载文件越过允许范围。")
            if not path.is_file():
                raise FileNotFoundError(path.name)
            return path

    def csv_file(self, name):
        if name not in CSV_FILES:
            raise ValueError("仅提供汇总目录中的指定 CSV。")
        path = (self.catalog_root / name).resolve()
        if not path.is_relative_to(self.catalog_root.resolve()):
            raise ValueError("CSV 文件越过允许范围。")
        if not path.is_file():
            raise FileNotFoundError(name)
        return path
