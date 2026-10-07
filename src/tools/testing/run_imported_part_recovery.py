"""Run the actual import/recover/read/save/BRep comparison APIs for external CAD.

Each sample runs in a separate native test process. A negative recovery is
recorded as unsupported (passed=false), even if its preservation checks pass.
No geometry is generated here; source files and provenance stay independent.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED


REPOSITORY = Path(__file__).resolve().parents[3]
PROBE = "ImportedPartBatchRecoverySDO.ExternalFileReplaysThroughPublicEditorAPI"
STAGES = ("inspectSource", "importRecognizeFit", "recover", "readDefinition", "saveDrawing", "compareReplay")
CAD_EXTENSIONS = {".step", ".stp", ".iges", ".igs"}


def terminate_native_probe(process: subprocess.Popen) -> None:
    """Bound both tree cleanup and child reaping after the sample timeout.

    taskkill can fail to open a process in a restricted Windows environment.
    Popen retains our own child's handle, so always fall back to that handle
    rather than waiting forever after an unchecked taskkill failure.
    """
    if os.name == "nt":
        try:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           check=False, timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            pass
    if process.poll() is None:
        process.kill()
    process.wait(timeout=10)


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def write_json(path: Path, data) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def load_json(path: Path):
    def decode(value):
        # The native Variant serializer preserves scalar types with wrappers;
        # reports use ordinary JSON so the runner must unwrap them recursively.
        if isinstance(value, dict):
            if "__variant_type" in value and "value" in value:
                return decode(value["value"])
            return {key: decode(item) for key, item in value.items()}
        if isinstance(value, list):
            return [decode(item) for item in value]
        return value
    try:
        return decode(json.loads(path.read_text(encoding="utf-8-sig")))
    except (OSError, ValueError):
        return {}


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=Path, default=REPOSITORY / "samples/structural-steel")
    parser.add_argument("--manifest", type=Path, help="Optional source/provenance manifest; samples[].file is relative to --samples")
    parser.add_argument("--file", action="append", help="Relative CAD path; repeat to select files")
    parser.add_argument("--runtime", type=Path, default=REPOSITORY / "src/x64/Release")
    parser.add_argument("--output", type=Path, help="Defaults to <samples>/recovery/<UTC timestamp>")
    parser.add_argument("--timeout", type=float, default=300.0, help="Seconds per original CAD, including all native stages")
    parser.add_argument("--workers", type=int, default=1, help="Independent native test processes; 1 gives uncontended timings")
    parser.add_argument("--split-assemblies", action="store_true", help="Export and test individual original solids; retain assembly and parent/solid provenance")
    args = parser.parse_args()
    if args.workers < 1 or args.timeout <= 0:
        parser.error("workers and timeout must be positive")
    args.samples = args.samples.resolve()
    args.runtime = args.runtime.resolve()
    args.output = (args.output or args.samples / "recovery" / time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())).resolve()
    return parser, args


def main() -> int:
    parser, args = arguments()
    binary = args.runtime / "TubeDesignerTest.exe"
    if not binary.is_file():
        parser.error(f"Native test executable is missing: {binary}")
    env = os.environ.copy()
    env["PATH"] = str(args.runtime) + os.pathsep + env.get("PATH", "")
    listed = subprocess.run([str(binary), "--gtest_list_tests", f"--gtest_filter={PROBE}"],
                            env=env, cwd=REPOSITORY, capture_output=True, timeout=30)
    if listed.returncode or b"ExternalFileReplaysThroughPublicEditorAPI" not in listed.stdout:
        parser.error("Native batch probe is not present; rebuild TubeDesignerTest in the selected runtime")

    manifest_path = (args.manifest or args.samples / "manifest.json").resolve()
    manifest = load_json(manifest_path) if manifest_path.is_file() else {}
    entries = manifest.get("samples", []) if isinstance(manifest, dict) else manifest
    provenance = {str(entry["file"]).replace("\\", "/"): entry
                  for entry in entries if isinstance(entry, dict) and "file" in entry}
    if args.file:
        selected = sorted(set(path.replace("\\", "/") for path in args.file))
    else:
        selected = sorted(path.relative_to(args.samples).as_posix()
                          for path in args.samples.rglob("*")
                          if path.is_file() and path.suffix.lower() in CAD_EXTENSIONS
                          and path.relative_to(args.samples).parts[0] != "recovery"
                          and not path.is_relative_to(args.output))
        # Missing manifest entries are retained as missing, never silently omitted.
        selected = sorted(set(selected) | {name for name in provenance if Path(name).suffix.lower() in CAD_EXTENSIONS})
    if not selected:
        parser.error(f"No STEP/IGES samples found under {args.samples}")
    args.output.mkdir(parents=True, exist_ok=True)
    binary_hash = digest(binary)
    runtime_hashes = {name: digest(args.runtime / name) for name in
                      ("TubeDesignerTest.exe", "TubeDesigner.dll", "ExtrusionRecognition.dll", "OpenCascadeResourceImport.dll")
                      if (args.runtime / name).is_file()}
    fitting_hashes = {path.parent.name: digest(path) for path in
                     (REPOSITORY / "src/apps/tube-designer/templates/profile").glob("*/fitting.py")}

    def run(relative: str, child=None, parent=None):
        source = Path(child["sourcePath"]).resolve() if child else (args.samples / relative).resolve()
        name = "__".join(Path(relative).parts) + "-" + hashlib.sha256(relative.encode()).hexdigest()[:8]
        report_path = args.output / (name + ".json")
        log_path = args.output / (name + ".log")
        record = {"file": relative, "sourcePath": str(source), "passed": False,
                  "provenance": parent.get("provenance") if parent else provenance.get(relative), "report": report_path.name,
                  "log": log_path.name, "nativeTestSha256": binary_hash}
        if child:
            record.update(parentFile=parent["file"], parentSourceSha256=parent["sourceSha256"],
                          solidIndex=child["solidIndex"], derivation=child["derivation"])
        if not source.is_file():
            record.update(status="missing", reason="Source CAD file is missing")
            write_json(report_path, record)
            return record
        record["sourceSha256"] = digest(source)
        process_env = env.copy()
        process_env["ICAX_RECOVERY_SOURCE"] = str(source)
        process_env["ICAX_RECOVERY_REPORT"] = str(report_path)
        process_env.pop("ICAX_RECOVERY_SPLIT_DIRECTORY", None)
        if args.split_assemblies and not child:
            process_env["ICAX_RECOVERY_SPLIT_DIRECTORY"] = str(args.output / "solids" / name)
        start = time.monotonic()
        timed_out = False
        with log_path.open("wb") as log:
            process = subprocess.Popen([str(binary), f"--gtest_filter={PROBE}", "--gtest_color=no"],
                                       cwd=REPOSITORY, env=process_env, stdout=log, stderr=subprocess.STDOUT)
            try:
                process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                terminate_native_probe(process)
        native = load_json(report_path)
        record.update(native)
        record.update(file=relative, sourceSha256=digest(source),
                      processSeconds=round(time.monotonic() - start, 6), exitCode=process.returncode)
        if timed_out:
            record.update(status="timeout", passed=False, reason=f"Native process exceeded {args.timeout:g} seconds")
        elif process.returncode or not native:
            record.update(status="failed", passed=False,
                          reason=native.get("reason", "Native process exited without a complete report"))
        elif record.get("status") not in {"passed", "unsupported", "failed", "assembly"}:
            record.update(status="failed", passed=False, reason="Native process returned an incomplete report")
        # A status or native assertion alone never establishes geometric equality.
        if record.get("status") == "passed":
            error = record.get("relativeSymmetricDifference")
            if not isinstance(error, (int, float)) or not 0 <= error <= 1e-8:
                record.update(status="failed", passed=False, reason="No valid independent replay comparison")
        write_json(report_path, record)
        return record

    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        pending = {executor.submit(run, name) for name in selected}
        while pending:
            done, pending = wait(pending, return_when=FIRST_COMPLETED)
            for future in done:
                result = future.result()
                results.append(result)
                for child in result.get("children", []):
                    pending.add(executor.submit(run, result["file"] + "#solid-" + str(child["solidIndex"]), child, result))
                results.sort(key=lambda entry: entry["file"])
                write_json(args.output / "progress.json", results)
                print(json.dumps({key: result.get(key) for key in
                                  ("file", "status", "stage", "processSeconds", "relativeSymmetricDifference", "reason")},
                                 ensure_ascii=False), flush=True)
    counts = {status: sum(result.get("status") == status for result in results)
              for status in ("passed", "unsupported", "failed", "timeout", "missing", "assembly")}
    summary = {"schemaVersion": 1, "samplesDirectory": str(args.samples),
               "runtime": str(args.runtime), "nativeTestSha256": binary_hash,
               "completedAtUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
               "runtimeSha256": runtime_hashes, "fittingSha256": fitting_hashes,
               "manifest": str(manifest_path) if manifest_path.is_file() else None,
               "originalFileCount": len(selected),
               "derivedSolidCount": sum("parentFile" in result for result in results),
               "workers": args.workers, "timeoutSeconds": args.timeout, "counts": counts,
               "timingNote": "importRecognizeFit includes file import, axis/contour recognition and profile fitting; these are not separately instrumented. processSeconds also includes executable startup.",
               "samples": results}
    write_json(args.output / "summary.json", summary)
    fields = ["file", "parentFile", "solidIndex", "status", "stage", "profileId", "lengthMm", "featureCount", "sourceSolidCount", "importedSolidCount",
              *[stage + "Seconds" for stage in STAGES], "totalSeconds", "processSeconds",
              "relativeSymmetricDifference", "originalUnchangedByRecovery", "reason", "sourceSha256"]
    with (args.output / "summary.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for result in results:
            row = {key: result.get(key) for key in fields}
            row["profileId"] = result.get("profile", {}).get("id")
            for stage in STAGES:
                row[stage + "Seconds"] = result.get("stages", {}).get(stage, {}).get("seconds")
            writer.writerow(row)
    print(json.dumps({"counts": counts, "output": str(args.output)}, ensure_ascii=False), flush=True)
    return 1 if counts["failed"] or counts["timeout"] or counts["missing"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
