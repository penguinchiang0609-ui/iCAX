"""Evaluate local machining functions and replay their neutral operations.

Functions own machining rules. This host owns names, repeated invocation,
transactional graph updates and the immutable record saved with a product.
It has no product catalogue, dimensions, joint rules or tool recipes.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys

SCHEMA = "icax.assembly-process-result"
INPUT_SCHEMA = "icax.assembly-process-input"
EXTENSION = "tubeDesigner.assemblyGeometryProcesses"
_PROVIDERS = ("assembly_tube_machining", "assembly_surface_machining", "assembly_structural_machining",
              "assembly_post_machining")
_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,199}$")


def _canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                      separators=(",", ":"))


def _finite(value, depth=0):
    if depth > 48:
        raise ValueError("加工数据嵌套过深")
    if value is None or isinstance(value, (str, bool, int)):
        return
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("加工数据必须为有限数值")
        return
    if isinstance(value, (list, tuple)):
        for child in value:
            _finite(child, depth + 1)
        return
    if isinstance(value, dict) and all(isinstance(key, str) for key in value):
        for child in value.values():
            _finite(child, depth + 1)
        return
    raise ValueError("加工数据必须为可保存的纯数据")


def _identifier(value, label):
    if not isinstance(value, str) or not _ID.fullmatch(value):
        raise ValueError(label + "无效")
    return value


def _registry():
    result = {}
    for name in _PROVIDERS:
        path = Path(__file__).with_name(name + ".py")
        if not path.is_file():
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        module_name = "icax_local_process_" + name + "_" + digest
        if module_name not in sys.modules:
            spec = importlib.util.spec_from_file_location(module_name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[module_name] = module
            try:
                spec.loader.exec_module(module)
            except Exception:
                sys.modules.pop(module_name, None)
                raise
        module = sys.modules[module_name]
        functions = getattr(module, "FUNCTIONS", {})
        if not isinstance(functions, dict):
            raise ValueError("加工函数登记必须为对象")
        for function_id, function in functions.items():
            _identifier(function_id, "加工函数标识")
            if not callable(function) or function_id in result:
                raise ValueError("加工函数重复或无效：" + function_id)
            result[function_id] = (function, digest, module)
    return result


def has_function(function_id):
    return isinstance(function_id, str) and function_id in _registry()


def normalize_input(process_input):
    if (not isinstance(process_input, dict) or set(process_input) != {
            "schema", "schemaVersion", "parts", "geometry"}
            or process_input.get("schema") != INPUT_SCHEMA
            or process_input.get("schemaVersion") != 1
            or not isinstance(process_input.get("parts"), dict)
            or "stock" not in process_input["parts"]
            or not isinstance(process_input.get("geometry"), dict)):
        raise ValueError("局部加工必须提供实际原材和必要几何输入")
    for role, part in process_input["parts"].items():
        _identifier(role, "加工角色")
        if not isinstance(part, dict) or not part:
            raise ValueError("加工角色必须提供实际数据：" + role)
    _finite(process_input)
    if len(_canonical(process_input).encode("utf-8")) > 4 * 1024 * 1024:
        raise ValueError("局部加工输入过大")
    return copy.deepcopy(process_input)


def validate_result(result):
    if not isinstance(result, dict) or result.get("schema") != SCHEMA:
        raise ValueError("局部加工返回格式无效")
    _finite(result)
    if result.get("geometrySpace", "document") not in ("document", "stock"):
        raise ValueError("加工几何坐标空间无效")
    geometry = result.get("geometry", [])
    operations = result.get("operations", [])
    if not isinstance(geometry, list) or len(geometry) > 8192:
        raise ValueError("加工几何声明必须为有限列表")
    external = result.get("externalGeometry", [])
    if not isinstance(external, list) or any(not isinstance(value, str) or not value for value in external):
        raise ValueError("固有配件观察者引用无效")
    keys = set(external)
    for node in geometry:
        if not isinstance(node, dict) or set(node) - {"key", "operator", "inputs", "arguments", "metadata"}:
            raise ValueError("加工几何声明字段无效")
        key = _identifier(node.get("key"), "加工几何标识")
        if key in keys or not isinstance(node.get("operator"), str) or not node["operator"]:
            raise ValueError("加工几何标识重复或算子缺失")
        inputs = node.get("inputs", [])
        if not isinstance(inputs, list) or any(source not in keys for source in inputs):
            raise ValueError("加工几何只能引用已声明的局部输入")
        if not isinstance(node.get("arguments", {}), dict):
            raise ValueError("加工几何参数无效")
        keys.add(key)
    if not isinstance(operations, list) or len(operations) > 1024:
        raise ValueError("加工操作必须为有限列表")
    ids = set()
    for operation in operations:
        if not isinstance(operation, dict) or set(operation) - {
                "id", "kind", "role", "operation", "tool", "tools", "arguments", "matrix", "occupiedRange"}:
            raise ValueError("加工操作字段无效")
        identity = _identifier(operation.get("id"), "加工操作标识")
        if identity in ids or operation.get("kind") != "neutral-csg" or operation.get("role") != "stock":
            raise ValueError("加工操作必须作用于实际原材，且标识唯一")
        ids.add(identity)
        if operation.get("operation") not in ("subtract", "intersect", "union"):
            raise ValueError("加工布尔类型无效")
        tools = operation.get("tools", [operation.get("tool")])
        if not isinstance(tools, list) or not tools or any(tool not in keys for tool in tools):
            raise ValueError("加工操作引用不存在的局部刀具")
        if not isinstance(operation.get("arguments", {}), dict):
            raise ValueError("加工操作参数无效")
    return result


def evaluate(function_id, process_input, parameters=None):
    values = {} if parameters is None else copy.deepcopy(parameters)
    result = {"schema": SCHEMA, "schemaVersion": 1, "templateId": function_id,
              "applicable": False, "reason": "", "parameters": values,
              "geometry": [], "operations": [], "materialRoles": ["stock"],
              "materialRequirements": [], "forming": [], "checks": [],
              "contactRequirements": [], "assemblySteps": [], "bom": []}
    try:
        _identifier(function_id, "加工函数标识")
        if not isinstance(values, dict):
            raise ValueError("加工参数必须为对象")
        _finite(values)
        local = normalize_input(process_input)
        provider = _registry().get(function_id)
        if provider is None:
            raise ValueError("加工函数不存在：" + function_id)
        function, digest, module = provider
        produced = function(copy.deepcopy(local), copy.deepcopy(values))
        if not isinstance(produced, dict):
            raise ValueError("加工函数必须返回纯数据对象")
        result.update(copy.deepcopy(produced))
        result.update(schema=SCHEMA, schemaVersion=1, templateId=function_id,
                      parameters=copy.deepcopy(values), materialRoles=["stock"])
        dependency_files = [Path(__file__)]
        for dependency in getattr(module, "DEPENDENCIES", []):
            path = (Path(module.__file__).parent / dependency).resolve()
            if not path.is_relative_to(Path(__file__).parent.parent.resolve()) or not path.is_file():
                raise ValueError("加工函数依赖不在模板目录内")
            dependency_files.append(path)
        result["functionDigest"] = hashlib.sha256((digest + "".join(
            hashlib.sha256(path.read_bytes()).hexdigest() for path in dependency_files)).encode()).hexdigest()
        result["processInput"] = local
        validate_result(result)
        if not isinstance(result.get("applicable"), bool):
            raise ValueError("加工函数必须明确返回适用性")
        if not result["applicable"]:
            result["geometry"], result["operations"] = [], []
    except (ValueError, TypeError, KeyError, IndexError, ArithmeticError) as error:
        result.update(applicable=False, reason=str(error), geometry=[], operations=[],
                      materialRequirements=[], forming=[], checks=[], contactRequirements=[], assemblySteps=[], bom=[])
    return result


def _rewrite(value, mapping):
    if isinstance(value, str):
        return mapping.get(value, value)
    if isinstance(value, (list, tuple)):
        return [_rewrite(child, mapping) for child in value]
    if isinstance(value, dict):
        return {key: _rewrite(child, mapping) for key, child in value.items()}
    return copy.deepcopy(value)


def _resource_runtime(name):
    path = Path(__file__).with_name(name + ".py")
    key = "icax_assembly_tool_materializer_" + name + "_" + hashlib.sha256(path.read_bytes()).hexdigest()
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        try:
            spec.loader.exec_module(module)
        except Exception:
            sys.modules.pop(key, None)
            raise
    return sys.modules[key]


def materialize_resolved_tool_plan(model, resolved_plan, section, length, geometry_prefix,
                                   *, user_mould_root=None):
    """Replay exact resource snapshots from a resolved plan on allocated stock.

    Callers supply material and an immutable recipe. Only this executor invokes
    the resource and emits its cutter graph, including the recipe's placement.
    A failed resource or graph update leaves no partial cutters in the model.
    """
    if (not isinstance(resolved_plan, dict) or resolved_plan.get("schema") != "icax.assembly-process-plan"
            or not resolved_plan.get("planDigest") or not isinstance(resolved_plan.get("operations"), list)
            or not isinstance(resolved_plan.get("instances"), list) or not resolved_plan["instances"]):
        raise ValueError("刀具物化必须来自已解析的真实工艺计划")
    _finite(resolved_plan)
    _identifier(geometry_prefix, "加工刀具前缀")
    _finite(length)
    if isinstance(length, bool) or not isinstance(length, (int, float)) or length <= 0:
        raise ValueError("加工母材长度必须为正数")
    profile = copy.deepcopy(section)
    punch, sections = _resource_runtime("punch_tool_runtime"), _resource_runtime("section_geometry")
    analysis = sections.from_profile(profile)
    outside = sections.closed_shell_metrics(analysis)["outside"]
    staged = []
    for index, operation in enumerate(copy.deepcopy(resolved_plan["operations"]), start=1):
        feature = operation.get("requestFeature")
        if feature is None:
            raise ValueError("工艺计划返回了不支持物化的加工动作")
        snapshot = punch._evaluate(feature["toolRef"], feature["toolParameters"], {
            "target": "part", "lengthUnit": "mm", "targetSection": profile,
            "targetSectionAnalysis": analysis,
            "bounds": {"min": [0, *outside["min"]], "max": [length, *outside["max"]]},
            "placement": {"rotation": feature.get("rotation", 0)},
            "feature": {"reference": "start", "station": feature["station"], "face": feature.get("face", "top")},
        }, user_root=user_mould_root or None)
        geometry = snapshot["geometry"]
        nodes, output = geometry.get("model", {}).get("geometry", []), geometry.get("outputKey")
        prefix = f"{geometry_prefix}.{index:04d}.mould"
        mapping = {node["key"]: prefix + "." + node["key"] for node in nodes}
        if not nodes or output not in mapping:
            raise ValueError("装配工艺返回的模具没有有效实体输出")
        staged.append((nodes, output, prefix, mapping, copy.deepcopy(operation["placement"]["frame"])))
    document = model._document
    original_length, original_keys = len(document["geometry"]), set(model._geometry_keys)
    try:
        cutters = []
        for nodes, output, prefix, mapping, placement in staged:
            for node in nodes:
                model.geometry(mapping[node["key"]], node["operator"],
                    inputs=[mapping.get(key, key) for key in node.get("inputs", [])],
                    arguments=_rewrite(node.get("arguments", {}), mapping))
            cutters.append(model.geometry(prefix + ".placed", "transform", inputs=[mapping[output]],
                                          arguments={"placement": placement}))
        return cutters
    except Exception:
        del document["geometry"][original_length:]
        model._geometry_keys = original_keys
        raise


def _placement(matrix):
    if not isinstance(matrix, (list, tuple)) or len(matrix) != 16:
        raise ValueError("加工放置必须为16项刚体矩阵")
    _finite(matrix)
    if any(abs(float(a)-b) > 1.e-7 for a, b in zip(matrix[12:], (0, 0, 0, 1))):
        raise ValueError("加工放置不是仿射矩阵")
    axes = [[float(matrix[row*4+column]) for row in range(3)] for column in range(3)]
    if any(abs(sum(v*v for v in axis)-1) > 1.e-6 for axis in axes) or any(
            abs(sum(a*b for a,b in zip(axes[i],axes[j]))) > 1.e-6 for i,j in ((0,1),(0,2),(1,2))):
        raise ValueError("加工放置坐标必须正交且归一化")
    cross = [axes[0][1]*axes[1][2]-axes[0][2]*axes[1][1],
             axes[0][2]*axes[1][0]-axes[0][0]*axes[1][2],
             axes[0][0]*axes[1][1]-axes[0][1]*axes[1][0]]
    if sum(a*b for a,b in zip(cross,axes[2])) < 1-1.e-6:
        raise ValueError("加工放置必须为右手坐标系")
    return {"origin": [float(matrix[index]) for index in (3,7,11)],
            **dict(zip(("xAxis","yAxis","zAxis"), axes))}


def _matrix_product(first, second):
    return [sum(first[row*4+k]*second[k*4+column] for k in range(4))
            for row in range(4) for column in range(4)]


def _result_identity(stock_id, result):
    return hashlib.sha256(_canonical({"stockId": stock_id, "result": result}).encode()).hexdigest()


def _execution_signature(target_key, result):
    return hashlib.sha256(_canonical({
        "target": target_key, "functionDigest": result["functionDigest"],
        "geometry": result.get("geometry", []), "operations": result.get("operations", []),
        "externalGeometry": result.get("externalGeometry", []),
        "stockPlacement": result.get("processInput", {}).get("parts", {}).get("stock", {}).get("matrix")
            if result.get("geometrySpace") == "stock" else None}).encode()).hexdigest()


def apply(model, target_key, result, instance_id, stock_id):
    """Replay one validated function result; an identical retry is a no-op."""
    instance_id = _identifier(instance_id, "工艺实例标识")
    stock_id = _identifier(stock_id, "母材标识")
    validate_result(result)
    if not result.get("applicable", False):
        raise ValueError("工艺实例 " + instance_id + "：" + str(result.get("reason", "不适用")))
    if target_key not in model._geometry_keys:
        raise ValueError("加工目标不是当前模型的真实原材")
    document = model._document
    extension = document.get("extensions", {}).get(EXTENSION, {})
    records = extension.get("instances", [])
    identity = _result_identity(stock_id, result)
    for record in records:
        if record["instanceId"] != instance_id:
            continue
        if record["identity"] != identity or target_key not in (record["targetGeometry"], record["resultGeometry"]):
            raise ValueError("同一工艺实例不能提供不同输入或母材")
        return record["resultGeometry"]
    execution_signature = _execution_signature(target_key, result)
    for reference in result.get("externalGeometry", []):
        if reference not in model._geometry_keys:
            raise ValueError("加工引用的固有配件实体不存在")
    prototype = next((record["resultGeometry"] for record in records
                      if record.get("executionSignature") == execution_signature), None)
    prefix = "process." + hashlib.sha256(instance_id.encode()).hexdigest()[:24]
    mapping = {node["key"]: prefix + "." + str(index) for index, node in enumerate(result.get("geometry", []))}
    # Local cutter construction is independent of the observed stock pose.
    # Share only exact graphs; placements, booleans and records stay per call.
    tool_signature = None
    tool_prototypes = getattr(model, "_assembly_process_tool_prototypes", {})
    shared_tools = False
    if prototype is None and result.get("geometrySpace") == "stock" and result.get("geometry"):
        tool_signature = hashlib.sha256(_canonical({
            "functionDigest": result["functionDigest"], "geometry": result["geometry"],
            "externalGeometry": result.get("externalGeometry", [])}).encode()).hexdigest()
        cached = tool_prototypes.get(tool_signature)
        if cached is not None and set(cached.values()) <= model._geometry_keys:
            mapping = dict(cached)
            shared_tools = True
    original_length = len(document["geometry"])
    original_keys = set(model._geometry_keys)
    try:
        current = prototype or target_key
        if prototype is None:
            if not shared_tools:
                for node in result.get("geometry", []):
                    model.geometry(mapping[node["key"]], node["operator"],
                                   inputs=_rewrite(node.get("inputs", []), mapping),
                                   arguments=_rewrite(node.get("arguments", {}), mapping))
            for index, operation in enumerate(result.get("operations", [])):
                tools = _rewrite(operation.get("tools", [operation.get("tool")]), mapping)
                placement = operation.get("matrix")
                if result.get("geometrySpace") == "stock":
                    stock_placement = result["processInput"]["parts"]["stock"].get("matrix")
                    if stock_placement is None:
                        raise ValueError("局部原材刀具缺少原材实际放置")
                    placement = stock_placement if placement is None else _matrix_product(stock_placement, placement)
                if placement is not None:
                    transformed = []
                    for tool_index, tool in enumerate(tools):
                        transformed.append(model.geometry(prefix + f".placement.{index}.{tool_index}", "transform",
                            inputs=[tool], arguments={"placement": _placement(placement)}))
                    tools = transformed
                arguments = _rewrite(operation.get("arguments", {}), mapping)
                if result.get("geometrySpace") == "stock" and "keepConnectedTo" in arguments:
                    witness = arguments["keepConnectedTo"]
                    if not isinstance(witness, (list, tuple)) or len(witness) != 3 or any(
                            isinstance(value, bool) or not isinstance(value, (int, float)) for value in witness):
                        raise ValueError("材料见证点必须为三个有限坐标")
                    # The witness belongs to the target stock, independently
                    # of the optional tool-only operation placement.
                    matrix = result["processInput"]["parts"]["stock"]["matrix"]
                    arguments["keepConnectedTo"] = [sum(matrix[row*4+column]*witness[column]
                        for column in range(3))+matrix[row*4+3] for row in range(3)]
                arguments.update(operation=operation["operation"], target=current, tools=tools)
                current = model.geometry(prefix + f".operation.{index}", "boolean", inputs=[current, *tools], arguments=arguments)
        record = {"instanceId": instance_id, "stockId": stock_id, "templateId": result["templateId"],
                  "functionDigest": result["functionDigest"], "identity": identity,
                  "executionSignature": execution_signature,
                  "targetGeometry": target_key, "resultGeometry": current,
                  "processInput": copy.deepcopy(result.get("processInput", {})),
                  "parameters": copy.deepcopy(result["parameters"]), "result": copy.deepcopy(result)}
        document.setdefault("extensions", {})[EXTENSION] = {
            "schema": "icax.assembly-geometry-process-plan", "schemaVersion": 1,
            "instances": [*records, record]}
        if tool_signature is not None and not shared_tools:
            tool_prototypes[tool_signature] = dict(mapping)
            model._assembly_process_tool_prototypes = tool_prototypes
        return current
    except Exception:
        del document["geometry"][original_length:]
        model._geometry_keys = original_keys
        raise


def invoke(model, target_key, function_id, process_input, parameters, instance_id, stock_id):
    from icax_template_sdk.manufacturing import is_manufacturing_declaration, DECLARATIONS
    if is_manufacturing_declaration():
        # Preserve initial geometry; there is no evaluated result at this stage.
        record = {"instanceId": instance_id, "functionId": function_id, "stockId": stock_id,
                  "targetKey": target_key, "processInput": copy.deepcopy(process_input),
                  "parameters": copy.deepcopy(parameters)}
        _finite(record)
        records = model._document.setdefault("extensions", {}).setdefault(DECLARATIONS, [])
        if any(value["instanceId"] == instance_id for value in records):
            raise ValueError("duplicate pending machining instance")
        records.append(record)
        return target_key
    return apply(model, target_key, evaluate(function_id, process_input, parameters), instance_id, stock_id)


def attach(document, model):
    records = model._document.get("extensions", {}).get(EXTENSION)
    if records:
        document.setdefault("extensions", {})[EXTENSION] = copy.deepcopy(records)
    return document


def remap_extension(extension, geometry_keys):
    """Keep replay identities consistent when a host namespaces a document."""
    result = copy.deepcopy(extension)
    for record in result.get("instances", []):
        record.setdefault("sourceIdentity", record["identity"])
        record.setdefault("sourceExecutionSignature", record["executionSignature"])
        for field in ("targetGeometry", "resultGeometry"):
            record[field] = geometry_keys[record[field]]
        record["result"] = _rewrite(record["result"], geometry_keys)
        record["processInput"] = _rewrite(record.get("processInput", {}), geometry_keys)
        record["identity"] = _result_identity(record["stockId"], record["result"])
        record["executionSignature"] = _execution_signature(record["targetGeometry"], record["result"])
    return result


def apply_frozen_cutters(model, target_key, cutter_keys, instance_id, stock_id, resolved_plan):
    """Replay neutral cutters already produced by a resolved tool-function plan.

    The tool runtime, rather than a product, freezes these exact resource
    cutters. Keep the source plan in the operation record for provenance.
    """
    if (not isinstance(resolved_plan, dict) or resolved_plan.get("schema") != "icax.assembly-process-plan"
            or not resolved_plan.get("planDigest") or not isinstance(resolved_plan.get("instances"), list)
            or not resolved_plan["instances"]):
        raise ValueError("冻结刀具必须来自已解析的真实工艺计划")
    if not isinstance(cutter_keys, list) or not cutter_keys:
        return target_key
    result = {"schema": SCHEMA, "schemaVersion": 1,
        "templateId": resolved_plan["instances"][0]["templateId"],
        "functionDigest": resolved_plan["planDigest"], "applicable": True, "reason": "",
        "parameters": {}, "processInput": {}, "geometry": [], "externalGeometry": list(cutter_keys),
        "resolvedPlan": copy.deepcopy(resolved_plan), "operations": [{"id":"frozen-cuts",
            "kind":"neutral-csg","role":"stock","operation":"subtract","tools":list(cutter_keys)}]}
    return apply(model, target_key, result, instance_id, stock_id)
