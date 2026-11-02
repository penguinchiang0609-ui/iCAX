"""Verify each recorded extracted STEP solid against its original assembly."""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from test_tube_cad_step_import import priority_weights, source_records, unpack_variant


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--runtime-directory", type=Path, required=True)
    parser.add_argument("--dependency-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--documents", action="store_true", help="Import every original assembly solid into a persisted neutral document")
    parser.add_argument("--timeout", type=float, default=900)
    parser.add_argument("--priority-report", type=Path,
                        help="Schedule previously slow assemblies first; verifies all solids again")
    args = parser.parse_args()
    groups = collections.defaultdict(list)
    for path, record in source_records(root).items():
        if record["kind"] != "extracted-solid":
            continue
        parent = Path(record["parentFile"])
        if not parent.is_absolute():
            parent = root / "samples/structural-steel" / parent
        parent_digest = hashlib.sha256(parent.read_bytes()).hexdigest()
        if parent_digest != record["parentSourceSha256"]:
            raise ValueError(f"Original assembly changed since extraction: {parent}")
        groups[parent].append({"sourcePath": str(path), "solidIndex": record["solidIndex"],
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                               "record": record["record"], "parentSourceSha256": parent_digest})
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PATH"] = str(args.runtime_directory.resolve()) + os.pathsep + str(args.dependency_directory.resolve()) + os.pathsep + environment["PATH"]
    resolved = {}
    for folder in (args.exe.parent, args.runtime_directory, args.dependency_directory):
        for library in sorted(folder.glob("*.dll")):
            resolved.setdefault(library.name.lower(), {"path": str(library.resolve()),
                                "sha256": hashlib.sha256(library.read_bytes()).hexdigest()})
    aggregate = {"runIdentity": {"exe": str(args.exe.resolve()),
                                 "exeSha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                                 "resolvedDlls": resolved}, "assemblyCount": len(groups),
                 "solidCount": sum(map(len, groups.values())), "results": [],
                 "coverage": "Original STEP solids, extracted files, production STEP decoder" + (", neutral document import, construction view reads, disk persistence and independent replay" if args.documents else "; neutral document import is reported separately")}

    def run(parent, children):
        digest = children[0]["parentSourceSha256"]
        output = args.output / (parent.stem + "-" + digest[:12])
        output.mkdir(parents=True, exist_ok=True)
        (output / "extraction-input.json").write_text(json.dumps(sorted(children, key=lambda x: x["solidIndex"])), encoding="utf-8")
        start = time.monotonic()
        with (output / "native.log").open("wb") as log:
            process = subprocess.Popen([str(args.exe), str(parent), str(output), "--verify-assembly-documents" if args.documents else "--verify-extracted-solids"],
                                       cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
                return {"source": str(parent), "sha256": digest, "passed": False, "status": "timeout",
                        "exitCode": code, "reportDirectory": str(output), "wallSeconds": time.monotonic() - start}
        try:
            report = unpack_variant(json.loads((output / "report.json").read_text(encoding="utf-8")))
        except (OSError, ValueError):
            report = {"passed": False, "status": "native-failure"}
        report.update(sha256=digest, exitCode=code, wallSeconds=time.monotonic() - start, reportDirectory=str(output))
        return report

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        weights = priority_weights(args.priority_report, root)
        ordered = sorted(groups.items(), key=lambda item: -weights.get(item[1][0]["parentSourceSha256"], 0))
        futures = [executor.submit(run, parent, children) for parent, children in ordered]
        for future in concurrent.futures.as_completed(futures):
            report = future.result()
            aggregate["results"].append(report)
            aggregate["completed"] = len(aggregate["results"])
            aggregate["verifiedSolids"] = sum(solid.get("passed", False) for item in aggregate["results"] for solid in item.get("solids", []))
            solids = [solid for item in aggregate["results"] for solid in item.get("solids", [])]
            aggregate["verifiedDecoderSolids"] = sum(solid.get("decoderVerified", False) for solid in solids)
            aggregate["verifiedArchivedSolids"] = sum(solid.get("archiveVerified", False) for solid in solids)
            aggregate["verifiedDocumentSolids"] = sum(solid.get("documentVerified", False) for solid in solids)
            aggregate["failedDocumentSolids"] = sum(solid.get("classification") == "neutral-document-conversion-rejected" for solid in solids)
            aggregate["unverifiableOriginalSolids"] = sum(solid.get("classification") == "unverifiable-original-boundary" for solid in solids)
            aggregate["decoderMismatchesOnVerifiableOriginals"] = sum(solid.get("classification") == "decoder-geometry-mismatch" for solid in solids)
            aggregate["invalidArchivedExtractions"] = sum(solid.get("classification") == "invalid-archived-extraction" for solid in solids)
            (args.output / "report.json").write_text(json.dumps(aggregate, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps({"completed": aggregate["completed"], "assembly": report.get("source"),
                              "status": report["status"], "verifiedSolids": aggregate["verifiedSolids"]}), flush=True)
    return 0 if all(item.get("passed") for item in aggregate["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
