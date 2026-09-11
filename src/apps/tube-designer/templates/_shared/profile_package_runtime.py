from __future__ import annotations

import ast
import base64
import hashlib
import json
import math
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
from typing import Any
import zipfile


PACKAGE_SCHEMA = "icax.tube-profile-package-record"
PACKAGE_SCHEMA_VERSION = 1
DESCRIPTOR_SCHEMA = "icax.tube-profile-descriptor"
DESCRIPTOR_SCHEMA_VERSION = 2
MAX_ARCHIVE_BYTES = 8 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_TOTAL_BYTES = 8 * 1024 * 1024
REQUIRED_MEMBERS = ("profile.json", "profile.py")
PASSWORD = "ICAX_TUBE_DESIGNER"
MAX_DIAGRAM_ANNOTATIONS = 128
MAX_DIAGRAM_COORDINATE = 1.0e9


def _diagram_expression(value: Any, numeric_keys: set[str], label: str) -> ast.AST:
    """Accept a small arithmetic language, never Python evaluation or attributes.

    Coordinates use XY section millimetres. Expressions may reference declared
    number/integer parameters, unary +/- and binary + - * /, min/max (1..8
    operands), abs/sqrt/sin/cos (one operand; angles in radians). contourX/Y(c,v)
    read actual polygon vertices after evaluation; both indices must be
    non-negative integers in bounds. No indexing, attributes, comprehensions,
    exponentiation, variables from build(), or arbitrary function calls exist.
    """
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        _finite_number(value, label)
        return ast.Constant(value=value)
    if not isinstance(value, str) or not value.strip() or len(value) > 256:
        raise ValueError(f"{label} 必须是数值或不超过 256 字符的坐标表达式")
    try:
        expression = ast.parse(value, mode="eval").body
    except (SyntaxError, ValueError, RecursionError) as error:
        raise ValueError(f"{label} 坐标表达式无效") from error
    if sum(1 for _ in ast.walk(expression)) > 64:
        raise ValueError(f"{label} 坐标表达式过于复杂")

    def validate(node: ast.AST, depth: int = 0) -> None:
        if depth > 16:
            raise ValueError(f"{label} 坐标表达式嵌套过深")
        if isinstance(node, ast.Constant):
            _finite_number(node.value, label)
        elif isinstance(node, ast.Name) and node.id in numeric_keys:
            return
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            validate(node.operand, depth + 1)
        elif isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult, ast.Div)):
            validate(node.left, depth + 1)
            validate(node.right, depth + 1)
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and not node.keywords:
            name = node.func.id
            valid_count = (
                name in ("min", "max") and 1 <= len(node.args) <= 8
                or name in ("abs", "sqrt", "sin", "cos") and len(node.args) == 1
                or name in ("contourX", "contourY") and len(node.args) == 2
            )
            if not valid_count:
                raise ValueError(f"{label} 包含不支持的函数或参数数量")
            for argument in node.args:
                validate(argument, depth + 1)
        else:
            raise ValueError(f"{label} 只能引用已声明的数值参数和安全算术函数")

    validate(expression)
    return expression


def _validate_parameter_diagram(descriptor: dict[str, Any]) -> None:
    diagram = descriptor.get("parameterDiagram")
    if diagram is None:
        return
    if (not isinstance(diagram, dict) or type(diagram.get("schemaVersion")) is not int
            or diagram["schemaVersion"] != 1):
        raise ValueError("parameterDiagram.schemaVersion 必须为 1")
    annotations = diagram.get("annotations")
    if not isinstance(annotations, list) or not 1 <= len(annotations) <= MAX_DIAGRAM_ANNOTATIONS:
        raise ValueError("parameterDiagram 必须包含 1 到 128 个 annotations")
    keys = {definition["key"] for definition in descriptor["parameters"]}
    numeric_keys = {definition["key"] for definition in descriptor["parameters"]
                    if definition["valueType"] in ("number", "integer")}
    for index, annotation in enumerate(annotations):
        label = f"parameterDiagram.annotations[{index}]"
        if (not isinstance(annotation, dict) or not isinstance(annotation.get("parameter"), str)
                or annotation["parameter"] not in keys):
            raise ValueError(f"{label}.parameter 必须引用已声明的管型参数")
        kind = annotation.get("kind")
        if kind not in ("linear", "leader"):
            raise ValueError(f"{label}.kind 必须为 linear 或 leader")
        if annotation.get("side") not in ("top", "bottom", "left", "right"):
            raise ValueError(f"{label}.side 必须为 top、bottom、left 或 right")
        if kind == "linear":
            axis = annotation.get("axis")
            if axis not in ("x", "y"):
                raise ValueError(f"{label}.axis 必须为 x 或 y")
            if (axis == "x") != (annotation["side"] in ("top", "bottom")):
                raise ValueError(f"{label}.side 与尺寸轴向不一致")
            if annotation["parameter"] not in numeric_keys:
                raise ValueError(f"{label} 线性尺寸必须引用数值参数")
        if "description" in annotation:
            _localized_text(annotation["description"], f"{label}.description")
        for field in (("from", "to") if kind == "linear" else ("point",)):
            point = annotation.get(field)
            if not isinstance(point, list) or len(point) != 2:
                raise ValueError(f"{label}.{field} 必须为二维坐标")
            for coordinate in point:
                _diagram_expression(coordinate, numeric_keys, f"{label}.{field}")


def _evaluate_parameter_diagram(
    descriptor: dict[str, Any], parameters: dict[str, Any], contours: list[dict[str, Any]],
) -> dict[str, Any] | None:
    diagram = descriptor.get("parameterDiagram")
    if diagram is None:
        return None
    numeric_keys = {definition["key"] for definition in descriptor["parameters"]
                    if definition["valueType"] in ("number", "integer")}

    def evaluate(node: ast.AST) -> float:
        if isinstance(node, ast.Constant):
            result = float(node.value)
        elif isinstance(node, ast.Name):
            result = float(parameters[node.id])
        elif isinstance(node, ast.UnaryOp):
            result = evaluate(node.operand) * (-1 if isinstance(node.op, ast.USub) else 1)
        elif isinstance(node, ast.BinOp):
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.Add):
                result = left + right
            elif isinstance(node.op, ast.Sub):
                result = left - right
            elif isinstance(node.op, ast.Mult):
                result = left * right
            else:
                result = left / right
        else:
            assert isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
            name = node.func.id
            arguments = [evaluate(argument) for argument in node.args]
            if name in ("contourX", "contourY"):
                contour_index, vertex_index = arguments
                if (not contour_index.is_integer() or not vertex_index.is_integer()
                        or not 0 <= contour_index < len(contours)):
                    raise ValueError("轮廓或顶点索引无效")
                contour = contours[int(contour_index)]
                points = contour.get("points")
                if (contour.get("kind") != "polygon" or not isinstance(points, list)
                        or not 0 <= vertex_index < len(points)):
                    raise ValueError("坐标锚点必须引用有效多边形轮廓顶点")
                point = points[int(vertex_index)]
                if not isinstance(point, (list, tuple)) or len(point) != 2:
                    raise ValueError("轮廓顶点必须为二维坐标")
                result = _finite_number(point[0 if name == "contourX" else 1], "轮廓顶点")
            else:
                functions = {"min": min, "max": max, "abs": abs, "sqrt": math.sqrt,
                             "sin": math.sin, "cos": math.cos}
                # min/max take an iterable so a single permitted argument is valid.
                result = functions[name](arguments) if name in ("min", "max") else functions[name](*arguments)
        if not math.isfinite(result) or abs(result) > MAX_DIAGRAM_COORDINATE:
            raise ValueError("坐标必须为绝对值不超过 10 亿的有限数值")
        return result

    annotations = []
    for index, source in enumerate(diagram["annotations"]):
        annotation = {field: source[field] for field in ("parameter", "kind", "side")}
        if source["kind"] == "linear":
            annotation["axis"] = source["axis"]
        if "description" in source:
            annotation["description"] = source["description"]
        for field in (("from", "to") if source["kind"] == "linear" else ("point",)):
            label = f"parameterDiagram.annotations[{index}].{field}"
            try:
                annotation[field] = [evaluate(_diagram_expression(value, numeric_keys, label))
                                     for value in source[field]]
            except (ArithmeticError, ValueError, TypeError) as error:
                raise ValueError(f"{label} 坐标求值失败：{error}") from error
        annotations.append(annotation)
    return {"schemaVersion": 1, "annotations": annotations}


def _localized_text(value: Any, name: str) -> str:
    if isinstance(value, str) and value.strip():
        return value.strip()
    if isinstance(value, dict):
        for locale in ("zh-CN", "zh", "en-US", "en"):
            text = value.get(locale)
            if isinstance(text, str) and text.strip():
                return text.strip()
        for text in value.values():
            if isinstance(text, str) and text.strip():
                return text.strip()
    raise ValueError(f"{name} 不能为空")


def _finite_number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} 必须是数值")
    try:
        result = float(value)
    except OverflowError as error:
        raise ValueError(f"{name} 必须是有限数值") from error
    if not math.isfinite(result):
        raise ValueError(f"{name} 必须是有限数值")
    return result


def _normalize_value(definition: dict[str, Any], value: Any) -> Any:
    key = str(definition["key"])
    value_type = definition["valueType"]
    if value_type == "number":
        result: Any = _finite_number(value, key)
    elif value_type == "integer":
        number = _finite_number(value, key)
        if not number.is_integer():
            raise ValueError(f"{key} 必须是整数")
        result = int(number)
    elif value_type == "string":
        if not isinstance(value, str):
            raise ValueError(f"{key} 必须是文本")
        result = value
    else:
        if not isinstance(value, bool):
            raise ValueError(f"{key} 必须是布尔值")
        result = value

    if value_type in ("number", "integer"):
        minimum = definition.get("min", definition.get("minimum"))
        maximum = definition.get("max", definition.get("maximum"))
        if minimum is not None and result < _finite_number(minimum, f"{key}.min"):
            raise ValueError(f"{key} 小于允许的最小值")
        if maximum is not None and result > _finite_number(maximum, f"{key}.max"):
            raise ValueError(f"{key} 大于允许的最大值")
    options = definition.get("options")
    if isinstance(options, list) and options:
        allowed = [item.get("value") if isinstance(item, dict) else item for item in options]
        if result not in allowed:
            raise ValueError(f"{key} 不在允许选项中")
    return result


def _validate_descriptor(value: Any) -> tuple[dict[str, Any], dict[str, Any]]:
    if not isinstance(value, dict):
        raise ValueError("profile.json 必须是 JSON 对象")
    if value.get("schema") != DESCRIPTOR_SCHEMA:
        raise ValueError("profile.json schema 不受支持")
    if value.get("schemaVersion") != DESCRIPTOR_SCHEMA_VERSION:
        raise ValueError("profile.json schemaVersion 不受支持")
    profile_id = value.get("id")
    if not isinstance(profile_id, str) or re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", profile_id) is None:
        raise ValueError("profile.json id 无效")
    version = value.get("version")
    if not isinstance(version, str) or not version.strip() or len(version) > 64:
        raise ValueError("profile.json version 无效")
    _localized_text(value.get("displayName"), "profile.json displayName")
    definitions = value.get("parameters")
    if not isinstance(definitions, list) or not 1 <= len(definitions) <= 64:
        raise ValueError("profile.json 必须声明 1 到 64 个 parameters")

    keys: set[str] = set()
    defaults: dict[str, Any] = {}
    for index, definition in enumerate(definitions):
        if not isinstance(definition, dict):
            raise ValueError(f"parameters[{index}] 必须是对象")
        key = definition.get("key")
        if not isinstance(key, str) or re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", key) is None:
            raise ValueError(f"parameters[{index}].key 无效")
        if key in keys:
            raise ValueError(f"管型参数重复：{key}")
        keys.add(key)
        if definition.get("valueType") not in ("number", "integer", "string", "boolean"):
            raise ValueError(f"管型参数 {key} 的 valueType 无效")
        _localized_text(definition.get("displayName"), f"管型参数 {key} 的 displayName")
        if "defaultValue" not in definition:
            raise ValueError(f"可编辑管型参数 {key} 必须声明 defaultValue")
        defaults[key] = _normalize_value(definition, definition["defaultValue"])
    _validate_parameter_diagram(value)
    return dict(value), defaults


def _normalize_parameters(
    descriptor: dict[str, Any], values: Any,
) -> dict[str, Any]:
    if not isinstance(values, dict):
        raise ValueError("管型参数必须是对象")
    result: dict[str, Any] = {}
    for definition in descriptor["parameters"]:
        key = definition["key"]
        source = values[key] if key in values else definition["defaultValue"]
        result[key] = _normalize_value(definition, source)
    unknown = sorted(set(values) - set(result))
    if unknown:
        raise ValueError(f"管型包含未知参数：{', '.join(unknown[:5])}")
    return result


def _load_script(script_source: str, digest: str) -> dict[str, Any]:
    if not isinstance(script_source, str) or not script_source.strip():
        raise ValueError("profile.py 不能为空")
    if len(script_source.encode("utf-8")) > MAX_MEMBER_BYTES:
        raise ValueError("profile.py 超过大小限制")
    namespace: dict[str, Any] = {
        "__name__": f"icax_user_profile_{digest[:20]}",
        "__file__": "<icaxprofile>/profile.py",
        "__package__": None,
    }
    exec(compile(script_source, namespace["__file__"], "exec"), namespace)
    if not callable(namespace.get("build")):
        raise ValueError("profile.py 缺少 build(parameters)")
    if not callable(namespace.get("contours")):
        raise ValueError("profile.py 缺少 contours(profile, clearance, swap_axes)")
    return namespace


def _evaluate(
    descriptor: dict[str, Any], script_source: str, values: Any,
    package_digest: str, source_file_name: str,
) -> dict[str, Any]:
    descriptor, _ = _validate_descriptor(descriptor)
    parameters = _normalize_parameters(descriptor, values)
    namespace = _load_script(script_source, package_digest)
    built = namespace["build"](dict(parameters))
    if not isinstance(built, dict):
        raise ValueError("profile.py 的 build 必须返回对象")
    contours = namespace["contours"](
        dict(built), clearance=0.0, swap_axes=False,
    )
    if not isinstance(contours, list) or not contours or len(contours) > 1000:
        raise ValueError("profile.py 必须返回非空轮廓数组")
    for index, contour in enumerate(contours):
        if not isinstance(contour, dict) or not isinstance(contour.get("kind"), str):
            raise ValueError(f"profile.py 返回的轮廓 {index} 无效")

    width = _finite_number(built.get("width"), "build.width")
    depth = _finite_number(built.get("depth"), "build.depth")
    wall = _finite_number(built.get("wallThickness", 0.0), "build.wallThickness")
    radius = _finite_number(built.get("cornerRadius", 0.0), "build.cornerRadius")
    if width <= 1.0e-6 or depth <= 1.0e-6 or wall < 0 or radius < 0:
        raise ValueError("profile.py 返回的截面尺寸无效")
    name = _localized_text(descriptor.get("displayName"), "displayName")
    result = {
        "schema": "icax.imported-tube-profile",
        "schemaVersion": 1,
        "kind": "parametric-package",
        "name": name,
        "sourceFileName": source_file_name,
        "sourceFormat": "icax.profile-package",
        "profilePackageId": descriptor["id"],
        "packageVersion": descriptor["version"],
        "packageDigest": package_digest,
        "width": width,
        "depth": depth,
        "wallThickness": wall,
        "cornerRadius": radius,
        "specification": str(built.get("specification", f"{width:g} × {depth:g} mm")),
        "hollow": len(contours) > 1,
        "contourCount": len(contours),
        "contours": contours,
        "parameters": parameters,
        "parameterDefinitions": descriptor["parameters"],
        "editableParameters": True,
        "frozenGeometry": False,
        "contentDigest": package_digest,
    }
    diagram = _evaluate_parameter_diagram(descriptor, parameters, contours)
    if diagram is not None:
        result["parameterDiagram"] = diagram
    json.dumps(result, ensure_ascii=False, allow_nan=False)
    return result


def _member_name(info: zipfile.ZipInfo) -> str:
    normalized = info.filename.replace("\\", "/")
    path = PurePosixPath(normalized)
    if path.is_absolute() or ":" in normalized or ".." in path.parts:
        raise ValueError("管型包包含不安全的文件路径")
    return "/".join(part for part in path.parts if part not in ("", "."))


def _read_archive(path: Path, password: str = "") -> tuple[bytes, bytes, bool, dict[str, str]]:
    if path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("管型包超过 8 MB")
    try:
        archive = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as error:
        raise ValueError("管型包不是有效 ZIP 文件") from error
    with archive:
        members: dict[str, zipfile.ZipInfo] = {}
        total_size = 0
        for info in archive.infolist():
            name = _member_name(info)
            if not name or info.is_dir():
                continue
            if name in members:
                raise ValueError(f"管型包文件重复：{name}")
            if info.file_size > MAX_MEMBER_BYTES:
                raise ValueError(f"管型包文件超过 4 MB：{name}")
            total_size += info.file_size
            if total_size > MAX_TOTAL_BYTES:
                raise ValueError("管型包解压后超过 8 MB")
            members[name] = info
        missing = [name for name in REQUIRED_MEMBERS if name not in members]
        if missing:
            raise ValueError(f"管型包缺少：{', '.join(missing)}")
        encrypted = all(info.flag_bits & 0x1 for info in members.values())
        if not encrypted:
            raise ValueError("ittt 必须使用产品固定密码保护")
        password_bytes = (password or PASSWORD).encode("utf-8")
        try:
            descriptor_bytes = archive.read(members["profile.json"], pwd=password_bytes)
            script_bytes = archive.read(members["profile.py"], pwd=password_bytes)
            resources = {
                name: base64.b64encode(archive.read(info, pwd=password_bytes)).decode("ascii")
                for name, info in members.items() if name not in REQUIRED_MEMBERS
            }
        except (RuntimeError, zipfile.BadZipFile) as error:
            raise ValueError("管型包密码错误或加密格式不受支持") from error
    return descriptor_bytes, script_bytes, encrypted, resources


def _package_from_sources(
    descriptor_bytes: bytes, script_bytes: bytes, source_file_name: str,
    encrypted: bool = False, resources: dict[str, str] | None = None,
) -> dict[str, Any]:
    try:
        descriptor = json.loads(descriptor_bytes.decode("utf-8-sig"))
        script_source = script_bytes.decode("utf-8-sig")
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("profile.json 或 profile.py 不是有效 UTF-8 内容") from error
    descriptor, defaults = _validate_descriptor(descriptor)
    canonical_descriptor = json.dumps(
        descriptor, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
    ).encode("utf-8")
    digest = "sha256:" + hashlib.sha256(
        canonical_descriptor + b"\0" + script_source.encode("utf-8"),
    ).hexdigest()
    preview = _evaluate(descriptor, script_source, defaults, digest, source_file_name)
    return {
        "schema": PACKAGE_SCHEMA,
        "schemaVersion": PACKAGE_SCHEMA_VERSION,
        "kind": "parametric-package",
        "name": _localized_text(descriptor.get("displayName"), "displayName"),
        "sourceFileName": source_file_name,
        "sourceFormat": "icax.profile-package",
        "passwordProtected": encrypted,
        "packageDigest": digest,
        "descriptor": descriptor,
        "scriptSource": script_source,
        "resources": dict(resources or {}),
        "defaultParameters": defaults,
        "previewProfile": preview,
    }


def _inspect(path: Path, password: str) -> dict[str, Any]:
    descriptor_bytes, script_bytes, encrypted, resources = _read_archive(path, password)
    return _package_from_sources(descriptor_bytes, script_bytes, path.name, encrypted, resources)


def _template_identifier(value: Any, label: str, *, template: bool = False) -> str:
    pattern = r"[A-Za-z0-9_][A-Za-z0-9_.:-]{0,239}" if template else r"[a-z][a-z0-9_-]{0,79}"
    if not isinstance(value, str) or re.fullmatch(pattern, value) is None:
        raise ValueError(f"{label} 必须是稳定标识，不能是文件路径")
    return value


def _template_root(value: Any) -> Path:
    if not isinstance(value, str) or not value.strip() or "\0" in value:
        raise ValueError("模板管型目录无效")
    root = Path(value).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("模板管型目录必须是文件夹")
    return root


def _confined_profile_path(root: Path, relative: Any) -> Path:
    if not isinstance(relative, str) or not relative or "\0" in relative:
        raise ValueError("模板管型必须声明包内相对路径")
    path = PurePosixPath(relative)
    if (path.is_absolute() or PureWindowsPath(relative).drive or "\\" in relative
            or ":" in relative or ".." in path.parts):
        raise ValueError("模板管型路径不得越过模板包目录")
    target = (root / relative).resolve(strict=True)
    if not target.is_relative_to(root):
        raise ValueError("模板管型路径不得通过符号链接或目录联接越过模板包目录")
    return target


def _read_template_member(root: Path, directory: Path, name: str) -> bytes:
    relative = (directory.relative_to(root) / name).as_posix()
    member = _confined_profile_path(root, relative)
    if not member.is_file():
        raise ValueError(f"模板管型缺少文件：{name}")
    if member.stat().st_size > MAX_MEMBER_BYTES:
        raise ValueError(f"模板管型文件超过 4 MB：{name}")
    with member.open("rb") as stream:
        content = stream.read(MAX_MEMBER_BYTES + 1)
    if len(content) > MAX_MEMBER_BYTES:
        raise ValueError(f"模板管型文件超过 4 MB：{name}")
    return content


def _template_identity(parameters: dict[str, Any], profile_id: Any = None) -> dict[str, Any]:
    reference = parameters.get("profileRef", {})
    if not isinstance(reference, dict):
        raise ValueError("模板管型 profileRef 必须是对象")
    template_id = _template_identifier(
        parameters.get("templateId", reference.get("templateId")), "模板 ID", template=True,
    )
    definition_id = _template_identifier(
        profile_id if profile_id is not None else parameters.get(
            "profileDefinitionId", parameters.get("id", reference.get("id")),
        ), "模板管型资源 ID",
    )
    expected_reference = {"scope": "template", "templateId": template_id, "id": definition_id}
    if reference and reference != expected_reference:
        raise ValueError("模板管型 profileRef 与模板和资源 ID 不一致")
    for key in ("profileScope", "libraryScope"):
        if key in parameters and parameters[key] != "template":
            raise ValueError("模板管型不能使用系统或用户作用域")
    return {
        "profileScope": "template",
        "libraryScope": "template",
        "templateId": template_id,
        "templateName": _localized_text(parameters.get("templateName", template_id), "模板名称"),
        "profileDefinitionId": definition_id,
        "profileRef": expected_reference,
        "sourceFormat": "icax.template-profile",
    }


def _stamp_template_profile(
    profile: dict[str, Any], identity: dict[str, Any], metadata: dict[str, Any],
) -> dict[str, Any]:
    profile.update(identity)
    for key in ("name", "category"):
        if key in metadata:
            profile[key] = _localized_text(metadata[key], f"模板管型 {key}")
    profile.pop("savedProfileId", None)
    return profile


def _template_package(parameters: dict[str, Any], profile_id: Any) -> dict[str, Any]:
    identity = _template_identity(parameters, profile_id)
    resources = parameters.get("profileResources")
    if not isinstance(resources, dict):
        raise ValueError("模板 profileResources 必须是声明对象")
    key = identity["profileDefinitionId"]
    if key not in resources:
        raise ValueError(f"模板未声明此管型资源：{key}")
    declaration = resources[key]
    if not isinstance(declaration, dict):
        raise ValueError(f"模板管型资源声明无效：{key}")
    root = _template_root(parameters.get("templateDirectory"))
    source = _confined_profile_path(root, declaration.get("path"))
    if source.is_dir():
        descriptor_bytes = _read_template_member(root, source, "profile.json")
        script_bytes = _read_template_member(root, source, "profile.py")
        if len(descriptor_bytes) + len(script_bytes) > MAX_TOTAL_BYTES:
            raise ValueError("模板管型包超过 8 MB")
        source_name = (source.relative_to(root) / "profile.py").as_posix()
        package = _package_from_sources(descriptor_bytes, script_bytes, source_name)
    elif source.is_file() and source.suffix.lower() == ".ittt":
        package = _inspect(source, "")
        package["sourceFileName"] = source.relative_to(root).as_posix()
        package["previewProfile"]["sourceFileName"] = package["sourceFileName"]
    else:
        raise ValueError("模板管型资源必须是含 profile.json/profile.py 的目录或 .ittt 包")
    package.update(identity)
    package["id"] = key
    for field in ("name", "category"):
        if field in declaration:
            package[field] = _localized_text(declaration[field], f"模板管型 {field}")
        elif field in package["descriptor"]:
            package[field] = _localized_text(package["descriptor"][field], f"模板管型 {field}")
    _stamp_template_profile(package["previewProfile"], identity, package)
    package.pop("savedProfileId", None)
    return package


def _list_template_packages(templates: Any) -> list[dict[str, Any]]:
    if not isinstance(templates, list):
        raise ValueError("模板管型列表必须提供 templates 数组")
    packages: list[dict[str, Any]] = []
    seen_templates: set[str] = set()
    for template in templates:
        if not isinstance(template, dict):
            raise ValueError("模板管型列表中的模板必须是对象")
        template_id = _template_identifier(template.get("templateId"), "模板 ID", template=True)
        if template_id in seen_templates:
            raise ValueError(f"模板管型列表包含重复模板 ID：{template_id}")
        seen_templates.add(template_id)
        resources = template.get("profileResources", {})
        if not isinstance(resources, dict):
            raise ValueError("模板 profileResources 必须是声明对象")
        for key in resources:
            _template_identifier(key, "模板管型资源 ID")
        for key in sorted(resources):
            packages.append(_template_package(template, key))
    return packages


def _system_profile_root(value: Any) -> Path:
    if value is None:
        root = (Path(__file__).resolve().parent.parent / "profile").resolve()
    elif isinstance(value, str) and value.strip():
        root = Path(value).expanduser().resolve()
    else:
        raise ValueError("系统管型目录无效")
    if not root.is_dir():
        raise FileNotFoundError(f"系统管型目录不存在：{root}")
    return root


def _system_package(directory: Path, profile_scope: str = "system") -> dict[str, Any]:
    if profile_scope not in ("system", "user"):
        raise ValueError("管型包来源无效")
    descriptor_path = directory / "profile.json"
    script_path = directory / "profile.py"
    if not descriptor_path.is_file() or not script_path.is_file():
        raise ValueError(f"系统管型包不完整：{directory.name}")
    descriptor_bytes = descriptor_path.read_bytes()
    script_bytes = script_path.read_bytes()
    if len(descriptor_bytes) > MAX_MEMBER_BYTES or len(script_bytes) > MAX_MEMBER_BYTES:
        raise ValueError(f"系统管型包文件超过 4 MB：{directory.name}")
    try:
        descriptor_source = json.loads(descriptor_bytes.decode("utf-8-sig"))
        script_source = script_bytes.decode("utf-8-sig")
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(
            f"系统管型 {directory.name} 的 profile.json 或 profile.py 不是有效 UTF-8 内容"
        ) from error
    descriptor, defaults = _validate_descriptor(descriptor_source)
    if descriptor["id"] != directory.name:
        raise ValueError(f"系统管型包目录与 id 不一致：{directory.name}")
    canonical_descriptor = json.dumps(
        descriptor, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
    ).encode("utf-8")
    digest = "sha256:" + hashlib.sha256(
        canonical_descriptor + b"\0" + script_source.encode("utf-8"),
    ).hexdigest()
    source_file_name = f"{directory.name}/profile.py"
    preview = _evaluate(
        descriptor, script_source, defaults, digest, source_file_name,
    )
    source_format = "icax.system-profile" if profile_scope == "system" else "icax.user-profile"
    preview.update({
        "profileScope": profile_scope,
        "profileDefinitionId": descriptor["id"],
        "sourceFormat": source_format,
    })
    return {
        "schema": PACKAGE_SCHEMA,
        "schemaVersion": PACKAGE_SCHEMA_VERSION,
        "kind": "parametric-package",
        "name": _localized_text(descriptor.get("displayName"), "displayName"),
        "sourceFileName": source_file_name,
        "sourceFormat": source_format,
        "passwordProtected": False,
        "packageDigest": digest,
        "descriptor": descriptor,
        "scriptSource": script_source,
        "defaultParameters": defaults,
        "previewProfile": preview,
    }


def _find_system_package(root: Path, profile_id: Any, profile_scope: str = "system") -> dict[str, Any]:
    if not isinstance(profile_id, str) or re.fullmatch(
        r"[a-z][a-z0-9_-]{0,79}", profile_id,
    ) is None:
        raise ValueError("系统管型 ID 无效")
    directory = root / profile_id
    if not directory.is_dir():
        raise ValueError(f"系统管型不存在：{profile_id}")
    return _system_package(directory, profile_scope)


def _list_system_packages(root: Path, profile_scope: str = "system") -> list[dict[str, Any]]:
    packages: list[dict[str, Any]] = []
    for directory in sorted(root.iterdir(), key=lambda item: item.name):
        if (not directory.is_dir()
                or re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", directory.name) is None
                or not (directory / "profile.json").is_file()
                or not (directory / "profile.py").is_file()):
            continue
        packages.append(_system_package(directory, profile_scope))
    return packages


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    del context
    action = str(parameters.get("action", "inspect"))
    if action == "inspect":
        source_path = parameters.get("sourcePath")
        if not isinstance(source_path, str) or not source_path.strip():
            raise ValueError("缺少管型包路径")
        path = Path(source_path).expanduser().resolve()
        if path.suffix.lower() != ".ittt":
            raise ValueError("请选择 .ittt 管型包")
        if not path.is_file():
            raise FileNotFoundError(f"管型包不存在：{path}")
        return {"package": _inspect(path, PASSWORD)}
    if action == "evaluate":
        descriptor = parameters.get("descriptor")
        script_source = parameters.get("scriptSource")
        package_digest = str(parameters.get("packageDigest", ""))
        source_file_name = str(parameters.get("sourceFileName", "管型包.ittt"))
        profile = _evaluate(
            descriptor,
            script_source,
            parameters.get("values", {}),
            package_digest,
            source_file_name,
        )
        reference = parameters.get("profileRef")
        if (parameters.get("profileScope") == "template" or parameters.get("libraryScope") == "template"
                or isinstance(reference, dict) and reference.get("scope") == "template"):
            # Frozen package evaluation uses only the descriptor/script supplied
            # by the committed package; never reopen its original template path.
            _stamp_template_profile(profile, _template_identity(parameters), parameters)
        return {"profile": profile}
    if action == "list-template":
        return {"templateProfiles": _list_template_packages(parameters.get("templates"))}
    if action == "evaluate-template":
        package = _template_package(parameters, parameters.get("profileId"))
        profile = _evaluate(
            package["descriptor"], package["scriptSource"], parameters.get("values", {}),
            package["packageDigest"], package["sourceFileName"],
        )
        _stamp_template_profile(profile, _template_identity(package), package)
        return {"profile": profile}
    if action == "list-system":
        root = _system_profile_root(parameters.get("profileRoot"))
        return {"systemProfiles": _list_system_packages(root)}
    if action == "evaluate-system":
        root = _system_profile_root(parameters.get("profileRoot"))
        package = _find_system_package(root, parameters.get("systemProfileId"))
        descriptor = package["descriptor"]
        profile = _evaluate(
            descriptor,
            package["scriptSource"],
            parameters.get("values", {}),
            package["packageDigest"],
            package["sourceFileName"],
        )
        profile.update({
            "profileScope": "system",
            "profileDefinitionId": descriptor["id"],
            "sourceFormat": "icax.system-profile",
        })
        return {"profile": profile}
    if action == "list-user":
        root = _system_profile_root(parameters.get("profileRoot"))
        return {"userProfiles": _list_system_packages(root, "user")}
    if action == "evaluate-user":
        root = _system_profile_root(parameters.get("profileRoot"))
        package = _find_system_package(root, parameters.get("userProfileId"), "user")
        profile = _evaluate(
            package["descriptor"],
            package["scriptSource"],
            parameters.get("values", {}),
            package["packageDigest"],
            package["sourceFileName"],
        )
        profile.update({
            "profileScope": "user",
            "profileDefinitionId": package["descriptor"]["id"],
            "sourceFormat": "icax.user-profile",
        })
        return {"profile": profile}
    raise ValueError(f"不支持的管型包操作：{action}")
