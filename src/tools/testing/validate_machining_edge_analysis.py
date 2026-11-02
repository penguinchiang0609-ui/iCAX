"""Audit the published whole-model edge stage against its native run and source uses."""
import argparse
import hashlib
import json
import math
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[3]
SKIN_COLORS = {"OnSkin": "#00B8D9", "OffSkin": "#F59E0B", "PartialOnSkin": "#A855F7"}
CONVEXITY_PATTERNS = {"Concave": "dashed", "Convex": "solid", "Coplanar": "dash-dot", "Free": "dotted"}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_runtime_evidence(summary):
    """Rehash every explicitly named producer and optional source-run summary."""
    def verify(record, label):
        assert isinstance(record, dict), f"{label} evidence is missing"
        path = record.get("path")
        expected = record.get("sha256")
        assert isinstance(path, str) and path, f"{label} path is missing"
        assert isinstance(expected, str) and len(expected) == 64 and all(value in "0123456789abcdefABCDEF" for value in expected), f"{label} SHA256 is missing or invalid"
        actual_path = Path(path)
        assert actual_path.is_file(), f"{label} file is missing: {path}"
        assert sha(actual_path) == expected.lower(), f"{label} file hash changed: {path}"

    def producer_pair(evidence, label):
        assert isinstance(evidence, dict), f"{label} runtime evidence is missing"
        for component in ("executable", "recognition"):
            verify(evidence.get(component), f"{label} {component}")

    producer_pair(summary, "native run")
    for row in summary["rows"]:
        if "runtimeEvidence" in row:
            producer_pair(row["runtimeEvidence"], f"model {row['id']}")
    if "runOrigins" in summary:
        assert isinstance(summary["runOrigins"], list), "runOrigins must be an explicit source-run evidence list"
        for index, origin in enumerate(summary["runOrigins"]):
            verify(origin, f"source run {index + 1}")


def unique_strings(values, message):
    result = list(map(str, values))
    assert len(result) == len(set(result)), message
    return result


def parameter_tolerance(first, last):
    return max(1e-12, abs(last - first) * 1e-11, 8 * math.ulp(first), 8 * math.ulp(last))


def validate_spans(data):
    """Check source/coedge ownership and complete, independent span facts."""
    assert data["success"] and data["complete"] and data["stage"] == "edgeAnalysis"
    assert data["adjacencyScope"] == "machining-faces-only"
    faces = data["faces"]
    face_ids = set(unique_strings([face["faceId"] for face in faces], "duplicated original face"))
    edges = data["edges"]
    keys = unique_strings([edge["edgeKey"] for edge in edges], "duplicated analysis span key")
    by_key = dict(zip(keys, edges))
    sources = data["sourceEdges"]
    source_ids = set(unique_strings([edge["edgeId"] for edge in sources], "duplicated source edge record"))
    excluded = data["excludedEdges"]
    excluded_ids = set(unique_strings([edge["edgeId"] for edge in excluded], "duplicated excluded topology edge"))
    assert not source_ids & excluded_ids, "excluded topology entered the enum-bearing source set"
    expected_owners, use_counts = {}, Counter()
    for face in faces:
        face_id = str(face["faceId"])
        source_uses = [use for wire in face["wires"] for group in wire["groups"] for use in group["sourceUses"]]
        assert set(str(use["edgeId"]) for use in source_uses) == set(unique_strings(face["edgeIds"], "duplicated face source ID")), "logical grouping changed original edge coverage"
        for use in source_uses:
            edge_id = str(use["edgeId"])
            assert isinstance(use["reversed"], bool), "source coedge orientation is missing"
            expected_owners.setdefault(edge_id, set()).add(face_id)
            use_counts[face_id, edge_id] += 1
    assert set(expected_owners) == source_ids | excluded_ids, "source coedges are missing from normal/excluded edge records"
    all_keys = []
    for source in sources:
        source_id = str(source["edgeId"])
        first, last = source["firstParameter"], source["lastParameter"]
        assert math.isfinite(first) and math.isfinite(last) and first < last, "invalid original edge parameter range"
        tolerance = parameter_tolerance(first, last)
        segment_keys = unique_strings(source["segmentKeys"], "source edge repeats an analysis span")
        assert segment_keys, "source edge has no analysis spans"
        assert all(key in by_key for key in segment_keys), "source edge references a missing analysis span"
        spans = [by_key[key] for key in segment_keys]
        assert all(str(span["edgeId"]) == source_id for span in spans), "span references the wrong original edge"
        assert spans == sorted(spans, key=lambda span: span["firstParameter"]), "source analysis spans are not in parameter order"
        assert [span["segmentId"] for span in spans] == list(range(1, len(spans) + 1)), "source segment numbering is incomplete"
        cursor = first
        for span in spans:
            a, b = span["firstParameter"], span["lastParameter"]
            assert math.isfinite(a) and math.isfinite(b) and a < b, "invalid analysis span range"
            assert abs(a - cursor) <= tolerance, "analysis spans overlap or leave a parameter gap"
            assert a >= first - tolerance and b <= last + tolerance, "analysis span exceeds the original parameter range"
            cursor = b
        assert abs(cursor - last) <= tolerance, "analysis spans do not cover the original last parameter"
        all_keys.extend(segment_keys)
    assert len(all_keys) == len(set(all_keys)) and set(all_keys) == set(keys), "analysis spans have missing or multiple source ownership"
    for edge in excluded:
        edge_id = str(edge["edgeId"])
        assert set(unique_strings(edge["faceIds"], "excluded edge repeats an owner")) == expected_owners[edge_id], "excluded edge lost its original owners"
        reason = edge["reason"]
        assert isinstance(reason, str) and ("seam" in reason.lower() or "degenerat" in reason.lower()), "ordinary machining edge was excluded instead of classified"
        assert "skinRelation" not in edge and "convexity" not in edge, "excluded topology was assigned business enums"
        if "seam" in reason.lower():
            assert any(use_counts[face_id, edge_id] > 1 for face_id in expected_owners[edge_id]), "excluded seam lost its repeated source uses"
    for face in faces:
        face_id = str(face["faceId"])
        expected_keys = {key for key, edge in by_key.items() if face_id in expected_owners[str(edge["edgeId"])]}
        assert set(unique_strings(face["edgeKeys"], "face repeats an analysis span")) == expected_keys, "face analysis spans differ from its original coedges"
    groups = set()
    for key, edge in by_key.items():
        edge_id = str(edge["edgeId"])
        source_owners = set(unique_strings(edge["sourceFaceIds"], "span repeats an original owner"))
        owners = set(unique_strings(edge["faceIds"], "span repeats a resolved owner"))
        assert source_owners == expected_owners[edge_id], "original edge owners changed"
        assert source_owners <= owners <= face_ids, "non-machining face entered analysis or an original owner was lost"
        assert edge["skinValid"] is True and edge["convexityValid"] is True, "ordinary analysis span has no unique enum pair"
        assert edge["skinRelation"] in SKIN_COLORS and edge["color"] == SKIN_COLORS[edge["skinRelation"]], "skin enum/color mismatch"
        assert edge["convexity"] in CONVEXITY_PATTERNS and edge["linePattern"] == CONVEXITY_PATTERNS[edge["convexity"]], "convexity enum/line-pattern mismatch"
        assert len(owners) == (1 if edge["convexity"] == "Free" else 2), "enum disagrees with the resolved machining adjacency"
        matched = set(unique_strings(edge["matchedSpanKeys"], "matched analysis span repeated"))
        assert key in matched and matched <= by_key.keys(), "matched span group lost its original identity or references missing geometry"
        matched_edge_ids = set(unique_strings(edge["matchedEdgeIds"], "matched original edge repeated"))
        assert matched_edge_ids == {str(by_key[peer]["edgeId"]) for peer in matched}, "matched source edges do not correspond to matched spans"
        assert not matched_edge_ids & set(map(str, edge["rejectedCoincidentEdgeIds"])), "a rejected same-side relationship re-entered the matched group"
        union_owners = set()
        for peer in matched:
            other = by_key[peer]
            assert set(map(str, other["matchedSpanKeys"])) == matched, "matched span relationship is asymmetric or non-transitive"
            assert (other["skinRelation"], other["convexity"]) == (edge["skinRelation"], edge["convexity"]), "deduplicated spans have different enum pairs"
            assert set(map(str, other["faceIds"])) == owners, "matched spans disagree about their resolved owners"
            union_owners.update(map(str, other["sourceFaceIds"]))
        assert union_owners == owners, "resolved neighbors are not explained by current matching spans"
        peers = [by_key[peer] for peer in matched]
        assert len({str(peer["edgeId"]) for peer in peers}) == len(peers), "different intervals of the same original edge were incorrectly deduplicated"
        for i, a in enumerate(peers):
            for b in peers[i + 1:]:
                assert not set(map(str, a["sourceFaceIds"])) & set(map(str, b["sourceFaceIds"])), "same-face source boundaries were incorrectly deduplicated"
                pa, pb = a["classificationSamples"], b["classificationSamples"]
                aa, az, ba, bz = pa[0]["position"], pa[-1]["position"], pb[0]["position"], pb[-1]["position"]
                if math.dist(aa, az) > 1e-6 or math.dist(ba, bz) > 1e-6:
                    assert min(max(math.dist(aa, ba), math.dist(az, bz)), max(math.dist(aa, bz), math.dist(az, ba))) <= 1.0001e-6, "matched open spans cover different geometric endpoints"
        samples = edge["classificationSamples"]
        assert len(samples) >= 3, "analysis span lacks geometric evidence"
        tolerance = parameter_tolerance(edge["firstParameter"], edge["lastParameter"])
        assert abs(samples[0]["parameter"] - edge["firstParameter"]) <= tolerance and abs(samples[-1]["parameter"] - edge["lastParameter"]) <= tolerance, "span samples do not use its declared original parameter interval"
        assert all(a["parameter"] < b["parameter"] for a, b in zip(samples, samples[1:])), "span evidence parameters are not increasing"
        assert all(len(sample["position"]) == 3 and all(math.isfinite(value) for value in sample["position"]) and math.isfinite(sample["skinDistanceMm"]) and sample["skinDistanceMm"] >= 0 for sample in samples), "span has invalid distance/position evidence"
        distances = [sample["skinDistanceMm"] for sample in samples]
        assert math.isclose(edge["minSkinDistanceMm"], min(distances), abs_tol=1e-12) and math.isclose(edge["maxSkinDistanceMm"], max(distances), abs_tol=1e-12), "span deviation bounds describe different evidence"
        assert math.isfinite(edge["maxAdjacencyGapMm"]) and 0 <= edge["maxAdjacencyGapMm"] <= 1.0001e-6, "span matching exceeds its stated geometric tolerance"
        groups.add(frozenset(matched))
    counts = data["counts"]
    assert counts["faces"] == len(faces) and counts["edges"] == len(edges) and counts["sourceEdges"] == len(sources) and counts["excludedTopologyEdges"] == len(excluded), "published counts do not describe the flat span schema"
    assert counts["unresolvedSkin"] == counts["unresolvedConvexity"] == 0, "normal spans still contain unresolved enums"
    assert counts["skin"] == dict(Counter(edge["skinRelation"] for edge in edges)) and counts["convexity"] == dict(Counter(edge["convexity"] for edge in edges)), "enum statistics dropped or duplicated spans"
    assert data["legend"]["skin"] == SKIN_COLORS and data["legend"]["convexity"] == CONVEXITY_PATTERNS, "legend changes the independent three-color/four-pattern mapping"
    return {"models": 1, "faces": len(faces), "sourceEdgeRecords": len(sources), "analysisSpans": len(edges),
            "excludedTopologyEdges": len(excluded), "sourceCoedgeUses": sum(use_counts.values()), "displayEdges": len(groups), "completeModels": 1}


def audit(run, published):
    summary = read(run / "summary.json")
    manifest = read(published / "manifest.json")
    rows = {int(row["id"]): row for row in summary["rows"]}
    models = {int(model["id"]): model for model in manifest["models"]}
    assert len(rows) == summary["selected"] == summary["completed"], "native run has not finished"
    assert len(rows) == len(summary["rows"]), "duplicate native result model IDs"
    assert len(models) == len(manifest["models"]), "duplicate model IDs"
    assert rows.keys() <= models.keys(), "native result missing from the visible catalog"
    validate_runtime_evidence(summary)
    failures = []
    skin_reasons, convexity_reasons = Counter(), Counter()
    unresolved_skin = unresolved_convexity = 0
    totals = Counter()
    for model_id, row in rows.items():
        assert row["sourceUnchanged"] and sha(Path(row["source"])) == row["sourceSha256"], "source neutral CAD changed"
        file = published / unquote(Path(urlsplit(models[model_id]["dataUrl"]).path).name)
        data = read(file)
        if row["status"] != "passed":
            assert data["status"] == "failed" and data.get("diagnostics"), "failure must be inspectable"
            unresolved_skin += sum(edge.get("skinValid") is False for edge in data.get("edges", []))
            unresolved_convexity += sum(edge.get("convexityValid") is False for edge in data.get("edges", []))
            failures.append({"id": model_id, "name": row["group"], "reason": row.get("error") or data.get("execution")})
            continue
        model_totals = validate_spans(data)
        assert data["name"] == row["group"] == models[model_id]["name"], "visible model no longer names the original input"
        assert models[model_id]["faceCount"] == model_totals["faces"] and models[model_id]["edgeCount"] == model_totals["analysisSpans"], "catalog span/face counts are stale"
        face_ids = {str(face["faceId"]) for face in data["faces"]}
        skin_reasons.update(edge["skinReason"] for edge in data["edges"])
        convexity_reasons.update(edge["convexityReason"] for edge in data["edges"])
        geometry_ids = {str(face["faceId"]) for face in data["geometry"]["faces"]}
        assert geometry_ids == face_ids and not data["geometry"]["issues"], "published complete model has a missing display face"
        for face in data["geometry"]["faces"]:
            assert len(face["positions"]) % 3 == len(face["indices"]) % 3 == 0
            assert all(math.isfinite(value) for value in face["positions"])
            assert all(isinstance(index, int) and 0 <= index < len(face["positions"]) // 3 for index in face["indices"])
        artifacts = data["artifacts"]
        assert artifacts["pathConvention"] == "same-directory-and-stem-as-analysis-json"
        assert file.with_suffix(".brep").is_file() and file.with_suffix(".step").is_file(), "whole-model geometry missing"
        assert artifacts["brepRoundTripPassed"]
        step = artifacts["stepRoundTrip"]
        assert step["passed"] and step["fontCoveragePassed"]
        assert step["sourceFaceCount"] == step["roundTripFaceCount"] == len(face_ids)
        assert step["expectedStyledEdges"] == step["roundTripStyledEdges"] == step["edgeOverlayCount"] == model_totals["displayEdges"], "STEP deduplication lost or repeated a physical analysis span"
        assert math.isfinite(step["areaRelativeError"]) and 0 <= step["areaRelativeError"] < 1e-6, "STEP changed the machining area"
        assert step["extraDisplayFaces"] == 0, "display styling added machining faces"
        for kind, extension in (("edgeAnalysisBrep", ".brep"), ("edgeAnalysis", ".step")):
            assert Path(row["artifacts"][kind]).resolve() == file.with_suffix(extension).resolve(), "whole model was published under a different source name"
        totals.update(model_totals)
    visible_geometry = {(published / unquote(Path(urlsplit(model["dataUrl"]).path).name)).with_suffix(".brep") for model in models.values()}
    assert all(file.parent == published and file in visible_geometry for file in published.rglob("*.brep")), "orphan or per-unit BREP violates one complete file per original model"
    result = {"schema": 1, "status": "passed" if not failures else "completed-with-failures", "selectedModels": len(rows),
              **totals, "unresolvedSkinEdges": unresolved_skin, "unresolvedConvexityEdges": unresolved_convexity,
              "skinReasons": dict(skin_reasons), "convexityReasons": dict(convexity_reasons), "failures": failures,
              "sourceCadUnchanged": True, "wholeModelFilesOnly": True, "sourceUseCoveragePassed": True,
              "runtimeProvenancePassed": True, "runOriginHashesPassed": True,
              "spanParameterCoveragePassed": True, "matchedSpanConsistencyPassed": True,
              "independentColorAndLinePatternPassed": True, "stepFontRoundTripsPassed": True}
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, default=ROOT / "output/tests/machining-edge-analysis-20261028")
    parser.add_argument("--published", type=Path, default=ROOT / "TestData/Tube/EdgeAnalysis")
    options = parser.parse_args()
    result = audit(options.run, options.published)
    (options.run / "validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result["status"] == "passed" else 1)
