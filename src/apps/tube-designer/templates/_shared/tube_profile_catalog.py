from __future__ import annotations

from dataclasses import dataclass, field
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
from typing import Any


PROFILE_SCHEMA = "icax.tube-profile"
PROFILE_SCHEMA_VERSION = 2
DESCRIPTOR_SCHEMA = "icax.tube-profile-descriptor"
DESCRIPTOR_SCHEMA_VERSION = 2
PROFILE_ROOT = Path(__file__).resolve().parent / "profiles"


def _swap_contour_axes(contour: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(contour)
    kind = result.get("kind")
    if kind in ("roundedRectangle", "ellipse", "capsule"):
        result["width"], result["height"] = result["height"], result["width"]
        return result
    if kind == "polygon":
        result["points"] = [[point[1], point[0]] for point in result.get("points", [])]
        return result
    if kind != "path":
        return result
    for segment in result.get("segments", []):
        for key in ("start", "middle", "end", "center"):
            point = segment.get(key)
            if isinstance(point, list) and len(point) == 2:
                segment[key] = [point[1], point[0]]
        points = segment.get("controlPoints")
        if isinstance(points, list):
            segment["controlPoints"] = [[point[1], point[0]] for point in points]
    return result


def _number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} 必须是数值")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{name} 必须是有限数值")
    return result


def _localized_text(value: Any) -> str:
    if isinstance(value, str) and value:
        return value
    if not isinstance(value, dict):
        raise ValueError("管型 displayName 必须是本地化文本")
    for locale in ("zh-CN", "zh", "en-US", "en"):
        text = value.get(locale)
        if isinstance(text, str) and text:
            return text
    for text in value.values():
        if isinstance(text, str) and text:
            return text
    raise ValueError("管型 displayName 不能为空")


def _parameter_values(
    descriptor: dict[str, Any], parameters: dict[str, Any], prefix: str,
    source_overrides: dict[str, str] | None = None,
) -> dict[str, Any]:
    definitions = descriptor.get("parameters")
    if not isinstance(definitions, list) or not definitions:
        raise ValueError(f"管型包 {descriptor['id']} 必须声明 parameters")
    result: dict[str, Any] = {}
    for index, definition in enumerate(definitions):
        if not isinstance(definition, dict):
            raise ValueError(f"管型包 {descriptor['id']} 的 parameters[{index}] 必须是对象")
        key = definition.get("key")
        if not isinstance(key, str) or re.fullmatch(r"[a-z][A-Za-z0-9]*", key) is None:
            raise ValueError(f"管型包 {descriptor['id']} 的 parameters[{index}].key 无效")
        value_type = definition.get("valueType")
        if value_type not in ("number", "integer", "string", "boolean"):
            raise ValueError(f"管型包 {descriptor['id']} 的参数 {key} 类型无效")
        source_key = (source_overrides or {}).get(
            key, prefix + key[0].upper() + key[1:])
        if source_key in parameters:
            value = parameters[source_key]
        elif "defaultValue" in definition:
            value = definition["defaultValue"]
        else:
            raise ValueError(f"缺少管型参数：{source_key}")
        if value_type == "number":
            value = _number(value, source_key)
        elif value_type == "integer":
            value = _number(value, source_key)
            if not value.is_integer():
                raise ValueError(f"{source_key} 必须是整数")
            value = int(value)
        elif value_type == "string":
            if not isinstance(value, str):
                raise ValueError(f"{source_key} 必须是字符串")
        elif not isinstance(value, bool):
            raise ValueError(f"{source_key} 必须是布尔值")
        result[key] = value
    return result


def _load_package(profile_id: str) -> tuple[dict[str, Any], Any]:
    if re.fullmatch(r"[a-z][a-z0-9_-]*", profile_id) is None:
        raise ValueError(f"管型 ID 无效：{profile_id}")
    package_root = PROFILE_ROOT / profile_id
    descriptor_path = package_root / "profile.json"
    script_path = package_root / "profile.py"
    if not descriptor_path.is_file() or not script_path.is_file():
        raise ValueError(f"管型包不存在或不完整：{profile_id}")
    descriptor_bytes = descriptor_path.read_bytes()
    script_bytes = script_path.read_bytes()
    descriptor = json.loads(descriptor_bytes.decode("utf-8"))
    if descriptor.get("schema") != DESCRIPTOR_SCHEMA:
        raise ValueError(f"管型包 {profile_id} 的 schema 不受支持")
    if descriptor.get("schemaVersion") != DESCRIPTOR_SCHEMA_VERSION:
        raise ValueError(f"管型包 {profile_id} 的 schemaVersion 不受支持")
    if descriptor.get("id") != profile_id:
        raise ValueError(f"管型包目录与 id 不一致：{profile_id}")
    digest = hashlib.sha256(descriptor_bytes + b"\0" + script_bytes).hexdigest()[:16]
    module_name = f"icax_tube_profile_{profile_id}_{digest}"
    module = sys.modules.get(module_name)
    if module is None:
        spec = importlib.util.spec_from_file_location(module_name, script_path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"无法加载管型脚本：{script_path}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
    if not callable(getattr(module, "build", None)):
        raise ValueError(f"管型包 {profile_id} 缺少 build(parameters)")
    if not callable(getattr(module, "contours", None)):
        raise ValueError(f"管型包 {profile_id} 缺少 contours(profile, ...)")
    return descriptor, module


@dataclass(frozen=True)
class Profile:
    profile_id: str
    package_version: str
    kind: str
    display_name: str
    specification: str
    width: float
    depth: float
    wall: float
    radius: float
    _profile_data: dict[str, Any] = field(repr=False, compare=False)
    _module: Any = field(repr=False, compare=False)
    _fixed_contours: list[dict[str, Any]] | None = field(
        default=None, repr=False, compare=False,
    )
    _source_file_name: str = field(default="", repr=False, compare=False)
    _content_digest: str = field(default="", repr=False, compare=False)

    def contours(
        self, *, clearance: float = 0.0, swap_axes: bool = False,
    ) -> list[dict[str, Any]]:
        _number(clearance, "clearance")
        if self._fixed_contours is not None:
            result = copy.deepcopy(self._fixed_contours)
            if swap_axes:
                result = [_swap_contour_axes(contour) for contour in result]
        else:
            result = self._module.contours(
                dict(self._profile_data),
                clearance=float(clearance),
                swap_axes=bool(swap_axes),
            )
        if not isinstance(result, list) or not result:
            raise ValueError(f"管型 {self.profile_id} 必须返回非空二维轮廓数组")
        for index, contour in enumerate(result):
            if not isinstance(contour, dict) or not isinstance(contour.get("kind"), str):
                raise ValueError(f"管型 {self.profile_id} 返回的轮廓 {index} 无效")
        return result

    @property
    def hollow(self) -> bool:
        return len(self.contours()) > 1

    def properties(self) -> dict[str, Any]:
        result = {
            "schema": PROFILE_SCHEMA,
            "schemaVersion": PROFILE_SCHEMA_VERSION,
            "id": self.profile_id,
            "packageVersion": self.package_version,
            "kind": self.kind,
            "width": self.width,
            "depth": self.depth,
            "wallThickness": self.wall,
            "cornerRadius": self.radius,
            "displayName": self.display_name,
            "specification": self.specification,
            "hollow": self.hollow,
            # Manufacturing members persist the exact section definitions on
            # their tube-profile component so list/grouping and downstream
            # editors do not need to re-evaluate the profile package.
            "contours": self.contours(),
        }
        # A parametric polygon's side/point count and star mode are part of its
        # manufacturing section identity. They are also needed to decide whether
        # a 180-degree nesting orientation maps the stock section onto itself.
        if self.profile_id == "polygon":
            result["parameters"] = {
                key: copy.deepcopy(self._profile_data[key])
                for key in ("shapeMode", "sideCount", "starInnerRatio")
                if key in self._profile_data
            }
        if self.kind in ("imported-dxf", "parametric-package"):
            result["sourceFormat"] = (
                "cad.dxf" if self.kind == "imported-dxf" else "icax.profile-package"
            )
            result["sourceFileName"] = self._source_file_name
            result["contentDigest"] = self._content_digest
            result["frozenGeometry"] = self.kind == "imported-dxf"
            result["editableParameters"] = self.kind == "parametric-package"
        return result


def _imported_profile(parameters: dict[str, Any], prefix: str) -> Profile | None:
    overrides = parameters.get("tubeDesignerProfileOverrides")
    if overrides is None:
        return None
    if not isinstance(overrides, dict):
        raise ValueError("tubeDesignerProfileOverrides 必须是对象")
    definition = overrides.get(prefix)
    if definition is None:
        return None
    if not isinstance(definition, dict):
        raise ValueError(f"导入管型 {prefix} 必须是对象")
    kind = definition.get("kind")
    if (definition.get("schema") != "icax.imported-tube-profile"
            or definition.get("schemaVersion") != 1
            or kind not in ("imported-dxf", "parametric-package")):
        raise ValueError(f"导入管型 {prefix} 的协议不受支持")
    contours = definition.get("contours")
    if not isinstance(contours, list) or not contours:
        raise ValueError(f"导入管型 {prefix} 缺少二维轮廓")
    for index, contour in enumerate(contours):
        if not isinstance(contour, dict) or not isinstance(contour.get("kind"), str):
            raise ValueError(f"导入管型 {prefix} 的轮廓 {index} 无效")
    digest = str(definition.get("contentDigest", ""))
    display_name = str(definition.get("name", "")).strip() or (
        "导入 DXF 管型" if kind == "imported-dxf" else "可编辑管型包"
    )
    return Profile(
        profile_id=digest or f"{kind}:{prefix}",
        package_version=str(definition.get("packageVersion", "1")),
        kind=str(kind),
        display_name=display_name,
        specification=str(definition.get("specification", "DXF")),
        width=_number(definition.get("width"), f"{prefix}.width"),
        depth=_number(definition.get("depth"), f"{prefix}.depth"),
        wall=_number(definition.get("wallThickness", 0.0), f"{prefix}.wallThickness"),
        radius=0.0,
        _profile_data=dict(definition),
        _module=None,
        _fixed_contours=copy.deepcopy(contours),
        _source_file_name=str(definition.get("sourceFileName", "")),
        _content_digest=digest,
    )


def load_profile(
    parameters: dict[str, Any],
    prefix: str,
    *,
    profile_id: str | None = None,
    depth_key: str | None = None,
) -> Profile:
    imported = _imported_profile(parameters, prefix)
    if imported is not None:
        return imported
    selected_id = profile_id or str(parameters[f"{prefix}ProfileType"])
    descriptor, module = _load_package(selected_id)
    profile_parameters = _parameter_values(
        descriptor, parameters, prefix,
        {"depth": depth_key} if depth_key else None,
    )
    built = module.build(profile_parameters)
    if not isinstance(built, dict):
        raise ValueError(f"管型 {selected_id} 的 build 返回值必须是对象")
    return Profile(
        profile_id=selected_id,
        package_version=str(descriptor.get("version", "")),
        kind=str(built.get("kind", selected_id)),
        display_name=_localized_text(descriptor.get("displayName")),
        specification=str(built.get("specification", "")),
        width=_number(built.get("width"), f"{selected_id}.width"),
        depth=_number(built.get("depth"), f"{selected_id}.depth"),
        wall=_number(built.get("wallThickness"), f"{selected_id}.wallThickness"),
        radius=_number(built.get("cornerRadius", 0.0), f"{selected_id}.cornerRadius"),
        _profile_data=dict(built),
        _module=module,
    )
