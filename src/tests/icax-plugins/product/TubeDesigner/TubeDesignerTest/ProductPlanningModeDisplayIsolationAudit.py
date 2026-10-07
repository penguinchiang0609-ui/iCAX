"""Compare complete public display documents for every declared planning mode.

Use the same meaningful generic scenarios as the independent manufacturing
audit. Keep both-mode rejections explicit; never treat a failed call as a pass.
"""
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import time

from AllProductsGenericRecheck import ROOT, OUT, SRC, TEST, attempt, audit_digests, descriptor, digest, frozen_source_digests, load, scenarios
from ProductGeometryResourceTests import assert_display_model


def document_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
        ensure_ascii=False).encode("utf-8")).hexdigest()


def differences(left, right, path="", limit=30):
    if left == right:
        return []
    if isinstance(left, dict) and isinstance(right, dict):
        result = []
        for key in sorted(set(left) | set(right)):
            location = path + "/" + key
            if key not in left or key not in right:
                result.append(location)
            else:
                result.extend(differences(left[key], right[key], location, limit))
            if len(result) >= limit:
                return result[:limit]
        return result
    if isinstance(left, list) and isinstance(right, list) and len(left) == len(right):
        result = []
        for index, (a, b) in enumerate(zip(left, right)):
            result.extend(differences(a, b, path + "/" + str(index), limit))
            if len(result) >= limit:
                return result[:limit]
        return result
    return [path or "/"]


def protection_digests():
    paths = {SRC.parent / key for key in frozen_source_digests()}
    paths.update(SRC / key for key in audit_digests())
    return {str(path.relative_to(SRC.parent)).replace("\\", "/"): digest(path) for path in sorted(paths)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suffix", default="")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    verified = OUT / "all-products-recheck-generic-verified-synthetic-system.json"
    data = json.loads((SRC / "apps/tube-designer/docs/deferred-products/aluminium-window/reference/systems/demonstration.json").read_text(encoding="utf-8"))
    data.update(status="verified", manufacturer="UNIT TEST ONLY",
        source="Synthetic public-entry audit fixture; not production manufacturer data")
    fixture_text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    if not verified.exists() or verified.read_text(encoding="utf-8") != fixture_text:
        verified.write_text(fixture_text, encoding="utf-8")
    report = {"schema": "icax.product-planning-mode-display-isolation", "sourceOnly": True,
        "comparison": "Exact full public display document equality; no geometry or property exclusions",
        "templates": [], "cases": [], "sourceDigestsStart": protection_digests(),
        "fixtureDigestStart": digest(verified)}
    destination = OUT / ("all-products-recheck-generic-mode-isolation" + args.suffix + ".json")
    def save():
        report["statusCounts"] = dict(Counter(case["status"] for case in report["cases"]))
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for path in sorted(ROOT.glob("*/template.json")):
        name = path.parent.name
        if "security_window" in name:
            continue
        data, defaults = descriptor(path)
        field = next((p for p in data["parameters"] if p["key"] == "assemblyPlanningMode"), None)
        if field is None:
            report["templates"].append({"template": name, "descriptorId": data["id"],
                "status": "not-applicable-no-declared-planning-mode", "modes": [], "cases": 0})
            continue
        modes = [choice["value"] for choice in field["choices"]]
        TEST.assertEqual(len(modes), len(set(modes)))
        module = load(path.with_name("template.py"))
        seen, cases = set(), []
        for case in scenarios(name, data, defaults, verified):
            changes = {k: v for k, v in case["overrides"].items() if k != "assemblyPlanningMode"}
            signature = json.dumps(changes, sort_keys=True)
            if signature not in seen:
                seen.add(signature)
                cases.append({"label": case["label"], "overrides": changes})
        report["templates"].append({"template": name, "descriptorId": data["id"],
            "status": "all-declared-modes-compared", "modes": modes, "cases": len(cases)})
        for case in cases:
            started = time.monotonic()
            result = {"template": name, **case, "modes": {}, "hostInputsUnchanged": True}
            documents = {}
            for mode in modes:
                values = {**deepcopy(defaults), **deepcopy(case["overrides"]), "assemblyPlanningMode": mode}
                frozen = deepcopy(values)
                document, error = attempt(lambda: module.display(values))
                TEST.assertEqual(values, frozen)
                result["modes"][mode] = {"accepted": error is None, "error": error}
                if document is not None:
                    assert_display_model(TEST, document)
                    documents[mode] = document
                    result["modes"][mode].update(documentDigest=document_digest(document),
                        items=len(document["items"]), resources=len(document["resources"]))
            if len(documents) == len(modes):
                first = documents[modes[0]]
                changed = {mode: differences(first, document) for mode, document in documents.items()
                           if document != first}
                result["fullDisplayEqual"] = not changed
                result["status"] = "passed" if not changed else "display-changed-with-planning-mode"
                if changed:
                    result.update(differences=changed, documents=documents)
            elif not documents:
                errors = {(r["error"]["type"], r["error"]["message"]) for r in result["modes"].values()}
                result["status"] = "rejected-both-modes" if len(errors) == 1 else "rejection-changed-with-planning-mode"
            else:
                result["status"] = "acceptance-changed-with-planning-mode"
                result["documents"] = documents
            result["seconds"] = round(time.monotonic() - started, 3)
            report["cases"].append(result)
            save()
            print(name, case["label"], result["status"], flush=True)
    report["sourceDigestsEnd"] = protection_digests()
    report["fixtureDigestEnd"] = digest(verified)
    report["changedSourceFiles"] = sorted(key for key in set(report["sourceDigestsStart"]) | set(report["sourceDigestsEnd"])
        if report["sourceDigestsStart"].get(key) != report["sourceDigestsEnd"].get(key))
    report["protectedInputsUnchanged"] = not report["changedSourceFiles"] and report["fixtureDigestStart"] == report["fixtureDigestEnd"]
    report["passed"] = report["protectedInputsUnchanged"] and all(case["status"] in
        {"passed", "rejected-both-modes"} for case in report["cases"])
    save()
    print("SUMMARY", json.dumps(report["statusCounts"], sort_keys=True), "PROTECTED", report["protectedInputsUnchanged"], flush=True)
    print("REPORT", str(destination), flush=True)
    return int(not report["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
