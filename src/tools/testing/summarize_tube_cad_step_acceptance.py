"""Join single-part and original-assembly proofs without treating an assembly as a failed part."""
import argparse
import json
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--step-report", type=Path, required=True)
    parser.add_argument("--assembly-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    step, assembly = load(args.step_report), load(args.assembly_report)
    for name in ("opencascaderesourceimport.dll", "data.dll", "templateruntime.dll"):
        left = step["runIdentity"]["resolvedDlls"][name]["sha256"]
        right = assembly["runIdentity"]["resolvedDlls"][name]["sha256"]
        if left != right:
            raise ValueError(f"Reports use different runtime binaries: {name}")
    if step["runIdentity"]["nativeExe"]["sha256"] != assembly["runIdentity"]["exeSha256"]:
        raise ValueError("Reports use different native acceptance executables")
    parents = {item["sha256"]: item for item in assembly["results"]}
    accepted, rejected, unresolved = [], [], []
    for item in step["results"]:
        entry = {"sha256": item["sha256"], "files": item["aliases"],
                 "reportDirectory": item["reportDirectory"], "status": item["status"]}
        if item.get("passed"):
            entry["expression"] = item.get("expression", {})
            accepted.append(entry)
        elif item["status"] == "assembly":
            parent = parents.get(item["sha256"])
            solids = parent.get("solids", []) if parent else []
            if (parent and parent.get("passed") and len(solids) == item["sourceSolids"]
                    and all(solid.get("archiveVerified") and solid.get("decoderVerified")
                            and solid.get("documentVerified") for solid in solids)):
                entry.update(status="assembly-documents-verified", solidCount=len(solids),
                             reportDirectory=parent["reportDirectory"])
                accepted.append(entry)
            else:
                entry.update(error="Original assembly does not have a complete per-solid document proof",
                             assemblyReportDirectory=parent.get("reportDirectory") if parent else None)
                unresolved.append(entry)
        elif item["status"] in ("no-solid", "invalid-source"):
            entry["error"] = item.get("error")
            rejected.append(entry)
        else:
            entry["error"] = item.get("error")
            unresolved.append(entry)
    complete = (step.get("completed") == step["uniqueFileCount"]
                and assembly.get("completed") == assembly["assemblyCount"])
    report = {"complete": complete, "allGeometricallyValidInputsVerified": complete and not unresolved,
              "inputFiles": step["fileCount"], "uniqueInputs": step["uniqueFileCount"],
              "completedInputs": step.get("completed", 0), "acceptedInputs": len(accepted),
              "rejectedSourceInputs": len(rejected), "unresolvedInputs": len(unresolved),
              "originalAssemblies": assembly["assemblyCount"],
              "originalAssemblySolids": assembly["solidCount"],
              "verifiedAssemblyDocuments": assembly.get("verifiedDocumentSolids", 0),
              "accepted": accepted, "sourceRejections": rejected, "unresolved": unresolved,
              "stepReport": str(args.step_report.resolve()),
              "assemblyReport": str(args.assembly_report.resolve()),
              "coverage": {"verified": ["native STEP decoding", "neutral document import",
                            "binary disk save and reopen", "original assembly per-solid geometry",
                            "independent bidirectional geometry difference"],
                           "notVerified": ["all editor UI interoperability",
                                           "parameter recovery and edits for every imported face"]}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ("accepted", "sourceRejections", "unresolved", "coverage")}, ensure_ascii=False))
    return 0 if report["allGeometricallyValidInputsVerified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
