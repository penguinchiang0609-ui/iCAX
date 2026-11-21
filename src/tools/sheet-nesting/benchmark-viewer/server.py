"""Read-only live viewer for original-input native nesting benchmark records."""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import re
import threading
import time
from urllib.parse import urlsplit, parse_qs, unquote

from construction_store import ConstructionStore
from materials_store import MaterialsStore
from json_file_io import read_json_bytes

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SCORE_FIELDS = {
    "comparisonGroup", "targetKind", "metric", "target", "targetComparable",
    "noComparableTargetReason", "executionStatus", "reviewStatus", "geometryValid",
    "complete", "certifiedComplete", "numericRelation", "reportedPrecisionRelation",
    "qualityReached", "qualityWithinMinute", "nativeWithinMinute", "endToEndWithinMinute",
    "nativeWallSeconds", "wholeProcessWallSeconds", "nativeValid", "nativeComplete",
    "nativePlaced", "required", "rawSha256", "nativeState", "firstCompleteWallSeconds",
    "lastImprovementWallSeconds", "eligibleMethods", "startedMethods", "actualWorkers",
    "fairScheduling", "targetStop", "deadlineStop", "selectedRoute", "candidateMethods",
    "inapplicableMethods", "reviewSha256", "placed", "boards", "auditMethod",
    "auditTolerance", "occupiedLengthExact", "targetLengthExact", "occupiedLength",
    "extentMethod", "reportedLengthExact", "qualityRelationUsed", "completeButQualityNotReached",
    "targetNotReachedIncludingIncomplete", "errors", "blockedReasons",
}


def iso_time(timestamp):
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat()


def within_minute(value):
    return isinstance(value, (int, float)) and math.isfinite(value) and 0 <= value <= 60


class JsonFiles:
    """Keep the last complete record while another process replaces a file."""
    def __init__(self):
        self.values = {}

    def read(self, path, default=None):
        path = Path(path)
        previous = self.values.get(path)
        try:
            stat = path.stat()
            signature = (stat.st_mtime_ns, stat.st_size)
            if previous and previous[0] == signature:
                return previous[1]
            content = read_json_bytes(path)
            value = json.loads(content.decode("utf-8-sig"))
            self.values[path] = (signature, value, hashlib.sha256(content).hexdigest())
            return value
        except (OSError, ValueError, UnicodeError):
            return previous[1] if previous else default

    def signature(self, path):
        value = self.values.get(Path(path))
        return value[0] if value else None

    def digest(self, path):
        value = self.values.get(Path(path))
        return value[2] if value else None


def route_measurements(diagnostics):
    data = {}
    line = next((s for s in diagnostics if s.startswith("parallel.construction portfolio ")), "")
    for key, field in (("eligible", "eligibleMethods"), ("started", "startedMethods"),
                       ("workers", "actualWorkers"), ("target-stop", "targetStop"),
                       ("deadline", "deadlineStop")):
        match = re.search(r"(?:^| )" + re.escape(key) + r"=(\d+)", line)
        if match:
            data[field] = int(match.group(1))
    match = re.search(r" selected=([^ ]+)", line)
    if match:
        data["selectedRoute"] = match.group(1)
    return data


class BenchmarkStore:
    def __init__(self, batch, workspace_root=ROOT):
        self.batch = Path(batch).resolve()
        self.workspace_root = Path(workspace_root).resolve()
        self.files = JsonFiles()
        self.lock = threading.RLock()
        self.revision = 0
        self.last_scan = 0.0
        self.snapshot_signature = None
        self.rows = []
        self.executions = {}
        self.reviews = {}
        self.score = {}
        self.progress = {}
        self.configuration = self.files.read(self.batch / "run-configuration.json", {})
        records = []
        for filename in ("prepared-index-1420.json", "supplemental-index-35.json", "additions-index.json"):
            document = self.files.read(self.batch / "indexes" / filename, {})
            records.extend(document.get("rows", []))
        if not records:
            raise ValueError("批次目录没有原始案例索引。")
        if len({row["id"] for row in records}) != len(records):
            raise ValueError("批次索引存在重复案例标识。")
        self.originals = {index: row for index, row in enumerate(records, 1)}
        self.indices = {row["id"]: index for index, row in self.originals.items()}
        self.refresh(force=True)

    def refresh(self, force=False):
        with self.lock:
            if not force and time.monotonic() - self.last_scan < 0.8:
                return
            self.last_scan = time.monotonic()
            scores_path = self.batch / "case-scores.json"
            scores = self.files.read(scores_path, {})
            self.score = self.files.read(self.batch / "score-progress.json", {})
            self.progress = self.files.read(self.batch / "progress.json", {})
            self.highlights = self.files.read(self.batch / "highlights.json", [])
            signatures = [self.files.signature(scores_path), self.files.signature(self.batch / "score-progress.json")]
            for folder, destination in (("executions", self.executions), ("reviews", self.reviews)):
                for path in sorted((self.batch / folder).glob("*.json")):
                    if not path.stem.isdecimal():
                        continue
                    index = int(path.stem)
                    if index not in self.originals:
                        continue
                    record = self.files.read(path)
                    if record is not None:
                        destination[index] = record
                        signatures.append((folder, index, self.files.signature(path)))
            for highlight in self.highlights:
                verification_path = self.local_batch_file(highlight.get("verification", ""))
                self.files.read(verification_path, {})
                signatures.append(self.files.signature(verification_path))
            signature = repr(signatures)
            if signature == self.snapshot_signature:
                return
            self.snapshot_signature = signature
            scored = {r["id"]: r for r in scores.get("cases", [])}
            self.rows = [self.case_row(i, r, scored.get(r["id"], {})) for i, r in self.originals.items()]
            self.revision += 1

    def case_row(self, index, original, scored):
        row = {key: value for key, value in scored.items() if key in SCORE_FIELDS}
        target = original.get("target", {})
        qualification = original.get("qualification", {})
        row.update(index=index, id=original["id"], title=original.get("title", original["id"]),
                   source=original.get("source", ""), inputStatus=original.get("status", "blocked"),
                   target=target, targetKind=target.get("kind", "none"),
                   qualification={k: qualification.get(k) for k in ("targetComparable", "profile", "reason", "sourceConditionComplete")},
                   blockedReasons=original.get("blockedReasons", []))
        has_target = target.get("kind") not in (None, "none", "paper-F-K") and (
            target.get("length") is not None or target.get("boards") is not None or target.get("utilization") is not None)
        row.setdefault("comparisonGroup", "public" if qualification.get("targetComparable") else "conditional" if has_target else "no-target")
        row.setdefault("executionStatus", "blocked" if row["inputStatus"] == "blocked" else "pending")
        row.setdefault("reviewStatus", "not-applicable" if row["inputStatus"] == "blocked" else "pending")
        row.setdefault("geometryValid", None)
        row.setdefault("certifiedComplete", False)
        row.setdefault("qualityReached", None)
        row.setdefault("errors", [])
        row.setdefault("required", original.get("requiredInstances") or original.get("originalConditions", {}).get("quantity"))
        execution = self.executions.get(index)
        review = self.reviews.get(index)
        if execution:
            row.update(executionStatus=execution.get("status"), nativeWallSeconds=execution.get("nativeWallSeconds"),
                       wholeProcessWallSeconds=execution.get("wholeProcessWallSeconds"),
                       nativeValid=execution.get("nativeValid"), nativeComplete=execution.get("nativeComplete"),
                       placed=execution.get("nativePlaced"), boards=execution.get("nativeBoards"),
                       required=execution.get("required", row.get("required")), rawSha256=execution.get("rawSha256"),
                       nativeWithinMinute=within_minute(execution.get("nativeWallSeconds")),
                       endToEndWithinMinute=within_minute(execution.get("wholeProcessWallSeconds")))
            signature = self.files.signature(self.batch / "executions" / f"{index:04d}.json")
            row["completedAt"] = iso_time(signature[0] / 1e9) if signature else None
            row.update(route_measurements(execution.get("diagnostics", [])))
            if execution.get("id") != original["id"] or execution.get("inputSha256") != original.get("inputSha256"):
                row.update(geometryValid=False, certifiedComplete=False, reviewStatus="binding-error",
                           errors=["执行记录与原始案例不一致。"])
            elif review:
                input_hash = review.get("inputSha256", review.get("inputHash"))
                raw_hash = review.get("rawSha256", review.get("rawHash"))
                bound = review.get("id") == original["id"] and input_hash == execution.get("inputSha256") and raw_hash == execution.get("rawSha256")
                row.update(reviewStatus=review.get("status", "reviewed") if bound else "binding-error",
                           geometryValid=bool(bound and review.get("valid")),
                           certifiedComplete=bool(bound and review.get("valid") and review.get("complete") and execution.get("nativeValid") and execution.get("nativeComplete")),
                           complete=bool(bound and review.get("valid") and review.get("complete") and execution.get("nativeComplete")),
                           errors=review.get("errors", []) if bound else ["审核记录与本轮原生结果不一致。"],
                           auditMethod=review.get("method"),
                           auditTolerance=review.get("tolerance", {"geometryMm": review.get("geometricComparisonTolerance")}),
                           placed=review.get("placed", row.get("placed")), boards=review.get("boards", row.get("boards")))
                if (scored.get("rawSha256") != execution.get("rawSha256") or
                    scored.get("reviewSha256") != self.files.digest(self.batch / "reviews" / f"{index:04d}.json")):
                    row["qualityReached"] = None
            else:
                row.update(geometryValid=None, certifiedComplete=False, complete=None, reviewStatus="pending", qualityReached=None)
        return row

    def summary(self):
        self.refresh()
        with self.lock:
            live = dict(total=len(self.rows), runnable=sum(r["inputStatus"] == "ready" for r in self.rows),
                        executed=len(self.executions), reviewed=len(self.reviews),
                        blocked=sum(r["inputStatus"] == "blocked" for r in self.rows),
                        valid=sum(r.get("geometryValid") is True for r in self.rows),
                        complete=sum(r.get("certifiedComplete") is True for r in self.rows),
                        awaitingReview=sum(i not in self.reviews for i in self.executions),
                        pending=sum(r["inputStatus"] == "ready" and r["index"] not in self.executions for r in self.rows))
            main = self.highlights[0] if self.highlights else {}
            verification = self.files.read(self.local_batch_file(main.get("verification", "")), {}) if main else None
            return dict(revision=self.revision, score=self.score, execution=self.progress, live=live,
                        g025=verification, configuration=self.configuration_summary())

    def configuration_summary(self):
        return {key: self.configuration.get(key) for key in (
            "strategy", "candidateMethods", "internalWorkersPerCase", "caseConcurrency", "secondsPerCase",
            "attemptsPerCase", "minuteConformanceSeconds", "postprocessing", "priorLayoutInput", "regression")}

    def cases(self):
        self.refresh()
        with self.lock:
            return dict(revision=self.revision, rows=self.rows)

    def local_batch_file(self, name):
        path = (self.batch / name).resolve()
        if not path.is_relative_to(self.batch):
            raise ValueError("文件不在当前报告批次内。")
        return path

    def detail(self, index):
        self.refresh()
        with self.lock:
            original = self.originals[index]
            case = next(r for r in self.rows if r["index"] == index)
            execution = self.executions.get(index)
            review = self.reviews.get(index)
            native = self.files.read(self.evidence_file(index, "raw"), {}) if execution else {}
            result = native.get("result")
            if result is None and native.get("runs"):
                result = native["runs"][0].get("construction", native["runs"][0].get("selected"))
            evidence = {}
            for kind in ("raw", "review", "input", "log"):
                try:
                    if self.evidence_file(index, kind).is_file():
                        evidence[kind] = f"/evidence/{index}/{kind}"
                except (FileNotFoundError, ValueError):
                    pass
            return dict(case=case, execution=execution, review=review, problem=native.get("problem"), result=result,
                        inputConditions=original.get("inputConditions", {}), originalConditions=original.get("originalConditions", {}),
                        configuration=self.configuration_summary(),
                        evidence=evidence)

    def highlight(self):
        self.refresh()
        with self.lock:
            if not self.highlights:
                raise KeyError("没有专项结果。")
            record = self.highlights[0]
            verification = self.files.read(self.local_batch_file(record["verification"]), {})
            native = self.files.read(self.local_batch_file(record["raw"]), {})
            result = native.get("result", {})
            valid = bool(verification.get("independentExactRationalAuditAllPassed"))
            case = dict(id=record["id"], title=record["title"], source="专项重复验证", inputStatus="ready",
                        comparisonGroup="public", target=record.get("target", {}), targetKind="boards",
                        certifiedComplete=valid and result.get("complete", False), geometryValid=valid,
                        nativeComplete=result.get("complete"), placed=result.get("placed"), boards=len(result.get("layouts", [])),
                        required=sum(p["quantity"] for p in native.get("problem", {}).get("parts", [])),
                        nativeWallSeconds=native.get("wallSeconds"), medianWallSeconds=verification.get("medianWallSeconds"),
                        qualityReached=valid and result.get("complete", False) and len(result.get("layouts", [])) <= record.get("target", {}).get("boards", 0),
                        reviewStatus="reviewed" if valid else "pending", executionStatus="native-result", **route_measurements(result.get("diagnostics", [])))
            return dict(case=case, verification=verification, problem=native.get("problem"), result=result,
                        execution=None, review=None, configuration=self.configuration_summary(),
                        description=record.get("description"), evidence={"verification": "/assets/highlight-verification.json", "image": "/assets/g025.png"})

    def evidence_file(self, index, kind):
        original = self.originals[index]
        if kind == "input":
            if not original.get("input"):
                raise FileNotFoundError("该案例没有可回放输入。")
            path = Path(original["input"]).resolve()
        elif kind == "raw":
            path = Path(self.executions.get(index, {}).get("raw", self.batch / "raw" / f"{index:04d}.json")).resolve()
        elif kind in ("review", "log"):
            path = self.batch / ("reviews" if kind == "review" else "raw") / f"{index:04d}.{'json' if kind == 'review' else 'log'}"
        else:
            raise KeyError(kind)
        if not path.is_relative_to(self.workspace_root):
            raise ValueError("证据文件不在当前工作区内。")
        return path


class ViewerHandler(BaseHTTPRequestHandler):
    server_version = "NestingReport/1"

    def log_message(self, *_args):
        pass

    def send_bytes(self, data, content_type, attachment=None):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'")
        if attachment:
            self.send_header("Content-Disposition", f'attachment; filename="{attachment}"')
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def send_json(self, value):
        self.send_bytes(json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8"), "application/json; charset=utf-8")

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = urlsplit(self.path).path
        store = self.server.store
        try:
            if path.startswith("/api/construction/"):
                construction = getattr(self.server, "construction_store", None)
                if construction is None:
                    materials = getattr(self.server, "materials_store", None)
                    if materials is None:
                        materials = MaterialsStore(store.workspace_root, files=store.files)
                        self.server.materials_store = materials
                    construction = ConstructionStore(store.workspace_root, materials=materials)
                    self.server.construction_store = construction
                pieces = [unquote(value) for value in path.split("/")[3:]]
                query = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
                material = len(pieces) > 1 and pieces[0] == "materials"
                if material:
                    pieces = pieces[1:]
                if pieces == ["tasks"]:
                    self.send_json(construction.tasks(query.get("materialTask", [None])[0]))
                elif len(pieces) == 2 and pieces[0] == "audits":
                    self.send_bytes(construction.audit(pieces[1]), "text/plain; charset=utf-8")
                elif len(pieces) == 1:
                    self.send_json(construction.metadata(pieces[0], material=material))
                elif len(pieces) == 2 and pieces[1] == "events":
                    offset = int(query.get("offset", ["0"])[0])
                    limit = int(query.get("limit", ["25"])[0])
                    if offset < 0 or not 1 <= limit <= 100:
                        raise ValueError("分页范围无效；每页限 1–100 条。")
                    filters = {name: query[name][0] for name in ("route", "scope", "stage") if name in query}
                    if "view" in query:
                        if query["view"][0] not in ("events", "construction"):
                            raise ValueError("无效的过程查看方式。")
                        filters["view"] = query["view"][0]
                        filters["path"] = query.get("path", ["selected"])[0]
                    if "ordinal" in query:
                        filters["ordinal"] = int(query["ordinal"][0])
                    self.send_json(construction.page(pieces[0], material=material, offset=offset, limit=limit, **filters))
                elif len(pieces) == 3 and pieces[1] == "events" and pieces[2].isdecimal():
                    self.send_json(construction.detail(pieces[0], int(pieces[2]), material=material))
                elif len(pieces) == 2 and pieces[1] in ("index", "result"):
                    self.send_bytes(construction.evidence(*pieces, material=material), "application/json; charset=utf-8")
                else:
                    self.send_error(404, "Record not available")
            elif path == "/api/materials" or path.startswith("/api/materials/"):
                materials = getattr(self.server, "materials_store", None)
                if materials is None:
                    materials = MaterialsStore(store.workspace_root, files=store.files)
                    self.server.materials_store = materials
                pieces = [unquote(value) for value in path.split("/")[3:]]
                query = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
                if not pieces or pieces == [""] or pieces == ["summary"]:
                    self.send_json(materials.summary())
                elif pieces in (["tasks"], ["assets"]):
                    filters = {key: query.get(key, [""])[0] for key in ("collection", "filter", "q")}
                    filters.update(offset=int(query.get("offset", ["0"])[0]), limit=int(query.get("limit", ["50"])[0]))
                    self.send_json((materials.tasks if pieces == ["tasks"] else materials.assets)(**filters))
                elif len(pieces) == 2 and pieces[0] == "tasks":
                    self.send_json(materials.task(pieces[1]))
                elif len(pieces) == 2 and pieces[0] == "assets":
                    self.send_json(materials.asset(pieces[1]))
                elif len(pieces) == 3 and pieces[0] == "assets" and pieces[2] == "dxf":
                    filename = materials.dxf_file(pieces[1])
                    self.send_bytes(filename.read_bytes(), "application/dxf", pieces[1] + ".dxf")
                elif len(pieces) == 2 and pieces[0] == "csv":
                    filename = materials.csv_file(pieces[1])
                    self.send_bytes(filename.read_bytes(), "text/csv; charset=utf-8", filename.name)
                else:
                    self.send_error(404, "Record not available")
            elif path == "/api/summary":
                self.send_json(store.summary())
            elif path == "/api/cases":
                self.send_json(store.cases())
            elif path == "/api/g025":
                self.send_json(store.highlight())
            elif re.fullmatch(r"/api/cases/\d+", path):
                self.send_json(store.detail(int(path.rsplit("/", 1)[1])))
            elif re.fullmatch(r"/evidence/\d+/(raw|review|input|log)", path):
                _, _, index, kind = path.split("/")
                filename = store.evidence_file(int(index), kind)
                self.send_bytes(filename.read_bytes(), "text/plain; charset=utf-8", f"case-{index}-{kind}{filename.suffix}")
            elif path in ("/assets/g025.png", "/assets/highlight-verification.json"):
                record = store.highlights[0]
                key = "image" if path.endswith(".png") else "verification"
                self.send_bytes(store.local_batch_file(record[key]).read_bytes(), "image/png" if key == "image" else "application/json; charset=utf-8")
            elif path in ("/reports/current", "/reports/csv"):
                filename = store.batch / ("批测达标报告.md" if path.endswith("current") else "达标情况.csv")
                self.send_bytes(filename.read_bytes(), "text/plain; charset=utf-8", "nesting-report.md" if path.endswith("current") else "nesting-results.csv")
            elif path in ("/", "/index.html", "/viewer.js", "/viewer.css", "/construction.html", "/construction.js", "/construction.css", "/materials.html", "/materials.js", "/materials.css", "/layoutGeometry.js"):
                filename = HERE / ("index.html" if path == "/" else path.lstrip("/"))
                content_type = {".html": "text/html", ".js": "text/javascript", ".css": "text/css"}[filename.suffix]
                self.send_bytes(filename.read_bytes(), content_type + "; charset=utf-8")
            else:
                self.send_error(404, "Not found")
        except (KeyError, IndexError, FileNotFoundError):
            self.send_error(404, "Record not available")
        except (ValueError, OSError) as error:
            self.send_response(503)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            self.wfile.write(json.dumps({"error": str(error)}, ensure_ascii=False).encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description="本地只读排样实时报告；不会运行或修改求解器。")
    parser.add_argument("--batch", required=True, type=Path)
    parser.add_argument("--port", type=int, default=8768)
    args = parser.parse_args()
    store = BenchmarkStore(args.batch)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), ViewerHandler)
    server.daemon_threads = True
    server.store = store
    print(json.dumps({"url": f"http://127.0.0.1:{server.server_port}/", "processId": __import__("os").getpid(), "batch": str(store.batch), "readOnly": True}), flush=True)
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
