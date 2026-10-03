"""Evaluate reusable local functions and replay their instances on raw stock.

No product shape catalogue or product-name dispatch is used by this module.
The API argument supplies the existing expression and machining primitives.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path

IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def _fold_module():
    spec = importlib.util.spec_from_file_location(
        "icax_assembly_fold_functions", Path(__file__).with_name("assembly_fold_functions.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _geometry_module():
    path = Path(__file__).with_name("assembly_geometry_process_runtime.py")
    spec = importlib.util.spec_from_file_location("icax_assembly_geometry_process_runtime", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _raw_request(part):
    return {"profileRef": copy.deepcopy(part["profileRef"]),
            "parameters": copy.deepcopy(part["parameters"]), "length": float(part["length"]),
            "features": [], "ends": {"start": {"type": "keep"}, "end": {"type": "keep"}}}


def _contact_requirements(api, entries, parts):
    """Keep physical contact obligations separate from machining instructions."""
    if not isinstance(entries, list) or len(entries) > 100:
        raise ValueError("接触校核必须是有限列表")
    result = []
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {
                "roles", "kind", "minimumContactAreaMm2", "maximumIntersectionVolumeMm3"}:
            raise ValueError("接触校核字段无效")
        roles = entry["roles"]
        if (not isinstance(roles, list) or len(roles) != 2 or
                any(not isinstance(role, str) or role not in parts for role in roles) or
                roles[0] == roles[1]):
            raise ValueError("接触校核必须引用两个实际加工角色")
        if entry["kind"] != "positive-area-contact":
            raise ValueError("接触校核类型尚不支持")
        area = api["_bound_number"](entry["minimumContactAreaMm2"], "最小接触面积", 0)
        if area <= 0:
            raise ValueError("接触校核必须要求正面积接触")
        volume = api["_bound_number"](entry["maximumIntersectionVolumeMm3"], "最大穿透体积", 0)
        result.append({"roles": list(roles), "kind": entry["kind"],
                       "minimumContactAreaMm2": area, "maximumIntersectionVolumeMm3": volume})
    return result


def _position_feature(feature, anchor, request, original_end=None, station_offset=None):
    result = copy.deepcopy(feature)
    if anchor["kind"] == "side":
        # The host site is the group centre; preserve each tool's intrinsic
        # array/row offset instead of moving the first hole to that centre.
        if station_offset is None:
            station_offset = result.get("station", 0) if result.get("reference", "center") == "center" else 0
        result.update(reference="start", station=anchor["station"]+station_offset,
                      offset=anchor.get("offset", 0)+result.get("offset", 0), face=anchor["face"],
                      rotation=anchor.get("rotation", 0)+result.get("rotation", 0))
    else:
        # End-adjacent side cuts keep their distance from the selected end.
        # Switching a connection to the other end mirrors this local datum;
        # the old sample's long-tube endpoint must not remain authoritative.
        reference = result.get("reference", "center")
        station = result.get("station", 0)
        absolute = (request["length"]-station if reference == "end" else
                    request["length"]/2+station if reference == "center" else station)
        source_end = original_end or "end"
        distance = absolute if source_end == "start" else request["length"]-absolute
        result.update(reference=anchor["end"], station=distance)
        if source_end != anchor["end"]:
            # Native arrays run away from their reference. Mirror explicit
            # material offsets too when the host selects the opposite end.
            if "arrayOffsets" in result:
                result["arrayOffsets"] = [-value for value in result["arrayOffsets"]]
            result["rotation"] = -result.get("rotation", 0)
        result["rotation"] = result.get("rotation", 0)+anchor.get("rotation", 0)
    return result


def evaluate(api, template_id, process_input, supplied_values=None, process_drafts=None):
    geometry_runtime = _geometry_module()
    if geometry_runtime.has_function(template_id):
        if process_drafts:
            raise ValueError("局部几何加工参数应由本次函数调用明确提供")
        return geometry_runtime.evaluate(template_id, process_input, supplied_values)
    result = {"schema": "icax.assembly-process-result", "schemaVersion": 1,
              "templateId": template_id, "applicable": False, "reason": "",
              "parameters": {}, "operations": [], "materialRequirements": [],
              "forming": [], "checks": [], "contactRequirements": [], "assemblySteps": [], "bom": []}
    try:
        descriptor = api["_template_by_id"](template_id)
        local = api["process_contract"].normalize(descriptor, process_input)
        geometry_keys = {p["key"] for p in descriptor["parameters"] if p.get("scope") == "scene"}
        geometry = {key: value for key, value in local["geometry"].items() if key in geometry_keys}
        if set(local["parts"]) == {"stock"} and "angle" in geometry:
            geometry["angle"] = abs(geometry["angle"])
        values, process_values, _ = api["_validated_preview_values"](descriptor, supplied_values, geometry)
        result["parameters"] = copy.deepcopy(process_values)
        api["_require_process_applicability"](descriptor, local, process_values)
        if set(local["parts"]) == {"stock"}:
            if "frame" not in local["geometry"]:
                theta = math.radians(local["geometry"].get("rotation", 0))
                local["geometry"]["frame"] = {
                    "origin": [local["geometry"]["station"], 0, 0], "xAxis": [1, 0, 0],
                    "yAxis": [0, math.cos(theta), math.sin(theta)],
                    "zAxis": [0, -math.sin(theta), math.cos(theta)]}
            prepared = []
            for process in descriptor.get("partProcesses", []):
                if api["_condition_matches"](process.get("appliesWhen"), values):
                    tool, ref, user_root = api["_function_process_resource"](
                        process, values, local.get("resources", {}),
                        api.get("_process_runtime_context", {}))
                    prepared.append({"id": process["id"], "tool": tool["id"],
                                     "descriptor": tool, "ref": ref, "user_root": user_root,
                                     "values": api["_process_values"](
                                         process, tool, {**values, "angle": abs(local["geometry"]["angle"])},
                                         process_drafts or {})})
            folded = _fold_module().evaluate_fold(template_id, local, process_values,
                                                  process_drafts, prepared)
            result.update(folded)
            result.update(schema="icax.assembly-process-result", schemaVersion=1, templateId=template_id)
            result["assemblySteps"] = api["_resolved_steps"](descriptor, values)
            result["bom"] = api["_resolved_bom"](descriptor, values)
        else:
            if local.get("resources"):
                raise ValueError("连接工艺的资源须按其声明选择，局部资源引用当前用于母材折弯加工")
            if descriptor["manufacturingPlan"]["realization"] != "separate":
                raise ValueError("请由宿主分配连续母材并提供本次加工站位，再调用折弯函数")
            # Preview and actual functions use the same local inputs. Existing
            # cut solvers retain their algorithms, not their example positions.
            plan = api["_preview_definition_plan"](template_id, process_values, process_drafts,
                                                    manufacturing_only=True, process_input=local)
            source_parts = {p["sourceRole"]: p for p in plan["manufacturingParts"]}
            active = [p for p in descriptor["partProcesses"]
                      if api["_condition_matches"](p.get("appliesWhen"), values)]
            for role, source in source_parts.items():
                part = local["parts"][role]
                request = source["request"]
                own_processes = [p for p in active if role in p["participants"]]
                has_cut = bool(request["features"]) or any(
                    end.get("type", "keep") != "keep" for end in request["ends"].values())
                anchor = part.get("anchor")
                if has_cut and (not isinstance(anchor, dict) or anchor.get("kind") not in ("side", "end")):
                    raise ValueError(f"角色 {role} 缺少明确加工锚点，不能沿用示例中心位置")
                if anchor and anchor.get("kind") == "side":
                    api["_bound_number"](anchor.get("station"), role+" 加工站位", 0, part["length"])
                    if anchor.get("face") not in ("top", "bottom", "left", "right"):
                        raise ValueError(f"角色 {role} 缺少实际加工面")
                if anchor and anchor.get("kind") == "end" and anchor.get("end") not in ("start", "end"):
                    raise ValueError(f"角色 {role} 缺少实际起端或末端选择")
                original_ends = [name for name, end in request["ends"].items()
                                 if end.get("type", "keep") != "keep"]
                original_end = original_ends[0] if len(original_ends) == 1 else None
                for index, feature in enumerate(request["features"]):
                    relative = descriptor.get("productBinding", {}).get("processes", {}).get(feature.get("id"), {})
                    station_offset = api["_resolve"](relative["stationOffset"], values) if "stationOffset" in relative else None
                    feature = _position_feature(feature, anchor, request, original_end, station_offset)
                    result["operations"].append({"id": f"{role}:feature:{index}", "kind": "cut", "role": role,
                                                  "processId": feature.get("id"),
                                                  "requestFeature": feature})
                end_count = sum(end.get("type", "keep") != "keep" for end in request["ends"].values())
                for end_name, end in request["ends"].items():
                    if end.get("type", "keep") == "keep":
                        continue
                    selected_end = anchor["end"] if anchor["kind"] == "end" and end_count == 1 else end_name
                    operation = copy.deepcopy(end)
                    operation["trim"] = operation.get("trim", 0)+anchor.get("trim", 0)
                    if "rotation" in operation:
                        operation["rotation"] += anchor.get("rotation", 0)
                    defined = next((item for item in plan["resolvedWorkflow"]["partOperations"]
                                    if item["blankId"] == source["blankId"]
                                    and item["placement"].get("target") == "end"
                                    and item["placement"].get("end") == end_name), {})
                    result["operations"].append({"id": f"{role}:end:{selected_end}", "kind": "end-cut",
                                                  "processId": defined.get("processId"),
                                                  "role": role, "end": selected_end, "requestEnd": operation})
                own_ends = [op["requestEnd"] for op in result["operations"]
                            if op["role"] == role and "requestEnd" in op]
                total_trim = sum(api["_bound_number"](end.get("trim", 0), role+" 端部退切", 0)
                                 for end in own_ends)
                if total_trim >= request["length"]-1e-6:
                    raise ValueError(f"角色 {role} 的端部退切会切空整根母材")
                result["materialRequirements"].append({"role": role, "stockLength": request["length"],
                                                        "minimumLength": request["length"],
                                                        "lengthAddition": request["length"]-part["length"],
                                                        "end": anchor.get("end") if anchor else None,
                                                        "datum": "part-start"})
            result["checks"] = copy.deepcopy(plan["resolvedWorkflow"]["checks"])
            result["contactRequirements"] = _contact_requirements(
                api, plan.get("contactRequirements", []), local["parts"])
            result["assemblySteps"] = copy.deepcopy(plan["resolvedWorkflow"]["assemblySteps"])
            result["bom"] = copy.deepcopy(plan["resolvedWorkflow"]["bom"])
            for check in result["checks"]:
                if check.get("status") == "fail" or check.get("blocking") and check.get("status") == "requires-definition":
                    raise ValueError(check.get("detail", "本次工艺几何条件未满足"))
            result["applicable"] = True
        if not api["_same_input"](result["parameters"], process_values):
            raise ValueError("工艺函数不得改写规范化加工参数")
        shared_files = list(Path(__file__).parent.glob("*.py"))
        shared_digest = api["_generation_file_digest"](shared_files, api["ROOT"].parent)
        result["functionDigest"] = api["_assembly_generation_digest"](
            api["ROOT"]/template_id, descriptor, shared_digest)
        tool_refs = [operation.get("toolRef", operation.get("requestFeature", {}).get("toolRef"))
                     for operation in result["operations"]]
        if any(tool_refs):
            result["functionDigest"] = hashlib.sha256(_canonical({
                "function": result["functionDigest"], "resources": tool_refs}).encode("utf-8")).hexdigest()
        return result
    except (ValueError, KeyError, TypeError, OverflowError) as error:
        result.update(applicable=False, reason=str(error) or "装配工艺必要数据无效",
                      operations=[], materialRequirements=[], forming=[], contactRequirements=[])
        return result


def _multiply(a, b):
    return [sum(a[row*4+k]*b[k*4+column] for k in range(4)) for row in range(4) for column in range(4)]


def _point(matrix, point):
    return [sum(matrix[row*4+k]*point[k] for k in range(3))+matrix[row*4+3] for row in range(3)]


def _mapping(api, target, stock, part):
    if isinstance(target, str):
        start, reverse = 0., False
    elif isinstance(target, dict) and set(target) <= {"stockId", "start", "reverse"}:
        start = api["_bound_number"](target.get("start", 0), "源区间起点", 0, stock["length"])
        reverse = target.get("reverse", False)
        if type(reverse) is not bool:
            raise ValueError("源区间方向必须为开关")
    else:
        raise ValueError("工艺目标必须明确引用实际母材及其区间")
    if start+part["length"] > stock["length"]+1e-6:
        raise ValueError("源管段区间超出母材长度")
    return start, reverse


def resolve(api, stocks, instances, native_profile_dependencies=None):
    if not isinstance(stocks, list) or not stocks or len(stocks) > 1000:
        raise ValueError("请提供实际原始母材列表")
    if not isinstance(instances, list) or len(instances) > 10000:
        raise ValueError("请提供有限数量的工艺实例")
    originals, requests, operation_ids, intervals, mappings, end_allowances = {}, {}, {}, {}, {}, {}
    for source in stocks:
        if not isinstance(source, dict) or set(source)-{"id", "profileRef", "parameters", "length", "matrix", "label"}:
            raise ValueError("母材输入不能包含已有加工；修改或撤销必须从原始母材重新生成")
        stock_id = api["_text"](source.get("id"), "母材标识")
        if stock_id in originals:
            raise ValueError("母材标识重复")
        original = api["process_contract"].normalize_part(source, stock_id, True)
        originals[stock_id] = original
        requests[stock_id] = _raw_request(original)
        operation_ids[stock_id], intervals[stock_id], mappings[stock_id] = [], [], []
        end_allowances[stock_id] = {"start": 0., "end": 0., "designLength": 0.}
    seen, normalized_instances, operations, forming, requirements, checks, bom, steps = {}, [], [], [], [], [], [], []
    contacts = []
    for instance in instances:
        if not isinstance(instance, dict) or set(instance)-{
                "instanceId", "templateId", "processInput", "parameters", "processDrafts", "targets"}:
            raise ValueError("工艺实例字段无效")
        instance_id = api["_text"](instance.get("instanceId"), "工艺实例标识")
        identity = _canonical(instance)
        if instance_id in seen:
            if seen[instance_id] != identity:
                raise ValueError(f"同一实例 {instance_id} 提供了不同配置")
            continue  # Retried identical instance is applied exactly once.
        seen[instance_id] = identity
        evaluated = evaluate(api, instance.get("templateId"), instance.get("processInput"),
                             instance.get("parameters"), instance.get("processDrafts"))
        if not evaluated["applicable"]:
            raise ValueError(f"工艺实例 {instance_id}：{evaluated['reason']}")
        geometry_function = evaluated.get("materialRoles") == ["stock"] and "geometry" in evaluated
        if geometry_function:
            local = _geometry_module().normalize_input(instance["processInput"])
            material_roles = {"stock"}
        else:
            descriptor = api["_template_by_id"](instance["templateId"])
            local = api["process_contract"].normalize(descriptor, instance["processInput"])
            material_roles = set(local["parts"])
        targets = instance.get("targets")
        if not isinstance(targets, dict) or set(targets) != material_roles:
            raise ValueError(f"工艺实例 {instance_id} 必须明确全部母材映射")
        role_mappings = {}
        for role, target in targets.items():
            stock_id = target if isinstance(target, str) else target.get("stockId") if isinstance(target, dict) else None
            if stock_id not in originals:
                raise ValueError(f"工艺实例 {instance_id} 引用了不存在的母材")
            stock, part = originals[stock_id], local["parts"][role]
            if not api["section_queries"].sections_match(stock, part):
                raise ValueError(f"工艺实例 {instance_id} 的截面与目标母材不一致")
            start, reverse = _mapping(api, target, stock, part)
            if geometry_function:
                if reverse or abs(start)>1.e-8 or abs(part["length"]-stock["length"])>1.e-6:
                    raise ValueError(f"工艺实例 {instance_id} 的中立加工必须使用完整实际母材")
                if evaluated.get("geometrySpace") == "stock" or "matrix" in part:
                    actual_frame = api["process_contract"].rigid_matrix(stock.get("matrix", IDENTITY))
                    input_frame = api["process_contract"].rigid_matrix(part.get("matrix", IDENTITY))
                    if any(abs(a-b)>1.e-6 for a,b in zip(actual_frame,input_frame)):
                        raise ValueError(f"工艺实例 {instance_id} 的原材坐标与实际目标母材不一致")
            role_mappings[role] = (stock_id, start, reverse)
            source_mapping = {"sourceId": part.get("id", role), "stockStart": start,
                              "length": part["length"], "reverse": reverse}
            if source_mapping not in mappings[stock_id]:
                mappings[stock_id].append(source_mapping)
        for contact in evaluated["contactRequirements"]:
            contact = copy.deepcopy(contact)
            contact.update(instanceId=instance_id,
                           partIds={role: local["parts"][role].get("id", role) for role in contact["roles"]},
                           stockIds={role: role_mappings[role][0] for role in contact["roles"]})
            contacts.append(contact)
        for requirement in evaluated["materialRequirements"]:
            requirement = copy.deepcopy(requirement)
            stock_id, start, reverse = role_mappings[requirement["role"]]
            part = local["parts"][requirement["role"]]
            if requirement.get("minimumLength", part["length"]) > originals[stock_id]["length"]-start+1e-6:
                raise ValueError(f"工艺实例 {instance_id} 的母材余量不足")
            requirement.update(instanceId=instance_id, stockId=stock_id)
            # Whole-stock mappings describe a design centreline interval plus
            # independent end allowances. Both ends must fit together rather
            # than each succeeding against the same unused allowance.
            if isinstance(targets[requirement["role"]], str) and requirement.get("end") in ("start", "end"):
                selected_end = requirement["end"]
                addition = max(0., requirement.get("lengthAddition", 0.))
                end_allowances[stock_id][selected_end] = max(end_allowances[stock_id][selected_end], addition)
                end_allowances[stock_id]["designLength"] = max(end_allowances[stock_id]["designLength"], part["length"])
            if "interval" in requirement:
                low, high = requirement["interval"]
                if reverse:
                    low, high = part["length"]-high, part["length"]-low
                low, high = low+start, high+start
                if low < -1e-6 or high > originals[stock_id]["length"]+1e-6:
                    raise ValueError(f"工艺实例 {instance_id} 加工区间超出母材")
                for previous_low, previous_high, previous_id in intervals[stock_id]:
                    if min(high, previous_high)-max(low, previous_low) > 1e-6:
                        raise ValueError(f"母材 {stock_id} 上实例 {previous_id} 与 {instance_id} 的折弯加工区重叠")
                intervals[stock_id].append((low, high, instance_id))
                requirement["interval"] = [low, high]
            requirements.append(requirement)
        for index, operation in enumerate(evaluated["operations"]):
            operation = copy.deepcopy(operation)
            role = operation["role"]
            stock_id, start, reverse = role_mappings[role]
            part, request = local["parts"][role], requests[stock_id]
            operation_id = instance_id+":"+operation.get("id", str(index))
            operation.update(operationId=operation_id, instanceId=instance_id, stockId=stock_id)
            if operation.get("kind") == "neutral-csg":
                if reverse or start:
                    raise ValueError("中立加工必须明确使用目标母材的材料坐标")
                neutral = {"instanceId": instance_id, "operationId": operation_id,
                           "geometry": copy.deepcopy(evaluated["geometry"]),
                           "geometrySpace": evaluated.get("geometrySpace", "document"),
                           "operation": copy.deepcopy(operation)}
                if evaluated.get("externalGeometry"):
                    raise ValueError("独立母材计划不能引用产品图中的外部观察者")
                request.setdefault("neutralOperations", []).append(neutral)
            elif "requestFeature" in operation:
                feature = copy.deepcopy(operation["requestFeature"])
                if feature.get("reference", "center") == "end":
                    length = originals[stock_id]["length"] if isinstance(targets[role], str) else part["length"]
                    feature["station"] = length-feature.get("station", 0)
                elif feature.get("reference", "center") == "center":
                    feature["station"] = part["length"]/2+feature.get("station", 0)
                domain_length = originals[stock_id]["length"] if isinstance(targets[role], str) else part["length"]
                station = api["_bound_number"](feature.get("station", 0), "加工位置", 0, domain_length)
                feature.update(reference="start", station=start+(part["length"]-station if reverse else station),
                               id=operation_id)
                if reverse:
                    feature["rotation"] = -feature.get("rotation", 0)
                    # Reflection reverses the whole distribution, not just
                    # its first site. Native pitches and explicit offsets are
                    # signed, keeping skipped-instance identities intact.
                    if "arrayOffsets" in feature:
                        feature["arrayOffsets"] = [-value for value in feature["arrayOffsets"]]
                    else:
                        feature["arrayPitch"] = -feature.get("arrayPitch", 0)
                    if "arrayGroups" in feature or "arrayTransforms" in feature:
                        raise ValueError("反向区间映射暂不支持独立阵列变换，请提供明确材料坐标")
                request["features"].append(feature)
                if len(request["features"]) > 256:
                    raise ValueError(f"母材 {stock_id} 的加工数量超过当前原生执行容量 256")
                operation["requestFeature"] = feature
            elif "requestEnd" in operation:
                end = operation["end"]
                if reverse:
                    end = "start" if end == "end" else "end"
                expected_station = start+(part["length"] if end == "end" else 0)
                boundary = originals[stock_id]["length"] if end == "end" else 0
                if isinstance(targets[role], str):
                    expected_station = boundary
                if abs(expected_station-boundary) > 1e-6:
                    raise ValueError(f"工艺实例 {instance_id} 的端切位于母材内部")
                if request["ends"][end].get("type", "keep") != "keep":
                    raise ValueError(f"母材 {stock_id} 的 {end} 端存在重复端切")
                request["ends"][end] = copy.deepcopy(operation["requestEnd"])
                operation["end"] = end
            operation_ids[stock_id].append(operation_id)
            operations.append(operation)
        for fold in evaluated["forming"]:
            fold = copy.deepcopy(fold)
            stock_id, start, reverse = role_mappings[fold["role"]]
            if reverse or start:
                raise ValueError("折弯实例必须使用整根母材的明确材料坐标")
            fold.update(instanceId=instance_id, stockId=stock_id,
                        sequence=local["geometry"].get("sequence", len(forming)))
            forming.append(fold)
        checks.extend({**copy.deepcopy(check), "instanceId": instance_id} for check in evaluated["checks"])
        bom.extend({**copy.deepcopy(item), "instanceId": instance_id} for item in evaluated["bom"])
        steps.extend({**copy.deepcopy(item), "instanceId": instance_id} for item in evaluated["assemblySteps"])
        normalized_instances.append({"instanceId": instance_id, "templateId": instance["templateId"],
                                     "functionDigest": evaluated["functionDigest"], "processInput": local,
                                     "parameters": evaluated["parameters"],
                                     "processDrafts": copy.deepcopy(instance.get("processDrafts", {})),
                                     "targets": copy.deepcopy(targets)})
    for stock_id, allowance in end_allowances.items():
        required = allowance["designLength"]+allowance["start"]+allowance["end"]
        if required > originals[stock_id]["length"]+1e-6:
            raise ValueError(f"母材 {stock_id} 的两端加工余量合计不足，需要至少 {required:g} mm")
        total_trim = sum(api["_bound_number"](end.get("trim", 0), "母材端部退切", 0)
                         for end in requests[stock_id]["ends"].values())
        if total_trim >= originals[stock_id]["length"]-1e-6:
            raise ValueError(f"母材 {stock_id} 的两端退切合计会切空整根母材")
    matrices = {key: list(IDENTITY) for key in originals}
    # Shop execution order does not change the material path. Compose target
    # geometry along each stock's stations even when a later operation is
    # edited or the instances are submitted in a different order.
    for fold in sorted(forming, key=lambda entry: (entry["stockId"], entry["station"], entry["instanceId"])):
        stock_id = fold["stockId"]
        previous = matrices[stock_id]
        fold["formedHingePoint"] = _point(previous, fold["hingePoint"])
        fold["formedAxis"] = [sum(previous[row*4+k]*fold["axis"][k] for k in range(3)) for row in range(3)]
        matrices[stock_id] = _multiply(previous, fold["targetTransform"])
        fold["cumulativeTransform"] = list(matrices[stock_id])
    dependency_paths = []
    for stock in originals.values():
        ref = stock["profileRef"]
        if ref["scope"] in ("system", "template"):
            for directory_name in ("profile", "tube", "section"):
                directory = api["ROOT"].parent/directory_name/ref["id"]
                if directory.is_dir():
                    dependency_paths.extend(path for path in directory.rglob("*") if path.is_file()
                                            and "__pycache__" not in path.parts)
    profile_digest = api["_generation_file_digest"](dependency_paths, api["ROOT"].parent)
    # Local geometry functions hash their own provider, while this host owns
    # stock mappings, contract validation and native operation serialization.
    # Their revisions also belong to the identity of a newly resolved plan.
    resolver_files = [Path(__file__), *(Path(__file__).with_name(name) for name in (
        "assembly_process_contract.py", "assembly_template_runtime.py",
        "assembly_geometry_process_runtime.py", "assembly_applicability_geometry.py"))]
    resolver_digest = api["_generation_file_digest"](resolver_files, api["ROOT"].parent)
    # Native callers include resolved user/library sections and their resource
    # revisions. They belong to cache identity, never the function parameters.
    identity = {"stocks": originals, "instances": normalized_instances, "profileDigest": profile_digest,
                "resolverDigest": resolver_digest,
                "nativeProfileDependencies": copy.deepcopy(native_profile_dependencies)}
    digest = hashlib.sha256(_canonical(identity).encode("utf-8")).hexdigest()
    plan = {"schema": "icax.assembly-process-plan", "schemaVersion": 1, "planDigest": digest,
            "stocks": list(originals.values()), "instances": normalized_instances,
            "manufacturingParts": [{"id": "manufacturing-"+stock_id, "stockId": stock_id,
                                    "blankId": stock_id, "request": request,
                                    "matrix": list(originals[stock_id]["matrix"]),
                                    "operationIds": operation_ids[stock_id],
                                    "sourceMappings": mappings[stock_id]}
                                   for stock_id, request in requests.items()],
            "operations": operations, "materialRequirements": requirements,
            "forming": forming, "checks": checks, "contactRequirements": contacts,
            "bom": bom, "assemblySteps": steps,
            "validationStatus": "planned", "formingValidation": "target-preview"}
    spec = importlib.util.spec_from_file_location(
        "icax_assembly_target_visualization", Path(__file__).with_name("fold_visualization.py"))
    visualization = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(visualization)
    plan["calibrationParts"] = visualization.build_target_parts(plan)
    return plan
