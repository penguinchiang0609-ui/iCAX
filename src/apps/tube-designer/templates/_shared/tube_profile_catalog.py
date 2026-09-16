from __future__ import annotations

from dataclasses import dataclass, field
from functools import lru_cache
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
DESCRIPTOR_SCHEMA_VERSION = 3
PROFILE_ROOT = Path(__file__).resolve().parent.parent / "profile"


def _offset_outer_contour(contour, distance):
    """Public-layer machining envelope; no template callback or parameter mutation.

    Analytic offsets for convex line/circle-arc boundaries. Other boundaries
    must use a geometry-kernel offset rather than a silently approximated shape.
    Inner contours are deliberately unchanged by this clearance operation.
    """
    c = copy.deepcopy(contour)
    kind = c["kind"]
    if kind == "circle":
        c["radius"] += distance
        if c["radius"] <= 0: raise ValueError("间隙偏置使圆半径失效")
        return c
    if kind in ("roundedRectangle", "capsule"):
        c["width"] += 2*distance; c["height"] += 2*distance
        if min(c["width"],c["height"]) <= 0: raise ValueError("间隙偏置使截面失效")
        if kind == "roundedRectangle":
            # A sharp corner uses a miter; rounded corners remain concentric.
            c["radius"] = c.get("radius",0)+distance if c.get("radius",0)>0 else 0
            if c["radius"]<0: raise ValueError("间隙偏置超过圆角半径")
        return c
    if kind == "polygon":
        points=c["points"]
        edges=[{"kind":"line","start":a,"end":b} for a,b in zip(points,points[1:]+points[:1])]
    elif kind == "path": edges=c["segments"]
    else: raise ValueError("该轮廓的加工间隙需要几何内核偏置，不由模板重新计算")
    points=[]
    for e in edges:
        if e["kind"] not in ("line","arc"): raise ValueError("此曲线的间隙需要几何内核偏置")
        points.append(e["start"])
        if e["kind"]=="arc": points.append(e["middle"])
    area=sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(points,points[1:]+points[:1]))
    if abs(area)<1e-12: raise ValueError("间隙轮廓退化")
    sign=1 if area>0 else -1
    for i,b in enumerate(points):
        a=points[i-1];d=points[(i+1)%len(points)]
        if sign*((b[0]-a[0])*(d[1]-b[1])-(b[1]-a[1])*(d[0]-b[0])) < -1e-8:
            raise ValueError("非凸轮廓的间隙需要几何内核偏置")
    shifted=[]
    for e in edges:
        n=copy.deepcopy(e);a=e["start"];b=e["end"]
        if e["kind"]=="line":
            dx=b[0]-a[0];dy=b[1]-a[1];length=math.hypot(dx,dy)
            if length<=1e-12: raise ValueError("间隙轮廓含退化边")
            delta=[sign*distance*dy/length,-sign*distance*dx/length]
            for key in ("start","end"): n[key]=[e[key][i]+delta[i] for i in (0,1)]
        else:
            m=e["middle"];x,y=m[0]-a[0],m[1]-a[1];u,v=b[0]-a[0],b[1]-a[1]
            den=2*(x*v-y*u)
            if abs(den)<1e-12: raise ValueError("间隙轮廓含退化圆弧")
            center=[a[0]+(v*(x*x+y*y)-y*(u*u+v*v))/den,a[1]+(x*(u*u+v*v)-u*(x*x+y*y))/den]
            radius=math.dist(a,center);new_radius=radius+distance
            if new_radius<=0: raise ValueError("间隙偏置超过圆弧半径")
            for key in ("start","middle","end"):
                n[key]=[center[i]+(e[key][i]-center[i])*new_radius/radius for i in (0,1)]
        shifted.append(n)
    for i,right in enumerate(shifted):
        left=shifted[i-1]
        if left["kind"]==right["kind"]=="line":
            a=left["start"];b=right["start"]
            u=[left["end"][j]-a[j] for j in (0,1)];v=[right["end"][j]-b[j] for j in (0,1)]
            den=u[0]*v[1]-u[1]*v[0]
            if abs(den)>1e-12:
                t=((b[0]-a[0])*v[1]-(b[1]-a[1])*v[0])/den
                left["end"]=right["start"]=[a[j]+t*u[j] for j in (0,1)]
        if math.dist(left["end"],right["start"])>1e-7:
            raise ValueError("非相切曲线接头的间隙需要几何内核偏置")
    return {"kind":"path","closed":True,"segments":shifted}


def _swap_contour_axes(contour: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(contour)
    kind = result.get("kind")
    if "center" in result:
        result["center"] = list(reversed(result["center"]))
    if kind in ("roundedRectangle", "ellipse", "capsule"):
        if "width" in result and "height" in result:
            result["width"], result["height"] = result["height"], result["width"]
        if kind == "ellipse":
            if "radiusX" in result and "radiusY" in result:
                result["radiusX"],result["radiusY"] = result["radiusY"],result["radiusX"]
            result["rotation"] = -result.get("rotation",0)
        return result
    if kind == "polygon":
        result["points"] = [[point[1], point[0]] for point in result.get("points", [])]
        return result
    if kind != "path":
        return result
    for segment in result.get("segments", []):
        if segment.get("kind") == "ellipseArc":
            # Reflection exchanges axes and reverses the angular parameter.
            segment["rotation"] = math.pi / 2 - segment.get("rotation", 0.0)
            segment["startAngle"], segment["endAngle"] = -segment["startAngle"], -segment["endAngle"]
        for key in ("start", "middle", "end", "center"):
            point = segment.get(key)
            if isinstance(point, list) and len(point) == 2:
                segment[key] = [point[1], point[0]]
        for key in ("controlPoints", "poles"):
            points = segment.get(key)
            if isinstance(points, list):
                segment[key] = [[point[1], point[0]] for point in points]
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
    if not isinstance(definitions, list) or (not definitions and descriptor.get("profileForm") != "fixed"):
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


def _definition_runtime():
    path = Path(__file__).with_name("profile_package_runtime.py")
    spec = importlib.util.spec_from_file_location("icax_profile_definition_runtime", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@lru_cache(maxsize=64)
def _load_package_from_root(
    profile_id: str, profile_root: str,
) -> tuple[dict[str, Any], Any]:
    from types import SimpleNamespace
    if re.fullmatch(r"[a-z][a-z0-9_-]*", profile_id) is None:
        raise ValueError(f"管型 ID 无效：{profile_id}")
    package_root = Path(profile_root) / profile_id
    runtime = _definition_runtime()
    package = runtime._system_package(package_root, preview=False)
    descriptor = package["descriptor"]
    script = package["scriptSource"]
    digest = package["packageDigest"]
    def build(values):
        parameters = runtime._normalize_parameters(descriptor, values)
        built, contours = runtime.evaluate_section(descriptor, script, parameters, digest, package.get("resources"))
        return {**built, "_resolvedContours": contours,
                **{key:copy.deepcopy(descriptor[key]) for key in ("provenance","manufacturing") if key in descriptor}}
    def contours(profile, *, clearance=0.0, swap_axes=False):
        if not clearance:
            result = copy.deepcopy(profile["_resolvedContours"])
            return [_swap_contour_axes(c) for c in result] if swap_axes else result
        result = copy.deepcopy(profile["_resolvedContours"])
        result[0] = _offset_outer_contour(result[0], clearance)
        return [_swap_contour_axes(c) for c in result] if swap_axes else result
    return descriptor, SimpleNamespace(build=build, contours=contours)


def _load_package(profile_id: str) -> tuple[dict[str, Any], Any]:
    # System profile packages are immutable for the lifetime of one template
    # host.  Loading one used to re-read, compile and execute its package for
    # every role reference in the same product (26 times for the default
    # security window).  Include the root in the key so isolated test/user
    # catalogues cannot share definitions accidentally.
    return _load_package_from_root(profile_id, str(PROFILE_ROOT.resolve()))


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
            if clearance:
                result[0] = _offset_outer_contour(result[0], clearance)
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
        resource_kind = (str(self._profile_data.get("kind", "fixed-section"))
                         if self._fixed_contours is not None else "built-in")
        result = {
            "schema": PROFILE_SCHEMA,
            "schemaVersion": PROFILE_SCHEMA_VERSION,
            "id": self.profile_id,
            "packageVersion": self.package_version,
            "kind": self.kind,
            "resourceKind": resource_kind,
            "profileForm": self._profile_data["profileForm"],
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
        for key in ("provenance", "manufacturing", "geometrySource", "sourceRevision", "manufacturingRoute", "sectionModel"):
            if key in self._profile_data:
                result[key] = copy.deepcopy(self._profile_data[key])
        # A parametric polygon's side/point count and star mode are part of its
        # manufacturing section identity. They are also needed to decide whether
        # a 180-degree nesting orientation maps the stock section onto itself.
        if self.profile_id == "polygon":
            result["parameters"] = {
                key: copy.deepcopy(self._profile_data[key])
                for key in ("shapeMode", "sideCount", "starInnerRatio")
                if key in self._profile_data
            }
        if self._fixed_contours is not None:
            result["sourceFormat"] = (
                "cad.dxf" if resource_kind == "fixed-section" else "icax.profile-package"
            )
            result["sourceFileName"] = self._source_file_name
            result["contentDigest"] = self._content_digest
            result["frozenGeometry"] = result["profileForm"] == "fixed"
            result["editableParameters"] = result["profileForm"] == "parametric"
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
    resource_kind = definition.get("kind")
    if (definition.get("schema") != "icax.imported-tube-profile"
            or definition.get("schemaVersion") != 1
            or resource_kind not in ("fixed-section", "profile-package")):
        raise ValueError(f"导入管型 {prefix} 的协议不受支持")
    if definition.get("profileForm") not in ("parametric", "fixed"):
        raise ValueError("管型缺少明确的 profileForm，请先迁移数据")
    contours = definition.get("contours")
    if not isinstance(contours, list) or not contours:
        raise ValueError(f"导入管型 {prefix} 缺少二维轮廓")
    for index, contour in enumerate(contours):
        if not isinstance(contour, dict) or not isinstance(contour.get("kind"), str):
            raise ValueError(f"导入管型 {prefix} 的轮廓 {index} 无效")
    digest = str(definition.get("contentDigest", ""))
    display_name = str(definition.get("name", "")).strip() or (
        "导入 DXF 管型" if resource_kind == "fixed-section" else "可编辑管型包"
    )
    return Profile(
        profile_id=digest or f"{resource_kind}:{prefix}",
        package_version=str(definition.get("packageVersion", "1")),
        # Resource kind (fixed/package) and geometric section kind are
        # orthogonal.  Geometry and node rules must only inspect sectionKind.
        kind=str(definition.get("sectionKind", "arbitrary")),
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


@lru_cache(maxsize=512)
def _built_system_profile(profile_id: str, encoded_parameters: str) -> Profile:
    descriptor, module = _load_package(profile_id)
    profile_parameters = json.loads(encoded_parameters)
    built = module.build(profile_parameters)
    if not isinstance(built, dict):
        raise ValueError(f"管型 {profile_id} 的 build 返回值必须是对象")
    return Profile(
        profile_id=profile_id,
        package_version=str(descriptor.get("version", "")),
        kind=str(built.get("kind", profile_id)),
        display_name=_localized_text(descriptor.get("displayName")),
        specification=str(built.get("specification", "")),
        width=_number(built.get("width"), f"{profile_id}.width"),
        depth=_number(built.get("depth"), f"{profile_id}.depth"),
        wall=_number(built.get("wallThickness", 0.0), f"{profile_id}.wallThickness"),
        radius=_number(built.get("cornerRadius", 0.0), f"{profile_id}.cornerRadius"),
        _profile_data={**built, "profileForm": descriptor["profileForm"]},
        _module=module,
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
    descriptor, _ = _load_package(selected_id)
    profile_parameters = _parameter_values(
        descriptor, parameters, prefix,
        {"depth": depth_key} if depth_key else None,
    )
    return _built_system_profile(
        selected_id,
        json.dumps(
            profile_parameters, ensure_ascii=False, allow_nan=False,
            sort_keys=True, separators=(",", ":"),
        ),
    )
