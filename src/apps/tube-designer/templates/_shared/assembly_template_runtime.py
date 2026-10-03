"""Load and validate declarative TubeDesigner assembly templates."""
from __future__ import annotations

import copy
import ast
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent / "assembly"
PROCESS_ROOT = Path(__file__).resolve().parent.parent / "mold"
SCHEMA = "icax.assembly-template"
MAX_BYTES = 4 * 1024 * 1024

_finished_spec = importlib.util.spec_from_file_location(
    "icax_finished_product_runtime", Path(__file__).with_name("finished_product_runtime.py"))
finished_products = importlib.util.module_from_spec(_finished_spec)
_finished_spec.loader.exec_module(finished_products)

_section_spec = importlib.util.spec_from_file_location(
    "icax_assembly_section_queries", Path(__file__).with_name("assembly_applicability_geometry.py"))
section_queries = importlib.util.module_from_spec(_section_spec)
_section_spec.loader.exec_module(section_queries)

_process_contract_spec = importlib.util.spec_from_file_location(
    "icax_assembly_process_contract", Path(__file__).with_name("assembly_process_contract.py"))
process_contract = importlib.util.module_from_spec(_process_contract_spec)
_process_contract_spec.loader.exec_module(process_contract)


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
    if item.get("scope", "process") not in ("process", "scene"):
        raise ValueError(f"{key} 的参数范围无效")
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
    key = condition.get("parameter", condition.get("key"))
    if not isinstance(key, str) or key not in values:
        return False
    actual = values[key]
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


def _validate_binding_condition(condition, definitions):
    if not isinstance(condition, dict) or not condition:
        raise ValueError("仅连接分支必须声明有效参数条件")
    operation = condition.get("op", "eq")
    if operation in ("all", "any"):
        children = condition.get("conditions")
        if not isinstance(children, list) or not children:
            raise ValueError("仅连接分支的组合条件不能为空")
        for child in children:
            _validate_binding_condition(child, definitions)
        return
    if operation == "not":
        _validate_binding_condition(condition.get("condition"), definitions)
        return
    if operation not in ("eq", "ne", "in", "notIn"):
        raise ValueError("仅连接分支的参数条件不受支持")
    key = condition.get("parameter", condition.get("key"))
    definition = definitions.get(key) if isinstance(key, str) else None
    if definition is None:
        raise ValueError("仅连接分支引用了不存在的参数")
    expected = condition.get("values") if operation in ("in", "notIn") else [condition.get("value")]
    if not isinstance(expected, list) or not expected:
        raise ValueError("仅连接分支缺少比较值")
    if definition["valueType"] == "choice":
        choices = [option["value"] for option in definition["options"]]
        if any(not any(type(value) is type(choice) and value == choice for choice in choices)
               for value in expected):
            raise ValueError(f"仅连接分支的 {key} 比较值不属于模板选项")
    elif definition["valueType"] == "boolean" and any(not isinstance(value, bool) for value in expected):
        raise ValueError(f"仅连接分支的 {key} 比较值必须是开关")
    elif definition["valueType"] in ("number", "integer") and any(
            isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
            or (definition["valueType"] == "integer" and int(value) != value)
            for value in expected):
        raise ValueError(f"仅连接分支的 {key} 比较值必须是有效数值")


_PRODUCT_FIT_KINDS = {
    "orthogonal-corner-contact": ({"orthogonal-corner"}, {"a": "end", "b": "end", "c": "end"},
                                  {"gap": "mm", "insertion": "mm"}),
    "end-end-miter": ({"L", "straight"}, {"a": "end", "b": "end"},
                       {"gap": "mm", "planeRotation": "°"}),
    "end-near-side-butt": ({"L"}, {"host": "end", "branch": "end"},
                           {"gap": "mm"}),
    "end-near-side-tab-slot": ({"L"}, {"host": "end", "branch": "end"},
                               {"clearance": "mm", "insertion": "mm"}),
    "end-side-single-insert": ({"T"}, {"host": "side", "branch": "end"},
                               {"gap": "mm"}),
    "end-side-through-insert": ({"T"}, {"host": "side", "branch": "end"},
                                {"gap": "mm"}),
    "end-side-saddle": ({"T"}, {"host": "side", "branch": "end"},
                        {"gap": "mm"}),
    "end-side-flat-contact": ({"T"}, {"host": "side", "branch": "end"},
                             {"gap": "mm"}),
    "end-side-tab-slot": ({"T"}, {"host": "side", "branch": "end"},
                          {"clearance": "mm", "insertion": "mm"}),
}


def _validate_product_fit_quantity(source, definitions, unit, label):
    fixed_key = "degrees" if unit == "°" else "millimeters"
    if not isinstance(source, dict) or len(source) != 1:
        raise ValueError(f"产品配合 {label} 必须声明唯一数值来源")
    if "parameter" in source:
        key = source["parameter"]
        definition = definitions.get(key) if isinstance(key, str) else None
        if (not definition or definition.get("valueType") not in ("number", "integer")
                or definition.get("unit") != unit):
            raise ValueError(f"产品配合 {label} 必须引用 {unit} 数值参数")
    elif fixed_key in source:
        value = source[fixed_key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f"产品配合 {label} 的固定数值无效")
        if (unit == "mm" and value < 0) or (unit == "°" and not -180 <= value <= 180):
            raise ValueError(f"产品配合 {label} 的固定数值超出范围")
    else:
        raise ValueError(f"产品配合 {label} 的数值来源无效")


def _validate_product_fit_checks(binding, definitions, roles, process_ids, topologies):
    checks = binding.get("fitChecks", [])
    if not isinstance(checks, list) or len(checks) > 16:
        raise ValueError("产品配合规则必须是最多 16 项的数组")
    if checks and binding["mode"] != "incremental-cut":
        raise ValueError("产品配合规则只能用于真实构件绑定")
    seen_ids = set()
    declared_topologies = {entry["topology"] for entry in topologies}
    for check in checks:
        if not isinstance(check, dict):
            raise ValueError("产品配合规则必须是对象")
        check_id = _text(check.get("id"), "产品配合规则 id")
        if check_id in seen_ids:
            raise ValueError("产品配合规则 id 不能重复")
        seen_ids.add(check_id)
        kind = check.get("kind")
        if not isinstance(kind, str) or kind not in _PRODUCT_FIT_KINDS:
            raise ValueError(f"产品配合 {check_id} 的类型不受支持")
        allowed_topologies, expected_roles, quantities = _PRODUCT_FIT_KINDS[kind]
        topology = check.get("topology")
        if not isinstance(topology, str) or topology not in allowed_topologies or topology not in declared_topologies:
            raise ValueError(f"产品配合 {check_id} 的成品拓扑无效")
        role_map = check.get("roles")
        if (not isinstance(role_map, dict) or set(role_map) != set(expected_roles)
                or any(not isinstance(role, str) or role not in roles
                       for role in role_map.values())
                or len(set(role_map.values())) != len(role_map)
                or any(binding["anchors"][role] != expected_roles[alias]
                       for alias, role in role_map.items())):
            raise ValueError(f"产品配合 {check_id} 的构件角色或锚点无效")
        declared_processes = check.get("processIds")
        if (not isinstance(declared_processes, list)
                or (not declared_processes and kind != "end-side-flat-contact")
                or any(not isinstance(item, str) or item not in process_ids
                       for item in declared_processes)
                or len(set(declared_processes)) != len(declared_processes)):
            raise ValueError(f"产品配合 {check_id} 的单件工序无效")
        if "when" in check:
            _validate_binding_condition(check["when"], definitions)
        face_source = ("anchor.approachFace" if kind.startswith("end-near-side-") else
                       "anchor.face" if kind.startswith("end-side-") else None)
        allowed_keys = {"id", "kind", "topology", "roles", "processIds", "when"} | set(quantities)
        if kind == "orthogonal-corner-contact":
            allowed_keys.update(("abJoint", "cJoint", "receiverRoles"))
            if (check.get("abJoint") not in ("miter", "wrap", "continuous")
                    or check.get("cJoint") not in ("weld", "insert", "tabs")
                    or check.get("receiverRoles") != ([role_map["a"], role_map["b"]]
                        if check["abJoint"] in ("miter", "continuous") else [role_map["a"]])):
                raise ValueError(f"产品配合 {check_id} 的角点承接规则无效")
        if face_source:
            allowed_keys.add("hostFaceSource")
            if check.get("hostFaceSource") != face_source:
                raise ValueError(f"产品配合 {check_id} 的主件接触面来源无效")
        if set(check) != allowed_keys - ({"when"} if "when" not in check else set()):
            raise ValueError(f"产品配合 {check_id} 的声明字段无效")
        for field, unit in quantities.items():
            _validate_product_fit_quantity(check[field], definitions, unit, f"{check_id}.{field}")


def _resolved_product_fit_checks(descriptor, values, active_processes, product_topology):
    binding = descriptor.get("productBinding", {})
    checks = binding.get("fitChecks", [])
    if not checks:
        return []
    active_topologies = _active_product_topologies(binding, values)
    topology = product_topology or (active_topologies[0] if len(active_topologies) == 1 else None)
    selected = [check for check in checks if check["topology"] == topology
                and _condition_matches(check.get("when"), values)]
    if len(selected) != 1:
        raise ValueError("当前产品连接必须恰好命中一条配合规则")
    check = selected[0]
    if set(check["processIds"]) != {process["id"] for process in active_processes}:
        raise ValueError(f"产品配合 {check['id']} 的工序与实际参数分支不一致")
    result = {key: copy.deepcopy(check[key]) for key in
              ("id", "kind", "topology", "roles", "processIds")}
    if "hostFaceSource" in check:
        result["hostFaceSource"] = check["hostFaceSource"]
    if check["kind"] == "orthogonal-corner-contact":
        result.update({key: copy.deepcopy(check[key]) for key in ("abJoint", "cJoint", "receiverRoles")})
    quantity_names = {"gap": "gapMm", "planeRotation": "planeRotationDeg",
                      "clearance": "clearanceMm", "insertion": "insertionMm"}
    for field, unit in _PRODUCT_FIT_KINDS[check["kind"]][2].items():
        source = check[field]
        value = values[source["parameter"]] if "parameter" in source else source[
            "degrees" if unit == "°" else "millimeters"]
        minimum, maximum = (-180, 180) if unit == "°" else (0, math.inf)
        numeric = _bound_number(value, f"产品配合 {check['id']}.{field}", minimum, maximum)
        if field == "insertion" and numeric <= 0 and check["kind"] != "orthogonal-corner-contact":
            raise ValueError(f"产品配合 {check['id']}.{field} 必须大于零")
        result[quantity_names[field]] = numeric
    return [result]


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
        if isinstance(node, ast.Name) and node.id == "pi":
            return math.pi
        if isinstance(node, ast.Name) and node.id in names:
            return names[node.id]
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "sin" and not node.keywords and len(node.args) == 1):
            angle = visit(node.args[0])
            if isinstance(angle, bool) or not isinstance(angle, (int, float)) or not math.isfinite(angle):
                raise ValueError("装配表达式的正弦参数必须是有限数值")
            return math.sin(angle)
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


def _miter_normal_gap_axial_trim(gap, angle, share):
    """Convert a cut-face normal gap to one member's axial retreat."""
    if (any(isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) for value in (gap, angle, share))
            or gap < 0 or not 0 <= angle < 180 or not 0 < share <= 1):
        raise ValueError("斜接法向间隙或分摊比例无效")
    projection = math.cos(math.radians(angle / 2))
    if projection <= 1e-6:
        raise ValueError("斜接角度过大，无法计算端部轴向退切量")
    retreat = gap * share / projection
    if not math.isfinite(retreat):
        raise ValueError("斜接端部轴向退切量无效")
    return retreat


def _process_binding_value(source, values):
    if isinstance(source, dict) and source.get("kind") == "miter-normal-gap-axial-trim":
        return _miter_normal_gap_axial_trim(values[source["gapParameter"]],
                                             values[source["angleParameter"]], source["share"])
    return _resolve(source, values)


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


def _validate_preview_scene(descriptor, roles, process_ids, directory):
    scene = descriptor.get("previewScene")
    if not isinstance(scene, dict):
        raise ValueError("装配模板必须声明 previewScene")
    preferred_view = scene.get("preferredView")
    if preferred_view is not None:
        axes = {"front": "y", "back": "y", "left": "x", "right": "x",
                "top": "z", "bottom": "z"}
        if not isinstance(preferred_view, str) or preferred_view != preferred_view.strip().lower():
            raise ValueError("装配预览 preferredView 无效")
        if preferred_view not in ("iso", "isometric"):
            directions = preferred_view.split("-")
            if (not 1 <= len(directions) <= 3 or any(item not in axes for item in directions)
                    or len({axes[item] for item in directions}) != len(directions)):
                raise ValueError("装配预览 preferredView 无效")
    design_parts = scene.get("designParts")
    if not isinstance(design_parts, list) or not 2 <= len(design_parts) <= 4:
        raise ValueError("装配预览必须声明 2～4 个设计位置零件")
    preview_roles = []
    scene_slots = set()
    for part in design_parts:
        if not isinstance(part, dict):
            raise ValueError("装配预览零件必须是对象")
        role = _text(part.get("role"), "装配预览角色")
        preview_roles.append(role)
        if "sceneSlot" in part:
            slot = part["sceneSlot"]
            if (not isinstance(slot, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,79}", slot)
                    or slot in scene_slots):
                raise ValueError(f"装配预览角色 {role} 的 sceneSlot 无效或重复")
            scene_slots.add(slot)
        reference = part.get("profileRef")
        if not isinstance(reference, dict) or reference.get("scope") not in ("system", "user", "template") or not reference.get("id"):
            raise ValueError(f"装配预览角色 {role} 的管型引用无效")
        if not isinstance(part.get("length"), (int, float, str)):
            raise ValueError(f"装配预览角色 {role} 缺少长度")
        if "stockLengthAddition" in part and not isinstance(part["stockLengthAddition"], (int, float, str)):
            raise ValueError(f"装配预览角色 {role} 的下料补长无效")
        if not isinstance(part.get("pose", {}), dict):
            raise ValueError(f"装配预览角色 {role} 的姿态无效")
    if sorted(preview_roles) != sorted(roles):
        raise ValueError("装配预览必须且只能覆盖全部逻辑零件")
    manufacturing = scene.get("manufacturingParts")
    if not isinstance(manufacturing, list) or not manufacturing:
        raise ValueError("装配预览必须声明下料结果")
    blank_roles = {item["id"]: set(item["participants"])
                   for item in descriptor["manufacturingPlan"]["blankParts"]}
    blank_ids = set(blank_roles)
    processes_by_id = {item["id"]: item for item in descriptor["partProcesses"]}
    declared = set()
    for part in manufacturing:
        if not isinstance(part, dict) or part.get("id") not in blank_ids:
            raise ValueError("装配预览引用了不存在的下料零件")
        declared.add(part["id"])
        if part.get("sourceRole") not in roles:
            raise ValueError(f"下料预览 {part['id']} 缺少有效 sourceRole")
        if part["sourceRole"] not in blank_roles[part["id"]]:
            raise ValueError(f"下料预览 {part['id']} 的 sourceRole 不属于该下料件")
        if "stockLengthAddition" in part and not isinstance(part["stockLengthAddition"], (int, float, str)):
            raise ValueError(f"下料预览 {part['id']} 的下料补长无效")
        process_refs = part.get("processes", [])
        if not isinstance(process_refs, list) or any(item not in process_ids for item in process_refs):
            raise ValueError(f"下料预览 {part['id']} 引用了无效单件工艺")
        for process_id in process_refs:
            if not set(processes_by_id[process_id]["participants"]).issubset(blank_roles[part["id"]]):
                raise ValueError(f"下料预览 {part['id']} 的单件工艺作用到其他下料件")
        if not isinstance(part.get("explodedPose"), dict):
            raise ValueError(f"下料预览 {part['id']} 缺少按装配路径定义的炸开姿态")
    if declared != blank_ids:
        raise ValueError("装配预览必须覆盖全部下料零件")
    compare = scene.get("compareLayout", {})
    if not isinstance(compare, dict):
        raise ValueError("装配对照布局无效")
    _finite_vector(compare.get("designOffset", [0, 0, 100]), 3, "设计件对照偏移")
    _finite_vector(compare.get("manufacturingOffset", [0, 0, -100]), 3, "下料件对照偏移")
    recipes = scene.get("formedPreviews", [])
    if not isinstance(recipes, list) or len(recipes) > 16:
        raise ValueError("成品预览配方必须是最多 16 项的数组")
    for recipe in recipes:
        if not isinstance(recipe, dict) or recipe.get("kind") not in ("continuous-cold-bend", "node-groove-fold"):
            raise ValueError("成品预览配方类型不受支持")
        if recipe.get("blankId") not in blank_ids or recipe.get("roles") != preview_roles:
            raise ValueError("成品预览配方必须匹配下料件及逻辑管段顺序")
        if not isinstance(recipe.get("angle"), (int, float, str)):
            raise ValueError("成品预览配方缺少角度")
        if recipe["kind"] == "continuous-cold-bend":
            for key in ("radius", "factor"):
                if key not in recipe:
                    raise ValueError(f"冷折成品预览配方缺少 {key}")
        elif recipe.get("processId") not in process_ids:
            raise ValueError("节点槽成品预览配方缺少有效单件工艺")
    script = scene.get("formedPreviewScript")
    if script is not None and (script != "assembly.py" or not (directory / "assembly.py").is_file()
                               or (directory / "assembly.py").is_symlink()):
        raise ValueError("成品预览脚本必须是模板目录中的 assembly.py")
    annotations = scene.get("annotations", [])
    if not isinstance(annotations, list) or len(annotations) > 128:
        raise ValueError("三维预览标注必须是最多 128 项的数组")
    seen_annotation_ids = set()
    for annotation in annotations:
        if not isinstance(annotation, dict):
            raise ValueError("三维预览标注必须是对象")
        annotation_id = _text(annotation.get("id"), "三维预览标注 id")
        if annotation_id in seen_annotation_ids:
            raise ValueError("三维预览标注 id 不能重复")
        seen_annotation_ids.add(annotation_id)
        if annotation.get("view") not in ("finished", "blank"):
            raise ValueError("三维预览标注视图无效")
        kind = annotation.get("kind")
        if kind not in ("length-role", "length-blank", "value-note"):
            raise ValueError("三维预览标注类型不受支持")
        if kind == "length-role" and annotation.get("role") not in roles:
            raise ValueError("三维预览标注角色无效")
        if kind == "length-blank" and annotation.get("blankId") not in blank_ids:
            raise ValueError("三维预览标注下料件无效")
        if kind == "value-note" and "value" not in annotation:
            raise ValueError("三维预览数值标注缺少值")


def _validate_template(descriptor, directory):
    descriptor = finished_products.bind_descriptor(descriptor)
    if descriptor.get("schema") != SCHEMA or descriptor.get("schemaVersion") != 1:
        raise ValueError("装配模板格式不受支持")
    if descriptor.get("id") != directory.name or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", directory.name):
        raise ValueError("装配模板 id 必须与目录名一致")
    _text(descriptor.get("version"), "装配模板版本")
    _text(descriptor.get("displayName"), "装配模板名称")
    _text(descriptor.get("category"), "装配模板类别")
    if descriptor.get("layoutShape") not in (
            "l", "t", "cross", "straight", "pi", "parallel", "other", "orthogonal-corner"):
        raise ValueError("装配模板连接形态 layoutShape 无效")
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
            for key, source in bindings.items():
                if not isinstance(source, dict):
                    continue
                gap_name = source.get("gapParameter")
                angle_name = source.get("angleParameter")
                gap_definition = parameter_map.get(gap_name) if isinstance(gap_name, str) else None
                angle_definition = parameter_map.get(angle_name) if isinstance(angle_name, str) else None
                share = source.get("share")
                if (key != "trim" or resource_id != "end-miter"
                        or not isinstance(process.get("previewPlacement"), dict)
                        or process["previewPlacement"].get("target") != "end"
                        or set(source) != {"kind", "gapParameter", "angleParameter", "share"}
                        or source.get("kind") != "miter-normal-gap-axial-trim"
                        or not gap_definition or gap_definition.get("valueType") != "number"
                        or gap_definition.get("unit") != "mm"
                        or not angle_definition or angle_definition.get("valueType") != "number"
                        or angle_definition.get("unit") != "°"
                        or isinstance(share, bool) or not isinstance(share, (int, float))
                        or not math.isfinite(share) or not 0 < share <= 1):
                    raise ValueError(f"单件工艺 {process_id} 的斜接法向间隙退切来源无效")
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
    for field in ("bomRules", "dimensionChecks"):
        if not isinstance(descriptor.get(field, []), list):
            raise ValueError(f"{field} 必须是数组")
    for field, entries in (("assemblyPath", steps),
                           ("bomRules", descriptor.get("bomRules", [])),
                           ("dimensionChecks", descriptor.get("dimensionChecks", []))):
        seen_ids = set()
        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError(f"{field} 的条目必须是对象")
            entry_id = _text(entry.get("id"), f"{field} 条目 id")
            if entry_id in seen_ids:
                raise ValueError(f"{field} 条目 id 不能重复：{entry_id}")
            seen_ids.add(entry_id)
            if field == "assemblyPath":
                _text(entry.get("label"), "装配步骤名称")
                if not isinstance(entry.get("distance", 0), (int, float, str)):
                    raise ValueError(f"装配步骤 {entry_id} 的距离无效")
            elif field == "bomRules":
                _text(entry.get("label"), "辅料名称")
                _text(entry.get("unit"), "辅料单位")
                if not isinstance(entry.get("quantity"), (int, float, str)):
                    raise ValueError(f"辅料 {entry_id} 的数量无效")
            elif entry.get("kind") not in ("coaxial-round-fit", "coaxial-section-fit", "insertion-depth", "minimum", "required-process"):
                raise ValueError(f"尺寸校验 {entry_id} 的类型不受支持")
    outputs = descriptor.get("outputs")
    if not isinstance(outputs, dict) or outputs.get("manufacturingPartCount") != len(blanks):
        raise ValueError("装配输出的下料零件数量与归并方案不一致")
    _validate_preview_scene(descriptor, roles, process_ids, directory)
    binding = descriptor.get("productBinding", {"mode": "unsupported"})
    if not isinstance(binding, dict) or binding.get("mode") not in ("unsupported", "incremental-cut"):
        raise ValueError("产品绑定能力声明无效")
    # Older descriptors remain readable, but are not eligible for a real
    # product connection until they explicitly declare finished topology.
    topologies = binding.get("compatibleProductTopologies", [])
    if not isinstance(topologies, list):
        raise ValueError("产品绑定的成品拓扑声明无效")
    allowed_topologies = {"L", "T", "X", "straight", "parallel", "pi", "orthogonal-corner"}
    for entry in topologies:
        if (not isinstance(entry, dict) or entry.get("topology") not in allowed_topologies
                or set(entry) - {"topology", "when"}):
            raise ValueError("产品绑定的成品拓扑声明无效")
        if "when" in entry:
            _validate_binding_condition(entry["when"], parameter_map)
    if "connectionOnlyWhen" in binding:
        if binding["mode"] != "incremental-cut":
            raise ValueError("仅连接分支需要可绑定的分件装配规则")
        _validate_binding_condition(binding["connectionOnlyWhen"], parameter_map)
    if binding["mode"] == "incremental-cut":
        scripted = binding.get("operationScript") is not None
        if scripted:
            if binding["operationScript"] != "assembly.py" or not (directory / "assembly.py").is_file():
                raise ValueError("实际装配加工脚本必须使用模板目录中的 assembly.py")
            dependencies = binding.get("operationScriptDependencies", [])
            if (not isinstance(dependencies, list) or len(set(dependencies)) != len(dependencies)
                    or any(not isinstance(name, str) or not re.fullmatch(r"_shared/[a-zA-Z0-9_]+\.py", name)
                           or not (ROOT.parent / name).is_file() for name in dependencies)):
                raise ValueError("实际装配加工脚本依赖声明无效")
        if plan["realization"] != "separate":
            raise ValueError("增量加工绑定只能作用于分件下料模板")
        anchors = binding.get("anchors")
        if not isinstance(anchors, dict) or set(anchors) != set(roles) or any(kind not in ("end", "side") for kind in anchors.values()):
            raise ValueError("产品绑定必须声明每个真实构件角色的端部或侧面锚点")
        axis_angle = binding.get("axisAngle")
        if axis_angle is not None:
            if (not isinstance(axis_angle, dict)
                    or set(axis_angle) != {"roles", "directions", "measure", "expected"}
                    or not isinstance(axis_angle["roles"], list)
                    or len(axis_angle["roles"]) != 2
                    or any(not isinstance(role, str) for role in axis_angle["roles"])
                    or len(set(axis_angle["roles"])) != 2
                    or any(role not in roles for role in axis_angle["roles"])
                    or not isinstance(axis_angle["directions"], list)
                    or len(axis_angle["directions"]) != 2
                    or any(not isinstance(direction, str) for direction in axis_angle["directions"])
                    or axis_angle["measure"] not in ("direct", "supplement")
                    or any(direction not in ("startToEnd", "awayFromAnchor")
                           for direction in axis_angle["directions"])):
                raise ValueError("产品绑定的轴线角度规则无效")
            for role, direction in zip(axis_angle["roles"], axis_angle["directions"]):
                if direction == "awayFromAnchor" and anchors[role] != "end":
                    raise ValueError("离开节点的轴线方向必须来自端部锚点")
            expected = axis_angle["expected"]
            if not isinstance(expected, dict) or len(expected) != 1:
                raise ValueError("产品绑定的期望角度来源无效")
            if "parameter" in expected:
                if not isinstance(expected["parameter"], str):
                    raise ValueError("产品绑定的期望角度参数名无效")
                definition = parameter_map.get(expected["parameter"])
                if (not definition or definition.get("valueType") != "number"
                        or definition.get("unit") != "°"):
                    raise ValueError("产品绑定的期望角度必须引用角度参数")
            elif "degrees" in expected:
                degrees = expected["degrees"]
                if (isinstance(degrees, bool) or not isinstance(degrees, (int, float))
                        or not math.isfinite(degrees) or not 0 <= degrees <= 180):
                    raise ValueError("产品绑定的固定期望角度无效")
            else:
                raise ValueError("产品绑定的期望角度来源无效")
        bound_processes = binding.get("processes")
        if not isinstance(bound_processes, dict) or not bound_processes or any(key not in process_ids for key in bound_processes):
            raise ValueError("产品绑定引用了无效单件工艺")
        for process_id, rule in bound_processes.items():
            if not isinstance(rule, dict) or rule.get("target") not in ("end", "side", "part"):
                raise ValueError(f"产品绑定 {process_id} 的工具目标无效")
            process = next(item for item in processes if item["id"] == process_id)
            if len(process["participants"]) != 1:
                raise ValueError(f"产品绑定 {process_id} 必须只加工一个真实构件")
            role = process["participants"][0]
            expected_anchor = "end" if rule["target"] == "end" else "side"
            relative = rule.get("relativePlacement")
            scripted_placement = scripted and relative == {"kind": "template-node-frame"}
            if anchors[role] != expected_anchor:
                if not scripted_placement and (anchors[role] != "end" or expected_anchor != "side"
                        or not isinstance(relative, dict)
                        or set(relative) != {"kind", "faceSource", "inset"}
                        or relative.get("kind") != "end-inset-side"
                        or relative.get("faceSource") != "anchor.approachFace"):
                    raise ValueError(f"产品绑定 {process_id} 缺少明确的端点至侧壁加工定位规则")
                inset = relative.get("inset") if not scripted_placement else None
                if not scripted_placement and (not isinstance(inset, dict)
                        or set(inset) != {"sectionRole", "axis", "factor"}
                        or inset.get("sectionRole") not in roles
                        or inset["sectionRole"] == role
                        or inset.get("axis") not in ("width", "depth")
                        or isinstance(inset.get("factor"), bool)
                        or not isinstance(inset.get("factor"), (int, float))
                        or not math.isfinite(inset["factor"])
                        or not 0 < inset["factor"] <= 1):
                    raise ValueError(f"产品绑定 {process_id} 的端部占位宽度规则无效")
            elif relative is not None and not scripted_placement:
                raise ValueError(f"产品绑定 {process_id} 不需要端点至侧壁加工转换")
            if rule.get("sectionRole") is not None and rule["sectionRole"] not in roles:
                raise ValueError(f"产品绑定 {process_id} 引用了无效截面角色")
            sources = rule.get("parameterSources", {})
            if not isinstance(sources, dict):
                raise ValueError(f"产品绑定 {process_id} 的实际构件参数来源无效")
            for key, source in sources.items():
                if not isinstance(source, dict) or source.get("role") not in roles:
                    raise ValueError(f"产品绑定 {process_id} 的实际构件参数来源无效")
                field = source.get("field")
                if field == "length":
                    if set(source) != {"role", "field"}:
                        raise ValueError(f"产品绑定 {process_id} 的长度来源无效")
                elif field == "anchor.face":
                    mapping = source.get("map")
                    if (anchors[source["role"]] != "side" or set(source) != {"role", "field", "map"}
                            or not isinstance(mapping, dict)
                            or set(mapping) != {"top", "bottom", "left", "right"}
                            or any(isinstance(value, bool) or not isinstance(value, (int, float))
                                   or not math.isfinite(value) for value in mapping.values())):
                        raise ValueError(f"产品绑定 {process_id} 的壁面方位映射无效")
                elif field == "productAnchor.end":
                    mapping = source.get("map")
                    match_face = source.get("matchHostFaceNormal")
                    operation = _load_part_process(process["resource"]["id"])
                    options = {option.get("value") for definition in operation.get("operationParameters", [])
                               if definition.get("key") == key for option in definition.get("options", [])}
                    if (anchors[source["role"]] != "end"
                            or set(source) != ({"role", "field", "map"}
                                               | ({"matchHostFaceNormal"} if match_face is not None else set()))
                            or not isinstance(mapping, dict) or set(mapping) != {"start", "end"}
                            or any(not isinstance(value, str) or value not in options
                                   for value in mapping.values())
                            or (match_face is not None and (match_face is not True
                                or key != "direction" or process["resource"]["id"] != "branch-profile"
                                or rule.get("target") != "part" or rule.get("sectionRole") != source["role"]))):
                        raise ValueError(f"产品绑定 {process_id} 的成品端向映射无效")
                elif field == "anchor.faceByProductEnd":
                    mapping = source.get("map")
                    if (set(source) != {"role", "endRole", "field", "map"}
                            or anchors[source["role"]] != "side"
                            or source["endRole"] not in roles
                            or anchors[source["endRole"]] != "end"
                            or not isinstance(mapping, dict) or set(mapping) != {"start", "end"}
                            or any(not isinstance(faces, dict)
                                   or set(faces) != {"top", "bottom", "left", "right"}
                                   or any(isinstance(value, bool) or not isinstance(value, (int, float))
                                          or not math.isfinite(value) for value in faces.values())
                                   for faces in mapping.values())):
                        raise ValueError(f"产品绑定 {process_id} 的成品端向壁面映射无效")
                elif field == "anchor.stockAllowance":
                    factor = source.get("factor", 1)
                    minimum = source.get("min", 0)
                    add_field = source.get("addField")
                    offset_name = source.get("offsetParameter")
                    offset_factor = source.get("offsetFactor", 0)
                    offset_projection = source.get("offsetProjection")
                    offset_angle = source.get("offsetAngleParameter")
                    angle_definition = parameter_map.get(offset_angle) if isinstance(offset_angle, str) else None
                    if (anchors[source["role"]] != "end"
                            or set(source) - {"role", "field", "factor", "min", "addField", "offsetParameter", "offsetFactor", "offsetProjection", "offsetAngleParameter"}
                            or isinstance(factor, bool) or not isinstance(factor, (int, float))
                            or not math.isfinite(factor) or factor <= 0
                            or isinstance(minimum, bool) or not isinstance(minimum, (int, float))
                            or not math.isfinite(minimum) or minimum < 0
                            or (add_field is not None and add_field != "anchor.contactInset")
                            or ("offsetParameter" in source) != ("offsetFactor" in source)
                            or (offset_name is not None and (offset_name not in parameter_map
                                or parameter_map[offset_name].get("valueType") != "number"))
                            or isinstance(offset_factor, bool)
                            or not isinstance(offset_factor, (int, float))
                            or not math.isfinite(offset_factor)
                            or ("offsetProjection" in source) != ("offsetAngleParameter" in source)
                            or (offset_projection is not None and (key != "trim"
                                or source["role"] != role
                                or rule["target"] != "end"
                                or process["resource"]["id"] != "end-miter"
                                or offset_projection != "miter-face-normal"
                                or not 0 < offset_factor <= 1
                                or offset_name not in parameter_map
                                or parameter_map[offset_name].get("unit") != "mm"
                                or not angle_definition or angle_definition.get("valueType") != "number"
                                or angle_definition.get("unit") != "°"
                                or not axis_angle or axis_angle["measure"] != "supplement"
                                or axis_angle["expected"] != {"parameter": offset_angle}))):
                        raise ValueError(f"产品绑定 {process_id} 的端部库存余量来源无效")
                elif field in ("nodeFrame.miterAngle", "nodeFrame.miterRotation"):
                    component = "angle" if field == "nodeFrame.miterAngle" else "rotation"
                    angle_name = source.get("angleParameter")
                    angle_definition = parameter_map.get(angle_name) if isinstance(angle_name, str) else None
                    if (key != component or set(source) != {"role", "field", "angleParameter"}
                            or source["role"] != role or anchors[role] != "end"
                            or rule["target"] != "end" or process["resource"]["id"] != "end-miter"
                            or not angle_definition or angle_definition.get("valueType") != "number"
                            or angle_definition.get("unit") != "°"
                            or not axis_angle or axis_angle["measure"] != "supplement"
                            or axis_angle["expected"] != {"parameter": angle_name}
                            or axis_angle["directions"] != ["awayFromAnchor", "awayFromAnchor"]):
                        raise ValueError(f"产品绑定 {process_id} 的节点斜接方向来源无效")
                elif field == "constant":
                    if (key != "datum" or set(source) != {"role", "field", "value"}
                            or source["role"] != role or rule["target"] != "end"
                            or process["resource"]["id"] != "end-miter"
                            or source["value"] != "center"):
                        raise ValueError(f"产品绑定 {process_id} 的固定端切基准无效")
                else:
                    raise ValueError(f"产品绑定 {process_id} 的实际构件参数来源无效")
            if any(source.get("field", "").startswith("nodeFrame.miter") for source in sources.values()):
                if (set(key for key, source in sources.items()
                        if source.get("field", "").startswith("nodeFrame.miter")) != {"angle", "rotation"}
                        or sources["angle"]["angleParameter"] != sources["rotation"]["angleParameter"]):
                    raise ValueError(f"产品绑定 {process_id} 必须同时声明节点斜接角度和切面方位")
    _validate_product_fit_checks(binding, parameter_map, roles, process_ids, topologies)
    for check in binding.get("fitChecks", []):
        if check["kind"] != "end-end-miter":
            continue
        ids = check["processIds"]
        if any(process_id not in bound_processes for process_id in ids):
            raise ValueError("斜接间隙引用了没有真实加工绑定的端切工序")
        sources = [bound_processes[process_id]["parameterSources"].get("trim", {})
                   for process_id in ids]
        gap_name = check["gap"].get("parameter")
        if not gap_name:
            if check["gap"].get("millimeters", 0) > 0:
                raise ValueError("斜接固定法向间隙必须由实际两端切削实现")
            continue
        angle_name = axis_angle["expected"].get("parameter") if axis_angle else None
        if (len(sources) != 2 or not angle_name
                or any(source.get("offsetProjection") != "miter-face-normal"
                       or source.get("offsetParameter") != gap_name
                       or source.get("offsetAngleParameter") != angle_name
                       for source in sources)
                or abs(sum(source["offsetFactor"] for source in sources) - 1) > 1e-9):
            raise ValueError("斜接法向间隙须由两端切削完整分摊")
        for process_id, source in zip(ids, sources):
            process = next(item for item in processes if item["id"] == process_id)
            preview = process.get("parameterBindings", {}).get("trim")
            if (not isinstance(preview, dict)
                    or preview.get("kind") != "miter-normal-gap-axial-trim"
                    or preview.get("gapParameter") != gap_name
                    or preview.get("angleParameter") != angle_name
                    or preview.get("share") != source["offsetFactor"]):
                raise ValueError("斜接示例退切与真实产品法向间隙来源不一致")
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
        # A hidden child keeps its draft value for later reactivation.  It must
        # not prevent a different, currently applicable manufacturing branch
        # from being generated.
        if not _condition_matches(definition.get("visibleWhen"), values):
            continue
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


def _validated_preview_values(descriptor, supplied, scene_supplied):
    """Validate separate process and scene parameter scopes."""
    if supplied is None:
        supplied = {}
    if not isinstance(supplied, dict):
        raise ValueError("装配参数必须是对象")
    if scene_supplied is None:
        scene_supplied = {}
    if not isinstance(scene_supplied, dict):
        raise ValueError("成品场景参数必须是对象")
    definitions = {item["key"]: item for item in descriptor.get("parameters", [])}
    if any(key in definitions and definitions[key].get("scope", "process") == "scene"
           for key in supplied):
        raise ValueError("工艺参数不能包含成品场景字段")
    if any(key not in definitions or definitions[key].get("scope", "process") != "scene"
           for key in scene_supplied):
        raise ValueError("成品场景参数包含未声明的场景字段")
    values = _validated_values(descriptor, {**supplied, **scene_supplied})
    process_values = {key: copy.deepcopy(values[key]) for key, definition in definitions.items()
                      if definition.get("scope", "process") != "scene"}
    scene_values = {key: copy.deepcopy(values[key]) for key, definition in definitions.items()
                    if definition.get("scope", "process") == "scene"}
    return values, process_values, scene_values


def _profile_request(part, values):
    length_source = _variant_value(part, "length", values)
    length = _resolve(length_source, values)
    addition = _resolve(part.get("stockLengthAddition", 0), values)
    if (isinstance(addition, bool) or not isinstance(addition, (int, float))
            or not math.isfinite(addition)):
        raise ValueError(f"逻辑零件 {part.get('role')} 的下料补长无效")
    length += addition
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


def _scene_design_parts(scene, values, supplied):
    """Overlay concrete scene stock on example defaults, separate from process values."""
    if supplied is None:
        supplied = {}
    if not isinstance(supplied, dict):
        raise ValueError("成品场景零件参数必须是对象")
    defaults = scene["designParts"]
    roles = {part["role"] for part in defaults}
    if any(role not in roles for role in supplied):
        raise ValueError("成品场景包含未知逻辑零件角色")
    result = []
    for default in defaults:
        role = default["role"]
        override = supplied.get(role, {})
        if not isinstance(override, dict):
            raise ValueError(f"成品场景角色 {role} 的参数必须是对象")
        if set(override) - {"profileRef", "parameters", "length"}:
            raise ValueError(f"成品场景角色 {role} 包含不允许的字段")
        source = copy.deepcopy(default)
        profile_changed = False
        if "profileRef" in override:
            reference = override["profileRef"]
            if (not isinstance(reference, dict) or set(reference) != {"scope", "id"}
                    or reference["scope"] not in ("system", "user", "template")
                    or not isinstance(reference["id"], str) or not reference["id"].strip()):
                raise ValueError(f"成品场景角色 {role} 的管型引用无效")
            profile_changed = reference != default["profileRef"]
            source["profileRef"] = copy.deepcopy(reference)
        if profile_changed and "parameters" not in override:
            raise ValueError(f"成品场景角色 {role} 更换管型时必须提供管型参数")
        if "parameters" in override:
            if not isinstance(override["parameters"], dict):
                raise ValueError(f"成品场景角色 {role} 的管型参数必须是对象")
            if profile_changed:
                source["parameters"] = copy.deepcopy(override["parameters"])
            else:
                source["parameters"] = {
                    **copy.deepcopy(default.get("parameters", {})),
                    **copy.deepcopy(override["parameters"]),
                }
        if "length" in override:
            length = override["length"]
            if (isinstance(length, bool) or not isinstance(length, (int, float))
                    or not math.isfinite(length) or not 1 <= length <= 100000):
                raise ValueError(f"成品场景角色 {role} 的长度无效")
            source["length"] = float(length)
        result.append(source)
    return result


def _validate_integrated_stock_compatibility(descriptor, scene_parts):
    if descriptor["manufacturingPlan"]["realization"] != "integrated":
        return
    for blank in descriptor["manufacturingPlan"]["blankParts"]:
        roles = blank["participants"]
        if len(roles) < 2:
            continue
        reference = scene_parts[roles[0]]
        if any(not section_queries.sections_match(scene_parts[role], reference)
               for role in roles[1:]):
            raise ValueError(
                f"一体下料件 {blank['id']} 的逻辑零件管型及截面参数必须一致，无法归并不同截面")


def _design_expression_values(values, design_parts):
    """Expose resolved design dimensions to formulas without changing user parameters."""
    result = dict(values)
    for part in design_parts:
        role = part["role"]
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", role):
            continue
        scalars = {"length": part["request"]["length"],
                   **part["request"].get("parameters", {})}
        profile_ref = part["request"].get("profileRef", {})
        if profile_ref.get("scope") == "system" and profile_ref.get("id") == "round":
            # Circular height is a formula dimension, not a profile input field.
            scalars["depth"] = scalars["width"]
        for field, value in scalars.items():
            if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", field):
                continue
            alias = f"{role}_{field}"
            if alias in result:
                raise ValueError(f"装配派生量与参数重名：{alias}")
            if isinstance(value, (int, float, str, bool)):
                result[alias] = value
    return result


def _validated_formed_mesh(mesh):
    if not isinstance(mesh, dict) or mesh.get("kind", 1) != 1:
        raise ValueError("assembly.py 必须返回三角网格对象")
    positions, indices = mesh.get("positions"), mesh.get("indices")
    if (not isinstance(positions, (list, tuple)) or not positions or len(positions) % 3
            or len(positions) > 300000 or not isinstance(indices, (list, tuple))
            or not indices or len(indices) % 3 or len(indices) > 900000):
        raise ValueError("assembly.py 返回的三角网格尺寸无效")
    if any(isinstance(value, bool) or not isinstance(value, (int, float))
           or not math.isfinite(value) for value in positions):
        raise ValueError("assembly.py 返回了非有限顶点")
    vertex_count = len(positions) // 3
    if any(isinstance(value, bool) or not isinstance(value, int)
           or value < 0 or value >= vertex_count for value in indices):
        raise ValueError("assembly.py 返回了无效三角形索引")
    metadata = mesh.get("metadata", {})
    if not isinstance(metadata, dict):
        raise ValueError("assembly.py 的网格元数据必须是对象")
    try:
        metadata_json = json.dumps(metadata, ensure_ascii=False, allow_nan=False)
    except (TypeError, ValueError) as error:
        raise ValueError("assembly.py 的网格元数据必须可序列化") from error
    if len(metadata_json.encode("utf-8")) > MAX_BYTES:
        raise ValueError("assembly.py 的网格元数据超过 4 MB")
    return {"kind": 1, "positions": list(positions), "indices": list(indices),
            "metadata": json.loads(metadata_json)}


def _load_assembly_script(template_id):
    path = ROOT / template_id / "assembly.py"
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_BYTES:
        raise ValueError("assembly.py 不存在或超过 4 MB")
    module_name = "icax_assembly_" + hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:16]
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ValueError("无法加载 assembly.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _same_input(left, right):
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(_same_input(left[key], right[key]) for key in left)
    if isinstance(left, list):
        return len(left) == len(right) and all(_same_input(a, b) for a, b in zip(left, right))
    return left == right


def _load_applicability_script(template_id):
    path = ROOT / template_id / "applicability.py"
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_BYTES:
        raise ValueError("该模板未提供有效的成品适用性函数")
    name = "icax_assembly_applicability_" + hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:16]
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError("无法加载模板的成品适用性函数")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as error:
        detail = str(error).strip() or type(error).__name__
        raise ValueError(f"无法加载模板的成品适用性函数：{detail}") from error
    checker = getattr(module, "check_applicability", None)
    if not callable(checker):
        raise ValueError("模板必须定义 check_applicability(finished_product, parameters)")
    return checker


def _require_process_applicability(descriptor, process_input, process_values):
    """Check only the immutable local measurements required by this function."""
    process_input = process_contract.normalize(descriptor, process_input)
    checker = _load_applicability_script(descriptor["id"])
    product_input, parameters_input = copy.deepcopy(process_input), copy.deepcopy(process_values)
    try:
        result = checker(product_input, parameters_input)
    except ValueError:
        raise
    except Exception as error:
        raise ValueError(f"模板成品适用性函数执行失败：{error}") from error
    if not _same_input(product_input, process_input) or not _same_input(parameters_input, process_values):
        raise ValueError("模板适用性函数不得改写成品或工艺输入")
    if (not isinstance(result, dict) or set(result) != {"applicable", "reason"}
            or type(result["applicable"]) is not bool or not isinstance(result["reason"], str)
            or (not result["applicable"] and not result["reason"].strip())
            or (result["applicable"] and result["reason"])):
        raise ValueError("模板适用性函数必须返回 applicable 布尔值和明确的 reason")
    if not result["applicable"]:
        raise ValueError(result["reason"])


def check_applicability(template_id, finished_product=None, supplied_values=None, process_input=None):
    """Public preflight; every process supplies its own product-input checker.

    A positive result covers the template's input contract. Native machining,
    fit and interference checks remain part of manufacturing verification.
    """
    result = {"schema": "icax.assembly-applicability", "schemaVersion": 1,
              "templateId": template_id, "applicable": False, "reason": ""}
    try:
        descriptor = _template_by_id(template_id)
        if process_input is None:
            product = finished_products.validate(finished_product)
            process_input = process_contract.from_product(descriptor, product, finished_product_plan(product),
                                                          example_selection=True)
        process_input = process_contract.normalize(descriptor, process_input)
        scene_values = {key: value for key, value in process_input["geometry"].items()
                        if key in {p["key"] for p in descriptor["parameters"] if p.get("scope") == "scene"}}
        if set(process_input["parts"]) == {"stock"} and "angle" in scene_values:
            scene_values["angle"] = abs(scene_values["angle"])
        _, process_values, _ = _validated_preview_values(descriptor, supplied_values, scene_values)
        _require_process_applicability(descriptor, process_input, process_values)
        result["applicable"] = True
    except (ValueError, OSError, KeyError, TypeError) as error:
        result["reason"] = str(error).strip() or "模板成品适用性检查失败"
    return result


def _load_example_product_script(template_id):
    path = ROOT / template_id / "example.py"
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_BYTES:
        raise ValueError("该模板未提供有效的示例成品函数")
    name = "icax_assembly_example_" + hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:16]
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError("无法加载模板的示例成品函数")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    provider = getattr(module, "get_example_product", None)
    if not callable(provider):
        raise ValueError("模板必须定义 get_example_product(parameters)")
    return provider


def get_example_product(template_id, supplied_values=None):
    """Ask the selected template for an applicable, process-free product input.

    This creates product data only. Product geometry and manufacturing plans
    are separate operations that the scene requests when it needs them.
    """
    descriptor = _template_by_id(template_id)
    process_descriptor = {**descriptor, "parameters": [p for p in descriptor["parameters"]
                          if p.get("scope", "process") != "scene"]}
    process_values = _validated_values(process_descriptor, supplied_values)
    provider = _load_example_product_script(template_id)
    parameters_input = copy.deepcopy(process_values)
    try:
        product = provider(parameters_input)
    except ValueError:
        raise
    except Exception as error:
        raise ValueError(f"模板示例成品函数执行失败：{error}") from error
    if not _same_input(parameters_input, process_values):
        raise ValueError("模板示例成品函数不得改写工艺输入")
    product = finished_products.validate(product)
    local_input = process_contract.from_product(descriptor, product, finished_product_plan(product),
                                                example_selection=True)
    _require_process_applicability(descriptor, local_input, process_values)
    return {"schema": "icax.assembly-example-product", "schemaVersion": 1,
            "templateId": template_id, "finishedProduct": product}


def _validate_script_plan(plan, descriptor, original_values, scene_parts, scene_values, finished_product=None,
                          process_input=None):
    """Keep a per-template plan hook inside the host's declared part contract."""
    required = {"schema", "schemaVersion", "templateId", "templateVersion", "parameters",
                "sceneParameters", "sceneParts",
                 "designParts", "manufacturingParts", "formedPreviewRecipe", "previewAnnotations",
                "resolvedWorkflow"}
    if finished_product is not None:
        required.add("finishedProduct")
    if process_input is not None:
        required.add("processInput")
    if isinstance(plan, dict) and "contactRequirements" in plan:
        required.add("contactRequirements")
        # Contact obligations are output checks, never changes to the supplied
        # product/role geometry. Use the same strict schema as function replay.
        _process_function_module()._contact_requirements(
            globals(), plan["contactRequirements"],
            {part["role"]: part for part in plan.get("designParts", [])})
    if not isinstance(plan, dict) or set(plan) != required:
        raise ValueError("assembly.py 返回的计划字段无效")
    if (plan["schema"] != "icax.assembly-preview-plan" or plan["schemaVersion"] != 1
            or plan["templateId"] != descriptor["id"] or plan["templateVersion"] != descriptor["version"]):
        raise ValueError("assembly.py 不得更改计划身份")
    if not _same_input(plan["parameters"], original_values):
        raise ValueError("assembly.py 不得更改输入参数")
    if not _same_input(plan["sceneParameters"], scene_values):
        raise ValueError("assembly.py 不得更改成品场景参数")
    if not _same_input(plan["sceneParts"], scene_parts):
        raise ValueError("assembly.py 不得更改成品场景参数")
    if finished_product is not None and not _same_input(plan["finishedProduct"], finished_product):
        raise ValueError("assembly.py 不得更改成品输入")
    if process_input is not None and not _same_input(plan["processInput"], process_input):
        raise ValueError("assembly.py 不得更改工艺必要输入")
    scene = descriptor["previewScene"]
    roles = [part["role"] for part in scene["designParts"]]
    blank_by_id = {part["id"]: part for part in descriptor["manufacturingPlan"]["blankParts"]}
    scene_blank_by_id = {part["id"]: part for part in scene["manufacturingParts"]}
    process_by_id = {part["id"]: part for part in descriptor["partProcesses"]}
    if not isinstance(plan["designParts"], list) or len(plan["designParts"]) != len(roles):
        raise ValueError("assembly.py 不得更改设计件数量")
    if not isinstance(plan["manufacturingParts"], list) or len(plan["manufacturingParts"]) != len(blank_by_id):
        raise ValueError("assembly.py 不得更改下料件数量")

    def check_matrix(matrix):
        _finite_vector(matrix, 16, "assembly.py 零件姿态")

    def check_request(request, allowed_tools):
        if not isinstance(request, dict) or not isinstance(request.get("profileRef"), dict):
            raise ValueError("assembly.py 的管型请求无效")
        reference = request["profileRef"]
        if reference.get("scope") not in ("system", "user", "template") or not reference.get("id"):
            raise ValueError("assembly.py 的管型引用无效")
        if not isinstance(request.get("parameters"), dict):
            raise ValueError("assembly.py 的管型参数无效")
        _bound_number(request.get("length"), "assembly.py 下料长度", 1, 100000)
        if not isinstance(request.get("features"), list) or not isinstance(request.get("ends"), dict):
            raise ValueError("assembly.py 的单件加工请求无效")
        if set(request["ends"]) != {"start", "end"}:
            raise ValueError("assembly.py 的端部加工请求无效")

        def inspect(value):
            if isinstance(value, dict):
                if "toolRef" in value:
                    tool = value["toolRef"]
                    if not isinstance(tool, dict) or tool.get("id") not in allowed_tools:
                        raise ValueError("assembly.py 引用了未声明的单件工艺")
                for nested in value.values():
                    inspect(nested)
            elif isinstance(value, list):
                for nested in value:
                    inspect(nested)

        inspect(request["features"])
        inspect(request["ends"])

    found_roles = []
    for part in plan["designParts"]:
        if not isinstance(part, dict) or part.get("id") != f"design-{part.get('role')}":
            raise ValueError("assembly.py 的设计件身份无效")
        found_roles.append(part.get("role"))
        check_request(part.get("request"), set())
        check_matrix(part.get("matrix"))
        check_matrix(part.get("compareMatrix"))
    if found_roles != roles:
        raise ValueError("assembly.py 不得更改逻辑角色")

    found_blanks = set()
    for part in plan["manufacturingParts"]:
        if not isinstance(part, dict) or part.get("blankId") not in blank_by_id:
            raise ValueError("assembly.py 的下料件身份无效")
        blank_id = part["blankId"]
        if blank_id in found_blanks or part.get("id") != f"manufacturing-{blank_id}":
            raise ValueError("assembly.py 的下料件身份无效或重复")
        found_blanks.add(blank_id)
        declared = blank_by_id[blank_id]
        if (part.get("sourceRole") != scene_blank_by_id[blank_id]["sourceRole"]
                or part.get("participantRoles") != declared["participants"]):
            raise ValueError("assembly.py 不得更改下料件角色映射")
        allowed_tools = set()
        for process_id in scene_blank_by_id[blank_id].get("processes", []):
            process = process_by_id[process_id]
            if _condition_matches(process.get("appliesWhen"), original_values):
                allowed_tools.add(_selected_process_descriptor(process, original_values)["id"])
        check_request(part.get("request"), allowed_tools)
        for key in ("matrix", "explodedMatrix", "compareMatrix"):
            check_matrix(part.get(key))
    if found_blanks != set(blank_by_id):
        raise ValueError("assembly.py 未覆盖全部下料件")

    workflow = plan["resolvedWorkflow"]
    if not isinstance(workflow, dict) or workflow.get("realization") != descriptor["manufacturingPlan"]["realization"]:
        raise ValueError("assembly.py 不得更改下料实现方式")
    if (not isinstance(workflow.get("blankParts"), list)
            or any(not isinstance(part, dict) for part in workflow["blankParts"])
            or {part.get("id") for part in workflow["blankParts"]} != set(blank_by_id)
            or not isinstance(workflow.get("partOperations"), list)
            or not isinstance(workflow.get("assemblySteps"), list)
            or not isinstance(workflow.get("bom"), list)
            or not isinstance(workflow.get("checks"), list)):
        raise ValueError("assembly.py 的制造工作流无效")
    for blank in workflow["blankParts"]:
        declared = blank_by_id[blank["id"]]
        if (blank.get("participantRoles") != declared["participants"]
                or blank.get("sourceRole") != scene_blank_by_id[blank["id"]]["sourceRole"]):
            raise ValueError("assembly.py 不得更改工作流下料件映射")
    for operation in workflow["partOperations"]:
        if not isinstance(operation, dict) or operation.get("processId") not in process_by_id:
            raise ValueError("assembly.py 引用了未声明的装配单件工艺")
        process = process_by_id[operation["processId"]]
        blank_id = operation.get("blankId")
        reference = operation.get("resourceRef")
        if (blank_id not in blank_by_id or operation["processId"] not in scene_blank_by_id[blank_id].get("processes", [])
                or not _condition_matches(process.get("appliesWhen"), original_values)
                or not isinstance(reference, dict) or reference.get("id")
                != _selected_process_descriptor(process, original_values)["id"]):
            raise ValueError("assembly.py 的单件工艺与模板声明不一致")
    if not isinstance(plan["previewAnnotations"], list):
        raise ValueError("assembly.py 的预览标注无效")
    if plan["formedPreviewRecipe"] is not None and not isinstance(plan["formedPreviewRecipe"], dict):
        raise ValueError("assembly.py 的成品预览配方无效")


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
    result.update({key: _process_binding_value(value, values) for key, value in bindings.items()})
    # Keep a hidden draft for the editor, but do not let it change the active
    # tool recipe or prevent a different branch from being generated.
    for definition in definitions:
        if not isinstance(definition, dict) or definition["key"] in bindings:
            continue
        if not _condition_matches(definition.get("visibleWhen"), result):
            result[definition["key"]] = copy.deepcopy(definition.get("defaultValue"))
    for definition in definitions:
        if not isinstance(definition, dict) or not _condition_matches(definition.get("visibleWhen"), result):
            continue
        key, value = definition["key"], result[definition["key"]]
        kind = definition.get("valueType")
        if kind in ("number", "integer"):
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"单件工艺 {process['id']} 的 {key} 必须是有限数值")
            if kind == "integer" and int(value) != value:
                raise ValueError(f"单件工艺 {process['id']} 的 {key} 必须是整数")
            if value < definition.get("min", -math.inf) or value > definition.get("max", math.inf):
                raise ValueError(f"单件工艺 {process['id']} 的 {key} 超出允许范围")
        elif kind == "boolean" and not isinstance(value, bool):
            raise ValueError(f"单件工艺 {process['id']} 的 {key} 必须是开关")
        elif kind == "choice" and value not in [item.get("value") for item in definition.get("options", [])]:
            raise ValueError(f"单件工艺 {process['id']} 的 {key} 不属于允许选项")
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


def _choice_label(definitions, key, value):
    definition = next((item for item in definitions if item["key"] == key), None)
    for option in definition.get("options", []) if definition else []:
        if option.get("value") == value:
            return option.get("label", str(value))
    return str(value)


def _resolved_steps(descriptor, values):
    steps = []
    definitions = descriptor.get("parameters", [])
    for source in descriptor["assemblyPath"]:
        if not _condition_matches(source.get("when"), values):
            continue
        label = _variant_value(source, "label", values)
        kind_source = _variant_value(source, "kind", values, "")
        kind = _resolve(kind_source, values)
        choice_key = kind_source[1:] if isinstance(kind_source, str) and re.fullmatch(r"\$[a-zA-Z][a-zA-Z0-9]*", kind_source) else ""
        distance_source = _variant_value(source, "distance", values, 0)
        distance = _resolve(distance_source, values)
        if isinstance(distance, bool) or not isinstance(distance, (int, float)) or not math.isfinite(distance):
            raise ValueError(f"装配步骤 {source['id']} 的距离必须是有限数值")
        if not isinstance(label, str) or not label.strip() or not isinstance(kind, str) or not kind.strip():
            raise ValueError(f"装配步骤 {source['id']} 未解析成明确动作")
        unit = source.get("unit", "")
        if not unit:
            referenced = _parameter_references(distance_source)
            units = {item.get("unit", "") for item in definitions if item["key"] in referenced}
            if referenced and len(units) == 1:
                unit = units.pop()
        steps.append({
            "id": source["id"], "label": label, "kind": kind,
            "kindLabel": _choice_label(definitions, choice_key, kind) if choice_key else kind,
            "direction": _resolve(source.get("direction", ""), values),
            "distance": float(distance), "unit": unit,
        })
    return steps


def _resolved_bom(descriptor, values):
    result = []
    for source in descriptor.get("bomRules", []):
        if not _condition_matches(source.get("when"), values):
            continue
        label = _variant_value(source, "label", values)
        quantity = _resolve(_variant_value(source, "quantity", values), values)
        if not isinstance(label, str) or not label.strip():
            raise ValueError(f"辅料 {source['id']} 没有明确名称")
        if isinstance(quantity, bool) or not isinstance(quantity, (int, float)) or not math.isfinite(quantity) or quantity <= 0:
            raise ValueError(f"辅料 {source['id']} 的数量必须大于零")
        result.append({
            "id": source["id"], "label": label, "quantity": quantity,
            "unit": source["unit"],
            "specification": _resolve(source.get("specification", {}), values),
            "specificationStatus": source.get("specificationStatus", "requires-selection"),
        })
    return result


def _section_number(parameters, key, default=None):
    value = parameters.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    return float(value)


def _section_corner_radii(parameters, independent_key, radius_key, prefix):
    if parameters.get(independent_key, False) is True:
        radii = [_section_number(parameters, f"{prefix}{index}") for index in range(1, 5)]
    else:
        radius = _section_number(parameters, radius_key, 0)
        radii = [radius] * 4
    return radii if all(radius is not None and radius >= 0 for radius in radii) else None


def _coaxial_section_fit_check(source, inner_part, outer_part, values):
    """Check supported section envelopes without hiding an incompatible preview.

    A rectangular fit is deliberately conservative at rounded corners. A
    custom section, eccentric bore or rotated rectangular section needs an
    actual section-intersection check and must not be reported as fitting.
    """
    inner, outer = inner_part["request"], outer_part["request"]
    inner_ref, outer_ref = inner["profileRef"], outer["profileRef"]
    inner_params, outer_params = inner["parameters"], outer["parameters"]
    expected = _resolve(source["expectedClearance"], values)
    tolerance = source.get("tolerance", 1e-6)
    inner_matrix, outer_matrix = inner_part["matrix"], outer_part["matrix"]
    inner_axis = [inner_matrix[index] for index in (0, 4, 8)]
    outer_axis = [outer_matrix[index] for index in (0, 4, 8)]
    offset = [inner_matrix[index] - outer_matrix[index] for index in (3, 7, 11)]
    radial = math.sqrt(sum(value * value for value in _cross(offset, outer_axis)))
    alignment = _dot(inner_axis, outer_axis)
    result = {
        "id": source["id"], "kind": source["kind"], "status": "fail", "blocking": True,
        "actual": None, "expected": expected, "axisOffset": radial,
        "axisAlignment": alignment, "unit": "mm",
    }

    def finish(actual, compatible, detail):
        result["actual"] = actual
        result["status"] = "pass" if compatible and radial <= tolerance and alignment >= 1 - tolerance else "fail"
        result["blocking"] = result["status"] != "pass"
        result["detail"] = (f"{detail}；轴线偏移 {radial:.4f} mm，方向余弦 {alignment:.6f}")
        return result

    inner_profile = inner_ref.get("id") if inner_ref.get("scope") == "system" else None
    outer_profile = outer_ref.get("id") if outer_ref.get("scope") == "system" else None
    if inner_profile != outer_profile or inner_profile not in ("round", "rect"):
        return finish(None, False, "当前管型组合没有经过同轴截面配合校验")
    if any(parameters.get("materialBoundary") for parameters in (inner_params, outer_params)):
        return finish(None, False, "自定义材料轮廓需要实际截面配合校验")

    inner_width = _section_number(inner_params, "width")
    outer_width = _section_number(outer_params, "width")
    inner_wall = _section_number(inner_params, "wallThickness")
    outer_wall = _section_number(outer_params, "wallThickness")
    if (any(value is None for value in (inner_width, outer_width, inner_wall, outer_wall))
            or inner_width <= 0 or outer_width <= 0 or inner_wall <= 0 or outer_wall <= 0
            or inner_width <= 2 * inner_wall or outer_width <= 2 * outer_wall):
        return finish(None, False, "截面宽度、外径或壁厚无效")
    bore_offset_x = _section_number(outer_params, "innerOffsetX", 0)
    bore_offset_y = _section_number(outer_params, "innerOffsetY", 0)
    if bore_offset_x is None or bore_offset_y is None:
        return finish(None, False, "承接件内孔偏心量无效")
    if math.hypot(bore_offset_x, bore_offset_y) > tolerance:
        return finish(None, False, "承接件内孔偏心，尚未验证同轴配合")

    if inner_profile == "round":
        actual = (outer_width - 2 * outer_wall - inner_width) / 2
        compatible = actual >= 0 and abs(actual - expected) <= tolerance
        return finish(actual, compatible,
                      f"实际单侧圆管间隙 {actual:.4f} mm，目标 {expected:.4f} mm")

    inner_depth = _section_number(inner_params, "depth")
    outer_depth = _section_number(outer_params, "depth")
    if (inner_depth is None or outer_depth is None or inner_depth <= 2 * inner_wall
            or outer_depth <= 2 * outer_wall):
        return finish(None, False, "方矩管高度或壁厚无效")
    inner_up = [inner_matrix[index] for index in (1, 5, 9)]
    outer_up = [outer_matrix[index] for index in (1, 5, 9)]
    section_alignment = abs(_dot(inner_up, outer_up))
    result["sectionAlignment"] = section_alignment
    if section_alignment < 1 - tolerance:
        return finish(None, False, "方矩管截面未同向对齐，当前未验证旋转配合")
    clearance_width = (outer_width - 2 * outer_wall - inner_width) / 2
    clearance_depth = (outer_depth - 2 * outer_wall - inner_depth) / 2
    actual = min(clearance_width, clearance_depth)
    insert_radii = _section_corner_radii(inner_params, "useOuterRadii", "cornerRadius", "outerRadius")
    bore_radii = _section_corner_radii(outer_params, "useInnerRadii", "innerRadius", "innerRadius")
    if insert_radii is None or bore_radii is None:
        return finish(actual, False, "方矩管角部半径无效，无法校验配合")
    # With at least as much rounding on the inserted exterior, the side
    # clearances are conservative lower bounds for the complete perimeter.
    # Less rounding may still fit, but needs an actual contour-distance check.
    corner_margin = min(insert_radii) - max(bore_radii)
    result["widthClearance"] = clearance_width
    result["depthClearance"] = clearance_depth
    result["cornerMargin"] = corner_margin
    compatible = (clearance_width >= 0 and clearance_depth >= 0
                  and abs(actual - expected) <= tolerance and corner_margin >= -tolerance)
    return finish(actual, compatible,
                  f"方矩管宽向间隙 {clearance_width:.4f} mm、高向间隙 {clearance_depth:.4f} mm，"
                  f"目标最小单侧间隙 {expected:.4f} mm，保守圆角余量 {corner_margin:.4f} mm")


def _resolved_dimension_checks(descriptor, values, design_parts, manufacturing_parts, operations):
    design_by_role = {item["role"]: item for item in design_parts}
    manufacturing_by_id = {item["blankId"]: item for item in manufacturing_parts}
    checks = []
    for source in descriptor.get("dimensionChecks", []):
        if not _condition_matches(source.get("when"), values):
            continue
        kind, check_id = source["kind"], source["id"]
        if kind == "required-process":
            checks.append({"id": check_id, "kind": kind, "status": "requires-definition",
                           "blocking": True, "detail": _text(source.get("detail"), "待补工艺说明")})
            continue
        if kind in ("coaxial-round-fit", "coaxial-section-fit"):
            checks.append(_coaxial_section_fit_check(
                source, design_by_role[source["innerRole"]], design_by_role[source["outerRole"]], values))
        elif kind == "insertion-depth":
            inner_part = design_by_role[source["innerRole"]]
            outer_part = design_by_role[source["outerRole"]]
            expected = _resolve(source["depth"], values)
            inner_length = inner_part["request"]["length"]
            outer_length = outer_part["request"]["length"]
            maximum = min(inner_length, outer_length)
            inner_matrix, outer_matrix = inner_part["matrix"], outer_part["matrix"]
            inner_axis = [inner_matrix[index] for index in (0, 4, 8)]
            outer_axis = [outer_matrix[index] for index in (0, 4, 8)]
            offset = [inner_matrix[index] - outer_matrix[index] for index in (3, 7, 11)]
            start = _dot(offset, outer_axis)
            actual = max(0.0, min(start + inner_length, outer_length) - max(start, 0.0))
            radial = math.sqrt(sum(value * value for value in _cross(offset, outer_axis)))
            alignment = _dot(inner_axis, outer_axis)
            tolerance = source.get("tolerance", 1e-6)
            passed = (0 < expected <= maximum and abs(actual - expected) <= tolerance
                      and radial <= tolerance and alignment >= 1 - tolerance)
            detail = f"实际重叠 {actual:.4f} mm，目标插入深度 {expected:.4f} mm，允许上限 {maximum:.4f} mm"
            checks.append({"id": check_id, "kind": kind, "status": "pass" if passed else "fail",
                           "blocking": not passed, "actual": actual, "expected": expected,
                           "maximum": maximum, "axisOffset": radial,
                           "axisAlignment": alignment,
                           "unit": "mm", "detail": detail})
        else:
            actual = _resolve(source["value"], values)
            minimum = _resolve(source["minimum"], values)
            if (isinstance(actual, bool) or not isinstance(actual, (int, float))
                    or not math.isfinite(actual) or isinstance(minimum, bool)
                    or not isinstance(minimum, (int, float)) or not math.isfinite(minimum)):
                raise ValueError(f"尺寸校验 {check_id} 的数值无效")
            passed = actual >= minimum
            detail = f"{source.get('label', check_id)} {actual:.4f}，最小允许 {minimum:.4f}"
            checks.append({"id": check_id, "kind": kind, "status": "pass" if passed else "fail",
                           "blocking": not passed, "actual": actual, "minimum": minimum,
                           "unit": source.get("unit", ""), "detail": detail})
        if checks[-1]["status"] == "fail" and source.get("fatalOnFail", False):
            raise ValueError(f"{descriptor['displayName']}：{checks[-1]['detail']}")
    for operation in operations:
        placement = operation["placement"]
        if placement.get("target") not in ("side", "part") or placement.get("reference", "center") != "center":
            continue
        count = placement.get("arrayCount", 1)
        pitch = placement.get("arrayPitch", 0)
        station = placement.get("station", 0)
        length = manufacturing_by_id[operation["blankId"]]["request"]["length"]
        if (isinstance(count, bool) or not isinstance(count, int) or count < 1
                or isinstance(station, bool) or not isinstance(station, (int, float))
                or isinstance(pitch, bool) or not isinstance(pitch, (int, float))
                or not math.isfinite(station) or not math.isfinite(pitch)):
            raise ValueError(f"单件工艺 {operation['processId']} 的阵列尺寸无效")
        first, last = station, station + (count - 1) * pitch
        passed = min(first, last) >= -length / 2 and max(first, last) <= length / 2
        detail = f"加工中心范围 {min(first, last):.4f}～{max(first, last):.4f} mm，下料件半长 {length / 2:.4f} mm"
        checks.append({"id": f"operation-span:{operation['id']}", "kind": "operation-span",
                       "status": "pass" if passed else "fail", "blocking": not passed,
                       "minimum": min(first, last),
                       "maximum": max(first, last), "partLength": length, "unit": "mm", "detail": detail})
    return checks


def _parameter_references(value):
    if isinstance(value, str):
        return set(re.findall(r"\$([A-Za-z][A-Za-z0-9_]*)", value))
    if isinstance(value, list):
        return set().union(*(_parameter_references(item) for item in value)) if value else set()
    if isinstance(value, dict):
        result = {value["parameter"]} if isinstance(value.get("parameter"), str) else set()
        if value.get("kind") == "miter-normal-gap-axial-trim":
            result.update((value["gapParameter"], value["angleParameter"]))
        for nested in value.values():
            result.update(_parameter_references(nested))
        return result
    return set()


def _variant_effect_references(owner, key, values):
    refs = _parameter_references(_variant_value(owner, key, values))
    for variant in owner.get(f"{key}Variants", []):
        refs.update(_parameter_references(variant.get("when")))
    return refs


def _parameter_effects(descriptor, values):
    scene = descriptor["previewScene"]
    geometry_refs = set()
    placement_refs = set()
    workflow_refs = set()
    for part in scene["designParts"]:
        for key in ("parameters", "length", "stockLengthAddition"):
            geometry_refs.update(_variant_effect_references(part, key, values))
        placement_refs.update(_variant_effect_references(part, "pose", values))
    for part in scene["manufacturingParts"]:
        for key in ("length", "stockLengthAddition"):
            geometry_refs.update(_variant_effect_references(part, key, values))
        placement_refs.update(_variant_effect_references(part, "explodedPose", values))
    for process in descriptor.get("partProcesses", []):
        geometry_refs.update(_parameter_references(process.get("appliesWhen")))
        if not _condition_matches(process.get("appliesWhen"), values):
            continue
        geometry_refs.update(_parameter_references(process.get("parameterBindings")))
        if isinstance(process.get("resourceSelection"), dict):
            selection_key = process["resourceSelection"]["parameter"]
            selected_id = values[selection_key]
            geometry_refs.add(selection_key)
            geometry_refs.update(_parameter_references(
                process.get("parameterBindingsByResource", {}).get(selected_id, {})))
        geometry_refs.update(_variant_effect_references(process, "previewPlacement", values))
    for source in descriptor["assemblyPath"]:
        workflow_refs.update(_parameter_references(source.get("when")))
        if not _condition_matches(source.get("when"), values):
            continue
        for key in ("label", "kind", "distance"):
            workflow_refs.update(_variant_effect_references(source, key, values))
        workflow_refs.update(_parameter_references(source.get("direction")))
    for source in descriptor.get("bomRules", []):
        workflow_refs.update(_parameter_references(source.get("when")))
        if not _condition_matches(source.get("when"), values):
            continue
        for key in ("label", "quantity"):
            workflow_refs.update(_variant_effect_references(source, key, values))
        workflow_refs.update(_parameter_references(source.get("specification")))
    for source in descriptor.get("dimensionChecks", []):
        workflow_refs.update(_parameter_references(source.get("when")))
        if _condition_matches(source.get("when"), values):
            workflow_refs.update(_parameter_references({
                key: value for key, value in source.items() if key != "when"}))
    for check in descriptor.get("productBinding", {}).get("fitChecks", []):
        workflow_refs.update(_parameter_references(check.get("when")))
        if _condition_matches(check.get("when"), values):
            quantities = _PRODUCT_FIT_KINDS[check["kind"]][2]
            workflow_refs.update(_parameter_references({
                key: check[key] for key in quantities}))
    effects = {}
    for definition in descriptor["parameters"]:
        key = definition["key"]
        active = _condition_matches(definition.get("visibleWhen"), values)
        manufacturing = active and key in geometry_refs
        placement = active and key in placement_refs
        workflow = active and key in workflow_refs
        effects[key] = {"active": active, "previewGeometry": manufacturing or placement,
                        "manufacturingGeometry": manufacturing, "workflow": workflow,
                        "descriptionOnly": active and not (manufacturing or placement or workflow)}
    return effects


def finished_product_plan(product):
    product = finished_products.validate(product)
    shape = finished_products.definition(product["shapeId"])
    parts = [{**copy.deepcopy(span), **copy.deepcopy(product["spans"][span["id"]]),
              "role": span["id"]} for span in shape["spans"]]
    requests = [(part, _profile_request(part, product["parameters"])) for part in parts]
    values = _design_expression_values(product["parameters"], [
        {"role": part["role"], "request": request} for part, request in requests])
    design = []
    for part, request in requests:
        pose = _variant_value(part, "pose", values, {})
        matrix = _pose_matrix(pose, values)
        design.append({"id": f"design-{part['role']}", "role": part["role"],
                       "label": part["label"], "request": request,
                       "matrix": matrix, "compareMatrix": list(matrix)})
    return {"schema": "icax.finished-product-preview", "schemaVersion": 1,
            "finishedProduct": product, "layoutShape": shape["layoutShape"],
            "sceneParameters": copy.deepcopy(product["parameters"]), "designParts": design}


def _process_function_module():
    spec = importlib.util.spec_from_file_location(
        "icax_assembly_process_functions", Path(__file__).with_name("assembly_process_functions.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _process_api(runtime_context=None):
    api = dict(globals())
    api["_process_runtime_context"] = copy.deepcopy(runtime_context or {})
    return api


def _function_process_resource(process, values, resources, runtime_context):
    selected = _selected_process_descriptor(process, values)
    resource = resources.get(process["id"])
    if resource is None:
        return selected, {"scope": "system", "id": selected["id"]}, ""
    spec = importlib.util.spec_from_file_location(
        "icax_assembly_function_resources", Path(__file__).with_name("punch_tool_runtime.py"))
    punch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(punch)
    ref = copy.deepcopy(resource["ref"])
    if ref.get("scope") == "template":
        raise ValueError("模板自带加工资源尚未提供可核对的资源目录")
    user_root = str(runtime_context.get("userMouldRoot", ""))
    root = punch._package_root(ref, user_root)
    if root is None:
        raise ValueError("所选加工资源目录不可用")
    descriptor, _, _, _, digest = punch._package(Path(root)/ref["id"], root)
    if descriptor["target"] != selected["target"]:
        raise ValueError("所选资源不适用于此加工步骤")
    if ref.get("version") and ref["version"] != descriptor["version"]:
        raise ValueError("所选加工资源版本已改变")
    if ref.get("digest") and ref["digest"] != digest:
        raise ValueError("所选加工资源内容已改变")
    ref.update(version=descriptor["version"], digest=digest)
    return descriptor, ref, user_root


def evaluate_process(template_id, process_input, supplied_values=None, process_drafts=None,
                     runtime_context=None):
    return _process_function_module().evaluate(_process_api(runtime_context), template_id, process_input,
                                               supplied_values, process_drafts)


def resolve_process_plan(stocks, instances, native_profile_dependencies=None, runtime_context=None):
    return _process_function_module().resolve(_process_api(runtime_context), stocks, instances,
                                              native_profile_dependencies)


def _stock_operation_preview(descriptor, process_input, supplied_values, process_drafts):
    stock = {"id": "stock", **copy.deepcopy(process_input["parts"]["stock"])}
    stock["id"] = "stock"
    stock = {key: value for key, value in stock.items() if key in (
        "id", "profileRef", "parameters", "length", "matrix", "label")}
    plan = resolve_process_plan([stock], [{"instanceId": "preview", "templateId": descriptor["id"],
        "processInput": process_input, "parameters": supplied_values or {},
        "processDrafts": process_drafts or {}, "targets": {"stock": "stock"}}])
    material = plan["manufacturingParts"][0]
    material.update(sourceRole="stock", participantRoles=["stock"], label="连续母材",
                    explodedMatrix=list(material["matrix"]), compareMatrix=list(material["matrix"]))
    return {"schema": "icax.assembly-preview-plan", "schemaVersion": 1,
            "templateId": descriptor["id"], "templateVersion": descriptor["version"],
            "parameters": plan["instances"][0]["parameters"], "processInput": copy.deepcopy(process_input),
            "sceneParameters": {}, "sceneParts": {"stock": copy.deepcopy(process_input["parts"]["stock"])},
            "designParts": [{"id": "design-stock", "role": "stock", "label": "原始母材",
                             "request": {**copy.deepcopy(material["request"]), "features": []},
                             "matrix": list(stock["matrix"]), "compareMatrix": list(stock["matrix"])}],
            "manufacturingParts": [material], "forming": plan["forming"],
            "formedPreviewRecipe": None, "previewAnnotations": [],
            "resolvedWorkflow": {"scope": "process-instance", "realization": "integrated",
                                 "blankParts": [{"id": "stock", "participantRoles": ["stock"]}],
                                 "partOperations": plan["operations"], "assemblySteps": plan["assemblySteps"],
                                 "bom": plan["bom"], "checks": plan["checks"],
                                 "validationStatus": plan["validationStatus"]}}


def _example_process_input(descriptor, supplied_values, scene_parts, scene_parameters):
    """Adapt the selected example with separate scene and process inputs."""
    if supplied_values is None:
        supplied_values = {}
    # A process argument must not carry geometry overrides.
    _validated_preview_values(descriptor, supplied_values, scene_parameters)
    if scene_parts is not None and not isinstance(scene_parts, dict):
        raise ValueError("成品场景零件参数必须是对象")
    geometry_keys = {p["key"] for p in descriptor["parameters"] if p.get("scope") == "scene"}
    process_values = copy.deepcopy(supplied_values)
    # The example chooses its product once. Changing a machining setting
    # must not silently widen a tube, change its wall, or move a finished part
    # merely to make that setting applicable.
    product = get_example_product(descriptor["id"], {})["finishedProduct"]
    example = descriptor["exampleInput"]
    aliases = example.get("parameterBindings", {})
    geometry_values = scene_parameters or {}
    for key, value in geometry_values.items():
        if key not in geometry_keys:
            raise ValueError("场景几何输入包含未声明字段")
        product["parameters"][aliases.get(key, key)] = copy.deepcopy(value)
    for role, override in (scene_parts or {}).items():
        if role not in example["roles"] or not isinstance(override, dict):
            raise ValueError("场景管段角色无效")
        current = product["spans"][example["roles"][role]]
        profile_changed = "profileRef" in override and override["profileRef"] != current["profileRef"]
        if "parameters" in override and not isinstance(override["parameters"], dict):
            raise ValueError(f"成品场景角色 {role} 的管型参数必须是对象")
        if "profileRef" in override and override["profileRef"] != current["profileRef"] and "parameters" not in override:
            raise ValueError(f"成品场景角色 {role} 更换管型时必须提供管型参数")
        for key, value in override.items():
            if key not in ("profileRef", "parameters", "length"):
                raise ValueError("场景管段包含不支持字段")
            if key == "parameters" and not profile_changed:
                current[key] = {**current[key], **copy.deepcopy(value)}
            else:
                current[key] = copy.deepcopy(value)
    product = finished_products.validate(product)
    return process_contract.from_product(descriptor, product, finished_product_plan(product),
                                         example_selection=True), process_values


def _preview_definition_plan(template_id, supplied_values=None, process_drafts=None,
                             scene_parts=None, scene_parameters=None, finished_product=None,
                             manufacturing_only=False, process_input=None):
    if type(manufacturing_only) is not bool:
        raise ValueError("manufacturingOnly 必须是布尔值")
    descriptor = _template_by_id(template_id)
    explicit_product = finished_product is not None
    if explicit_product:
        finished_product = finished_products.validate(finished_product)
        if scene_parts is not None or scene_parameters is not None:
            raise ValueError("完整成品输入不能同时传入场景覆盖参数")
        if process_input is not None:
            raise ValueError("成品适配输入与独立工艺输入不能混用")
        process_input = process_contract.from_product(descriptor, finished_product,
                                                      finished_product_plan(finished_product), example_selection=True)
    if process_input is None:
        process_input, supplied_values = _example_process_input(
            descriptor, supplied_values, scene_parts, scene_parameters)
    process_input = process_contract.normalize(descriptor, process_input)
    if set(process_input["parts"]) == {"stock"}:
        return _stock_operation_preview(descriptor, process_input, supplied_values, process_drafts)
    geometry_keys = {p["key"] for p in descriptor["parameters"] if p.get("scope") == "scene"}
    scene_parameters = {key: value for key, value in process_input["geometry"].items() if key in geometry_keys}
    values, process_values, scene_values = _validated_preview_values(
        descriptor, supplied_values, scene_parameters)
    _require_process_applicability(descriptor, process_input, process_values)
    scene = descriptor["previewScene"]
    scene_design_parts = [{"role": role, "label": part.get("label", role),
                           **{key: copy.deepcopy(part[key]) for key in ("profileRef", "parameters", "length")}}
                          for role, part in process_input["parts"].items()]
    part_by_role = {item["role"]: item for item in scene_design_parts}
    compare = _resolve(scene.get("compareLayout", {}), values)
    design_offset = _finite_vector(compare.get("designOffset", [0, 0, 100]), 3, "设计件对照偏移")
    manufacturing_offset = _finite_vector(compare.get("manufacturingOffset", [0, 0, -100]), 3, "下料件对照偏移")
    participant_names = {item["role"]: item["label"] for item in descriptor["participants"]}
    # Resolve the section and stock dimensions before evaluating poses.  A
    # connection template can then place mating ends from actual section
    # dimensions without exposing a redundant user-controlled setback.
    design_requests = [(item, _profile_request(item, values)) for item in scene_design_parts]
    resolved_scene_parts = {item["role"]: {
        "profileRef": copy.deepcopy(request["profileRef"]),
        "parameters": copy.deepcopy(request["parameters"]),
        "length": float(_resolve(_variant_value(item, "length", values), values)),
    } for item, request in design_requests}
    _validate_integrated_stock_compatibility(descriptor, resolved_scene_parts)
    expression_values = _design_expression_values(values, [
        {"role": item["role"], "request": request} for item, request in design_requests
    ])
    design_parts = []
    for item, request in design_requests:
        actual_matrix = list(process_input["parts"][item["role"]]["matrix"])
        compare_matrix = list(actual_matrix)
        for index, value in zip((3, 7, 11), design_offset):
            compare_matrix[index] += value
        design_parts.append({
            "id": f"design-{item['role']}",
            "role": item["role"],
            "label": participant_names[item["role"]],
            "request": request,
            "matrix": actual_matrix,
            "compareMatrix": compare_matrix,
        })
    input_design_parts = copy.deepcopy(design_parts)
    processes = {item["id"]: item for item in descriptor.get("partProcesses", [])}
    blank_participants = {item["id"]: list(item["participants"]) for item in descriptor["manufacturingPlan"]["blankParts"]}
    manufacturing_parts = []
    part_operations = []
    for item in scene["manufacturingParts"]:
        source = part_by_role[item["sourceRole"]]
        request_source = {
            **source,
            **{key: item[key] for key in ("length", "lengthVariants", "stockLengthAddition") if key in item},
        }
        request = _profile_request(request_source, expression_values)
        for process_id in item.get("processes", []):
            process = processes[process_id]
            if not _condition_matches(process.get("appliesWhen"), values):
                continue
            descriptor_summary = _selected_process_descriptor(process, values)
            resolved_values = _process_values(process, descriptor_summary, expression_values, process_drafts or {})
            placement = _resolve(_variant_value(process, "previewPlacement", expression_values), expression_values)
            definition_keys = {definition["key"] for definition in descriptor_summary.get("parameters", [])}
            operation_keys = {definition["key"] for definition in descriptor_summary.get("operationParameters", [])}
            part_operations.append({
                "id": f"{item['id']}:{process_id}", "processId": process_id,
                "blankId": item["id"], "label": process.get("label", process_id),
                "participantRoles": list(process["participants"]),
                "resourceRef": {"id": descriptor_summary["id"], "version": descriptor_summary.get("version", "")},
                "toolParameters": {key: copy.deepcopy(value) for key, value in resolved_values.items() if key in definition_keys},
                "operationParameters": {key: copy.deepcopy(value) for key, value in resolved_values.items() if key in operation_keys},
                "placement": placement,
            })
            _apply_process(request, process, descriptor_summary, resolved_values, placement, part_by_role, expression_values)
        pose = _variant_value(item, "explodedPose", expression_values, {})
        exploded_matrix = _pose_matrix(pose, expression_values)
        manufacturing_parts.append({
            "id": f"manufacturing-{item['id']}",
            "blankId": item["id"],
            "sourceRole": item["sourceRole"],
            "participantRoles": blank_participants[item["id"]],
            "label": item.get("label", item["id"]),
            "request": request,
            "matrix": exploded_matrix,
            "explodedMatrix": list(exploded_matrix),
            "compareMatrix": _pose_matrix(pose, expression_values, manufacturing_offset),
        })
    checks = _resolved_dimension_checks(descriptor, expression_values, design_parts, manufacturing_parts, part_operations)
    bom = _resolved_bom(descriptor, expression_values)
    workflow = {
        "schema": "icax.assembly-workflow", "schemaVersion": 1,
        "scope": "template-example",
        "realization": descriptor["manufacturingPlan"]["realization"],
        "blankParts": [{
            "id": item["id"], "label": item["label"],
            "participantRoles": list(item["participants"]),
            "sourceRole": next(part["sourceRole"] for part in scene["manufacturingParts"] if part["id"] == item["id"]),
        } for item in descriptor["manufacturingPlan"]["blankParts"]],
        "partOperations": part_operations,
        "assemblySteps": _resolved_steps(descriptor, expression_values),
        "bom": bom,
        "bomStatus": "declared" if bom else "none-for-configuration" if descriptor.get("bomRules") else "not-declared",
        "checks": checks,
        "validationStatus": "failed" if any(check["status"] == "fail" for check in checks)
        else "requires-definition" if any(check.get("blocking") for check in checks)
        else "dimension-checked-example" if checks else "example-unchecked",
        "parameterEffects": _parameter_effects(descriptor, values),
    }
    formed_recipe = None
    for source in scene.get("formedPreviews", []):
        if _condition_matches(source.get("when"), values):
            formed_recipe = _resolve({key: value for key, value in source.items()
                                      if key != "when"}, expression_values)
            break
    preview_annotations = [
        _resolve({key: value for key, value in source.items() if key != "when"}, expression_values)
        for source in scene.get("annotations", [])
        if _condition_matches(source.get("when"), values)
    ]
    result = {
        "schema": "icax.assembly-preview-plan",
        "schemaVersion": 1,
        "templateId": descriptor["id"],
        "templateVersion": descriptor["version"],
        "parameters": values,
        "sceneParameters": scene_values,
        "sceneParts": resolved_scene_parts,
        "designParts": design_parts,
        "manufacturingParts": manufacturing_parts,
        "formedPreviewRecipe": formed_recipe,
        "previewAnnotations": preview_annotations,
        "resolvedWorkflow": workflow,
        "processInput": copy.deepcopy(process_input),
    }
    if explicit_product:
        result["finishedProduct"] = copy.deepcopy(finished_product)
    if scene.get("formedPreviewScript"):
        module = _load_assembly_script(template_id)
        plan_builder = getattr(module, "build_plan", None)
        mesh_builder = getattr(module, "build_formed_preview", None)
        if not callable(plan_builder) and not callable(mesh_builder):
            raise ValueError("assembly.py 必须定义 build_plan(plan) 或 build_formed_preview(plan)")
        if callable(plan_builder):
            result = plan_builder(copy.deepcopy(result))
            _validate_script_plan(result, descriptor, values, resolved_scene_parts, scene_values, finished_product,
                                  process_input)
            if not _same_input(result["designParts"], input_design_parts):
                raise ValueError("装配工艺脚本不得改写输入的成品造型")
        if callable(mesh_builder) and not manufacturing_only:
            result["formedPreviewMesh"] = _validated_formed_mesh(mesh_builder(copy.deepcopy(result)))
    result["parameters"] = process_values
    if explicit_product:
        result["finishedProduct"] = copy.deepcopy(finished_product)
    return result


def preview_plan(template_id, supplied_values=None, process_drafts=None,
                 scene_parts=None, scene_parameters=None, finished_product=None,
                 manufacturing_only=False, process_input=None):
    """Public preview uses exactly the operations returned by the local function.

    The definition compiler is also available privately to that function for
    existing tool solvers. Keeping this boundary prevents recursive previews
    and makes an explicit processing anchor authoritative in the UI as well
    as in a repeated stock plan.
    """
    formal = process_input is not None or finished_product is not None
    plan = _preview_definition_plan(template_id, supplied_values, process_drafts,
                                    scene_parts, scene_parameters, finished_product,
                                    manufacturing_only, process_input)
    descriptor = _template_by_id(template_id)
    if not formal or descriptor["manufacturingPlan"]["realization"] != "separate":
        return plan
    local = plan["processInput"]
    evaluated = evaluate_process(template_id, local, supplied_values, process_drafts)
    if not evaluated["applicable"]:
        raise ValueError(evaluated["reason"])
    parts = {part["sourceRole"]: part for part in plan["manufacturingParts"]}
    for part in parts.values():
        part["request"]["features"] = []
        part["request"]["ends"] = {"start": {"type": "keep"}, "end": {"type": "keep"}}
    for operation in evaluated["operations"]:
        request = parts[operation["role"]]["request"]
        if "requestFeature" in operation:
            feature = copy.deepcopy(operation["requestFeature"])
            feature["id"] = operation["id"]
            request["features"].append(feature)
        elif "requestEnd" in operation:
            request["ends"][operation["end"]] = copy.deepcopy(operation["requestEnd"])
    definitions = {item["processId"]: item for item in plan["resolvedWorkflow"]["partOperations"]}
    actual_operations = []
    for operation in evaluated["operations"]:
        metadata = copy.deepcopy(definitions.get(operation.get("processId"), {}))
        metadata.update(id=operation["id"], blankId=parts[operation["role"]]["blankId"],
                        participantRoles=[operation["role"]])
        if "requestFeature" in operation:
            feature = operation["requestFeature"]
            metadata["placement"] = {"target": "part" if feature.get("toolTarget") == "part" else "side",
                                     **{key: copy.deepcopy(feature[key]) for key in
                                        ("face", "reference", "station", "offset", "rotation", "arrayCount", "arrayPitch")
                                        if key in feature}}
        else:
            end = operation["requestEnd"]
            metadata["placement"] = {"target": "end", "end": operation["end"],
                                     "datum": end.get("datum", "long")}
            metadata.setdefault("operationParameters", {}).update(
                {key: copy.deepcopy(end[key]) for key in ("rotation", "trim", "offset") if key in end})
        actual_operations.append(metadata)
    plan["resolvedWorkflow"]["partOperations"] = actual_operations
    plan["resolvedWorkflow"]["operations"] = copy.deepcopy(evaluated["operations"])
    plan["resolvedWorkflow"]["scope"] = "process-instance"
    return plan


def _bound_number(value, label, minimum=-math.inf, maximum=math.inf):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} 必须是有限数值")
    if not minimum <= value <= maximum:
        raise ValueError(f"{label} 超出允许范围")
    return float(value)


def _bound_participants(descriptor, participants):
    if any(item.get("count", 1) != 1 for item in descriptor["participants"]):
        raise ValueError("当前实际绑定仅支持每个角色对应一个真实构件")
    roles = {item["role"] for item in descriptor["participants"]}
    if not isinstance(participants, list) or len(participants) != len(roles):
        raise ValueError("实际构件必须一对一覆盖全部装配角色")
    result, member_ids, item_keys = {}, set(), set()
    expected_anchors = descriptor.get("productBinding", {}).get("anchors", {})
    for source in participants:
        if not isinstance(source, dict):
            raise ValueError("实际构件必须是对象")
        role = source.get("role")
        if role not in roles or role in result:
            raise ValueError("实际构件角色无效或重复")
        member_id = _text(source.get("memberEntityId"), "实际构件身份")
        item_key = _text(source.get("itemKey"), "实际构件稳定键")
        if member_id in member_ids or item_key in item_keys:
            raise ValueError("不同装配角色不能绑定同一实际构件")
        member_ids.add(member_id)
        item_keys.add(item_key)
        length = _bound_number(source.get("length"), f"{role} 实际长度", 1, 100000)
        section = source.get("section")
        if (not isinstance(section, dict) or section.get("schema") not in
                ("icax.tube-profile", "icax.imported-tube-profile")
                or section.get("schemaVersion") != 1
                or not isinstance(section.get("contours"), list) or not 1 <= len(section["contours"]) <= 1000
                or any(not isinstance(contour, dict) or not contour for contour in section["contours"])):
            raise ValueError(f"{role} 缺少真实截面轮廓，不能使用示例截面补缺")
        if not section.get("id", section.get("kind")):
            raise ValueError(f"{role} 真实截面缺少管型身份")
        try:
            json.dumps(section, allow_nan=False)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{role} 真实截面包含无效数值或对象") from error
        anchor = source.get("anchor")
        if not isinstance(anchor, dict) or anchor.get("kind") not in ("side", "end"):
            raise ValueError(f"{role} 必须提供明确端部或侧面锚点")
        if role in expected_anchors and anchor["kind"] != expected_anchors[role]:
            raise ValueError(f"{role} 的实际锚点与装配角色不匹配")
        allowed = {"kind", "rotation", "end", "trim", "approachFace", "stockAllowance", "contactInset"} if anchor["kind"] == "end" else {
            "kind", "rotation", "face", "reference", "station", "offset"}
        if set(anchor) - allowed:
            raise ValueError(f"{role} 的锚点包含未支持的定位字段")
        normalized_anchor = copy.deepcopy(anchor)
        normalized_anchor["rotation"] = _bound_number(anchor.get("rotation", 0), f"{role} 锚点旋转", -360, 360)
        if anchor["kind"] == "end":
            if anchor.get("end") not in ("start", "end"):
                raise ValueError(f"{role} 必须明确选择起端或末端")
            if "approachFace" in anchor and anchor["approachFace"] not in ("top", "bottom", "left", "right"):
                raise ValueError(f"{role} 的端部连接方向必须是有效的制造局部侧面")
            normalized_anchor["trim"] = _bound_number(anchor.get("trim", 0), f"{role} 端部余量", 0, length)
            if "stockAllowance" in anchor:
                normalized_anchor["stockAllowance"] = _bound_number(
                    anchor["stockAllowance"], f"{role} 端部库存余量", 0, length)
            if "contactInset" in anchor:
                normalized_anchor["contactInset"] = _bound_number(
                    anchor["contactInset"], f"{role} 接头近侧外表面距离", 0, length)
        else:
            if anchor.get("face") not in ("top", "bottom", "left", "right") or anchor.get("reference") != "start":
                raise ValueError(f"{role} 侧面锚点必须明确面和从起端计算的定位基准")
            normalized_anchor["station"] = _bound_number(anchor.get("station"), f"{role} 锚点位置", 0, length)
            normalized_anchor["offset"] = _bound_number(anchor.get("offset", 0), f"{role} 锚点横移", -100000, 100000)
        normalized_node_frame = None
        if "nodeFrame" in source:
            frame = source["nodeFrame"]
            pair_keys = {"selfAwayPart", "otherAwayPart", "selfNodePart"}
            node_keys = {"selfAwayPart", "selfNodePart", "awayPartsByRole", "sourceToPart", "partToSource"}
            if not isinstance(frame, dict) or set(frame) not in (pair_keys, node_keys):
                raise ValueError(f"{role} 的已核对制造节点坐标无效")
            normalized_node_frame = {}
            for key in ("selfAwayPart", "selfNodePart") + (("otherAwayPart",) if set(frame) == pair_keys else ()):
                normalized_node_frame[key] = _finite_vector(frame[key], 3, f"{role}.{key}")
            for key in ("selfAwayPart",) + (("otherAwayPart",) if set(frame) == pair_keys else ()):
                magnitude = math.sqrt(sum(value * value for value in normalized_node_frame[key]))
                if abs(magnitude - 1) > 1e-6:
                    raise ValueError(f"{role} 的已核对制造节点轴向不是单位向量")
            if set(frame) == node_keys:
                axes = frame["awayPartsByRole"]
                if not isinstance(axes, dict) or set(axes) != roles:
                    raise ValueError(f"{role} 的节点方向必须覆盖全部真实角色")
                normalized_node_frame["awayPartsByRole"] = {
                    key: _finite_vector(axis, 3, f"{role}.{key} 节点方向") for key, axis in axes.items()}
                if any(abs(math.sqrt(_dot(axis, axis))-1) > 1e-6 for axis in normalized_node_frame["awayPartsByRole"].values()):
                    raise ValueError(f"{role} 的节点方向必须是单位向量")
                if any(abs(a-b) > 1e-6 for a, b in zip(normalized_node_frame["selfAwayPart"], normalized_node_frame["awayPartsByRole"][role])):
                    raise ValueError(f"{role} 的自身节点方向不一致")
                for key in ("sourceToPart", "partToSource"):
                    normalized_node_frame[key] = _finite_vector(frame[key], 16, f"{role}.{key}")
        result[role] = {"role": role, "memberEntityId": member_id, "itemKey": item_key,
                        "length": length, "section": copy.deepcopy(section), "anchor": normalized_anchor}
        # The native adapter supplies the committed product anchor separately
        # from the manufacturing-normalized anchor. A reversed stock flips the
        # latter end name but must not flip the former's physical direction.
        product_anchor = source.get("productAnchor")
        if product_anchor is not None:
            if (not isinstance(product_anchor, dict)
                    or product_anchor.get("kind") != normalized_anchor["kind"]):
                raise ValueError(f"{role} 的产品节点锚点无效")
            if normalized_anchor["kind"] == "end":
                if product_anchor.get("end") not in ("start", "end"):
                    raise ValueError(f"{role} 的产品节点端向无效")
                result[role]["productAnchor"] = {"kind": "end", "end": product_anchor["end"]}
        if normalized_node_frame is not None:
            result[role]["nodeFrame"] = normalized_node_frame
    return result


def _bound_unsupported_reason(descriptor, values):
    capability = descriptor.get("productBinding", {})
    if descriptor["manufacturingPlan"]["realization"] != "separate":
        return capability.get("reason", "一体或归并下料尚不支持真实构件增量加工")
    if capability.get("mode") != "incremental-cut":
        return capability.get("reason", "该模板尚未声明真实构件增量加工规则")
    if not _active_product_topologies(capability, values):
        return "该模板尚未声明当前参数可适用的成品连接拓扑"
    for branch in capability.get("unsupportedBranches", []):
        if _condition_matches(branch.get("when"), values):
            return _text(branch.get("reason"), "不支持原因")
    for check in descriptor.get("dimensionChecks", []):
        if check.get("kind") == "required-process" and _condition_matches(check.get("when"), values):
            return _text(check.get("detail"), "缺少实际加工工序")
    return ""


def _active_product_topologies(capability, values):
    return sorted({entry["topology"] for entry in capability.get("compatibleProductTopologies", [])
                   if _condition_matches(entry.get("when"), values)})


def _bound_template_digest(descriptor, active_processes):
    # Example dimensions and poses are deliberately absent from production identity.
    projected = {key: copy.deepcopy(descriptor.get(key)) for key in (
        "id", "version", "parameters", "participants", "manufacturingPlan",
        "productBinding", "assemblyPath", "bomRules", "dimensionChecks")}
    projected["partProcesses"] = [{key: value for key, value in process.items()
        if key not in ("previewPlacement", "previewPlacementVariants")}
        for process in active_processes]
    digest = hashlib.sha256(json.dumps(projected, ensure_ascii=False, sort_keys=True,
                                      separators=(",", ":"), allow_nan=False).encode("utf-8"))
    resource_ids = sorted({process["resource"]["id"] for process in active_processes
                           if isinstance(process.get("resource"), dict)})
    for resource_id in resource_ids:
        for name in ("tool.json", "tool.py"):
            path = PROCESS_ROOT / resource_id / name
            if path.is_file():
                digest.update(f"{resource_id}/{name}\0".encode("utf-8"))
                digest.update(path.read_bytes())
    if descriptor.get("productBinding", {}).get("operationScript"):
        # Only script-backed bindings gain script identity. Existing declarative
        # bindings retain their established digest and persistence contract.
        for path in sorted((ROOT / descriptor["id"]).glob("*.py")):
            if path.is_symlink() or path.stat().st_size > MAX_BYTES:
                raise ValueError("实际装配脚本依赖无效")
            digest.update((path.name + "\0").encode("utf-8"))
            digest.update(path.read_bytes())
        for name in ["_shared/assembly_template_runtime.py", *descriptor["productBinding"].get("operationScriptDependencies", [])]:
            digest.update((name + "\0").encode("utf-8"))
            digest.update((ROOT.parent / name).read_bytes())
    return digest.hexdigest()


def _bound_process_values(process, tool, values, drafts, rule, participants):
    selected_draft = drafts.get(process["id"], {}) if isinstance(drafts, dict) else {}
    selected_draft = selected_draft.get(tool["id"], {}) if isinstance(selected_draft, dict) else {}
    shape_keys = {item["key"] for item in tool.get("parameters", [])}
    if not isinstance(selected_draft, dict) or set(selected_draft) - shape_keys:
        raise ValueError(f"{process['id']} 的实际绑定草稿只能修改刀具形状参数，定位须使用显式锚点")
    result = _process_values(process, tool, values, drafts)
    definitions = {item["key"]: item for item in _parameter_definitions(tool)}
    for key, source in rule.get("parameterSources", {}).items():
        if key not in definitions:
            raise ValueError(f"{process['id']} 的实际构件参数来源引用了不存在的工具字段")
        if source["field"] == "length":
            result[key] = participants[source["role"]]["length"]
        elif source["field"] == "anchor.stockAllowance":
            allowance = participants[source["role"]]["anchor"].get("stockAllowance")
            if allowance is None:
                raise ValueError(f"{process['id']} 缺少产品声明的端部库存余量")
            result[key] = allowance * source.get("factor", 1)
            if source.get("addField") == "anchor.contactInset":
                inset = participants[source["role"]]["anchor"].get("contactInset")
                if inset is None:
                    raise ValueError(f"{process['id']} 缺少产品声明的接头近侧距离")
                result[key] += inset
            if "offsetParameter" in source:
                if source.get("offsetProjection") == "miter-face-normal":
                    result[key] += _miter_normal_gap_axial_trim(
                        values[source["offsetParameter"]],
                        values[source["offsetAngleParameter"]],source["offsetFactor"])
                else:
                    result[key] += source["offsetFactor"] * values[source["offsetParameter"]]
            if result[key] < source.get("min", 0):
                raise ValueError(f"{process['id']} 的端部库存余量不足以形成真实退切")
        elif source["field"] == "productAnchor.end":
            original = participants[source["role"]].get("productAnchor",
                                                        participants[source["role"]]["anchor"])
            end = original.get("end") if isinstance(original, dict) else None
            if end not in source["map"]:
                raise ValueError(f"{process['id']} 缺少已提交成品端向")
            result[key] = source["map"][end]
            if source.get("matchHostFaceNormal"):
                section = participants[source["role"]]["section"]
                frame = section.get("sectionFrame")
                face_normal = frame.get("hostFaceNormalToolPart") if isinstance(frame, dict) else None
                if face_normal is not None:
                    normal = _finite_vector(face_normal, 3, "已核对主管入口面法向")
                    if abs(math.sqrt(sum(value * value for value in normal)) - 1) > 1e-6:
                        raise ValueError(f"{process['id']} 的主管入口面法向不是单位向量")
                    angle = math.radians(_bound_number(result.get("angle"), "刀具轴线夹角"))
                    azimuth = math.radians(_bound_number(result.get("azimuth"), "刀具截面方位"))
                    axis = (math.cos(angle), math.sin(angle) * math.sin(azimuth),
                            math.sin(angle) * math.cos(azimuth))
                    signed = sum(left * right for left, right in zip(axis, normal))
                    if abs(signed) <= 1e-6:
                        raise ValueError(f"{process['id']} 的刀具轴线没有穿过主管实际入口面")
                    branch_u = frame.get("xAxisToolPart")
                    branch_v = frame.get("yAxisToolPart")
                    if branch_u is not None and branch_v is not None:
                        u = _finite_vector(branch_u, 3, "已核对支管截面第一方向")
                        v = _finite_vector(branch_v, 3, "已核对支管截面第二方向")
                        branch_axis = (u[1] * v[2] - u[2] * v[1],
                                       u[2] * v[0] - u[0] * v[2],
                                       u[0] * v[1] - u[1] * v[0])
                        if abs(math.sqrt(sum(component * component for component in branch_axis)) - 1) > 1e-6:
                            raise ValueError(f"{process['id']} 的支管截面方向无效")
                        away = tuple(component * (1 if end == "start" else -1)
                                     for component in branch_axis)
                        axial = sum(left * right for left, right in zip(axis, away))
                        if abs(axial) < 1 - 1e-6:
                            raise ValueError(f"{process['id']} 的刀具轴线与实际支管不同轴")
                        result[key] = "positive" if axial > 0 else "negative"
                        if sum(left * right for left, right in zip(away, normal)) <= 1e-6:
                            raise ValueError(f"{process['id']} 的支管没有从主管入口面向外伸出")
                    else:
                        result[key] = "positive" if signed > 0 else "negative"
                elif section.get("geometrySource") == "providedBoundary":
                    raise ValueError(f"{process['id']} 缺少已核对的主管入口面方向")
        elif source["field"] == "anchor.faceByProductEnd":
            original = participants[source["endRole"]].get("productAnchor",
                                                         participants[source["endRole"]]["anchor"])
            end = original.get("end") if isinstance(original, dict) else None
            face = participants[source["role"]]["anchor"].get("face")
            if end not in source["map"] or face not in source["map"][end]:
                raise ValueError(f"{process['id']} 缺少已提交产品壁面或端向")
            result[key] = source["map"][end][face]
        elif source["field"] in ("nodeFrame.miterAngle", "nodeFrame.miterRotation"):
            angle, rotation = _bound_node_miter_orientation(
                process["id"], source, participants, values, result.get("rotation", 0))
            result[key] = angle if source["field"] == "nodeFrame.miterAngle" else rotation
        elif source["field"] == "constant":
            result[key] = source["value"]
        else:
            face = participants[source["role"]]["anchor"]["face"]
            result[key] = source["map"][face]
        definition = definitions[key]
        if definition.get("valueType") in ("number", "integer"):
            _bound_number(result[key], f"{process['id']} 的 {key}",
                          definition.get("min", -math.inf), definition.get("max", math.inf))
    return result


def _bound_centered_rectangular_boundary(section, process_id):
    """Accept an actual centered rectangular tube boundary without trusting its profile ID."""
    def rectangle(contour):
        segments = contour.get("segments") if isinstance(contour, dict) else None
        if (not isinstance(contour, dict) or contour.get("kind") != "path" or contour.get("closed") is not True
                or not isinstance(segments, list) or len(segments) != 4):
            raise ValueError(f"{process_id} 的提供截面不是闭合矩形管轮廓")
        vertices = []
        for segment in segments:
            if not isinstance(segment, dict) or segment.get("kind") != "line":
                raise ValueError(f"{process_id} 的提供截面包含非矩形边")
            vertices.append(_finite_vector(segment.get("start"), 2, "提供截面起点"))
            _finite_vector(segment.get("end"), 2, "提供截面终点")
        for index, segment in enumerate(segments):
            if any(abs(segment["end"][axis] - vertices[(index + 1) % 4][axis]) > 1e-6
                   for axis in (0, 1)):
                raise ValueError(f"{process_id} 的提供截面边未闭合")
        left, right = min(point[0] for point in vertices), max(point[0] for point in vertices)
        bottom, top = min(point[1] for point in vertices), max(point[1] for point in vertices)
        corners = {(left, bottom), (right, bottom), (right, top), (left, top)}
        if (right - left <= 1e-6 or top - bottom <= 1e-6
                or {(point[0], point[1]) for point in vertices} != corners):
            raise ValueError(f"{process_id} 的提供截面不是有效矩形")
        for index, point in enumerate(vertices):
            following = vertices[(index + 1) % 4]
            if (point[0] == following[0]) == (point[1] == following[1]):
                raise ValueError(f"{process_id} 的提供截面包含斜边或零长度边")
        signed_area = sum(point[0] * vertices[(index + 1) % 4][1]
                          - vertices[(index + 1) % 4][0] * point[1]
                          for index, point in enumerate(vertices))
        return (left, right, bottom, top), signed_area

    contours = section["contours"]
    if len(contours) != 2:
        raise ValueError(f"{process_id} 的提供截面需要一外一内两个矩形轮廓")
    outside, outer_area = rectangle(contours[0])
    inside, inner_area = rectangle(contours[1])
    if (outer_area * inner_area >= 0 or not (outside[0] < inside[0] < inside[1] < outside[1])
            or not (outside[2] < inside[2] < inside[3] < outside[3])):
        raise ValueError(f"{process_id} 的提供截面内外轮廓不构成空心矩形管")
    if (abs(outside[0] + outside[1] - inside[0] - inside[1]) > 0.1
            or abs(outside[2] + outside[3] - inside[2] - inside[3]) > 0.1):
        raise ValueError(f"{process_id} 的提供截面内外轮廓未同心")
    width = _bound_section_extent(section, "width")
    depth = _bound_section_extent(section, "depth")
    if abs(outside[1] - outside[0] - width) > 0.05 or abs(outside[3] - outside[2] - depth) > 0.05:
        raise ValueError(f"{process_id} 的提供截面声明尺寸与实际外轮廓不一致")


def _bound_node_miter_orientation(process_id, source, participants, values, fallback_rotation):
    """Use a verified part frame, not a fixed role sign, for a corner bisector."""
    if len(participants) != 2:
        raise ValueError(f"{process_id} 需要两个实际端部构件")
    role = source["role"]
    member = participants[role]
    frame = member.get("nodeFrame")
    if frame is None:
        raise ValueError(f"{process_id} 缺少已核对的制造节点坐标")
    if any(abs(frame["selfNodePart"][index]) > 0.05 for index in (1, 2)):
        raise ValueError(f"{process_id} 的节点偏离端切刀具横向中心，当前斜接不支持")
    self_away = frame["selfAwayPart"]
    other_away = frame["otherAwayPart"]
    if abs(abs(self_away[0]) - 1) > 1e-6 or math.hypot(*self_away[1:]) > 1e-6:
        raise ValueError(f"{process_id} 的构件轴线未对齐实际制造件")
    end = member["anchor"]["end"]
    if (end == "start" and self_away[0] <= 0) or (end == "end" and self_away[0] >= 0):
        raise ValueError(f"{process_id} 的节点远离方向与实际物理端不一致")
    allowance = member["anchor"].get("stockAllowance")
    if allowance is None:
        raise ValueError(f"{process_id} 缺少产品声明的端部库存余量")
    expected_node_x = self_away[0] * (allowance - member["length"] / 2)
    if abs(frame["selfNodePart"][0] - expected_node_x) > 0.05:
        raise ValueError(f"{process_id} 的节点纵向位置与实际物理端及库存余量不一致")
    for participant in participants.values():
        section = participant["section"]
        if section.get("geometrySource") == "providedBoundary":
            _bound_centered_rectangular_boundary(section, process_id)
        elif section.get("kind") == "fixed-section" or section.get("id", section.get("kind")) not in (
                "rect", "round", "round-bar"):
            raise ValueError(f"{process_id} 当前仅支持居中方矩管或圆管斜接")
    first, second = [participant["section"] for participant in participants.values()]
    if first["contours"] != second["contours"]:
        raise ValueError(f"{process_id} 的两根实际截面不同，不能确认整面斜接")
    joint_angle = values[source["angleParameter"]]
    if joint_angle == 0:
        return 0.0, fallback_rotation
    for participant in participants.values():
        inset = participant["anchor"].get("contactInset")
        if inset is None or inset <= 0:
            raise ValueError(f"{process_id} 缺少产品声明的接头近侧外表面距离")
    cross_size = math.hypot(other_away[1], other_away[2])
    if cross_size <= 1e-6:
        raise ValueError(f"{process_id} 的另一构件轴向没有有效横向投影")
    angle = math.copysign(joint_angle / 2, self_away[0])
    rotation = math.degrees(math.atan2(other_away[2], other_away[1]))
    return angle, rotation


def _bound_section_extent(section, axis):
    parameters = section.get("parameters", {})
    value = section.get(axis, parameters.get(axis) if isinstance(parameters, dict) else None)
    if value is None and axis == "depth" and section.get("id", section.get("kind")) in ("round", "round-bar"):
        return _bound_section_extent(section, "width")
    return _bound_number(value, f"真实截面 {axis}", 1e-6, 100000)


def _flat_contact_outer(section, label):
    """Recognize a centered, flat-sided tube from its actual outer contour."""
    contours = section.get("contours", [])
    if len(contours) != 2:
        raise ValueError(f"{label} 贴合仅支持有内外轮廓的方矩管")
    frame = section.get("sectionFrame", {})
    if frame and frame.get("verification") != "committed-source-brep-replay":
        raise ValueError(f"{label} 的真实管型截面尚未核对")
    index = frame.get("outerContourIndex", 0)
    if not isinstance(index, int) or index not in (0, 1):
        raise ValueError(f"{label} 的外轮廓索引无效")
    contour = contours[index]
    if contour.get("kind") == "roundedRectangle":
        center = _finite_vector(contour.get("center"), 2, f"{label} 截面中心")
        width = _bound_number(contour.get("width"), f"{label} 宽度", 1, 100000)
        depth = _bound_number(contour.get("height"), f"{label} 高度", 1, 100000)
        radius = _bound_number(contour.get("radius", 0), f"{label} 圆角", 0, min(width, depth) / 2)
        if any(abs(value) > 0.05 for value in center):
            raise ValueError(f"{label} 当前仅支持居中方矩管贴合")
        return width, depth, width - 2 * radius, depth - 2 * radius
    segments = contour.get("segments") if contour.get("kind") == "path" and contour.get("closed") is True else None
    if not isinstance(segments, list) or len(segments) not in (4, 8):
        raise ValueError(f"{label} 当前仅支持平面方矩管贴合；圆管须使用相贯端切")
    starts, ends, middles = [], [], []
    for segment in segments:
        if segment.get("kind") not in ("line", "arc"):
            raise ValueError(f"{label} 截面含非方矩管轮廓")
        starts.append(_finite_vector(segment.get("start"), 2, f"{label} 轮廓起点"))
        ends.append(_finite_vector(segment.get("end"), 2, f"{label} 轮廓终点"))
        if segment["kind"] == "arc":
            middles.append(_finite_vector(segment.get("middle"), 2, f"{label} 圆角中点"))
    for i, end in enumerate(ends):
        if any(abs(end[k] - starts[(i + 1) % len(starts)][k]) > 1e-5 for k in (0, 1)):
            raise ValueError(f"{label} 截面外轮廓没有闭合")
    left, right = min(p[0] for p in starts), max(p[0] for p in starts)
    bottom, top = min(p[1] for p in starts), max(p[1] for p in starts)
    width, depth = right - left, top - bottom
    if width <= 1 or depth <= 1 or abs(left + right) > 0.1 or abs(bottom + top) > 0.1:
        raise ValueError(f"{label} 当前仅支持居中方矩管贴合")
    sides = {}
    for segment, start, end in zip(segments, starts, ends):
        if segment["kind"] != "line":
            continue
        if abs(start[0] - end[0]) < 1e-6:
            side = "left" if abs(start[0] - left) < 1e-6 else "right" if abs(start[0] - right) < 1e-6 else None
            span = abs(start[1] - end[1])
        elif abs(start[1] - end[1]) < 1e-6:
            side = "bottom" if abs(start[1] - bottom) < 1e-6 else "top" if abs(start[1] - top) < 1e-6 else None
            span = abs(start[0] - end[0])
        else:
            side, span = None, 0
        if not side or side in sides or span <= 1e-6:
            raise ValueError(f"{label} 当前仅支持四边平直的方矩管贴合")
        sides[side] = span
    if set(sides) != {"left", "right", "top", "bottom"} or len(middles) not in (0, 4):
        raise ValueError(f"{label} 当前仅支持四边平直的方矩管贴合")
    if any(not left - 1e-6 <= p[0] <= right + 1e-6 or not bottom - 1e-6 <= p[1] <= top + 1e-6
           for p in middles):
        raise ValueError(f"{label} 截面圆角超出方矩管外包络")
    return width, depth, min(sides["top"], sides["bottom"]), min(sides["left"], sides["right"])


def _validate_flat_contact_fit(check, participants, values):
    host = participants[check["roles"]["host"]]
    branch = participants[check["roles"]["branch"]]
    if abs(values.get("intersectionAngle", 0) - 90) > 1e-6 or check["gapMm"] != 0:
        raise ValueError("直角平面贴合只支持 90° 零间隙节点")
    host_frame = host["section"].get("sectionFrame")
    branch_frame = branch["section"].get("sectionFrame")
    if (not isinstance(host_frame, dict) or not isinstance(branch_frame, dict)
            or host_frame.get("verification") != "committed-source-brep-replay"
            or branch_frame.get("verification") != "committed-source-brep-replay"):
        raise ValueError("平面贴合须有两件已核对的真实制造截面")
    product_anchor = branch.get("productAnchor")
    if not isinstance(product_anchor, dict) or product_anchor.get("end") not in ("start", "end"):
        raise ValueError("平面贴合须有已核对的支管产品端向")
    branch_u = _finite_vector(branch_frame.get("xAxisToolPart"), 3, "支管真实截面横轴")
    branch_v = _finite_vector(branch_frame.get("yAxisToolPart"), 3, "支管真实截面纵轴")
    face_normal = _finite_vector(branch_frame.get("hostFaceNormalToolPart"), 3,
                                 "主管真实接触面法向")
    branch_axis = _cross(branch_u, branch_v)
    away = branch_axis if product_anchor["end"] == "start" else [-item for item in branch_axis]
    if (any(abs(_dot(axis, axis) - 1) > 1e-6 for axis in (branch_u, branch_v, face_normal))
            or abs(_dot(branch_u, branch_v)) > 1e-6
            or _dot(away, face_normal) < 1 - 1e-6):
        raise ValueError("平面贴合的支管没有沿主管所选接触面的法向向外伸出")
    host_width, host_depth, host_top_span, host_side_span = _flat_contact_outer(host["section"], "主管")
    branch_width, branch_depth, _, _ = _flat_contact_outer(branch["section"], "支管")
    face = host["anchor"]["face"]
    flat_span = host_top_span if face in ("top", "bottom") else host_side_span
    envelope_radius = math.hypot(branch_width, branch_depth) / 2
    if (abs(host["anchor"]["offset"]) + envelope_radius > flat_span / 2 + 1e-6
            or host["anchor"]["station"] < envelope_radius - 1e-6
            or host["length"] - host["anchor"]["station"] < envelope_radius - 1e-6):
        raise ValueError("支管端面超出主管平直接触面或靠近主管端部，不能按平面贴合")
    if branch["anchor"]["trim"] != 0:
        raise ValueError("平面贴合支管端面不能再有隐含退切")


def _validate_tab_slot_wall_mapping(check, participants):
    host = participants[check["roles"]["host"]]
    branch = participants[check["roles"]["branch"]]
    host_frame = host["section"].get("sectionFrame")
    branch_frame = branch["section"].get("sectionFrame")
    if (not isinstance(host_frame, dict) or not isinstance(branch_frame, dict)
            or host_frame.get("verification") != "committed-source-brep-replay"
            or branch_frame.get("verification") != "committed-source-brep-replay"):
        raise ValueError("公母插舌绑定须有两件已核对的真实制造截面")
    vectors = [_finite_vector(branch_frame.get(key), 3, "公母插舌真实壁面方向")
               for key in ("branchManufacturingZHostToolPart", "xAxisToolPart", "yAxisToolPart")]
    z, u, v = vectors
    dot = lambda a, b: sum(x * y for x, y in zip(a, b))
    if (any(abs(dot(axis, axis) - 1) > 1e-6 for axis in vectors)
            or abs(dot(u, v)) > 1e-6
            or max(abs(dot(z, u)), abs(dot(z, v))) < 1 - 1e-6):
        raise ValueError("公母插舌的制造壁面与真实方矩管截面不对齐")
    face = host["anchor"]["face"]
    normal_index, across_index = (2, 1) if face in ("top", "bottom") else (1, 2)
    if (abs(z[normal_index]) > 1e-6
            or max(abs(z[0]), abs(z[across_index])) < 1 - 1e-6):
        raise ValueError("公母插舌的制造壁面与主管接触面不对齐")


def _bound_operation_check(process_id, participant, entry, target):
    role, length = participant["role"], participant["length"]
    if target == "end":
        trim = _bound_number(entry.get("trim", 0), f"{role} 实际端切余量", 0, length)
        extent = trim
        if entry["toolRef"]["id"] == "end-key-joint":
            tool = entry["toolParameters"]
            extent += tool["straightDepth"] + tool["width"] / 2
        if extent >= length:
            raise ValueError(f"{role} 的端部加工范围达到或越过真实构件全长")
        return {"id": f"local-bound:{process_id}", "status": "pass", "kind": "end-anchor",
                "memberEntityId": participant["memberEntityId"], "detail": "端部锚点和加工余量位于真实构件长度范围内"}
    count = entry["arrayCount"]
    if (isinstance(count, bool) or not isinstance(count, (int, float)) or not math.isfinite(count)
            or int(count) != count or not 1 <= count <= 1000):
        raise ValueError(f"{role} 的加工阵列数量无效")
    count = entry["arrayCount"] = int(count)
    pitch = _bound_number(entry["arrayPitch"], f"{role} 加工阵列间距", 0, 100000)
    station = _bound_number(entry["station"], f"{role} 加工位置")
    offset = _bound_number(entry["offset"], f"{role} 加工横移")
    rotation = _bound_number(entry["rotation"], f"{role} 加工旋转", -360, 360)
    radius_x = radius_y = 0.0
    tool_id, parameters = entry["toolRef"]["id"], entry["toolParameters"]
    if tool_id == "circle":
        radius_x = radius_y = parameters["diameter"] / 2
    elif tool_id == "slot":
        along, across = parameters["spanAlong"] / 2, parameters["spanAcross"] / 2
        cosine, sine = abs(math.cos(math.radians(rotation))), abs(math.sin(math.radians(rotation)))
        radius_x, radius_y = along * cosine + across * sine, along * sine + across * cosine
    first, last = station, station + (count - 1) * pitch
    if min(first, last) - radius_x < -1e-7 or max(first, last) + radius_x > length + 1e-7:
        raise ValueError(f"{role} 的完整加工包络越过真实构件端部")
    if radius_y:
        transverse = _bound_section_extent(participant["section"],
            "width" if entry["face"] in ("top", "bottom") else "depth")
        if abs(offset) + radius_y > transverse / 2 + 1e-7:
            raise ValueError(f"{role} 的完整加工包络越过实际侧面边界")
    return {"id": f"local-bound:{process_id}", "status": "pass", "kind": "tool-envelope",
            "memberEntityId": participant["memberEntityId"],
            "axialMinimum": min(first, last) - radius_x, "axialMaximum": max(first, last) + radius_x,
            "detail": "显式锚点和已知刀具完整包络位于真实构件局部尺寸范围内"}


def _bound_tool_anchor(process_id, rule, participant, participants):
    """Derive a cutter location without changing the product's connection node."""
    anchor = participant["anchor"]
    relative = rule.get("relativePlacement")
    if relative is None:
        return anchor
    if anchor["kind"] != "end" or anchor["trim"] != 0:
        raise ValueError(f"{process_id} 的端点至侧壁定位需要无余量的成品端点")
    face = anchor.get("approachFace")
    if face not in ("top", "bottom", "left", "right"):
        raise ValueError(f"{process_id} 缺少产品节点声明的朝向侧面，不能猜测加工面")
    inset_rule = relative["inset"]
    partner = participants[inset_rule["sectionRole"]]
    inset = _bound_section_extent(partner["section"], inset_rule["axis"]) * inset_rule["factor"]
    if inset <= 0 or inset >= participant["length"]:
        raise ValueError(f"{process_id} 的支件轴向占位不能落在主件端部以内")
    station = inset if anchor["end"] == "start" else participant["length"] - inset
    return {"kind": "side", "face": face, "reference": "start", "station": station,
            "offset": 0.0, "rotation": anchor["rotation"]}


def _bound_operation(process, tool, resolved_values, rule, participant, participants, values):
    role = participant["role"]
    anchor = _bound_tool_anchor(process["id"], rule, participant, participants)
    target = rule["target"]
    if tool.get("target", "side") != target:
        raise ValueError(f"{process['id']} 的工具目标与真实绑定规则不一致")
    if anchor["kind"] != ("end" if target == "end" else "side"):
        raise ValueError(f"{role} 的实际锚点不适用于 {process['id']} 工具")
    shape_keys = {item["key"] for item in tool.get("parameters", [])}
    operation_keys = {item["key"] for item in tool.get("operationParameters", [])}
    operation = {key: copy.deepcopy(value) for key, value in resolved_values.items() if key in operation_keys}
    entry = {"type": tool["id"], "toolRef": {"id": tool["id"], "version": tool.get("version", "")},
             "toolParameters": {key: copy.deepcopy(value) for key, value in resolved_values.items() if key in shape_keys},
             "operationParameterValues": copy.deepcopy(operation),
             **operation}
    if tool.get("requiresSection"):
        section_role = rule.get("sectionRole")
        if section_role not in participants or section_role == role:
            raise ValueError(f"{process['id']} 缺少其他实际构件的截面输入")
        entry["section"] = {"profile": copy.deepcopy(participants[section_role]["section"])}
    if target == "end":
        entry["trim"] = _bound_number(operation.get("trim", 0), "工艺端部余量", 0, 100000) + anchor["trim"]
        entry["rotation"] = (anchor["rotation"] + _bound_number(operation.get("rotation", 0), "工艺轴向旋转") + 180) % 360 - 180
        entry["datum"] = operation.get("datum", "long")
        if entry["datum"] not in ("long", "center", "short"):
            raise ValueError("端部加工的尺寸基准无效")
    else:
        entry.update({"id": f"assembly:{process['id']}", "enabled": True,
            "face": anchor["face"], "reference": "start",
            "station": anchor["station"] + _resolve(rule.get("stationOffset", 0), values),
            "offset": anchor["offset"],
            "rotation": (anchor["rotation"] + _resolve(rule.get("rotation", 0), values) + 180) % 360 - 180,
            "arrayCount": _resolve(rule.get("arrayCount", 1), values),
            "arrayPitch": _resolve(rule.get("arrayPitch", 0), values),
            "rowCount": 1, "rowPitch": 0, "opposite": bool(rule.get("opposite", False)),
            "blindHole": bool(operation.get("blindHole", False)), "allowOpen": False})
        if entry["blindHole"]:
            entry["cutDepth"] = _bound_number(operation.get("cutDepth"), "定深加工深度", 0.001, 100000)
        if target == "part":
            entry["toolTarget"] = "part"
    check = _bound_operation_check(process["id"], participant, entry, target)
    return entry, check


def resolve_bound_plan(template_id, supplied_values=None, process_drafts=None, participants=None, product_topology=None):
    descriptor = _template_by_id(template_id)
    values = _validated_values(descriptor, supplied_values)
    processes = [process for process in descriptor.get("partProcesses", [])
                  if _condition_matches(process.get("appliesWhen"), values)]
    active_topologies = _active_product_topologies(descriptor.get("productBinding", {}), values)
    reason = _bound_unsupported_reason(descriptor, values)
    if product_topology is not None and (not isinstance(product_topology, str)
            or product_topology not in active_topologies):
        raise ValueError("装配工艺与当前产品连接的成品拓扑不兼容")
    fit_checks = [] if reason else _resolved_product_fit_checks(
        descriptor, values, processes, product_topology)
    connection_only = not reason and "connectionOnlyWhen" in descriptor["productBinding"] and _condition_matches(
        descriptor["productBinding"]["connectionOnlyWhen"], values)
    result = {
        "schema": "icax.assembly-bound-plan", "schemaVersion": 1,
        "templateId": descriptor["id"], "templateVersion": descriptor["version"],
        "templateDigest": _bound_template_digest(descriptor, processes),
        "parameters": values, "compatibleProductTopologies": active_topologies,
        "axisAngle": copy.deepcopy(descriptor.get("productBinding", {}).get("axisAngle", {})),
        "fitChecks": copy.deepcopy(fit_checks),
        "supported": not bool(reason),
        "supportStatus": "unsupported" if reason else "supported",
        "manufacturingEffect": "none" if reason else "connection-only" if connection_only else "incremental-cut",
        "assemblyFitStatus": "not-verified", "parts": [], "checks": [],
    }
    workflow = {
        "schema": "icax.assembly-workflow", "schemaVersion": 1,
        "scope": "product-members", "realization": descriptor["manufacturingPlan"]["realization"],
        "blankParts": [], "partOperations": [],
        "assemblySteps": _resolved_steps(descriptor, values),
        "bom": _resolved_bom(descriptor, values), "assemblyFitStatus": "not-verified",
        "fitChecks": copy.deepcopy(fit_checks),
    }
    workflow["bomStatus"] = "declared" if workflow["bom"] else "none-for-configuration" if descriptor.get("bomRules") else "not-declared"
    if reason:
        result["checks"] = [{"id": "binding-unsupported", "kind": "capability", "status": "unsupported",
                             "blocking": True, "detail": reason}]
        workflow["validationStatus"] = "unsupported"
        result["summary"] = reason
    else:
        actual = _bound_participants(descriptor, participants)
        for fit_check in fit_checks:
            if fit_check["kind"] == "end-side-flat-contact":
                _validate_flat_contact_fit(fit_check, actual, values)
            elif fit_check["kind"] == "end-side-tab-slot":
                _validate_tab_slot_wall_mapping(fit_check, actual)
        if connection_only and any(part["anchor"]["kind"] == "end" and part["anchor"]["trim"] != 0
                                   for part in actual.values()):
            raise ValueError("仅连接分支的端部锚点余量必须为零，不能隐含单件切削")
        rules = descriptor["productBinding"]["processes"]
        groups = {role: {key: part[key] for key in ("role", "memberEntityId", "itemKey")}
                  | {"features": [], "ends": {}} for role, part in actual.items()}
        scripted_operations = None
        if descriptor["productBinding"].get("operationScript"):
            module = _load_assembly_script(template_id)
            builder = getattr(module, "build_bound_operations", None)
            if not callable(builder):
                raise ValueError("实际装配脚本必须提供 build_bound_operations(context)")
            context = {"parameters": copy.deepcopy(values), "participants": copy.deepcopy(actual),
                       "fitChecks": copy.deepcopy(fit_checks), "processes": []}
            for process in processes:
                tool = _selected_process_descriptor(process, values)
                context["processes"].append({"id": process["id"], "role": process["participants"][0],
                    "tool": copy.deepcopy(tool), "values": _bound_process_values(process, tool, values,
                        process_drafts or {}, rules[process["id"]], actual)})
            before = copy.deepcopy(context)
            scripted = builder(context)
            if not _same_input(context, before):
                raise ValueError("实际装配脚本不得修改宿主输入")
            if (not isinstance(scripted, dict) or set(scripted) != {"parameters", "operations", "nodeContacts"}
                    or not _same_input(scripted["parameters"], values)
                    or not isinstance(scripted["operations"], list)
                    or len(scripted["operations"]) != len(processes)):
                raise ValueError("实际装配脚本必须完整返回原参数及全部启用工序")
            scripted_operations = {}
            for operation in scripted["operations"]:
                if (not isinstance(operation, dict) or set(operation) - {"processId", "role", "values", "anchor", "sectionFrame"}
                        or operation.get("processId") in scripted_operations):
                    raise ValueError("实际装配脚本返回了无效或重复工序")
                scripted_operations[operation["processId"]] = operation
            if set(scripted_operations) != {process["id"] for process in processes}:
                raise ValueError("实际装配脚本引用了未启用工序")
            contacts = scripted["nodeContacts"]
            if not isinstance(contacts, list) or not 1 <= len(contacts) <= 6:
                raise ValueError("实际装配脚本必须声明节点接触关系")
            pairs = set()
            for contact in contacts:
                if (not isinstance(contact, dict) or set(contact) != {"roles", "maximumSeparationMm"}
                        or not isinstance(contact["roles"], list) or len(contact["roles"]) != 2
                        or len(set(contact["roles"])) != 2 or any(role not in actual for role in contact["roles"])):
                    raise ValueError("实际装配脚本的节点接触角色无效")
                pair = tuple(sorted(contact["roles"]))
                if pair in pairs:
                    raise ValueError("实际装配脚本的节点接触关系重复")
                pairs.add(pair)
                _bound_number(contact["maximumSeparationMm"], "节点最大接触间隙", 0, 10.05)
            result["nodeContacts"] = copy.deepcopy(contacts)
        for process in processes:
            if len(process["participants"]) != 1 or process["id"] not in rules:
                raise ValueError(f"{process['id']} 尚未声明单个真实构件的加工规则")
            role, rule = process["participants"][0], rules[process["id"]]
            tool = _selected_process_descriptor(process, values)
            operation_actual, operation_participant = actual, actual[role]
            if scripted_operations is not None:
                operation = scripted_operations[process["id"]]
                if operation.get("role") != role or not isinstance(operation.get("values"), dict):
                    raise ValueError("实际装配脚本改变了加工构件身份")
                resolved_values = operation["values"]
                definitions = {item["key"]: item for item in _parameter_definitions(tool)}
                if set(resolved_values) != set(definitions):
                    raise ValueError("实际装配脚本必须返回完整工具参数")
                for key, definition in definitions.items():
                    value = resolved_values[key]
                    if definition.get("valueType") in ("number", "integer"):
                        _bound_number(value, f"{process['id']}.{key}", definition.get("min", -math.inf), definition.get("max", math.inf))
                        if definition["valueType"] == "integer" and int(value) != value:
                            raise ValueError("实际装配工具整数参数无效")
                    elif definition.get("valueType") == "boolean" and not isinstance(value, bool):
                        raise ValueError("实际装配工具开关参数无效")
                    elif definition.get("options") and value not in [item["value"] for item in definition["options"]]:
                        raise ValueError("实际装配工具参数不属于允许选项")
                operation_actual = copy.deepcopy(actual)
                operation_participant = operation_actual[role]
                anchor = operation.get("anchor")
                expected = "end" if rule["target"] == "end" else "side"
                allowed = {"kind", "end", "rotation", "trim"} if expected == "end" else {
                    "kind", "face", "reference", "station", "offset", "rotation"}
                if not isinstance(anchor, dict) or set(anchor) != allowed or anchor.get("kind") != expected:
                    raise ValueError("实际装配脚本的刀具定位无效")
                if expected == "end":
                    if anchor["end"] != actual[role]["anchor"]["end"]:
                        raise ValueError("实际装配脚本不能更改真实端点")
                    _bound_number(anchor["trim"], "脚本锚点余量", 0, actual[role]["length"])
                else:
                    if anchor["face"] not in ("top", "bottom", "left", "right") or anchor["reference"] != "start":
                        raise ValueError("实际装配脚本的加工侧面无效")
                    _bound_number(anchor["station"], "脚本加工位置", 0, actual[role]["length"])
                    _bound_number(anchor["offset"], "脚本加工横移", -100000, 100000)
                _bound_number(anchor["rotation"], "脚本锚点旋转", -360, 360)
                operation_participant["anchor"] = copy.deepcopy(anchor)
                if "sectionFrame" in operation:
                    section_role = rule.get("sectionRole")
                    if section_role not in operation_actual or section_role == role or not tool.get("requiresSection"):
                        raise ValueError("实际装配脚本提供了未声明的配合截面")
                    frame = operation["sectionFrame"]
                    if not isinstance(frame, dict) or frame.get("verification") != "committed-source-brep-replay":
                        raise ValueError("实际装配脚本的配合截面坐标未经真实原管验证")
                    json.dumps(frame, allow_nan=False)
                    operation_actual[section_role]["section"]["sectionFrame"] = copy.deepcopy(frame)
            else:
                resolved_values = _bound_process_values(process, tool, values, process_drafts or {}, rule, actual)
            operation_rule = {key: value for key, value in rule.items() if key != "relativePlacement"} if scripted_operations is not None else rule
            entry, check = _bound_operation(process, tool, resolved_values, operation_rule, operation_participant, operation_actual, values)
            if connection_only:
                if (rule["target"] != "end" or tool["id"] != "end-miter"
                        or _bound_number(entry["toolParameters"].get("angle"), "仅连接端切角度") != 0
                        or entry["trim"] != 0):
                    raise ValueError(f"{process['id']} 在仅连接分支必须是零角度、零余量的端面接触，不能产生单件切削")
                check["detail"] = "真实构件端部锚点有效；原长端面接触无需新增切削"
                result["checks"].append(check)
                continue
            group, anchor = groups[role], operation_participant["anchor"]
            if rule["target"] == "end":
                if anchor["end"] in group["ends"]:
                    raise ValueError(f"{role} 的同一端部不能同时绑定多个端切工序")
                group["ends"][anchor["end"]] = entry
            else:
                group["features"].append(entry)
            result["checks"].append(check)
            placement = {"target": rule["target"]}
            if rule["target"] == "end":
                placement.update({"end": anchor["end"], **{key: entry[key] for key in ("trim", "rotation", "datum")}})
            else:
                placement.update({key: entry[key] for key in ("face", "reference", "station", "offset",
                    "rotation", "arrayCount", "arrayPitch", "rowCount", "rowPitch", "opposite")})
            operation_keys = {definition["key"] for definition in tool.get("operationParameters", [])}
            workflow["partOperations"].append({
                "id": f"{role}:{process['id']}", "processId": process["id"],
                "role": role, "memberEntityId": actual[role]["memberEntityId"], "itemKey": actual[role]["itemKey"],
                "label": process.get("label", process["id"]), "resourceRef": copy.deepcopy(entry["toolRef"]),
                "toolParameters": copy.deepcopy(entry["toolParameters"]),
                "operationParameters": {key: copy.deepcopy(entry[key]) for key in operation_keys if key in entry},
                "placement": placement, "anchor": copy.deepcopy(anchor),
            })
        result["parts"] = [] if connection_only else [group for group in groups.values() if group["features"] or group["ends"]]
        if not connection_only and not result["parts"]:
            raise ValueError("当前方案没有可应用的单件加工操作，不能假称工艺已应用")
        for blank in descriptor["manufacturingPlan"]["blankParts"]:
            if len(blank["participants"]) != 1:
                raise ValueError("真实构件增量加工不能归并下料件")
            role = blank["participants"][0]
            workflow["blankParts"].append({"id": blank["id"], "label": blank["label"],
                "participantRoles": [role], "sourceRole": role,
                "memberEntityId": actual[role]["memberEntityId"], "itemKey": actual[role]["itemKey"]})
        result["checks"].append({
            "id": "native-connection-check" if connection_only else "native-material-check",
            "kind": "actual-shape", "status": "requires-native-validation", "blocking": True,
            "detail": "须在现有制造实体上核对真实构件身份和端部锚点" if connection_only
                else "须在现有制造实体上检查刀具接触、既有缺料重叠和加工后实体有效性，再冻结增量刀具",
        })
        result["checks"].append({
            "id": "assembly-fit", "kind": "assembly-fit", "status": "not-verified", "blocking": False,
            "detail": "实际构件装配位置和端面接触仍待核对" if connection_only
                else "当前仅解析既定位置上的单件加工；实际构件装配位置、孔系对位和配合仍待核对",
        })
        workflow["validationStatus"] = "host-validation-required"
        result["summary"] = ("已解析真实构件端面接触；待原生构件校验后记录连接，装配位置待核对"
            if connection_only else "已解析真实构件单件加工；待原生实体校验后应用，装配位置待核对")
    workflow["checks"] = copy.deepcopy(result["checks"])
    result["workflow"] = workflow
    return result


def validate_existing_bindings(bindings):
    """Recheck saved template identity and finished topology without rebuilding tools."""
    if not isinstance(bindings, list) or len(bindings) > 64:
        raise ValueError("已保存装配工艺清单无效")
    checks = []
    for binding in bindings:
        if not isinstance(binding, dict):
            raise ValueError("已保存装配工艺记录无效")
        binding_id = binding.get("bindingId")
        if not isinstance(binding_id, str) or not binding_id:
            raise ValueError("已保存装配工艺缺少身份")
        detail = ""
        try:
            descriptor = _template_by_id(binding.get("templateId"))
            values = _validated_values(descriptor, binding.get("parameters"))
            processes = [process for process in descriptor.get("partProcesses", [])
                         if _condition_matches(process.get("appliesWhen"), values)]
            topology = binding.get("productTopology")
            if topology not in _active_product_topologies(descriptor.get("productBinding", {}), values):
                detail = "成品连接形状与当前工艺模板不兼容"
            elif (not isinstance(binding.get("templateDigest"), str)
                  or binding["templateDigest"] != _bound_template_digest(descriptor, processes)):
                detail = "装配工艺模板已更新，需要重新检查"
            else:
                detail = _bound_unsupported_reason(descriptor, values)
        except (ValueError, OSError, KeyError, TypeError) as error:
            detail = f"装配工艺模板无法重新验证：{error}"
        checks.append({"bindingId": binding_id, "current": not bool(detail), "detail": detail})
    return {"checks": checks}


def _generation_file_digest(paths, root):
    digest = hashlib.sha256()
    for path in sorted(set(paths)):
        if path.is_file() and not path.is_symlink():
            digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0")
            digest.update(path.read_bytes())
    return digest.hexdigest()


def _assembly_generation_digest(directory, descriptor, shared_digest):
    """Public cache identity includes executable dependencies, not only a version."""
    resources = set()
    for process in descriptor.get("partProcesses", []):
        if isinstance(process.get("resource"), dict):
            resources.add(process["resource"]["id"])
        resources.update(item["id"] for item in
                         process.get("resourceSelection", {}).get("resources", []))
    files = list(directory.glob("*.py")) + list(directory.glob("*.json"))
    for resource_id in resources:
        package = PROCESS_ROOT / resource_id
        files.extend(path for path in package.rglob("*") if path.is_file()
                     and "__pycache__" not in path.parts and path.suffix not in (".pyc", ".pyo"))
    digest = hashlib.sha256(shared_digest.encode("ascii"))
    digest.update(_generation_file_digest(files, ROOT.parent).encode("ascii"))
    return digest.hexdigest()


def catalogue():
    templates, errors = [], []
    if not ROOT.is_dir():
        return {"assemblies": [], "errors": ["装配模板目录不存在"]}
    shared_digest = _generation_file_digest(
        list((ROOT.parent / "_shared").glob("*.py"))
        + list((ROOT.parent / "finished-product").glob("*.json")), ROOT.parent)
    for manifest in sorted(ROOT.glob("*/assembly.json")):
        try:
            raw = manifest.read_bytes()
            if len(raw) > MAX_BYTES:
                raise ValueError("装配模板超过 4 MB")
            descriptor = json.loads(raw)
            validated = _validate_template(descriptor, manifest.parent)
            if not validated.get("catalogueHidden", False):
                public = copy.deepcopy(validated)
                public["previewScene"].pop("designParts", None)
                public["parameters"] = [p for p in public["parameters"] if p.get("scope") != "scene"]
                public["generationDigest"] = _assembly_generation_digest(manifest.parent, validated, shared_digest)
                templates.append(public)
        except (ValueError, OSError, json.JSONDecodeError, KeyError, TypeError) as error:
            errors.append(f"{manifest.parent.name}: {error}")
    return {"assemblies": templates, "finishedShapes": finished_products.catalogue(), "errors": errors}


def generate(parameters, _context):
    if parameters.get("action") == "catalogue":
        return catalogue()
    if parameters.get("action") == "validate-existing-bindings":
        return validate_existing_bindings(parameters.get("bindings"))
    if parameters.get("action") == "check-applicability":
        return check_applicability(parameters.get("templateId"), parameters.get("finishedProduct"),
                                   parameters.get("parameters"), parameters.get("processInput"))
    if parameters.get("action") == "evaluate-process":
        return evaluate_process(parameters.get("templateId"), parameters.get("processInput"),
                                parameters.get("parameters"), parameters.get("processDrafts"), _context)
    if parameters.get("action") == "process-plan":
        return resolve_process_plan(parameters.get("stocks"), parameters.get("instances"),
                                    parameters.get("nativeProfileDependencies"), _context)
    if parameters.get("action") == "example-product":
        return get_example_product(parameters.get("templateId"), parameters.get("parameters"))
    if parameters.get("action") == "preview-plan":
        manufacturing_only = parameters.get("manufacturingOnly", False)
        if type(manufacturing_only) is not bool:
            raise ValueError("manufacturingOnly 必须是布尔值")
        if manufacturing_only and parameters.get("finishedOnly") is True:
            raise ValueError("成品与下料预览模式不能同时启用")
        if parameters.get("finishedOnly") is True:
            return finished_product_plan(parameters.get("finishedProduct"))
        return preview_plan(
            parameters.get("templateId"),
            parameters.get("parameters"),
            parameters.get("processDrafts"),
            parameters.get("sceneParts"),
            parameters.get("sceneParameters"),
            parameters.get("finishedProduct"),
            manufacturing_only,
            parameters.get("processInput"),
        )
    if parameters.get("action") == "resolve-bound-plan":
        return resolve_bound_plan(
            parameters.get("templateId"), parameters.get("parameters"),
            parameters.get("processDrafts"), parameters.get("participants"),
            parameters.get("productTopology"),
        )
    raise ValueError("未知装配模板运行时操作")
