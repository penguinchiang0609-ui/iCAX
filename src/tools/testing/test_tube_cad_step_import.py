"""Isolated native STEP -> neutral document -> disk -> independent replay checks."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading
import time


def unpack_variant(value):
    if isinstance(value, list):
        return [unpack_variant(item) for item in value]
    if isinstance(value, dict):
        if "__variant_type" in value:
            return unpack_variant(value["value"])
        return {key: unpack_variant(item) for key, item in value.items()}
    return value


def saved_results(path):
    previous = json.loads(path.read_text(encoding="utf-8"))
    by_hash = {}
    for entry in previous["results"]:
        item = unpack_variant(entry)
        item.update({key: value for key, value in entry.items() if key not in ("__variant_type", "value")})
        by_hash[item["sha256"]] = item
    return by_hash


def priority_weights(path, root):
    """Prior runs affect queue order only; every source is verified again."""
    if path is None:
        return {}
    prior = json.loads(path.read_text(encoding="utf-8"))
    weights = {item["sha256"]: item.get("wallSeconds", item.get("seconds", 0))
               for item in prior.get("results", []) if "sha256" in item}
    for report in path.parent.glob("*/report.json"):
        item = json.loads(report.read_text(encoding="utf-8"))
        source = Path(item.get("source", ""))
        if not source.is_absolute():
            source = root / source
        if not source.is_file():
            continue
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        elapsed = item.get("seconds", 0)
        if item.get("status") == "running":
            elapsed += max(0, time.time() - report.stat().st_mtime)
        weights[digest] = max(weights.get(digest, 0), elapsed)
    return weights


def source_records(root):
    """Reuse acquisition and solid-extraction records; do not invent provenance."""
    records = {}
    for folder in (root / "samples/production-tubes", root / "samples/structural-steel"):
        for manifest in folder.rglob("manifest.json"):
            if "recovery" in manifest.parts:
                continue
            data = json.loads(manifest.read_text(encoding="utf-8-sig"))
            items = data if isinstance(data, list) else data.get("samples", data.get("files", []))
            for item in items:
                if not isinstance(item, dict):
                    continue
                name = item.get("file", item.get("path"))
                if name:
                    candidates = [(manifest.parent / name).resolve(), (folder / name).resolve()]
                    existing = [path for path in dict.fromkeys(candidates) if path.is_file()]
                    if len(existing) != 1:
                        raise ValueError(f"Cannot resolve recorded source {name} in {manifest}")
                    records[existing[0]] = {
                        "kind": "original", "record": str(manifest.relative_to(root)), "source": item}
    report_dir = root / "samples/structural-steel/recovery/report"
    for report in report_dir.glob("*.json"):
        data = json.loads(report.read_text(encoding="utf-8-sig"))
        if isinstance(data, dict) and data.get("parentFile") and data.get("sourcePath"):
            records[Path(data["sourcePath"]).resolve()] = {
                "kind": "extracted-solid", "record": str(report.relative_to(root)),
                "parentFile": data["parentFile"], "parentSourceSha256": data.get("parentSourceSha256"),
                "solidIndex": data.get("solidIndex"), "derivation": data.get("derivation"),
                "source": data.get("provenance", {})}
    return records


def collect_summary(aggregate, rows, directory):
    for item in rows:
        message = item.get("error", "")
        if item.get("passed"):
            item["failureCategory"] = None
        elif "Source BRep cannot be verified against an independent copy" in message:
            item["failureCategory"] = "source-boundary-unverifiable"
        elif item.get("status") == "assembly":
            item["failureCategory"] = "assembly-requires-per-solid-verification"
        elif item.get("status") in ("no-solid", "invalid-source", "timeout", "native-failure"):
            item["failureCategory"] = item["status"]
        else:
            item["failureCategory"] = "geometry-conversion-rejected"
    aggregate["results"] = list(rows)
    aggregate["completed"] = len(rows)
    aggregate["passed"] = sum(item.get("passed", False) for item in rows)
    aggregate["featureEditRoundtripPassCount"] = sum(
        item.get("wrappedEditRoundtrip", {}).get("passed", False)
        or item.get("circleEditRoundtrip", {}).get("passed", False)
        or item.get("sectionEditRoundtrip", {}).get("passed", False) for item in rows)
    aggregate["statusCounts"] = {
        status: sum(item["status"] == status for item in rows)
        for status in sorted({item["status"] for item in rows})
    }
    passed = [item for item in rows if item.get("passed")]
    aggregate["geometryCoverageCounts"] = {
        "noActiveExactBody": sum(item.get("expression", {}).get("activeExactBodyNodes") == 0 for item in passed),
        "containsActiveExactBody": sum(item.get("expression", {}).get("activeExactBodyNodes", 0) > 0 for item in passed),
        "hasUnavailableConstruction": sum(item.get("unavailableConstructionCount", 0) > 0 for item in passed),
    }
    assemblies = []
    for parent in rows:
        if parent.get("status") != "assembly":
            continue
        children = {}
        for item in rows:
            for alias, provenance in zip(item["aliases"], item["provenance"]):
                if provenance.get("parentSourceSha256") == parent["sha256"]:
                    children[provenance["solidIndex"]] = {
                        "solidIndex": provenance["solidIndex"], "file": alias,
                        "sha256": item["sha256"], "status": item["status"],
                        "passed": item.get("passed", False), "failureCategory": item["failureCategory"],
                        "expression": item.get("expression", {}), "reportDirectory": item["reportDirectory"]}
        expected = parent.get("sourceSolids", 0)
        missing = [index for index in range(1, expected + 1) if index not in children]
        assemblies.append({"file": parent["aliases"][0], "sha256": parent["sha256"],
                           "sourceSolids": expected, "missingSolidIndices": missing,
                           "allSolidsImported": bool(children) and not missing
                               and all(item["passed"] for item in children.values()),
                           "passedSolids": sum(item["passed"] for item in children.values()),
                           "solids": [children[index] for index in sorted(children)]})
    aggregate["assemblyCoverage"] = assemblies
    aggregate["coverage"] = {
        "verified": ["production STEP decoding", "registered neutral document import",
                     "construction sketch reads", "binary disk save and reload",
                     "independent bidirectional solid difference against original STEP"],
        "notVerified": ["editor UI interoperability", "edit round trips for all imported feature types"],
        "timeoutMeaning": "No result within the per-file limit; geometric support remains unverified",
        "exactBodyMeaning": "An active exact BRep body is preserved geometry, not a recovered editable feature",
    }
    if any(item.get("wrappedEditRoundtrip", {}).get("passed", False) for item in rows):
        aggregate["coverage"]["verified"].append(
            "imported wrapped UV edit, 3D change, disk round trip and original contour restoration")
    if any(item.get("circleEditRoundtrip", {}).get("passed", False) for item in rows):
        aggregate["coverage"]["verified"].append(
            "imported circle sketch edit, 3D feature move, 2D reread, disk round trip and original geometry restoration")
    if any(item.get("sectionEditRoundtrip", {}).get("passed", False) for item in rows):
        aggregate["coverage"]["verified"].append(
            "imported base section edit, 3D placement edit, 2D reread, disk round trip and original geometry restoration")
    aggregate["timingNote"] = "Elapsed wall time from concurrent native runs; not a serial performance benchmark"
    aggregate["timeoutLimitOvershoots"] = [
        item["aliases"][0] for item in rows
        if item["status"] == "timeout" and item["wallSeconds"] > aggregate["timeoutSeconds"] + 1
    ]
    (directory / "summary.json").write_text(
        json.dumps(aggregate, ensure_ascii=False, indent=2), encoding="utf-8")
    fields = ["source", "status", "stage", "seconds", "sourceFaces", "features", "planarViews",
              "activeExactBodyNodes", "unavailableConstructions", "relativeDifference", "error", "aliases"]
    with (directory / "summary.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for item in sorted(rows, key=lambda row: row["aliases"][0]):
            expression = item.get("expression", {})
            writer.writerow({
                "source": item["aliases"][0], "status": item["status"], "stage": item.get("stage"),
                "seconds": round(item["wallSeconds"], 3), "sourceFaces": item.get("sourceFaces"),
                "features": expression.get("activeGeometricFeatures"),
                "planarViews": expression.get("activePlanarViews"),
                "activeExactBodyNodes": expression.get("activeExactBodyNodes"),
                "unavailableConstructions": item.get("unavailableConstructionCount"),
                "relativeDifference": item.get("comparison", {}).get("relativeDifference"),
                "error": item.get("error", ""), "aliases": " | ".join(item["aliases"]),
            })


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--collect-only", action="store_true")
    parser.add_argument("--list-only", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--priority-report", type=Path,
                        help="Schedule previously slow inputs first; does not reuse their results")
    edit = parser.add_mutually_exclusive_group()
    edit.add_argument("--exercise-wrapped-edit", action="store_true")
    edit.add_argument("--exercise-circle-edit", action="store_true")
    edit.add_argument("--exercise-section-edit", action="store_true")
    parser.add_argument("--output", type=Path, default=root / "output/tests/tube-cad-step/batch")
    parser.add_argument("--exe", type=Path, default=root / "output/tests/tube-cad-step/native/TubeCadStepAcceptance.exe")
    parser.add_argument("--runtime-directory", type=Path, default=root / "src/x64/Debug")
    parser.add_argument("--dependency-directory", type=Path,
                        help="Matching configuration dependencies for an isolated candidate DLL directory")
    args = parser.parse_args()
    sources = args.source or [
        "samples/tube-geometry", "samples/production-tubes", "samples/manual-acceptance/machined-step",
        "samples/structural-steel",
    ]
    paths = []
    for source in sources:
        path = (root / source).resolve()
        paths.extend(sorted(p for p in path.rglob("*") if p.suffix.lower() in (".step", ".stp")) if path.is_dir() else [path])
    paths = sorted(set(paths))
    provenance = source_records(root)
    unique = {}
    for path in paths:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        unique.setdefault(digest, []).append(path)
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    dependency_directory = args.dependency_directory or args.runtime_directory
    environment["PATH"] = str(args.runtime_directory.resolve()) + os.pathsep + str(dependency_directory.resolve()) + os.pathsep + environment["PATH"]
    lock = threading.Lock()
    began = time.monotonic()
    aggregate = {"schemaVersion": 1, "fileCount": len(paths), "uniqueFileCount": len(unique),
                 "timeoutSeconds": args.timeout, "jobs": args.jobs, "results": []}
    def fingerprint(path):
        return {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    if not args.list_only and not args.collect_only:
        # Windows resolves adjacent EXE DLLs before PATH. Record the actual
        # first available file for every runtime name across those locations.
        resolved_dlls = {}
        for folder in (args.exe.parent, args.runtime_directory, dependency_directory):
            for library in sorted(folder.glob("*.dll")):
                resolved_dlls.setdefault(library.name.lower(), fingerprint(library))
        aggregate["runIdentity"] = {
            "nativeExe": fingerprint(args.exe),
            "cadRuntime": fingerprint(args.runtime_directory / "OpenCascadeResourceImport.dll"),
            "dependencyDirectory": str(dependency_directory.resolve()),
            "resolvedDlls": resolved_dlls,
            "editMode": "wrapped" if args.exercise_wrapped_edit else "circle" if args.exercise_circle_edit else "section" if args.exercise_section_edit else "import"}
    inventory = [{"sha256": digest, "files": [str(path.relative_to(root)) for path in aliases],
                  "provenance": [provenance.get(path, {"kind": "unclassified"}) for path in aliases]}
                 for digest, aliases in unique.items()]
    (args.output / "source-inventory.json").write_text(
        json.dumps({"fileCount": len(paths), "uniqueFileCount": len(unique), "sources": inventory},
                   ensure_ascii=False, indent=2), encoding="utf-8")
    if args.list_only:
        print(json.dumps({"files": len(paths), "unique": len(unique),
                          "inventory": str(args.output / "source-inventory.json")}), flush=True)
        return 0

    if args.collect_only:
        report_path = args.output / "report.json"
        by_hash = saved_results(report_path)
        previous = json.loads(report_path.read_text(encoding="utf-8"))
        aggregate = previous
        collect_summary(aggregate, list(by_hash.values()), args.output)
        print(json.dumps({key: value for key, value in aggregate.items() if key != "results"}, ensure_ascii=False), flush=True)
        return 0

    if args.resume:
        previous = json.loads((args.output / "report.json").read_text(encoding="utf-8"))
        if previous.get("runIdentity") != aggregate["runIdentity"]:
            raise ValueError("Resume requires the same native executable, CAD DLL, dependencies and edit mode; use a new output directory")
    completed = saved_results(args.output / "report.json") if args.resume else {}
    aggregate["results"] = [item for digest, item in completed.items() if digest in unique]

    def run(digest, aliases):
        source = aliases[0]
        directory = args.output / (source.stem + "-" + digest[:12])
        directory.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        timed_out = False
        with (directory / "native.log").open("wb") as log:
            command = [str(args.exe), str(source), str(directory)]
            if args.exercise_wrapped_edit:
                command.append("--exercise-wrapped-edit")
            elif args.exercise_circle_edit:
                command.append("--exercise-circle-edit")
            elif args.exercise_section_edit:
                command.append("--exercise-section-edit")
            process = subprocess.Popen(command,
                                       cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()  # Only this runner's child; never a desktop application.
                code = process.wait()
        path = directory / "report.json"
        try:
            report = unpack_variant(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, ValueError):
            report = {"passed": False, "stage": "native-process", "status": "failed"}
        report.update(sha256=digest, aliases=[str(p.relative_to(root)) for p in aliases],
                      provenance=[provenance.get(p, {"kind": "unclassified"}) for p in aliases],
                      exitCode=code, wallSeconds=time.monotonic() - start, reportDirectory=str(directory))
        if timed_out:
            report.update(status="timeout", passed=False, error="Native check exceeded per-file time limit")
        elif code != 0 and report.get("status") == "running":
            report.update(status="native-failure", passed=False, error="Native process exited before completing stage; see native.log")
        with lock:
            aggregate["results"].append(report)
            aggregate["completed"] = len(aggregate["results"])
            aggregate["passed"] = sum(r.get("passed", False) for r in aggregate["results"])
            aggregate["seconds"] = time.monotonic() - began
            (args.output / "report.json").write_text(json.dumps(aggregate, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps({"completed": aggregate["completed"], "total": len(unique), "file": source.name,
                              "status": report["status"], "stage": report.get("stage"),
                              "seconds": round(report["wallSeconds"], 2), "error": report.get("error")}, ensure_ascii=False), flush=True)

    print(json.dumps({"files": len(paths), "unique": len(unique), "output": str(args.output)}), flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        weights = priority_weights(args.priority_report, root)
        ordered = sorted(unique.items(), key=lambda item: -weights.get(item[0], 0))
        futures = [executor.submit(run, digest, aliases) for digest, aliases in ordered if digest not in completed]
        for future in futures:
            future.result()
    collect_summary(aggregate, aggregate["results"], args.output)
    return 0 if aggregate.get("passed") == len(unique) else 1


if __name__ == "__main__":
    raise SystemExit(main())
