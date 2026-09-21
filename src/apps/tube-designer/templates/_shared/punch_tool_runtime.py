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


def _validate_section_analysis(descriptor):
    if "applicability" in descriptor:
        raise ValueError("旧管型标签约束不再支持，请将模具升级为截面 analyze 协议")
    if "sectionAnalysis" not in descriptor:
        return
    if descriptor["sectionAnalysis"] != {"schemaVersion": 1} or descriptor.get("kind") != "programmatic":
        raise ValueError("模具截面分析协议无效")


def _section_queries():
    import importlib.util
    path = Path(__file__).with_name("section_geometry.py")
    spec = importlib.util.spec_from_file_location("mold_section_geometry", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _analyze_mould(namespace, descriptor, parameters, context):
    analyzer = namespace.get("analyze")
    if analyzer is None and "sectionAnalysis" not in descriptor:
        return parameters, None
    if not callable(analyzer):
        raise ValueError("此模具必须提供 analyze(parameters, section, context)")
    # targetSection is always the original generic tube-profile expression.
    # The BRep-derived analysis payload is private to section-topology queries
    # used by section-aware notches and must never replace the operation input.
    section = context.get("targetSectionAnalysis", context.get("targetSection"))
    if (isinstance(section, dict) and section.get("schema") == "icax.mold-section"
            and section.get("status") == "unavailable"):
        # An extrusion projection can fail at circular seams even though the
        # exact contours that built this blank are available. Read those curves;
        # do not weaken the individual template's applicability checks.
        section = namespace["section_geometry"].from_profile(context.get("targetSection"), section.get("tolerance", 0.001))
    result = analyzer(copy.deepcopy(parameters), copy.deepcopy(section), copy.deepcopy(context))
    if (not isinstance(result, dict) or type(result.get("applicable")) is not bool
            or not isinstance(result.get("reason", ""), str) or len(_json_bytes(result)) > MAX_BYTES):
        raise ValueError("模具截面分析结果无效")
    if not result["applicable"]:
        raise ValueError("模具截面分析不通过：" + (result.get("reason") or "目标截面不满足要求"))
    if not isinstance(result.get("data"), dict):
        raise ValueError("模具截面分析必须返回 data")
    derived = result.get("derivedParameters", {})
    allowed = {p["key"] for p in descriptor.get("parameters", []) if p.get("derived") is True}
    if not isinstance(derived, dict) or set(derived) - allowed:
        raise ValueError("模具截面分析只能填写已声明的派生参数")
    parameters = _parameters(descriptor, {**parameters, **derived})
    return parameters, result


def _target_snapshot(value):
    # Recognition tolerances are 0.001 mm; don't invalidate frozen tools for
    # sub-micrometre BRep serialization noise. Never change cutter geometry.
    if isinstance(value, dict):
        return {key: _target_snapshot(child) for key, child in value.items()}
    if isinstance(value, list):
        return [_target_snapshot(child) for child in value]
    if isinstance(value, float) and math.isfinite(value):
        return value
    return value


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
            raise ValueError(f"{item.get('displayName', key)}不是可用选项")
        result[key] = value
    if set(supplied) - set(result):
        raise ValueError("刀具包含未声明的参数")
    return result


def _operation_parameters(descriptor, supplied=None):
    definitions = descriptor.get("operationParameters", [])
    if not isinstance(definitions, list) or len(definitions) > 32:
        raise ValueError("单件工艺操作参数最多 32 项")
    keys = [item.get("key") for item in definitions if isinstance(item, dict)]
    if len(keys) != len(definitions) or any(
            not isinstance(key, str) or not re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", key)
            for key in keys) or len(set(keys)) != len(keys):
        raise ValueError("单件工艺操作参数键无效或重复")
    return _parameters({"parameters": definitions}, supplied or {})


def _validate_inputs(descriptor):
    definitions = descriptor.get("inputs", [])
    if not isinstance(definitions, list) or len(definitions) > 16:
        raise ValueError("单件工艺输入最多 16 项")
    keys = []
    for item in definitions:
        if not isinstance(item, dict):
            raise ValueError("单件工艺输入声明无效")
        key = item.get("key")
        if not isinstance(key, str) or not re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", key):
            raise ValueError("单件工艺输入键无效")
        if item.get("valueType") != "profile":
            raise ValueError("单件工艺输入类型无效")
        keys.append(key)
    if len(set(keys)) != len(keys):
        raise ValueError("单件工艺输入键重复")
    return definitions


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
    _validate_inputs(descriptor)
    operation_defaults = _operation_parameters(descriptor)
    _validate_section_analysis(descriptor)
    defaults = _parameters(descriptor, {})
    member = directory / ("tool.py" if descriptor["kind"] == "programmatic" else "geometry.json")
    if member.resolve().parent != directory:
        raise ValueError("刀具文件越界")
    content = member.read_bytes()
    if len(raw) + len(content) > MAX_BYTES:
        raise ValueError("刀具包超过 4 MB")
    queries = Path(__file__).with_name("section_geometry.py").read_bytes()
    digest = hashlib.sha256(raw + b"\0" + content + queries).hexdigest()
    return descriptor, defaults, operation_defaults, content, digest


def _catalogue_root(root, scope="system", template_id="", template_name=""):
    tools, errors = [], []
    root = Path(root)
    if not root.is_dir():
        return tools, [f"{root.name}: 刀具目录不存在"]
    for manifest in sorted(root.glob("*/tool.json")):
        try:
            descriptor, defaults, operation_defaults, _, digest = _package(manifest.parent, root)
            item = {**descriptor, "digest": digest, "defaultParameters": defaults,
                    "defaultOperationParameters": operation_defaults, "libraryScope": scope}
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
    if "targetSectionAnalysis" not in old and "targetSectionAnalysis" in context:
        context = {key: child for key, child in context.items() if key != "targetSectionAnalysis"}
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
    if "targetProfile" in old and "targetProfile" in context:
        compatible = compatible and old["targetProfile"] == context.get("targetProfile")
    if "targetSection" in old:
        compatible = compatible and old["targetSection"] == context.get("targetSection")
        compatible = compatible and old.get("placement") == context.get("placement")
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
            descriptor, defaults, _, content, digest = _package(package_root / ref["id"], package_root)
        except (ValueError, OSError, KeyError, TypeError):
            return _restore_frozen(frozen, ref, supplied, context)
        if (ref.get("version") and ref["version"] != descriptor["version"]) or (ref.get("digest") and ref["digest"] != digest):
            return _restore_frozen(frozen, ref, supplied, context)
    if _user and ref.get("digest") and ref["digest"] != digest:
        return _restore_frozen(frozen, ref, supplied, context)
    if descriptor["target"] != context["target"]:
        raise ValueError("刀具不适用于当前加工位置")
    if context["target"] == "end":
        # Validate declarative pose controls before executing a tool, not after
        # its geometry has already consumed a potentially invalid angle.
        placement = context["placement"]
        operations = _operation_parameters(descriptor, {
            definition["key"]: placement[definition["key"]]
            for definition in descriptor.get("operationParameters", [])
            if definition["key"] in placement
        })
        context = {**context, "placement": {**placement, **operations}}
    for input_definition in _validate_inputs(descriptor):
        key = input_definition["key"]
        if input_definition.get("required") is True and key not in context:
            raise ValueError("单件工艺缺少必需输入：" + str(input_definition.get("displayName", input_definition["key"])))
        if key in context:
            profile = context[key]
            if not isinstance(profile, dict) or not isinstance(profile.get("contours"), list) or not profile["contours"]:
                raise ValueError("单件工艺输入不是有效截面：" + str(input_definition.get("displayName", key)))
    _validate_section_analysis(descriptor)
    if "sectionAnalysis" not in descriptor and "targetSectionAnalysis" in context:
        context = {key: value for key, value in context.items() if key != "targetSectionAnalysis"}
    parameters = _parameters(descriptor, supplied)
    # An unchanged recipe reuses the exact saved cutter even on the original
    # computer. Regenerate only after an intentional parameter/context change.
    if "sectionAnalysis" not in descriptor and frozen and frozen.get("ref") == ref and frozen.get("parameters") == parameters and frozen.get("context") == context:
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
        namespace["section_geometry"] = _section_queries()
        exec(compile(content, namespace["__file__"], "exec"), namespace)
        if not callable(namespace.get("generate")):
            raise ValueError("程式刀具须提供 generate(parameters, context)")
        parameters, analysis = _analyze_mould(namespace, descriptor, parameters, context)
        if analysis is not None:
            context = copy.deepcopy(context)
            context["analysis"] = analysis["data"]
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


BRANCH_PLACEMENT_KEYS = ("angle", "azimuth", "roll", "offsetY", "offsetZ", "direction", "length")


def _default_parameters(descriptor):
    return {p["key"]: copy.deepcopy(p["defaultValue"])
            for p in descriptor.get("parameters", [])}


def _installed(ref, user_tools=None, user_root=None):
    if not isinstance(ref, dict):
        return False
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
        descriptor, _, _, _, digest = _package(package_root / str(ref.get("id", "")), package_root)
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
    parameters = copy.deepcopy(parameters) if isinstance(parameters, dict) else {}
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
        if not isinstance(item.get("toolRef"), dict):
            raise ValueError("刀具特征必须引用当前模具包")
        _check_locked(item, user_tools, user_root)
        if item.get("enabled") is False:
            if item.get("frozenTool") and _installed(item.get("toolRef"), user_tools, user_root):
                item["frozenTool"]["instance"] = _instance(item)
            result["features"].append(item)
            continue
        ref = item["toolRef"]
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
            supplied = _default_parameters(_descriptor)
        # Part tools consume a saved section and an explicit blank-coordinate
        # placement. Their Python is still product-level, never drawing code.
        target = item.get("toolTarget", "side")
        context = {"target": target, "lengthUnit": "mm"}
        if "targetSection" in parameters:
            context["targetSection"] = _target_snapshot(parameters["targetSection"])
        if "targetSectionAnalysis" in parameters:
            context["targetSectionAnalysis"] = _target_snapshot(parameters["targetSectionAnalysis"])
        if target == "part":
            context.update(bounds=parameters["bounds"], feature={key: copy.deepcopy(item[key])
                for key in ("station", "reference", "section") if key in item})
            context["placement"] = {key: copy.deepcopy(item[key]) for key in (
                "rotation", *BRANCH_PLACEMENT_KEYS) if key in item}
        elif target != "side":
            raise ValueError("特征刀具目标无效")
        snapshot = _evaluate(ref, supplied, context, item.get("frozenTool"), user_tools, user_root)
        descriptor = snapshot["descriptor"]
        operation_values = _operation_parameters(descriptor, {
            definition["key"]: item.get(definition["key"], definition["defaultValue"])
            for definition in descriptor.get("operationParameters", [])
        })
        item.update(operation_values)
        item.update(type=snapshot["ref"]["id"], toolRef=copy.deepcopy(snapshot["ref"]), toolParameters=copy.deepcopy(snapshot["parameters"]),
                    toolLabel=descriptor["displayName"], toolKind=descriptor["kind"], toolSnapshot=snapshot)
        _freeze_item(item, snapshot)
        result["features"].append(item)
    for key in ("start", "end"):
        item = copy.deepcopy(parameters.get("ends", {}).get(key, {"type": "keep"}))
        item.pop("toolSnapshot", None)
        if item.get("type", "keep") == "keep" and not item.get("toolRef"):
            result["ends"][key] = item
            continue
        _check_locked(item, user_tools, user_root)
        if not isinstance(item.get("toolRef"), dict):
            raise ValueError("端部加工必须引用当前模具包")
        ref = item["toolRef"]
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
            supplied = _default_parameters(_descriptor)
        placement = {name: item.get(name, default) for name, default in (("trim", 0), ("rotation", 0), ("datum", "long"))}
        placement.update({key: copy.deepcopy(item[key]) for key in (
            "angle", "azimuth", "roll", "axialOffset", "offsetY", "offsetZ", "offset") if key in item})
        context = {"target": "end", "end": key, "bounds": parameters["bounds"], "placement": placement, "lengthUnit": "mm"}
        if "targetSection" in parameters:
            context["targetSection"] = _target_snapshot(parameters["targetSection"])
        if "targetSectionAnalysis" in parameters:
            context["targetSectionAnalysis"] = _target_snapshot(parameters["targetSectionAnalysis"])
        if "section" in item:
            if not isinstance(item["section"], dict):
                raise ValueError("端部刀具截面须为对象")
            context["section"] = copy.deepcopy(item["section"])
        snapshot = _evaluate(ref, supplied, context, item.get("frozenTool"), user_tools, user_root)
        descriptor = snapshot["descriptor"]
        operation_values = _operation_parameters(descriptor, {
            definition["key"]: item.get(definition["key"], definition["defaultValue"])
            for definition in descriptor.get("operationParameters", [])
        })
        item.update(operation_values)
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
