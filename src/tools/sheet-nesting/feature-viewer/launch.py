"""Run one original-input trace demonstration and export a self-contained viewer."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import time
import webbrowser


ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
CASES = ROOT / "output/work/sheet-nesting/public-input-inventory-20261106/publicbench-cases-221-source-bound.json"
AUDIT_DRIVER = ROOT / "output/work/sheet-nesting/rotation-compaction-20261106/run_public.py"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def load_auditor():
    spec = importlib.util.spec_from_file_location("feature_viewer_original_audit", AUDIT_DRIVER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(case, result, trace):
    auditor = load_auditor()
    frame = "source-free" if case.get("rotationDomain") == "free" else "original"
    auditor.verify_original_problem(case, result["problem"], frame)
    auditor.verify_original_problem(case, trace["problem"], frame)
    run = result["runs"][0]
    selected = run["selected"]
    strict = auditor.exact_audit(result["problem"], selected)
    same_phases = all(run[phase] == selected for phase in ["construction", "before", "candidate"])
    final_events = [event for event in trace["events"] if event["stage"] == "selected"]
    selected_matches = None
    if final_events:
        event_members = sorted((member["partIndex"], member["instance"], member["group"],
                                member["pose"]["x"], member["pose"]["y"], member["pose"]["angle"])
                               for member in final_events[-1]["members"])
        parts = {part["id"]: index for index, part in enumerate(result["problem"]["parts"])}
        final_members = sorted((parts[pose["part"]], pose["instance"], pose["group"],
                                pose["x"], pose["y"], pose["angle"])
                               for board in selected["layouts"] for pose in board["placements"])
        selected_matches = event_members == final_members
    return {
        "originalInputVerified": True, "traceOriginalInputVerified": True,
        "nativeValid": selected["valid"], "complete": selected["complete"], "placed": selected["placed"],
        "originalPieces": case["originalPieces"], "strictAudit": strict,
        "identitiesConserved": strict["valid"] and auditor.identities(run["construction"]) == auditor.identities(selected),
        "sameConstructionPhases": same_phases,
        "traceSelectedMatchesCanonicalResult": selected_matches,
        "postprocessor": result["postprocessor"], "rotationStarted": run["rotationStarted"],
        "auditDriverSha256": digest(AUDIT_DRIVER),
    }


def export(output, trace, result, audit, meta):
    bundle = {"trace": trace, "result": result, "audit": audit, "meta": meta}
    data = json.dumps(bundle, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    # JSON remains data even when a native ID or diagnostic contains HTML text.
    for character, escaped in [("&", "\\u0026"), ("<", "\\u003c"), (">", "\\u003e"),
                               ("\u2028", "\\u2028"), ("\u2029", "\\u2029")]:
        data = data.replace(character, escaped)
    document = (HERE / "index.html").read_text(encoding="utf-8")
    css = (HERE / "viewer.css").read_text(encoding="utf-8")
    javascript = (HERE / "viewer.js").read_text(encoding="utf-8")
    if "</script" in javascript.lower() or "</style" in css.lower():
        raise ValueError("Viewer source contains an unexpected closing raw-text element")
    document = document.replace('<link rel="stylesheet" href="viewer.css">', "<style>\n" + css + "\n</style>")
    document = document.replace('<script id="trace-data" type="application/json">null</script>',
                                '<script id="trace-data" type="application/json">' + data + '</script>')
    document = document.replace('<script src="viewer.js" defer></script>', "<script>\n" + javascript + "\n</script>")
    path = output / "index.html"
    path.write_text(document, encoding="utf-8")
    return path


def main():
    parser = argparse.ArgumentParser(description="原生特征排样过程演示，导出可双击的自包含HTML")
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--case", default="ndtm--Shapes1_0")
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--attempts", type=int, default=200000)
    parser.add_argument("--strategy", choices=[
        "feature.greedy.trace", "feature.greedy.waste.trace", "feature.greedy.low.trace",
        "feature.greedy.remaining.trace", "feature.contact.trace"], default="feature.greedy.trace")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--from-trace", type=Path)
    parser.add_argument("--result", type=Path)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    meta = {"caseName": args.case, "traceEnabled": True, "requestedSeconds": args.seconds,
            "requestedAttempts": args.attempts, "performanceRun": False}
    audit = None
    if args.from_trace:
        args.output.mkdir(parents=True, exist_ok=True)
        trace_path = args.from_trace.resolve()
        trace = read_json(trace_path)
        trace_digest = digest(trace_path)
        previous_verification = args.output / "verification.json"
        if previous_verification.is_file():
            previous = read_json(previous_verification)
            previous_meta = previous.get("meta", {})
            if previous_meta.get("caseName") == args.case and previous_meta.get("traceFileSha256") == trace_digest:
                meta.update(previous["meta"])
        result_path = args.result
        if result_path is None:
            name = trace.get("resultFile")
            if isinstance(name, str) and Path(name).name == name:
                candidate = trace_path.parent / name
                if candidate.is_file():
                    result_path = candidate
        result = read_json(result_path) if result_path else None
        if result:
            meta["requestedSeconds"] = result.get("secondsPerConstructor")
            meta["requestedAttempts"] = result.get("wholeJobAttemptBudget")
            meta["strategyID"] = result["runs"][0]["constructor"]
        meta["traceFileSha256"] = trace_digest
        if result_path:
            meta["resultFileSha256"] = digest(result_path)
        if result and CASES.is_file():
            case = next((case for case in read_json(CASES) if case["name"] == args.case), None)
            if case:
                case = dict(case)
                case["frozenDat"] = case["dat"]
                if case.get("originalSourceJson"):
                    case["frozenOriginalSourceJson"] = case["originalSourceJson"]
                audit = verify(case, result, trace)
    else:
        if args.executable is None or args.seconds <= 0 or args.attempts <= 0:
            parser.error("构造演示需要--executable和正的时间／尝试预算")
        all_cases = read_json(CASES)
        case = next((case for case in all_cases if case["name"] == args.case), None)
        if case is None:
            parser.error("--case需要原221任务清单中的完整名称")
        args.output.mkdir(parents=True, exist_ok=False)
        inputs = args.output / "inputs"; inputs.mkdir()
        source = Path(case["dat"])
        if digest(source) != case["datSha256"]:
            raise ValueError("Original DAT hash changed")
        frozen = inputs / (case["name"] + ".dat")
        shutil.copy2(source, frozen)
        case = dict(case); case["frozenDat"] = str(frozen.resolve())
        if case.get("originalSourceJson"):
            source = Path(case["originalSourceJson"])
            if digest(source) != case["originalSourceJsonSha256"]:
                raise ValueError("Original source JSON hash changed")
            original_json = inputs / (case["name"] + ".source.json")
            shutil.copy2(source, original_json); case["frozenOriginalSourceJson"] = str(original_json.resolve())
        (args.output / "case.json").write_text(json.dumps(case, ensure_ascii=False, indent=2), encoding="utf-8")
        result_path = args.output / "result.json"
        frame = "source-free" if case.get("rotationDomain") == "free" else "original"
        command = [str(args.executable.resolve()), str(frozen.resolve()), str(case["width"]),
                   str(result_path.resolve()), str(args.seconds), str(args.attempts),
                   args.strategy, "0", frame, "construct"]
        program_hash = digest(args.executable)
        started = time.perf_counter()
        with (args.output / "native.log").open("wb") as stream:
            process = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                                     timeout=max(60, args.seconds * 10))
        meta.update({"processWallSeconds": time.perf_counter()-started, "nativeExitCode": process.returncode,
                     "programSha256": program_hash, "programStable": digest(args.executable) == program_hash,
                     "command": command, "datSha256": case["datSha256"], "strategyID": args.strategy})
        if process.returncode:
            raise RuntimeError(f"Native trace process exited {process.returncode}. See {args.output / 'native.log'}")
        trace_path = result_path.with_suffix(".trace.json")
        trace = read_json(trace_path); result = read_json(result_path)
        audit = verify(case, result, trace)
        meta.update({"traceFileSha256": digest(trace_path), "resultFileSha256": digest(result_path)})
        (args.output / "verification.json").write_text(json.dumps({"audit": audit, "meta": meta}, ensure_ascii=False, indent=2), encoding="utf-8")
    if trace.get("schemaVersion") != 1 or not isinstance(trace.get("events"), list):
        raise ValueError("Unsupported native construction trace schema")
    path = export(args.output, trace, result, audit, meta)
    (args.output / "viewer-verification.json").write_text(
        json.dumps({"audit": audit, "meta": meta, "htmlSha256": digest(path)}, ensure_ascii=False, indent=2),
        encoding="utf-8")
    print(json.dumps({"html": str(path.resolve()), "events": len(trace["events"]),
                      "truncated": trace.get("meta", {}).get("truncated"),
                      "traceEnabled": True, "requestedSeconds": meta.get("requestedSeconds")}, ensure_ascii=False))
    if args.open:
        webbrowser.open(path.resolve().as_uri())


if __name__ == "__main__":
    main()
