"""Immutable, product-independent inputs for reusable assembly functions.

Product adapters may select local regions. Processing functions only see those
regions, their material coordinates, and the measurements they require.
"""
from __future__ import annotations

import copy
import math
import uuid

SCHEMA = "icax.assembly-process-input"
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
PLACEMENT_KEYS = {"station", "frame", "rotation", "face", "sequence"}


def bind_descriptor(descriptor, bound_type=dict):
    """Compile expression symbols without consulting a product catalogue.

    These section fields declare dimensions available to expression validation;
    they are never substitutes for a missing actual processing input.
    """
    contract = descriptor.get("inputContract")
    if (not isinstance(contract, dict) or contract.get("schemaVersion") != 1
            or contract.get("roles") != [p["role"] for p in descriptor.get("participants", [])]):
        raise ValueError("装配工艺必要数据角色契约无效")
    if "designParts" in descriptor.get("previewScene", {}) or any(
            p.get("scope") == "scene" for p in descriptor.get("parameters", [])):
        raise ValueError("工艺不能定义成品造型或混入成品参数")
    result = bound_type(copy.deepcopy(descriptor))
    keys = {p["key"] for p in result.get("parameters", [])}
    definitions = contract.get("geometryParameters")
    if not isinstance(definitions, list):
        raise ValueError("工艺必须声明必要几何量")
    for definition in definitions:
        if not isinstance(definition, dict) or definition.get("key") in keys:
            raise ValueError("工艺几何量与加工参数重名或无效")
        keys.add(definition["key"])
        result["parameters"].append({**copy.deepcopy(definition), "scope": "scene"})
    unknown = set(contract.get("requirements", {}))-{p["key"] for p in definitions}
    if unknown:
        raise ValueError("工艺能力条件引用未声明几何量")
    labels = {p["role"]: p.get("label", p["role"]) for p in result["participants"]}
    result["previewScene"]["designParts"] = [
        {"role": role, "label": labels[role], "profileRef": {"scope": "system", "id": "rect"},
         "parameters": {"width": 1, "depth": 1, "wallThickness": 0.1,
                        "cornerRadius": 0, "innerRadius": 0}, "length": 1, "pose": {}}
        for role in contract["roles"]]
    return result


def number(value, label, minimum=-math.inf, maximum=math.inf):
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or not minimum <= value <= maximum):
        raise ValueError(f"{label} 必须为范围内的有限数值")
    return float(value)


def rigid_matrix(value, label="加工局部坐标"):
    if not isinstance(value, (list, tuple)) or len(value) != 16:
        raise ValueError(f"{label}必须为 16 项矩阵")
    result = [number(component, label) for component in value]
    if any(abs(a-b) > 1e-7 for a, b in zip(result[12:], [0, 0, 0, 1])):
        raise ValueError(f"{label}不是仿射坐标")
    axes = [[result[row*4+column] for row in range(3)] for column in range(3)]
    for i, axis in enumerate(axes):
        if abs(sum(v*v for v in axis)-1) > 1e-6:
            raise ValueError(f"{label}轴必须是单位向量")
        if any(abs(sum(a*b for a, b in zip(axis, axes[j]))) > 1e-6 for j in range(i)):
            raise ValueError(f"{label}轴必须互相垂直")
    cross = [axes[0][1]*axes[1][2]-axes[0][2]*axes[1][1],
             axes[0][2]*axes[1][0]-axes[0][0]*axes[1][2],
             axes[0][0]*axes[1][1]-axes[0][1]*axes[1][0]]
    if sum(a*b for a, b in zip(cross, axes[2])) < 1-1e-6:
        raise ValueError(f"{label}必须是右手坐标系")
    return result


def normalize_part(part, role, material=False):
    if not isinstance(part, dict):
        raise ValueError(f"{role} 必须提供实际管材数据")
    allowed = {"id", "label", "profileRef", "parameters", "length", "matrix", "anchor",
               "section", "memberEntityId", "itemKey", "nodeFrame", "productAnchor"}
    if set(part)-allowed:
        raise ValueError(f"{role} 包含未声明的加工输入字段")
    result = copy.deepcopy(part)
    ref = result.get("profileRef")
    if (not isinstance(ref, dict) or set(ref)-{"scope", "id", "templateId"}
            or ref.get("scope") not in ("system", "user", "template")
            or not isinstance(ref.get("id"), str) or not ref["id"].strip() or len(ref["id"]) > 80):
        raise ValueError(f"{role} 缺少实际管型引用")
    if ref["scope"] == "template" and (not isinstance(ref.get("templateId"), str) or not ref["templateId"].strip()):
        raise ValueError(f"{role} 模板管型缺少 templateId")
    if ref["scope"] == "user":
        try:
            uuid.UUID(ref["id"])
        except (ValueError, TypeError):
            raise ValueError(f"{role} 用户管型标识不是有效 UUID") from None
    if not isinstance(result.get("parameters"), dict):
        raise ValueError(f"{role} 缺少实际截面参数")
    for value in result["parameters"].values():
        if not isinstance(value, (str, bool, int, float)) or isinstance(value, float) and not math.isfinite(value):
            raise ValueError(f"{role} 截面参数无效")
    result["length"] = number(result.get("length"), f"{role} 长度", 1, 100000)
    if "matrix" not in result and not material:
        raise ValueError(f"{role} 缺少实际局部姿态，不能使用示例位置")
    result["matrix"] = rigid_matrix(result.get("matrix", IDENTITY), f"{role} 局部坐标")
    if "anchor" in result and not isinstance(result["anchor"], dict):
        raise ValueError(f"{role} 加工锚点无效")
    if "anchor" in result:
        anchor = result["anchor"]
        if anchor.get("kind") not in ("side", "end"):
            raise ValueError(f"{role} 必须选择实际端部或侧面加工定位")
        if "rotation" in anchor:
            number(anchor["rotation"], f"{role} 加工轴向旋转", -360000, 360000)
        if anchor["kind"] == "end":
            if anchor.get("end") not in ("start", "end"):
                raise ValueError(f"{role} 必须选择起端或末端")
            if "trim" in anchor:
                number(anchor["trim"], f"{role} 端部退切", 0, result["length"])
        else:
            if anchor.get("reference") != "start" or anchor.get("face") not in ("top", "bottom", "left", "right"):
                raise ValueError(f"{role} 侧面定位必须明确加工面并从原材起点计算")
            number(anchor.get("station"), f"{role} 加工站位", 0, result["length"])
            if "offset" in anchor:
                number(anchor["offset"], f"{role} 加工横移", -100000, 100000)
    if "section" in result and not isinstance(result["section"], dict):
        raise ValueError(f"{role} 实际截面无效")
    return result


def normalize(descriptor, process_input):
    if (not isinstance(process_input, dict) or set(process_input)-{
            "schema", "schemaVersion", "parts", "geometry", "resources"}
            or not {"schema", "schemaVersion", "parts", "geometry"} <= set(process_input)
            or process_input.get("schema") != SCHEMA or process_input.get("schemaVersion") != 1):
        raise ValueError("必须提供独立的装配工艺必要数据输入")
    contract = descriptor.get("inputContract")
    if not isinstance(contract, dict):
        raise ValueError("装配工艺未声明必要数据契约")
    parts = process_input["parts"]
    if not isinstance(parts, dict) or not parts:
        raise ValueError("装配工艺必须提供实际局部管件")
    stock_mode = set(parts) == {"stock"}
    if stock_mode:
        if "stock-operation" not in contract.get("supportedInputModes", []):
            raise ValueError("该连接工艺需要其声明的实际参与部位")
    elif set(parts) != set(contract["roles"]):
        raise ValueError("实际加工输入必须覆盖本次调用的角色")
    normalized_parts = {role: normalize_part(part, role, stock_mode) for role, part in parts.items()}
    geometry = process_input["geometry"]
    if not isinstance(geometry, dict):
        raise ValueError("工艺几何量必须为对象")
    definitions = {item["key"]: item for item in contract.get("geometryParameters", [])}
    if set(geometry)-set(definitions)-PLACEMENT_KEYS:
        raise ValueError("工艺几何输入包含未声明字段")
    # A stock operation describes a local bend, not a fictitious two-arm shape.
    required = {key for key in definitions if not stock_mode or key not in ("planeRotation",)}
    if set(geometry) < required or required-set(geometry):
        raise ValueError("工艺缺少必要几何量：" + ", ".join(sorted(required-set(geometry))))
    result = copy.deepcopy(geometry)
    for key, definition in definitions.items():
        if key not in result:
            continue
        value = result[key]
        kind = definition["valueType"]
        if kind in ("number", "integer"):
            checked_value = abs(value) if stock_mode and key == "angle" and type(value) in (int, float) else value
            number(checked_value, key, definition.get("min", -math.inf), definition.get("max", math.inf))
            if kind == "integer" and int(value) != value:
                raise ValueError(f"{key} 必须为整数")
        elif kind == "boolean" and not isinstance(value, bool):
            raise ValueError(f"{key} 必须为开关")
        elif kind == "choice" and value not in [item["value"] for item in definition["options"]]:
            raise ValueError(f"{key} 不属于工艺允许选项")
    for key, requirement in contract.get("requirements", {}).items():
        if stock_mode and key == "planeRotation":
            continue
        value = result.get(key)
        if stock_mode and key == "angle" and type(value) in (int, float):
            value = abs(value)
        if value is None or ("values" in requirement and value not in requirement["values"]):
            raise ValueError(f"本次工艺不支持几何量 {key}={value}")
        if any((op == "min" and value < limit) or (op == "max" and value > limit)
               for op, limit in requirement.items() if op in ("min", "max")):
            raise ValueError(f"本次工艺几何量 {key} 超出能力范围")
    if "station" in result:
        number(result["station"], "加工站位", 0, 100000)
    elif stock_mode:
        raise ValueError("母材上的加工必须明确提供从起点计算的站位")
    if "rotation" in result:
        number(result["rotation"], "加工朝向", -360000, 360000)
    if "sequence" in result:
        number(result["sequence"], "工序顺序", 0, 100000)
    if isinstance(result.get("frame"), (list, tuple)):
        result["frame"] = rigid_matrix(result["frame"], "加工坐标")
    elif "frame" in result and not isinstance(result["frame"], dict):
        raise ValueError("加工坐标无效")
    normalized = {"schema": SCHEMA, "schemaVersion": 1, "parts": normalized_parts, "geometry": result}
    if "resources" in process_input:
        resources = process_input["resources"]
        declared = {process["id"] for process in descriptor.get("partProcesses", [])}
        if not isinstance(resources, dict) or set(resources)-declared:
            raise ValueError("加工资源必须对应本次工艺声明的加工步骤")
        for process_id, resource in resources.items():
            if not isinstance(resource, dict) or set(resource) != {"ref"}:
                raise ValueError(f"加工资源 {process_id} 必须提供明确的资源引用")
            ref = resource["ref"]
            if (not isinstance(ref, dict) or set(ref)-{"scope", "id", "version", "digest", "templateId"}
                    or ref.get("scope", "system") not in ("system", "user", "template")
                    or not isinstance(ref.get("id"), str) or not ref["id"].strip()
                    or len(ref["id"]) > 80):
                raise ValueError(f"加工资源 {process_id} 的引用无效")
            if ref.get("scope") == "template" and not ref.get("templateId"):
                raise ValueError(f"加工资源 {process_id} 缺少所属模板")
            for key in ("version", "digest", "templateId"):
                if key in ref and (not isinstance(ref[key], str) or not ref[key] or len(ref[key]) > 128):
                    raise ValueError(f"加工资源 {process_id} 的 {key} 无效")
        normalized["resources"] = copy.deepcopy(resources)
    return normalized


def select_example_anchors(descriptor, process_input):
    """Select demonstrator sites in the host from actual sample axes.

    This is an explicit example-selection operation, never a missing-data
    fallback inside a processing function.
    """
    result = copy.deepcopy(process_input)
    parts = result["parts"]
    declared = descriptor.get("productBinding", {}).get("anchors", {})
    def dot(a, b):
        return sum(x*y for x, y in zip(a, b))
    def site(part):
        matrix = part["matrix"]
        return [matrix[i] for i in (3, 7, 11)], [matrix[i] for i in (0, 4, 8)]
    for role, part in parts.items():
        kind = declared.get(role)
        processes = [p for p in descriptor.get("partProcesses", []) if role in p["participants"]]
        if kind is None:
            placements = [p.get("previewPlacement", {}) for p in processes]
            kind = "side" if any(p.get("target") == "side" for p in placements) else "end"
        origin, axis = site(part)
        others = [other for key, other in parts.items() if key != role]
        if kind == "end":
            def distance(station):
                point = [o+a*station for o, a in zip(origin, axis)]
                distances = []
                for other in others:
                    other_origin, other_axis = site(other)
                    offset = [p-o for p, o in zip(point, other_origin)]
                    along = min(other["length"], max(0., dot(offset, other_axis)))
                    distances.append(sum((d-a*along)**2 for d, a in zip(offset, other_axis)))
                return min(distances, default=0.)
            selected = "start" if distance(0) <= distance(part["length"]) else "end"
            part["anchor"] = {"kind": "end", "end": selected, "rotation": 0, "trim": 0}
        else:
            candidates = []
            for other in others:
                other_origin, other_axis = site(other)
                offset = [o-q for o, q in zip(origin, other_origin)]
                parallel = dot(axis, other_axis)
                if abs(1-parallel*parallel) > 1e-8:
                    station = (parallel*dot(other_axis, offset)-dot(axis, offset))/(1-parallel*parallel)
                else:
                    # Centre the actual common axial interval, not each part.
                    first = dot([q-o for q, o in zip(other_origin, origin)], axis)
                    last = first+parallel*other["length"]
                    low, high = max(0., min(first, last)), min(part["length"], max(first, last))
                    if high < low:
                        continue
                    station = (low+high)/2
                if -1e-6 <= station <= part["length"]+1e-6:
                    candidates.append(min(part["length"], max(0., station)))
            if not candidates:
                raise ValueError(f"示例角色 {role} 没有可选的实际连接站位")
            placement = next((p.get("previewPlacement", {}) for p in processes
                              if p.get("previewPlacement", {}).get("face") in ("top", "bottom", "left", "right")), {})
            part["anchor"] = {"kind": "side", "reference": "start", "station": candidates[0],
                              "offset": 0, "rotation": 0, "face": placement.get("face", "top")}
    return result


def from_product(descriptor, product, product_plan, role_mapping=None, example_selection=False):
    """Host adapter. Product identities never reach the function."""
    example = descriptor["exampleInput"]
    mapping = role_mapping or example.get("roles", {})
    roles = descriptor.get("inputContract", {}).get("roles", [p["role"] for p in descriptor["participants"]])
    if set(mapping) != set(roles):
        raise ValueError("请选择本次工艺所需的实际管段角色")
    design = {part["role"]: part for part in product_plan["designParts"]}
    parts = {}
    for role, region in mapping.items():
        if region not in design:
            raise ValueError(f"缺少角色 {role} 的实际管段，请重新映射")
        source = design[region]
        parts[role] = {key: copy.deepcopy(source["request"][key]) for key in ("profileRef", "parameters", "length")}
        parts[role]["matrix"] = list(source["matrix"])
        parts[role]["id"] = region
        parts[role]["label"] = source.get("label", region)
    aliases = example.get("parameterBindings", {})
    geometry = {}
    for definition in descriptor.get("inputContract", {}).get("geometryParameters", []):
        key = definition["key"]
        source_key = aliases.get(key, key)
        if source_key not in product["parameters"]:
            raise ValueError(f"成品适配层缺少必要几何量 {key}")
        geometry[key] = copy.deepcopy(product["parameters"][source_key])
    local = normalize(descriptor, {"schema": SCHEMA, "schemaVersion": 1, "parts": parts, "geometry": geometry})
    return select_example_anchors(descriptor, local) if example_selection else local
