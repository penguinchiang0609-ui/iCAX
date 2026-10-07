"""Read-only DAG and contour complexity evidence for the native slow branch."""
import ctypes
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


def resource_statistics(model):
    resources = {node["key"]: node for node in model["resources"]}
    def evaluated_inputs(node):
        inputs = node.get("inputs", [])
        if node["operator"] != "boolean":
            return inputs
        arguments = node["arguments"]
        return [arguments.get("target", inputs[0])] + arguments.get("tools", inputs[1:])
    selected = []
    for item in model["items"]:
        geometry = item.get("geometry") or item.get("representations", {}).get("result")
        if geometry:
            selected.append({"itemKey": item["key"], "resource": geometry["resource"]})
    reached = set()
    pending = [entry["resource"] for entry in selected]
    while pending:
        key = pending.pop()
        if key in reached:
            continue
        if key not in resources:
            raise ValueError(f"Unknown selected/dependency resource: {key}")
        reached.add(key)
        pending.extend(evaluated_inputs(resources[key]))
    profiles = []
    for key, resource in resources.items():
        contours = resource.get("arguments", {}).get("contours")
        if contours is not None:
            segments = [segment for contour in contours for segment in contour.get("segments", [])]
            profiles.append({"key": key, "selectedRootReachable": key in reached,
                             "contours": len(contours), "pathSegments": len(segments),
                             "segmentKinds": dict(Counter(segment["kind"] for segment in segments)),
                             "primitiveContourKinds": dict(Counter(contour["kind"] for contour in contours))})
    reachable_profiles = [entry for entry in profiles if entry["selectedRootReachable"]]
    return {
        "resources": len(resources), "operators": dict(Counter(node["operator"] for node in resources.values())),
        "selectedGeometryRoots": selected, "selectedRootReachResources": len(reached),
        "unusedBySelectedRootsResources": len(resources) - len(reached),
        "selectedRootReachOperators": dict(Counter(resources[key]["operator"] for key in reached)),
        "profileResources": len(profiles), "contours": sum(entry["contours"] for entry in profiles),
        "pathSegments": sum(entry["pathSegments"] for entry in profiles),
        "selectedRootReachProfiles": len(reachable_profiles),
        "selectedRootReachContours": sum(entry["contours"] for entry in reachable_profiles),
        "selectedRootReachPathSegments": sum(entry["pathSegments"] for entry in reachable_profiles),
        "maximumContoursInOneResource": max(profiles, key=lambda entry: entry["contours"], default=None),
        "maximumPathSegmentsInOneResource": max(profiles, key=lambda entry: entry["pathSegments"], default=None),
        "maximumDependencyFanIn": max((len(evaluated_inputs(node)) for node in resources.values()), default=0),
        "booleanArgumentRefsEqualDeclaredInputs": all(evaluated_inputs(node) == node.get("inputs", [])
                                                      for node in resources.values() if node["operator"] == "boolean"),
        "selectedRootReachUsesNativeBooleanTargetAndTools": True,
    }


class MemoryStatus(ctypes.Structure):
    _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                ("totalPhysical", ctypes.c_ulonglong), ("availablePhysical", ctypes.c_ulonglong),
                ("totalPageFile", ctypes.c_ulonglong), ("availablePageFile", ctypes.c_ulonglong),
                ("totalVirtual", ctypes.c_ulonglong), ("availableVirtual", ctypes.c_ulonglong),
                ("availableExtendedVirtual", ctypes.c_ulonglong)]


if __name__ == "__main__":
    folder = Path(__file__).resolve().parents[6] / "output/tests/assembly-process"
    reference_file = folder / "all-products-recheck-native-shared-workpieces-door-uncalled-round_scene-two_blocks-source-reference.json"
    reference = json.loads(reference_file.read_text(encoding="utf-8"))
    memory = MemoryStatus()
    memory.length = ctypes.sizeof(memory)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
        raise OSError("GlobalMemoryStatusEx failed")
    report = {"at": datetime.now(timezone.utc).isoformat(), "referenceFile": str(reference_file),
              "display": resource_statistics(reference["display"]),
              "legacyManufacturing": resource_statistics(reference["document"]),
              "currentSourceExecution": resource_statistics(reference["sourceExecution"]),
              "memory": {name: getattr(memory, name) for name in
                         ("load", "totalPhysical", "availablePhysical", "totalPageFile", "availablePageFile")}}
    output = folder / "all-products-recheck-native-shared-workpieces-round-scene-complexity.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
