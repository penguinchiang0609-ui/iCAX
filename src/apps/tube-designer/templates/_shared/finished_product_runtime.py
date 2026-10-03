"""Finished shape inputs. This module never reads an assembly or mold template.

Spans are geometric regions of the desired shape, not manufacturing parts.
Their conversion into one or several blanks belongs to the chosen process.
"""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path

CATALOGUE = Path(__file__).resolve().parent.parent / "finished-product" / "shapes.json"


class BoundProcessDescriptor(dict):
    """In-memory validation adapter; never serialized as a stored template."""


def catalogue():
    data = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    if data.get("schema") != "icax.finished-product-shapes" or data.get("schemaVersion") != 1:
        raise ValueError("成品造型目录格式无效")
    return data["shapes"]


def definition(shape_id):
    for shape in catalogue():
        if shape["id"] == shape_id:
            return shape
    raise ValueError(f"成品造型不存在：{shape_id}")


def create(shape_id):
    shape = definition(shape_id)
    return {"schema": "icax.finished-product", "schemaVersion": 1, "shapeId": shape_id,
            "parameters": {p["key"]: copy.deepcopy(p["defaultValue"]) for p in shape["parameters"]},
            "spans": {span["id"]: {key: copy.deepcopy(span[key])
                      for key in ("profileRef", "parameters", "length")} for span in shape["spans"]}}


def validate(product):
    if (not isinstance(product, dict) or set(product) != {
            "schema", "schemaVersion", "shapeId", "parameters", "spans"}
            or product["schema"] != "icax.finished-product" or product["schemaVersion"] != 1):
        raise ValueError("装配工艺必须接收完整的成品造型输入")
    shape = definition(product["shapeId"])
    values, spans = product["parameters"], product["spans"]
    if not isinstance(values, dict) or set(values) != {p["key"] for p in shape["parameters"]}:
        raise ValueError("成品外形参数不完整或含有工艺字段")
    for p in shape["parameters"]:
        value = values[p["key"]]
        if p["valueType"] == "choice":
            if value not in [option["value"] for option in p["options"]]:
                raise ValueError(f"成品参数 {p['key']} 不属于允许选项")
        elif (isinstance(value, bool) or not isinstance(value, (int, float))
              or not math.isfinite(value) or not p.get("min", -math.inf) <= value <= p.get("max", math.inf)):
            raise ValueError(f"成品参数 {p['key']} 超出允许范围")
    if not isinstance(spans, dict) or set(spans) != {span["id"] for span in shape["spans"]}:
        raise ValueError("成品外形区域不完整")
    for key, span in spans.items():
        if not isinstance(span, dict) or set(span) != {"profileRef", "parameters", "length"}:
            raise ValueError(f"成品外形区域 {key} 含有工艺或未知字段")
        ref, length = span["profileRef"], span["length"]
        if (not isinstance(ref, dict) or set(ref) != {"scope", "id"}
                or ref["scope"] not in ("system", "user", "template")
                or not isinstance(ref["id"], str) or not ref["id"].strip()):
            raise ValueError("成品截面引用无效")
        if (isinstance(length, bool) or not isinstance(length, (int, float))
                or not math.isfinite(length) or not 1 <= length <= 100000):
            raise ValueError("成品外形长度无效")
        if not isinstance(span["parameters"], dict):
            raise ValueError("成品截面尺寸无效")
        for value in span["parameters"].values():
            if not isinstance(value, (str, int, float, bool)) or isinstance(value, float) and not math.isfinite(value):
                raise ValueError("成品截面尺寸必须是有限数值或选项")
    return copy.deepcopy(product)


def check_process_input(contract, product):
    if product["shapeId"] != contract["shapeId"]:
        raise ValueError("当前装配工艺不适用于此成品造型")
    for key, requirement in contract.get("requirements", {}).items():
        value = product["parameters"].get(key)
        if value is None or ("values" in requirement and value not in requirement["values"]):
            raise ValueError(f"当前工艺不支持成品参数 {key}={value}")
        if any((op == "min" and value < limit) or (op == "max" and value > limit)
               for op, limit in requirement.items() if op in ("min", "max")):
            raise ValueError(f"当前工艺不支持成品参数 {key}={value}；请更换工艺，成品保持不变")


def bind_descriptor(descriptor):
    """Internal adapter for existing process solvers; the process owns no shape.

    The public descriptor stays process-only. Materialize read-only input
    definitions solely for expression and role validation.
    """
    if isinstance(descriptor, BoundProcessDescriptor):
        return copy.deepcopy(descriptor)
    if "inputContract" not in descriptor:
        raise ValueError("装配工艺必须声明 inputContract")
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "icax_assembly_process_contract", Path(__file__).with_name("assembly_process_contract.py"))
    contract_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(contract_module)
    return contract_module.bind_descriptor(descriptor, BoundProcessDescriptor)
