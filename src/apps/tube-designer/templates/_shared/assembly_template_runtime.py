"""Load and validate declarative TubeDesigner assembly templates."""
from __future__ import annotations

import copy
import ast
import json
import math
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent / "assembly"
PROCESS_ROOT = Path(__file__).resolve().parent.parent / "mold"
SCHEMA = "icax.assembly-template"
MAX_BYTES = 4 * 1024 * 1024


def _text(value, label):
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label}不能为空")
    return value


def _validate_parameter(item, keys):
    if not isinstance(item, dict):
        raise ValueError("装配参数必须是对象")
    key = _text(item.get("key"), "装配参数键")
    if not re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", key) or key in keys:
        raise ValueError(f"装配参数键无效或重复：{key}")
    keys.add(key)
    kind = item.get("valueType")
    value = item.get("defaultValue")
    if kind in ("number", "integer"):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f"{key} 的默认值必须是有限数值")
        if kind == "integer" and int(value) != value:
            raise ValueError(f"{key} 的默认值必须是整数")
        if value < item.get("min", -math.inf) or value > item.get("max", math.inf):
            raise ValueError(f"{key} 的默认值超出范围")
    elif kind == "boolean":
        if not isinstance(value, bool):
            raise ValueError(f"{key} 的默认值必须是开关")
    elif kind == "choice":
        choices = item.get("options")
        if not isinstance(choices, list) or not choices:
            raise ValueError(f"{key} 必须声明选项")
        values = [option.get("value") for option in choices if isinstance(option, dict)]
        if len(values) != len(choices) or value not in values:
            raise ValueError(f"{key} 的默认值不属于选项")
    else:
        raise ValueError(f"{key} 的参数类型不受支持")


def _load_part_process(process_id):
    manifest = PROCESS_ROOT / process_id / "tool.json"
    if not manifest.is_file():
        raise ValueError(f"缺少单件工艺：{process_id}")
    raw = manifest.read_bytes()
    if len(raw) > MAX_BYTES:
        raise ValueError(f"单件工艺 {process_id} 超过 4 MB")
    descriptor = json.loads(raw)
    if descriptor.get("id") != process_id or descriptor.get("schema") != "icax.punch-tool":
        raise ValueError(f"单件工艺 {process_id} 的描述文件无效")
    return descriptor


def _process_summary(descriptor):
    return {
        "id": descriptor["id"],
        "version": descriptor.get("version", ""),
        "displayName": descriptor.get("displayName", descriptor["id"]),
        "category": descriptor.get("category", "其他"),
        "description": descriptor.get("description", ""),
        "target": descriptor.get("target", "side"),
        "requiresSection": bool(descriptor.get("requiresSection", False)),
        "parameters": copy.deepcopy(descriptor.get("parameters", [])),
        "operationParameters": copy.deepcopy(descriptor.get("operationParameters", [])),
        "parameterDiagram": copy.deepcopy(descriptor.get("parameterDiagram")),
    }


def _parameter_definitions(descriptor):
    return [
        *descriptor.get("parameters", []),
        *descriptor.get("operationParameters", []),
    ]


def _condition_matches(condition, values):
    if not condition:
        return True
    if not isinstance(condition, dict):
        return False
    operation = condition.get("op", "eq")
    if operation == "all":
        return all(_condition_matches(item, values) for item in condition.get("conditions", []))
    if operation == "any":
        return any(_condition_matches(item, values) for item in condition.get("conditions", []))
    if operation == "not":
        return not _condition_matches(condition.get("condition"), values)
    actual = values.get(condition.get("parameter", condition.get("key")))
    expected = condition.get("value")
    if operation == "eq":
        return actual == expected
    if operation == "ne":
        return actual != expected
    if operation == "in":
        return actual in condition.get("values", [])
    if operation == "notIn":
        return actual not in condition.get("values", [])
    return False


def _evaluate_expression(source, values):
    names = {}

    def replace(match):
        key = match.group(1)
        if key not in values:
            raise ValueError(f"装配表达式引用了不存在的参数：{key}")
        name = f"parameter_{key}"
        names[name] = values[key]
        return name

    translated = re.sub(r"\$([A-Za-z][A-Za-z0-9_]*)", replace, source)
    tree = ast.parse(translated, mode="eval")

    def visit(node):
        if isinstance(node, ast.Expression):
            return visit(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float, str, bool)):
            return node.value
        if isinstance(node, ast.Name) and node.id in names:
            return names[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = visit(node.operand)
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("装配表达式的一元运算只支持数值")
            return value if isinstance(node.op, ast.UAdd) else -value
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult, ast.Div)):
            left, right = visit(node.left), visit(node.right)
            if isinstance(left, bool) or isinstance(right, bool) or not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
                raise ValueError("装配表达式的算术运算只支持数值")
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if right == 0:
                raise ValueError("装配表达式不能除以零")
            return left / right
        raise ValueError("装配表达式包含不受支持的语法")

    return visit(tree)


def _resolve(value, values):
    if isinstance(value, str) and "$" in value:
        return _evaluate_expression(value, values)
    if isinstance(value, list):
        return [_resolve(item, values) for item in value]
    if isinstance(value, dict):
        return {key: _resolve(item, values) for key, item in value.items()}
    return copy.deepcopy(value)


def _finite_vector(value, size, label):
    if not isinstance(value, list) or len(value) != size:
        raise ValueError(f"{label} 必须包含 {size} 个数值")
    result = []
    for item in value:
        if isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item):
            raise ValueError(f"{label} 必须是有限数值")
        result.append(float(item))
    return result


def _normalize(vector, label):
    length = math.sqrt(sum(item * item for item in vector))
    if length <= 1e-9:
        raise ValueError(f"{label} 不能为零向量")
    return [item / length for item in vector]


def _cross(left, right):
    return [
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    ]


def _dot(left, right):
    return sum(a * b for a, b in zip(left, right))


def _rotate(vector, axis, degrees):
    axis = _normalize(axis, "旋转轴")
    radians = math.radians(degrees)
    cosine, sine = math.cos(radians), math.sin(radians)
    cross = _cross(axis, vector)
    dot = _dot(axis, vector)
    return [
        vector[index] * cosine + cross[index] * sine + axis[index] * dot * (1 - cosine)
        for index in range(3)
    ]


def _variant_value(owner, key, values, default=None):
    source = owner.get(key, default)
    variants = owner.get(f"{key}Variants", [])
    if not isinstance(variants, list):
        raise ValueError(f"{key}Variants 必须是数组")
    for variant in variants:
        if not isinstance(variant, dict):
            raise ValueError(f"{key}Variants 的分支必须是对象")
        if _condition_matches(variant.get("when"), values):
            source = variant.get("value", source)
            break
    return copy.deepcopy(source)


def _pose_matrix(pose, values, offset=None):
    pose = _resolve(pose or {}, values)
    origin = _finite_vector(pose.get("origin", [0, 0, 0]), 3, "零件原点")
    x_axis = _finite_vector(pose.get("direction", [1, 0, 0]), 3, "零件轴向")
    up = _finite_vector(pose.get("up", [0, 1, 0]), 3, "零件截面向上方向")
    axis_angle = pose.get("axisAngle")
    if axis_angle:
        if not isinstance(axis_angle, dict):
            raise ValueError("轴角旋转声明无效")
        axis = _finite_vector(axis_angle.get("axis"), 3, "旋转轴")
        degrees = axis_angle.get("degrees")
        if isinstance(degrees, bool) or not isinstance(degrees, (int, float)) or not math.isfinite(degrees):
            raise ValueError("旋转角度必须是有限数值")
        x_axis, up = _rotate(x_axis, axis, degrees), _rotate(up, axis, degrees)
    bend_angle = pose.get("bendAngle")
    if bend_angle is not None:
        plane_rotation = pose.get("bendPlaneRotation", 0)
        if (isinstance(bend_angle, bool) or not isinstance(bend_angle, (int, float)) or not math.isfinite(bend_angle)
                or isinstance(plane_rotation, bool) or not isinstance(plane_rotation, (int, float)) or not math.isfinite(plane_rotation)):
            raise ValueError("折弯姿态角度必须是有限数值")
        bend_axis = _rotate([0.0, 1.0, 0.0], x_axis, plane_rotation)
        x_axis, up = _rotate(x_axis, bend_axis, bend_angle), _rotate(up, bend_axis, bend_angle)
    x_axis = _normalize(x_axis, "零件轴向")
    up = [up[index] - _dot(up, x_axis) * x_axis[index] for index in range(3)]
    y_axis = _normalize(up, "零件截面向上方向")
    z_axis = _normalize(_cross(x_axis, y_axis), "零件截面法向")
    axial_translation = pose.get("axialTranslation", 0)
    if (isinstance(axial_translation, bool) or not isinstance(axial_translation, (int, float))
            or not math.isfinite(axial_translation)):
        raise ValueError("零件轴向定位量必须是有限数值")
    origin = [origin[index] + x_axis[index] * axial_translation for index in range(3)]
    if offset:
        origin = [origin[index] + offset[index] for index in range(3)]
    return [
        x_axis[0], y_axis[0], z_axis[0], origin[0],
        x_axis[1], y_axis[1], z_axis[1], origin[1],
        x_axis[2], y_axis[2], z_axis[2], origin[2],
        0.0, 0.0, 0.0, 1.0,
    ]


def _validate_preview_scene(descriptor, roles, process_ids):
    scene = descriptor.get("previewScene")
    if not isinstance(scene, dict):
        raise ValueError("装配模板必须声明 previewScene")
    design_parts = scene.get("designParts")
    if not isinstance(design_parts, list) or not 2 <= len(design_parts) <= 4:
        raise ValueError("装配预览必须声明 2～4 个设计位置零件")
    preview_roles = []
    for part in design_parts:
        if not isinstance(part, dict):
            raise ValueError("装配预览零件必须是对象")
        role = _text(part.get("role"), "装配预览角色")
        preview_roles.append(role)
        reference = part.get("profileRef")
        if not isinstance(reference, dict) or reference.get("scope") not in ("system", "user", "template") or not reference.get("id"):
            raise ValueError(f"装配预览角色 {role} 的管型引用无效")
        if not isinstance(part.get("length"), (int, float, str)):
            raise ValueError(f"装配预览角色 {role} 缺少长度")
        if not isinstance(part.get("pose", {}), dict):
            raise ValueError(f"装配预览角色 {role} 的姿态无效")
    if sorted(preview_roles) != sorted(roles):
        raise ValueError("装配预览必须且只能覆盖全部逻辑零件")
    manufacturing = scene.get("manufacturingParts")
    if not isinstance(manufacturing, list) or not manufacturing:
        raise ValueError("装配预览必须声明下料结果")
    blank_ids = {item.get("id") for item in descriptor["manufacturingPlan"]["blankParts"]}
    declared = set()
    for part in manufacturing:
        if not isinstance(part, dict) or part.get("id") not in blank_ids:
            raise ValueError("装配预览引用了不存在的下料零件")
        declared.add(part["id"])
        if part.get("sourceRole") not in roles:
            raise ValueError(f"下料预览 {part['id']} 缺少有效 sourceRole")
        process_refs = part.get("processes", [])
        if not isinstance(process_refs, list) or any(item not in process_ids for item in process_refs):
            raise ValueError(f"下料预览 {part['id']} 引用了无效单件工艺")
        if not isinstance(part.get("explodedPose"), dict):
            raise ValueError(f"下料预览 {part['id']} 缺少按装配路径定义的炸开姿态")
    if declared != blank_ids:
        raise ValueError("装配预览必须覆盖全部下料零件")
    compare = scene.get("compareLayout", {})
    if not isinstance(compare, dict):
        raise ValueError("装配对照布局无效")
    _finite_vector(compare.get("designOffset", [0, 0, 100]), 3, "设计件对照偏移")
    _finite_vector(compare.get("manufacturingOffset", [0, 0, -100]), 3, "下料件对照偏移")


def _validate_template(descriptor, directory):
    if descriptor.get("schema") != SCHEMA or descriptor.get("schemaVersion") != 1:
        raise ValueError("装配模板格式不受支持")
    if descriptor.get("id") != directory.name or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", directory.name):
        raise ValueError("装配模板 id 必须与目录名一致")
    _text(descriptor.get("version"), "装配模板版本")
    _text(descriptor.get("displayName"), "装配模板名称")
    _text(descriptor.get("category"), "装配模板类别")
    participants = descriptor.get("participants")
    if not isinstance(participants, list) or not 2 <= len(participants) <= 4:
        raise ValueError("装配模板必须声明 2～4 个逻辑零件")
    roles = [_text(item.get("role") if isinstance(item, dict) else None, "逻辑零件角色") for item in participants]
    if len(set(roles)) != len(roles):
        raise ValueError("逻辑零件角色不能重复")

    plan = descriptor.get("manufacturingPlan")
    if not isinstance(plan, dict) or plan.get("realization") not in ("integrated", "separate", "hybrid"):
        raise ValueError("装配模板必须声明下料实现方式")
    blanks = plan.get("blankParts")
    if not isinstance(blanks, list) or not blanks:
        raise ValueError("装配模板必须声明下料零件")
    assigned = []
    blank_ids = set()
    for blank in blanks:
        if not isinstance(blank, dict):
            raise ValueError("下料零件必须是对象")
        blank_id = _text(blank.get("id"), "下料零件 id")
        if blank_id in blank_ids:
            raise ValueError("下料零件 id 不能重复")
        blank_ids.add(blank_id)
        members = blank.get("participants")
        if not isinstance(members, list) or not members or any(role not in roles for role in members):
            raise ValueError(f"下料零件 {blank_id} 引用了无效的逻辑零件")
        assigned.extend(members)
    if sorted(assigned) != sorted(roles):
        raise ValueError("每个逻辑零件必须且只能归入一个下料零件")
    if plan["realization"] == "integrated" and len(blanks) >= len(participants):
        raise ValueError("一体成形必须减少下料零件数量")
    if plan["realization"] == "separate" and len(blanks) != len(participants):
        raise ValueError("分件装配必须保留逻辑零件数量")

    keys = set()
    parameters = descriptor.get("parameters", [])
    if not isinstance(parameters, list) or len(parameters) > 64:
        raise ValueError("装配参数最多 64 项")
    for item in parameters:
        _validate_parameter(item, keys)

    processes = descriptor.get("partProcesses", [])
    if not isinstance(processes, list):
        raise ValueError("前置单件工艺必须是数组")
    parameter_map = {item["key"]: item for item in parameters}
    process_ids = set()
    for process in processes:
        if not isinstance(process, dict):
            raise ValueError("单件工艺引用必须是对象")
        process_id = _text(process.get("id"), "单件工艺引用 id")
        if process_id in process_ids:
            raise ValueError(f"单件工艺引用 id 重复：{process_id}")
        process_ids.add(process_id)
        targets = process.get("participants", [])
        if not isinstance(targets, list) or not targets or any(role not in roles for role in targets):
            raise ValueError(f"单件工艺 {process_id} 的作用对象无效")
        resource = process.get("resource")
        selection = process.get("resourceSelection")
        if isinstance(resource, dict):
            resource_id = resource.get("id")
            if resource.get("kind") != "part-process" or not isinstance(resource_id, str):
                raise ValueError(f"单件工艺 {process_id} 的资源引用无效")
            process_descriptor = _load_part_process(resource_id)
            definitions = {item.get("key") for item in _parameter_definitions(process_descriptor) if isinstance(item, dict)}
            bindings = process.get("parameterBindings", {})
            if not isinstance(bindings, dict) or any(key not in definitions for key in bindings):
                raise ValueError(f"单件工艺 {process_id} 的参数映射引用了不存在的参数")
            resource["descriptor"] = _process_summary(process_descriptor)
        elif isinstance(selection, dict):
            parameter_key = selection.get("parameter")
            definition = parameter_map.get(parameter_key)
            options = selection.get("options")
            if selection.get("kind") != "part-process" or not isinstance(definition, dict) or not isinstance(options, list) or not options:
                raise ValueError(f"单件工艺 {process_id} 的可选资源声明无效")
            declared_values = {option.get("value") for option in definition.get("options", []) if isinstance(option, dict)}
            if set(options) != declared_values:
                raise ValueError(f"单件工艺 {process_id} 的资源选项必须与装配参数一致")
            summaries = []
            process_definitions = {}
            for resource_id in options:
                process_descriptor = _load_part_process(resource_id)
                required_category = selection.get("category")
                if required_category and process_descriptor.get("category") != required_category:
                    raise ValueError(f"单件工艺 {resource_id} 不属于 {required_category}")
                process_definitions[resource_id] = {item.get("key") for item in _parameter_definitions(process_descriptor) if isinstance(item, dict)}
                summaries.append(_process_summary(process_descriptor))
            selection["resources"] = summaries
            bindings_by_resource = process.get("parameterBindingsByResource", {})
            if not isinstance(bindings_by_resource, dict) or any(key not in options for key in bindings_by_resource):
                raise ValueError(f"单件工艺 {process_id} 的分支参数映射无效")
            for resource_id, bindings in bindings_by_resource.items():
                if not isinstance(bindings, dict) or any(key not in process_definitions[resource_id] for key in bindings):
                    raise ValueError(f"单件工艺 {resource_id} 的参数映射引用了不存在的参数")
        else:
            raise ValueError(f"单件工艺 {process_id} 缺少资源引用")
        placement = process.get("previewPlacement")
        if not isinstance(placement, dict) or placement.get("target") not in ("side", "end", "part"):
            raise ValueError(f"单件工艺 {process_id} 缺少有效的预览落点")
        if placement.get("target") == "end" and placement.get("end") not in ("start", "end"):
            raise ValueError(f"单件工艺 {process_id} 的端部落点无效")
        if placement.get("sectionRole") is not None and placement.get("sectionRole") not in roles:
            raise ValueError(f"单件工艺 {process_id} 的截面输入角色无效")

    steps = descriptor.get("assemblyPath")
    if not isinstance(steps, list) or not steps:
        raise ValueError("装配模板必须包含实现步骤")
    outputs = descriptor.get("outputs")
    if not isinstance(outputs, dict) or outputs.get("manufacturingPartCount") != len(blanks):
        raise ValueError("装配输出的下料零件数量与归并方案不一致")
    _validate_preview_scene(descriptor, roles, process_ids)
    diagram = descriptor.get("parameterDiagram")
    if not isinstance(diagram, dict) or diagram.get("schemaVersion") != 2:
        raise ValueError("装配模板必须声明二维参数示意图")
    return descriptor


def _template_by_id(template_id):
    if not isinstance(template_id, str) or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", template_id):
        raise ValueError("装配模板 id 无效")
    manifest = ROOT / template_id / "assembly.json"
    if not manifest.is_file():
        raise ValueError(f"装配模板不存在：{template_id}")
    raw = manifest.read_bytes()
    if len(raw) > MAX_BYTES:
        raise ValueError("装配模板超过 4 MB")
    return _validate_template(json.loads(raw), manifest.parent)


def _validated_values(descriptor, supplied):
    if supplied is None:
        supplied = {}
    if not isinstance(supplied, dict):
        raise ValueError("装配参数必须是对象")
    definitions = {item["key"]: item for item in descriptor.get("parameters", [])}
    if any(key not in definitions for key in supplied):
        raise ValueError("装配参数包含未声明字段")
    values = {key: copy.deepcopy(item.get("defaultValue")) for key, item in definitions.items()}
    values.update(copy.deepcopy(supplied))
    for key, definition in definitions.items():
        value = values[key]
        kind = definition.get("valueType")
        if kind in ("number", "integer"):
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{key} 必须是有限数值")
            if kind == "integer" and int(value) != value:
                raise ValueError(f"{key} 必须是整数")
            if value < definition.get("min", -math.inf) or value > definition.get("max", math.inf):
                raise ValueError(f"{key} 超出允许范围")
        elif kind == "boolean" and not isinstance(value, bool):
            raise ValueError(f"{key} 必须是开关")
        elif kind == "choice":
            choices = [item.get("value") for item in definition.get("options", [])]
            if value not in choices:
                raise ValueError(f"{key} 不属于允许选项")
    return values


def _profile_request(part, values):
    length_source = _variant_value(part, "length", values)
    length = _resolve(length_source, values)
    if isinstance(length, bool) or not isinstance(length, (int, float)) or not math.isfinite(length) or length < 1 or length > 100000:
        raise ValueError(f"逻辑零件 {part.get('role')} 的预览长度无效")
    parameters = _resolve(_variant_value(part, "parameters", values, {}), values)
    if not isinstance(parameters, dict):
        raise ValueError("管型参数必须是对象")
    return {
        "profileRef": copy.deepcopy(part["profileRef"]),
        "parameters": parameters,
        "length": float(length),
        "features": [],
        "ends": {"start": {"type": "keep"}, "end": {"type": "keep"}},
        "diagnosticBooleanPreview": True,
    }


def _selected_process_descriptor(process, values):
    if isinstance(process.get("resource"), dict):
        return process["resource"]["descriptor"]
    selection = process["resourceSelection"]
    selected_id = values[selection["parameter"]]
    for descriptor in selection.get("resources", []):
        if descriptor.get("id") == selected_id:
            return descriptor
    raise ValueError(f"单件工艺 {process['id']} 没有匹配的资源")


def _process_values(process, descriptor, values, drafts):
    definitions = _parameter_definitions(descriptor)
    result = {item["key"]: copy.deepcopy(item.get("defaultValue")) for item in definitions if isinstance(item, dict)}
    process_drafts = drafts.get(process["id"], {}) if isinstance(drafts, dict) else {}
    selected_drafts = process_drafts.get(descriptor["id"], {}) if isinstance(process_drafts, dict) else {}
    if isinstance(selected_drafts, dict):
        result.update({key: copy.deepcopy(value) for key, value in selected_drafts.items() if key in result})
    bindings = {**process.get("parameterBindings", {}), **process.get("parameterBindingsByResource", {}).get(descriptor["id"], {})}
    result.update({key: _resolve(value, values) for key, value in bindings.items()})
    return result


def _section_input(role, part_by_role, values):
    source = part_by_role[role]
    return {
        "profileRef": copy.deepcopy(source["profileRef"]),
        "parameters": _resolve(source.get("parameters", {}), values),
    }


def _apply_process(request, process, descriptor, process_values, placement, part_by_role, values):
    parameter_keys = {item.get("key") for item in descriptor.get("parameters", []) if isinstance(item, dict)}
    operation_keys = {item.get("key") for item in descriptor.get("operationParameters", []) if isinstance(item, dict)}
    tool_parameters = {key: copy.deepcopy(value) for key, value in process_values.items() if key in parameter_keys}
    operation = {key: copy.deepcopy(value) for key, value in process_values.items() if key in operation_keys}
    tool_ref = {"id": descriptor["id"], "version": descriptor.get("version", "")}
    target = placement["target"]
    resolved = _resolve(placement, values)
    if target == "end":
        end = resolved["end"]
        entry = {
            "type": descriptor["id"],
            "toolRef": tool_ref,
            "toolParameters": tool_parameters,
            "datum": resolved.get("datum", "long"),
            **operation,
        }
        if descriptor.get("requiresSection"):
            role = resolved.get("sectionRole")
            if not role:
                raise ValueError(f"单件工艺 {process['id']} 需要截面输入")
            entry["section"] = _section_input(role, part_by_role, values)
        request["ends"][end] = entry
        return
    entry = {
        "id": process["id"],
        "enabled": True,
        "toolRef": tool_ref,
        "toolParameters": tool_parameters,
        "face": resolved.get("face", "top"),
        "reference": resolved.get("reference", "center"),
        "station": resolved.get("station", 0),
        "offset": resolved.get("offset", 0),
        "rotation": resolved.get("rotation", 0),
        "arrayCount": resolved.get("arrayCount", 1),
        "arrayPitch": resolved.get("arrayPitch", 0),
        "rowCount": resolved.get("rowCount", 1),
        "rowPitch": resolved.get("rowPitch", 0),
        **operation,
    }
    for key in ("blindHole", "cutDepth", "opposite", "allowOpen"):
        if key in resolved:
            entry[key] = resolved[key]
    if target == "part":
        entry["toolTarget"] = "part"
    if descriptor.get("requiresSection"):
        role = resolved.get("sectionRole")
        if not role:
            raise ValueError(f"单件工艺 {process['id']} 需要截面输入")
        entry["section"] = _section_input(role, part_by_role, values)
    request["features"].append(entry)


def preview_plan(template_id, supplied_values=None, process_drafts=None):
    descriptor = _template_by_id(template_id)
    values = _validated_values(descriptor, supplied_values)
    scene = descriptor["previewScene"]
    part_by_role = {item["role"]: item for item in scene["designParts"]}
    compare = _resolve(scene.get("compareLayout", {}), values)
    design_offset = _finite_vector(compare.get("designOffset", [0, 0, 100]), 3, "设计件对照偏移")
    manufacturing_offset = _finite_vector(compare.get("manufacturingOffset", [0, 0, -100]), 3, "下料件对照偏移")
    participant_names = {item["role"]: item["label"] for item in descriptor["participants"]}
    design_parts = []
    for item in scene["designParts"]:
        design_parts.append({
            "id": f"design-{item['role']}",
            "role": item["role"],
            "label": participant_names[item["role"]],
            "request": _profile_request(item, values),
            "matrix": _pose_matrix(_variant_value(item, "pose", values, {}), values),
            "compareMatrix": _pose_matrix(_variant_value(item, "pose", values, {}), values, design_offset),
        })
    processes = {item["id"]: item for item in descriptor.get("partProcesses", [])}
    blank_participants = {item["id"]: list(item["participants"]) for item in descriptor["manufacturingPlan"]["blankParts"]}
    manufacturing_parts = []
    for item in scene["manufacturingParts"]:
        source = part_by_role[item["sourceRole"]]
        request_source = {
            **source,
            **{key: item[key] for key in ("length", "lengthVariants") if key in item},
        }
        request = _profile_request(request_source, values)
        for process_id in item.get("processes", []):
            process = processes[process_id]
            if not _condition_matches(process.get("appliesWhen"), values):
                continue
            descriptor_summary = _selected_process_descriptor(process, values)
            resolved_values = _process_values(process, descriptor_summary, values, process_drafts or {})
            _apply_process(request, process, descriptor_summary, resolved_values, process["previewPlacement"], part_by_role, values)
        pose = _variant_value(item, "explodedPose", values, {})
        exploded_matrix = _pose_matrix(pose, values)
        manufacturing_parts.append({
            "id": f"manufacturing-{item['id']}",
            "blankId": item["id"],
            "sourceRole": item["sourceRole"],
            "participantRoles": blank_participants[item["id"]],
            "label": item.get("label", item["id"]),
            "request": request,
            "matrix": exploded_matrix,
            "explodedMatrix": exploded_matrix,
            "compareMatrix": _pose_matrix(pose, values, manufacturing_offset),
        })
    return {
        "schema": "icax.assembly-preview-plan",
        "schemaVersion": 1,
        "templateId": descriptor["id"],
        "templateVersion": descriptor["version"],
        "parameters": values,
        "designParts": design_parts,
        "manufacturingParts": manufacturing_parts,
    }


def catalogue():
    templates, errors = [], []
    if not ROOT.is_dir():
        return {"assemblies": [], "errors": ["装配模板目录不存在"]}
    for manifest in sorted(ROOT.glob("*/assembly.json")):
        try:
            raw = manifest.read_bytes()
            if len(raw) > MAX_BYTES:
                raise ValueError("装配模板超过 4 MB")
            descriptor = json.loads(raw)
            validated = _validate_template(descriptor, manifest.parent)
            if not validated.get("catalogueHidden", False):
                templates.append(copy.deepcopy(validated))
        except (ValueError, OSError, json.JSONDecodeError, KeyError, TypeError) as error:
            errors.append(f"{manifest.parent.name}: {error}")
    return {"assemblies": templates, "errors": errors}


def generate(parameters, _context):
    if parameters.get("action") == "catalogue":
        return catalogue()
    if parameters.get("action") == "preview-plan":
        return preview_plan(
            parameters.get("templateId"),
            parameters.get("parameters"),
            parameters.get("processDrafts"),
        )
    raise ValueError("未知装配模板运行时操作")
