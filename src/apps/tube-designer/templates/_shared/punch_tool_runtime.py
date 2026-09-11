"""Product-level scripts; drawing-level immutable, code-free cutter instances."""
from __future__ import annotations
import copy
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent / "mold"
SCHEMA = "icax.punch-tool"
MAX_BYTES = 4 * 1024 * 1024


def _json_bytes(value):
    # JSON/JS/C++ round-trips may encode 10.0 as 10; geometry identity must not
    # depend on that spelling (including negative zero).
    def canonical(item):
        if isinstance(item, float) and math.isfinite(item) and item.is_integer():
            return int(item)
        if isinstance(item, dict):
            return {key: canonical(child) for key, child in item.items()}
        if isinstance(item, list):
            return [canonical(child) for child in item]
        return item
    return json.dumps(canonical(value), ensure_ascii=False, sort_keys=True, allow_nan=False).encode("utf-8")


def _number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} 必须是有限数值")
    return value


def _parameters(descriptor, supplied):
    if not isinstance(supplied, dict):
        raise ValueError("刀具参数须为对象")
    result = {}
    for item in descriptor.get("parameters", []):
        key = item["key"]
        value = supplied.get(key, item["defaultValue"])
        kind = item["valueType"]
        if kind in ("number", "integer"):
            _number(value, key)
            if kind == "integer" and int(value) != value:
                raise ValueError(f"{key} 须为整数")
            if value < item.get("min", -math.inf) or value > item.get("max", math.inf):
                raise ValueError(f"{key} 超出允许范围")
        elif kind == "boolean":
            if not isinstance(value, bool):
                raise ValueError(f"{key} 须为开关")
        elif kind == "string":
            if not isinstance(value, str):
                raise ValueError(f"{key} 须为文本")
        else:
            raise ValueError("不支持的刀具参数类型")
        if item.get("options") and value not in [o["value"] for o in item["options"]]:
            raise ValueError(f"{key} 不是可用选项")
        result[key] = value
    if set(supplied) - set(result):
        raise ValueError("刀具包含未声明的参数")
    return result


def _package(directory, package_root=None):
    # No symlink escapes, optional code paths, or code supplied by the project.
    directory = directory.resolve()
    package_root = Path(ROOT if package_root is None else package_root).resolve()
    if directory.parent != package_root:
        raise ValueError("刀具目录越界")
    manifest = directory / "tool.json"
    try:
        raw = manifest.read_bytes()
    except FileNotFoundError as error:
        raise ValueError(f"本机缺少刀具 {directory.name}。已保存的零件实体不受影响；请安装对应刀具或明确选择替换后再计算") from error
    descriptor = json.loads(raw)
    if descriptor.get("schema") != SCHEMA or descriptor.get("schemaVersion") != 1:
        raise ValueError(f"{directory.name}: 刀具模板格式不受支持")
    if descriptor.get("id") != directory.name or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", directory.name):
        raise ValueError("刀具 id 须与目录名一致")
    if not isinstance(descriptor.get("version"), str) or not descriptor["version"]:
        raise ValueError("刀具须有版本号")
    if descriptor.get("kind") not in ("programmatic", "fixed") or descriptor.get("target") not in ("side", "end", "part"):
        raise ValueError("刀具种类或适用位置无效")
    if not isinstance(descriptor.get("displayName"), str) or not descriptor["displayName"]:
        raise ValueError("刀具须有名称")
    definitions = descriptor.get("parameters", [])
    if not isinstance(definitions, list) or len(definitions) > 64:
        raise ValueError("刀具参数最多 64 项")
    keys = [item.get("key") for item in definitions]
    if any(not isinstance(k, str) or not re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", k) for k in keys) or len(set(keys)) != len(keys):
        raise ValueError("刀具参数键无效或重复")
    if any(item.get("placement") is True for item in definitions):
        raise ValueError("刀具定义不能声明位置或姿态参数；请由使用记录保存")
    if descriptor["kind"] == "fixed" and definitions:
        raise ValueError("定式刀具不能声明可变形状参数")
    defaults = _parameters(descriptor, {})
    member = directory / ("tool.py" if descriptor["kind"] == "programmatic" else "geometry.json")
    if member.resolve().parent != directory:
        raise ValueError("刀具文件越界")
    content = member.read_bytes()
    if len(raw) + len(content) > MAX_BYTES:
        raise ValueError("刀具包超过 4 MB")
    digest = hashlib.sha256(raw + b"\0" + content).hexdigest()
    return descriptor, defaults, content, digest


def _catalogue_root(root, scope="system", template_id="", template_name=""):
    tools, errors = [], []
    root = Path(root)
    if not root.is_dir():
        return tools, [f"{root.name}: 刀具目录不存在"]
    for manifest in sorted(root.glob("*/tool.json")):
        try:
            descriptor, defaults, _, digest = _package(manifest.parent, root)
            item = {**descriptor, "digest": digest, "defaultParameters": defaults,
                    "libraryScope": scope}
            if not item.get("category"):
                item["category"] = "端面" if item.get("target") == "end" else (
                    "支管" if item.get("requiresSection") else "孔型")
            if scope == "template":
                item["templateId"] = str(template_id)
                item["templateName"] = str(template_name or template_id)
            tools.append(item)
        except (ValueError, OSError, KeyError, TypeError) as error:
            errors.append(f"{manifest.parent.name}: {error}")
    return tools, errors


def catalogue(sources=None):
    tools, errors = _catalogue_root(ROOT)
    for source in sources or []:
        if not isinstance(source, dict) or source.get("scope") not in ("template", "user"):
            errors.append("刀具目录来源无效")
            continue
        scope = str(source.get("scope"))
        current, current_errors = _catalogue_root(
            source.get("root", ""), scope, source.get("templateId", ""), source.get("templateName", ""))
        tools.extend(current)
        errors.extend(current_errors)
    return {"tools": tools, "errors": errors}


def _frozen(snapshot):
    # Persist only evaluated geometry and provenance, never executable code or
    # parameter definitions from the product catalogue.
    value = {"schema": "icax.frozen-punch-tool", "schemaVersion": 1,
             "ref": copy.deepcopy(snapshot["ref"]), "parameters": copy.deepcopy(snapshot["parameters"]),
             "context": copy.deepcopy(snapshot["context"]), "geometry": copy.deepcopy(snapshot["geometry"]),
             "label": snapshot["descriptor"]["displayName"], "sourceKind": snapshot["descriptor"]["kind"],
             "processCode": snapshot["descriptor"].get("processCode", "")}
    value["geometryDigest"] = hashlib.sha256(_json_bytes(value["geometry"])).hexdigest()
    return value


def _restore_frozen(value, ref, supplied, context):
    if not isinstance(value, dict) or value.get("schema") != "icax.frozen-punch-tool" or value.get("schemaVersion") != 1:
        raise ValueError("本机缺少匹配刀具，且此实例没有固化刀具；请安装原版刀具或明确替换")
    if value.get("ref") != ref or value.get("parameters") != supplied:
        raise ValueError("固化刀具与程式号或参数不一致；缺少程式时不能修改形状参数")
    geometry = value.get("geometry")
    _validate_geometry(geometry, context["target"])
    if value.get("geometryDigest") != hashlib.sha256(_json_bytes(geometry)).hexdigest():
        raise ValueError("固化刀具几何校验失败，请恢复图纸或安装原版刀具")
    old = value.get("context", {})
    if context["target"] == "end":
        # Native replay uses the already placed frozenCut BRep and immutable
        # base resource. Tiny bounds changes after BRep codec round-trip must
        # not demand the unavailable script or move a frozen cutter.
        compatible = (old.get("target") == "end" and old.get("lengthUnit") == context["lengthUnit"]
                      and old.get("end") == context["end"] and old.get("placement") == context["placement"]
                      and old.get("section") == context.get("section"))
    elif context["target"] == "part":
        # Replay uses the persisted world-space cutter, never these rounded
        # bounds. Main-blank changes are separately rejected for missing tools.
        compatible = (old.get("target") == "part" and old.get("lengthUnit") == context["lengthUnit"]
                      and old.get("feature") == context.get("feature"))
    else:
        compatible = old == context
    if not compatible:
        raise ValueError("固化端部刀具依赖原毛坯和定位，缺少程式时请保持原端部设置或明确替换刀具")
    descriptor = {"id": ref["id"], "version": ref.get("version", ""), "kind": value.get("sourceKind", "fixed"),
                  "displayName": value.get("label", ref["id"]), "target": context["target"],
                  "processCode": value.get("processCode", ""), "parameters": []}
    return {"ref": copy.deepcopy(ref), "parameters": copy.deepcopy(supplied), "context": copy.deepcopy(old),
            "geometry": copy.deepcopy(geometry), "descriptor": descriptor, "resolution": "frozen"}


def _user_entry(ref, user_tools):
    """Return a persisted personal-tool payload matching a recipe reference.

    Personal tools are deliberately passed as data from the host product.  The
    runtime never scans the product database and never treats a user supplied
    path as executable code; only the already validated payload is considered.
    """
    if not isinstance(ref, dict) or not isinstance(user_tools, list):
        return None
    wanted_id = str(ref.get("id", ""))
    wanted_version = str(ref.get("version", ""))
    for item in user_tools:
        if not isinstance(item, dict) or str(item.get("id", "")) != wanted_id:
            continue
        if wanted_version and str(item.get("version", "")) != wanted_version:
            continue
        descriptor = item.get("descriptor") if isinstance(item.get("descriptor"), dict) else item
        if not isinstance(descriptor, dict):
            continue
        return item, descriptor
    return None


def _package_root(ref, user_root=None):
    if isinstance(ref, dict) and str(ref.get("libraryScope", ref.get("scope", ""))) == "user":
        if not isinstance(user_root, str) or not user_root.strip():
            return None
        return Path(user_root).expanduser().resolve()
    return ROOT


def _evaluate(ref, supplied, context, frozen=None, user_tools=None, user_root=None):
    if not isinstance(ref, dict) or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", str(ref.get("id", ""))):
        raise ValueError("请选择刀具模板")
    _user = _user_entry(ref, user_tools)
    if _user:
        item, descriptor = _user
        defaults = item.get("defaultParameters", descriptor.get("defaultParameters", {}))
        content = item.get("scriptSource", "")
        geometry = item.get("geometry")
        digest = str(item.get("digest") or item.get("packageDigest") or "")
        if not digest:
            digest = hashlib.sha256(_json_bytes({"descriptor": descriptor, "geometry": geometry, "script": content})).hexdigest()
        if descriptor.get("kind") == "programmatic" and not isinstance(content, str):
            return _restore_frozen(frozen, ref, supplied, context)
        if descriptor.get("kind") == "fixed" and not isinstance(geometry, dict):
            return _restore_frozen(frozen, ref, supplied, context)
        descriptor = copy.deepcopy(descriptor)
        descriptor["id"] = str(descriptor.get("id", ref["id"]))
        descriptor["version"] = str(descriptor.get("version", ref.get("version", "1.0.0")))
        if descriptor.get("target") != context["target"]:
            raise ValueError("刀具不适用于当前加工位置")
    else:
        try:
            package_root = _package_root(ref, user_root)
            if package_root is None:
                raise ValueError("用户刀具目录不可用")
            descriptor, defaults, content, digest = _package(package_root / ref["id"], package_root)
        except (ValueError, OSError, KeyError, TypeError):
            return _restore_frozen(frozen, ref, supplied, context)
        if (ref.get("version") and ref["version"] != descriptor["version"]) or (ref.get("digest") and ref["digest"] != digest):
            return _restore_frozen(frozen, ref, supplied, context)
    if _user and ref.get("digest") and ref["digest"] != digest:
        return _restore_frozen(frozen, ref, supplied, context)
    if descriptor["target"] != context["target"]:
        raise ValueError("刀具不适用于当前加工位置")
    # Branch intersections and end-profile cuts use a filled outer envelope.
    # Older recipes may carry the removed cutRegion switch; ignore it when the
    # current template is regenerated. Exact frozen snapshots are handled
    # above and remain byte-for-byte immutable.
    if descriptor["id"] in ("branch-profile", "end-profile") and isinstance(supplied, dict) and "cutRegion" in supplied and not any(
            item["key"] == "cutRegion" for item in descriptor.get("parameters", [])):
        if supplied["cutRegion"] not in ("outer", "material"):
            raise ValueError("旧端部切除截面区域无效")
        supplied = {key: value for key, value in supplied.items() if key != "cutRegion"}
    parameters = _parameters(descriptor, supplied)
    # An unchanged recipe reuses the exact saved cutter even on the original
    # computer. Regenerate only after an intentional parameter/context change.
    if frozen and frozen.get("ref") == ref and frozen.get("parameters") == parameters and frozen.get("context") == context:
        result = _restore_frozen(frozen, ref, parameters, context)
        result.update(descriptor=descriptor, resolution="installed")
        return result
    if descriptor["kind"] == "fixed":
        if not _user:
            geometry = json.loads(content)
        if context["target"] == "end":
            if geometry.get("coordinateSpace", "end-local") != "end-local":
                raise ValueError("定式端部刀具须使用 end-local 坐标")
            geometry["coordinateSpace"] = "end-local"
    else:
        package_root = _package_root(ref, user_root) or ROOT
        namespace = {"__name__": f"icax_punch_tool_{digest}", "__file__": str(package_root / ref["id"] / "tool.py")}
        exec(compile(content, namespace["__file__"], "exec"), namespace)
        if not callable(namespace.get("generate")):
            raise ValueError("程式刀具须提供 generate(parameters, context)")
        geometry = namespace["generate"](copy.deepcopy(parameters), copy.deepcopy(context))
    _validate_geometry(geometry, context["target"])
    pinned = {"id": descriptor["id"], "version": descriptor["version"], "digest": digest}
    return {"ref": pinned, "parameters": parameters, "descriptor": descriptor,
            "context": context, "geometry": geometry, "resolution": "installed"}


def _validate_geometry(value, target):
    if not isinstance(value, dict) or len(_json_bytes(value)) > MAX_BYTES:
        raise ValueError("刀具几何无效或超过 4 MB")
    if target == "part" and value.get("coordinateSpace") not in ("part", "part-local"):
        raise ValueError("三维刀具须使用零件坐标系")
    if target == "side" and value.get("mode") == "profile":
        if not isinstance(value.get("contours"), list) or not 1 <= len(value["contours"]) <= 256:
            raise ValueError("截面刀具须提供闭合 contours")
    elif value.get("mode") == "solid":
        model = value.get("model", {})
        if model.get("schema") != "icax.neutral-model" or model.get("schemaVersion") != 1:
            raise ValueError("实体刀具须提供中性模型")
        if not isinstance(model.get("geometry"), list) or not 1 <= len(model["geometry"]) <= 512:
            raise ValueError("刀具中性模型节点数量无效")
        # Fixed and saved neutral geometry cannot load arbitrary local resources.
        if any(node.get("operator") not in ("profile2d", "extrude", "boolean", "transform", "compound") for node in model["geometry"]):
            raise ValueError("刀具只允许自包含中性几何，不允许资源或脚本引用")
        if not isinstance(value.get("outputKey"), str):
            raise ValueError("实体刀具缺少输出节点")
    else:
        raise ValueError("刀具输出模式与加工位置不兼容")


V_NOTCH_STYLE_TO_ID = {
    "sharp_v": "v-notch-sharp", "asymmetric_v": "v-notch-sharp",
    "left_arc": "edge-arc-groove",
    "right_arc": "edge-arc-groove", "relief_v": "v-notch-sharp",
    # The former unverified flat-v shape is represented by a sharp V with a
    # preserved flat root, so old recipes remain editable without that package.
    "flat_v": "v-notch-sharp",
}
V_NOTCH_SHAPE_KEYS = {
    "v-notch-sharp": {"angle", "asymmetric", "leftAngle", "rightAngle", "leaveBottom", "bottomStrategy", "flatWidth", "roundRadius", "reliefLength", "reliefHeight", "reliefRadius", "maleFemale", "maleFemaleSize"},
    "edge-arc-groove": {"angle", "leftArc", "bridge", "reliefDiameter", "reliefLift", "bottomCut", "bottomCutWidth"},
}


def _migrate_sharp_parameters(params, old_style=""):
    """Translate former combined-V fields to the independent sharp V."""
    if old_style == "asymmetric_v":
        params.setdefault("asymmetric", True)
        if "angle" not in params and all(key in params for key in ("leftAngle", "rightAngle")):
            params["angle"] = float(params["leftAngle"]) + float(params["rightAngle"])
    if "bridge" in params and "leaveBottom" not in params:
        params["leaveBottom"] = params["bridge"]
    if "rootWidth" in params and "flatWidth" not in params:
        params["flatWidth"] = params["rootWidth"]
    if "rootRadius" in params and "roundRadius" not in params:
        params["roundRadius"] = params["rootRadius"]
    if "reliefWidth" in params and "reliefHeight" not in params:
        params["reliefHeight"] = params["reliefWidth"]
    if params.get("flatWidth", 0) and "bottomStrategy" not in params:
        params["bottomStrategy"] = "flat"
    if params.get("reliefDiameter", 0) and "reliefWidth" not in params:
        params["reliefLength"] = params["reliefDiameter"]
        params["reliefHeight"] = params.get("reliefDiameter")
        params["reliefRadius"] = 0
        params.setdefault("bottomStrategy", "relief")
    if old_style == "relief_v":
        diameter = params.get("holeDiameter", params.get("reliefDiameter"))
        if diameter is not None:
            params.setdefault("reliefLength", diameter)
            params.setdefault("reliefHeight", diameter)
            params.setdefault("reliefRadius", 0)
        params.setdefault("bottomStrategy", "relief")
    if old_style == "flat_v" and params.get("flatWidth", 0):
        params["bottomStrategy"] = "flat"
    for key in ("bridge", "rootWidth", "reliefWidth", "reliefDiameter", "reliefLift", "bottomCut", "bottomCutWidth", "rootRadius", "holeDiameter", "holeLift", "radius", "curveRadius"):
        params.pop(key, None)


BRANCH_PLACEMENT_KEYS = ("angle", "azimuth", "roll", "offsetY", "offsetZ", "direction", "length")
END_PLACEMENT_KEYS = {
    "end-convex": ("angle", "offset"), "end-cope": ("angle", "offset"),
    "end-key-joint": ("offset",),
    "end-profile": ("angle", "azimuth", "roll", "axialOffset", "offsetY", "offsetZ"),
}


def _migrate_record(item):
    """Convert old flat recipes to geometry-only tool parameters."""
    item = copy.deepcopy(item) if isinstance(item, dict) else {}
    supplied = item.get("toolParameters")
    had_parameters = isinstance(supplied, dict)
    params = copy.deepcopy(supplied) if isinstance(supplied, dict) else {}
    ref = item.get("toolRef") if isinstance(item.get("toolRef"), dict) else {}
    tool_id = str(ref.get("id") or item.get("type") or "")
    changed = False
    if tool_id == "v-notch":
        style = str(params.get("style", item.get("style", "sharp_v")))
        tool_id = V_NOTCH_STYLE_TO_ID.get(style, "v-notch-sharp")
        item["type"] = tool_id
        item["toolRef"] = {"id": tool_id}
        if "rotation" in params:
            item.setdefault("rotation", params["rotation"])
        params.pop("style", None); params.pop("rotation", None)
        if style == "flat_v" and "rootWidth" not in params and "flatWidth" in params:
            params["rootWidth"] = params["flatWidth"]
        if tool_id == "edge-arc-groove":
            params["leftArc"] = style == "left_arc"
        if style == "rounded_v":
            _migrate_rounded_parameters(params)
            _migrate_sharp_parameters(params)
        elif tool_id == "v-notch-sharp":
            _migrate_sharp_parameters(params, style)
        item.pop("style", None); item.pop("flatWidth", None)
        allowed = V_NOTCH_SHAPE_KEYS[tool_id]
        params = {key: value for key, value in params.items() if key in allowed}
        changed = True
    elif tool_id in V_NOTCH_SHAPE_KEYS:
        before = copy.deepcopy(params)
        if tool_id == "v-notch-sharp":
            _migrate_sharp_parameters(params)
        allowed = V_NOTCH_SHAPE_KEYS[tool_id]
        params = {key: value for key, value in params.items() if key in allowed}
        changed = changed or before != params
        if "rotation" in params:
            item.setdefault("rotation", params.pop("rotation"))
            changed = True
        if changed:
            item["toolRef"] = {"id": tool_id}
    if tool_id == "branch-profile":
        for key in BRANCH_PLACEMENT_KEYS:
            if key in params:
                item.setdefault(key, params.pop(key)); changed = True
    for key in END_PLACEMENT_KEYS.get(tool_id, ()):
        if key in params:
            item.setdefault(key, params.pop(key)); changed = True
    if had_parameters or params:
        item["toolParameters"] = params
    else:
        item.pop("toolParameters", None)
    if changed:
        item.pop("frozenTool", None); item.pop("frozenCut", None)
        if tool_id:
            item["toolRef"] = {"id": tool_id}
    return item


def _migrate_recipe(value):
    recipe = copy.deepcopy(value) if isinstance(value, dict) else {}
    if isinstance(recipe.get("features"), list):
        recipe["features"] = [_migrate_record(item) for item in recipe["features"]]
    if isinstance(recipe.get("ends"), dict):
        recipe["ends"] = {key: _migrate_record(item) for key, item in recipe["ends"].items()}
    return recipe


def _migrate_payload(value):
    parameters = copy.deepcopy(value) if isinstance(value, dict) else {}
    migrated = _migrate_recipe(parameters)
    if isinstance(migrated.get("original"), dict):
        migrated["original"] = _migrate_recipe(migrated["original"])
    return migrated


def _legacy_parameters(item, descriptor):
    # Migration only: old projects stored flat shape parameters.
    return {p["key"]: item.get(p["key"], p["defaultValue"]) for p in descriptor.get("parameters", [])}


def _installed(ref, user_tools=None, user_root=None):
    if not isinstance(ref, dict):
        return True  # Legacy migration is validated by the normal evaluator.
    _user = _user_entry(ref, user_tools)
    if _user:
        item, descriptor = _user
        digest = str(item.get("digest") or item.get("packageDigest") or "")
        return (not ref.get("version") or ref["version"] == descriptor.get("version")) and (
            not ref.get("digest") or not digest or ref["digest"] == digest)
    try:
        package_root = _package_root(ref, user_root)
        if package_root is None:
            return False
        descriptor, _, _, digest = _package(package_root / str(ref.get("id", "")), package_root)
        return (not ref.get("version") or ref["version"] == descriptor["version"]) and (
            not ref.get("digest") or ref["digest"] == digest)
    except (ValueError, OSError, KeyError, TypeError):
        return False


def _instance(item):
    return {key: copy.deepcopy(value) for key, value in item.items() if key not in ("toolSnapshot", "frozenTool", "frozenCut")}


def _check_locked(item, user_tools=None, user_root=None):
    if _installed(item.get("toolRef"), user_tools, user_root):
        return
    frozen = item.get("frozenTool", {})
    if frozen.get("instance") != _instance(item):
        raise ValueError("退化定式刀具节点只读，仅可删除；不允许改参数、移动、复制、阵列或停用")


def _freeze_item(item, snapshot):
    value = _frozen(snapshot)
    value["instance"] = _instance(item)
    item["frozenTool"] = value


def _check_original(parameters, user_tools=None, user_root=None):
    # Supplied only by the backend from the persisted component, never forwarded
    # from the request. Prevent replacing/relabeling a locked node in a raw API call.
    original = parameters.get("original", {})
    current = parameters.get("features", [])
    old_ids = [item.get("id") for item in original.get("features", [])]
    current_ids = [item.get("id") for item in current]
    if any(not _installed(item.get("toolRef"), user_tools, user_root) for item in original.get("features", [])):
        if [key for key in old_ids if key in current_ids] != [key for key in current_ids if key in old_ids]:
            raise ValueError("退化定式刀具节点只读，仅可删除，不能改变原节点顺序")
    for old in original.get("features", []):
        if _installed(old.get("toolRef"), user_tools, user_root):
            continue
        matches = [item for item in current if item.get("id") == old.get("id")]
        if matches and (len(matches) != 1 or recipe_item(matches[0]) != recipe_item(old)):
            raise ValueError("退化定式刀具节点只读，仅可删除")
    for key, old in original.get("ends", {}).items():
        new = parameters.get("ends", {}).get(key, {"type": "keep"})
        if not _installed(old.get("toolRef"), user_tools, user_root) and new.get("type") != "keep" and recipe_item(new) != recipe_item(old):
            raise ValueError("退化定式端部刀具只读，仅可删除")


def prepare(parameters):
    # Migrate the persisted recipe before lock checks or tool resolution. The
    # product no longer ships the old combined V-notch package.
    parameters = _migrate_payload(parameters)
    user_tools = parameters.get("userTools", [])
    user_root = parameters.get("userToolRoot")
    _check_original(parameters, user_tools, user_root)
    if parameters.get("rebased") and any(not _installed(item.get("toolRef"), user_tools, user_root)
            for item in [*parameters.get("features", []), *parameters.get("ends", {}).values()]):
        raise ValueError("存在只读退化刀具，不能更换主管截面或长度；请先删除这些节点或恢复原版刀具")
    ids = [item["id"] for item in parameters.get("features", []) if "id" in item]
    if len(ids) != len(set(ids)):
        raise ValueError("刀具节点 ID 不能重复")
    result = {"features": [], "ends": {}}
    for source in parameters.get("features", []):
        item = copy.deepcopy(source)
        item.pop("toolSnapshot", None)
        _check_locked(item, user_tools, user_root)
        if item.get("enabled") is False:
            if item.get("frozenTool") and _installed(item.get("toolRef"), user_tools, user_root):
                item["frozenTool"]["instance"] = _instance(item)
            result["features"].append(item)
            continue
        ref = item.get("toolRef") or {"id": item.get("type", "circle")}
        supplied = item.get("toolParameters")
        if supplied is None:
            _user = _user_entry(ref, user_tools)
            if _user:
                _descriptor = _user[1]
            else:
                package_root = _package_root(ref, user_root)
                if package_root is None:
                    raise ValueError("用户刀具目录不可用")
                _descriptor = _package(package_root / ref["id"], package_root)[0]
            supplied = _legacy_parameters(item, _descriptor)
        # Part tools consume a saved section and an explicit blank-coordinate
        # placement. Their Python is still product-level, never drawing code.
        target = item.get("toolTarget", "side")
        context = {"target": target, "lengthUnit": "mm"}
        if target == "part":
            context.update(bounds=parameters["bounds"], feature={key: copy.deepcopy(item[key])
                for key in ("station", "reference", "section") if key in item})
            context["placement"] = {key: copy.deepcopy(item[key]) for key in (
                "rotation", *BRANCH_PLACEMENT_KEYS) if key in item}
        elif target != "side":
            raise ValueError("特征刀具目标无效")
        snapshot = _evaluate(ref, supplied, context, item.get("frozenTool"), user_tools, user_root)
        descriptor = snapshot["descriptor"]
        item.update(type=snapshot["ref"]["id"], toolRef=copy.deepcopy(snapshot["ref"]), toolParameters=copy.deepcopy(snapshot["parameters"]),
                    toolLabel=descriptor["displayName"], toolKind=descriptor["kind"], toolSnapshot=snapshot)
        _freeze_item(item, snapshot)
        result["features"].append(item)
    for key in ("start", "end"):
        item = copy.deepcopy(parameters.get("ends", {}).get(key, {"type": "keep"}))
        item.pop("toolSnapshot", None)
        _check_locked(item, user_tools, user_root)
        if item.get("type", "keep") == "keep" and not item.get("toolRef"):
            result["ends"][key] = item
            continue
        ref = item.get("toolRef") or {"id": "end-" + item["type"]}
        supplied = item.get("toolParameters")
        if supplied is None:
            _user = _user_entry(ref, user_tools)
            if _user:
                _descriptor = _user[1]
            else:
                package_root = _package_root(ref, user_root)
                if package_root is None:
                    raise ValueError("用户刀具目录不可用")
                _descriptor = _package(package_root / ref["id"], package_root)[0]
            supplied = _legacy_parameters(item, _descriptor)
        placement = {name: item.get(name, default) for name, default in (("trim", 0), ("rotation", 0), ("datum", "long"))}
        placement.update({key: copy.deepcopy(item[key]) for key in (
            "angle", "azimuth", "roll", "axialOffset", "offsetY", "offsetZ", "offset") if key in item})
        context = {"target": "end", "end": key, "bounds": parameters["bounds"], "placement": placement, "lengthUnit": "mm"}
        if "section" in item:
            if not isinstance(item["section"], dict):
                raise ValueError("端部刀具截面须为对象")
            context["section"] = copy.deepcopy(item["section"])
        snapshot = _evaluate(ref, supplied, context, item.get("frozenTool"), user_tools, user_root)
        descriptor = snapshot["descriptor"]
        item.update(type="template", toolRef=copy.deepcopy(snapshot["ref"]), toolParameters=copy.deepcopy(snapshot["parameters"]),
                    toolLabel=descriptor["displayName"], toolKind=descriptor["kind"], toolSnapshot=snapshot)
        _freeze_item(item, snapshot)
        result["ends"][key] = item
    if len(_json_bytes(result)) > 4 * MAX_BYTES:
        raise ValueError("刀具实例清单超过 16 MB，请减少配置或简化模板")
    result["recipe"] = {"features": [recipe_item(item) for item in result["features"]],
                        "ends": {key: recipe_item(item) for key, item in result["ends"].items()}}
    result["recipe"]["operations"] = (
        [{"operator": "subtract", "target": "end", "key": key} for key, item in result["ends"].items()
         if item.get("type") != "keep"] +
        [{"operator": "subtract", "target": "side", "key": item.get("id", str(i + 1)), "featureIndex": i}
         for i, item in enumerate(result["features"])])
    return result


def recipe_item(item):
    return {key: copy.deepcopy(value) for key, value in item.items() if key != "toolSnapshot"}


def generate(parameters, context):
    action = parameters.get("action")
    if action == "catalogue":
        return catalogue(parameters.get("catalogueSources"))
    if action == "prepare":
        return prepare(parameters)
    raise ValueError("未知刀具运行时操作")
