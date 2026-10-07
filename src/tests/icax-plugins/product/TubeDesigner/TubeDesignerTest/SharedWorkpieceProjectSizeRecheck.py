"""Read-only persistence-size diagnosis of the captured native round-scene run.

This reports a conservative typed-Variant lower bound, not an invented exact
native byte count: standard JSON cannot preserve every native numeric Variant
type.  The bound deliberately gives every number the shortest supported numeric
type tag and a one-byte value, so crossing the real reader limit is conclusive.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[6]
DIRECTORY = REPOSITORY / "output/tests/assembly-process"
STEM = "all-products-recheck-native-shared-workpieces-door-uncalled-round_scene-two_blocks"


def text_bytes(value: str) -> int:
    # VariantSerializer::_WriteEscaped preserves UTF-8 and uses these escapes.
    return len(value.encode("utf-8")) + sum(value.count(c) for c in '"\\\b\f\n\r\t') + 2


def tag_bytes(tag: str) -> int:
    return len('{"__variant_type":"' + tag + '","value":') + 1


def typed_lower_bound(value) -> int:
    if value is None:
        return tag_bytes("null") + 4
    if isinstance(value, bool):
        return tag_bytes("bool") + (4 if value else 5)
    if isinstance(value, (int, float)):
        return tag_bytes("int") + 1
    if isinstance(value, str):
        return tag_bytes("string") + text_bytes(value)
    if isinstance(value, list):
        return tag_bytes("Array") + 2 + max(0, len(value) - 1) + sum(map(typed_lower_bound, value))
    if isinstance(value, dict):
        return (tag_bytes("Object") + 2 + max(0, len(value) - 1)
                + sum(text_bytes(key) + 1 + typed_lower_bound(child) for key, child in value.items()))
    raise TypeError(type(value).__name__)


def inspect(value):
    counts = {"object": 0, "array": 0, "number": 0, "string": 0, "bool": 0, "null": 0}
    maximum_string = {"utf8Bytes": 0, "path": ""}

    def visit(node, path):
        if isinstance(node, dict):
            counts["object"] += 1
            for key, child in node.items():
                visit(child, path + "." + key)
        elif isinstance(node, list):
            counts["array"] += 1
            for index, child in enumerate(node):
                visit(child, f"{path}[{index}]")
        elif isinstance(node, str):
            counts["string"] += 1
            size = len(node.encode("utf-8"))
            if size > maximum_string["utf8Bytes"]:
                maximum_string.update(utf8Bytes=size, path=path)
        elif isinstance(node, bool):
            counts["bool"] += 1
        elif isinstance(node, (int, float)):
            counts["number"] += 1
        elif node is None:
            counts["null"] += 1
        else:
            raise TypeError(type(node).__name__)

    visit(value, "$")
    return {
        "standardJsonCompactUtf8Bytes": len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")),
        "nativeTypedVariantConservativeLowerBoundBytes": typed_lower_bound(value),
        "counts": counts,
        "largestOrdinaryString": maximum_string,
    }


def restore_matched_numeric_types(captured, source, restored):
    # StandardJsonCodec emits integral-valued doubles without a decimal suffix.
    # The untouched source reference retains Python JSON numeric types. Only
    # restore types at equal, matching numeric positions; never alter a value.
    if isinstance(captured, bool) or isinstance(source, bool):
        return captured
    if isinstance(captured, (int, float)) and isinstance(source, (int, float)):
        if captured == source and isinstance(source, float) and not isinstance(captured, float):
            restored[0] += 1
            return float(captured)
        return captured
    if isinstance(captured, dict) and isinstance(source, dict):
        return {key: restore_matched_numeric_types(value, source.get(key), restored)
                for key, value in captured.items()}
    if isinstance(captured, list) and isinstance(source, list) and len(captured) == len(source):
        return [restore_matched_numeric_types(value, source[index], restored)
                for index, value in enumerate(captured)]
    return captured


def main():
    fixture = DIRECTORY / (STEM + "-native-snapshot.json")
    captured = json.loads(fixture.read_text(encoding="utf-8"))
    snapshot = captured["snapshot"]
    fields = {
        "NeutralModel": snapshot["display"]["document"],
        "GeneratedParameters": snapshot["generatedParameters"],
        "ResolvedComponentModels": snapshot["resolvedComponentModels"],
        "ManufacturingDefinition": snapshot["manufacturing"]["document"],
        "ManufacturingParameters": snapshot["manufacturingParameters"],
        "ManufacturingModel": snapshot["manufacturingExecution"]["document"],
    }
    # Disassemble's committed transaction explicitly assigns the same actual
    # manufacturing map to both fields for every pure display-v2/raw-v4 run.
    fields["AssemblyManufacturingModel"] = fields["ManufacturingModel"]
    selected = inspect(fields)
    assert selected["nativeTypedVariantConservativeLowerBoundBytes"] > 64 * 1024 * 1024
    restored = [0]
    source_fixture = DIRECTORY / (STEM + "-source-reference.json")
    source = json.loads(source_fixture.read_text(encoding="utf-8"))
    native_diagnostic_fields = {
        "NeutralModel": restore_matched_numeric_types(fields["NeutralModel"], source["display"], restored),
        "GeneratedParameters": restore_matched_numeric_types(fields["GeneratedParameters"], source["parameters"], restored),
        "ResolvedComponentModels": fields["ResolvedComponentModels"],
        "ManufacturingDefinition": restore_matched_numeric_types(fields["ManufacturingDefinition"], source["manufacturing"], restored),
        "ManufacturingParameters": restore_matched_numeric_types(fields["ManufacturingParameters"], source["parameters"], restored),
        "ManufacturingModel": restore_matched_numeric_types(fields["ManufacturingModel"], source["sourceExecution"], restored),
    }
    native_diagnostic_fields["AssemblyManufacturingModel"] = native_diagnostic_fields["ManufacturingModel"]
    assert native_diagnostic_fields == fields
    native_fixture = DIRECTORY / "all-products-recheck-native-shared-workpieces-round-scene-captured-properties.json"
    native_fixture.write_text(json.dumps(native_diagnostic_fields, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    report = {
        "case": "door-uncalled-round_scene-two_blocks",
        "status": "conclusive-size-lower-bound; exact native serialization still requires a native diagnostic",
        "fixture": str(fixture.relative_to(REPOSITORY)).replace("\\", "/"),
        "fixtureSha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
        "actualComponentClass": "CGenerationRunComponent",
        "binaryStringField": "Component.Properties (the entire typed ObjectMap, not one raw script document)",
        "readerLimitBytes": 64 * 1024 * 1024,
        "fieldStatistics": {key: inspect(value) for key, value in fields.items()},
        "selectedActualComponentProperties": selected,
        "selectedSubsetOnly": True,
        "omittedSmallProperties": ["ProductID", "TemplateID", "TemplateVersion", "Status", "PartCount", "IssueCount", "PackageDigest", "AppliedAssemblyBindings"],
        "assemblyManufacturingModelEqualityEvidence": "TubeDesignerSDO.cpp Disassemble committed transaction assigns _PreparedProduct.ManufacturingModel to both ManufacturingModel and AssemblyManufacturingModel when display.schema==icax.display-model",
        "failedProjectFileWritten": False,
        "failureBeforeWriteEvidence": "CProjectFileCodec::WriteAtomic calls Encode then Decode before WriteAllAndFlush; native SaveAndReopen uses this real project-file path",
        "numericTypeCaveat": "Captured standard JSON omits native numeric Variant types. All numbers use int tag and one-byte value for this lower bound; exact serialization must use Data::VariantSerializer in a native process.",
        "nativeDiagnosticFixture": str(native_fixture.relative_to(REPOSITORY)).replace("\\", "/"),
        "nativeDiagnosticFixtureSha256": hashlib.sha256(native_fixture.read_bytes()).hexdigest(),
        "sourceMatchedFloatTypesRestored": restored[0],
        "numericTypeSourceFixture": str(source_fixture.relative_to(REPOSITORY)).replace("\\", "/"),
        "numericTypeSourceFixtureSha256": hashlib.sha256(source_fixture.read_bytes()).hexdigest(),
        "numericValuesUnchanged": native_diagnostic_fields == fields,
    }
    destination = DIRECTORY / "all-products-recheck-native-shared-workpieces-round-scene-project-size.json"
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(destination.relative_to(REPOSITORY)).replace("\\", "/"), "selectedTypedLowerBoundBytes": selected["nativeTypedVariantConservativeLowerBoundBytes"], "readerLimitBytes": report["readerLimitBytes"], "ordinaryLargestString": selected["largestOrdinaryString"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
