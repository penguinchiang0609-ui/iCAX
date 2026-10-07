"""Host adapter from allocated window stock to reusable local processes.

The window owns path allocation and source-span mappings. The assembly runtime
owns every groove recipe, cutter range and forming result. Frozen neutral cuts
are emitted from that result for downstream document consumers.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
TOOL_FUNCTIONS = {
    "v-notch-sharp": "node-v-notch-integrated",
    "edge-arc-groove": "node-edge-arc-integrated",
    "embedded-arc-notch": "node-embedded-arc-integrated",
    "segmented-bend": "segmented-bend",
    "flexible-slit-bend": "flexible-slit-bend-integrated",
}


def _load(name):
    path = Path(__file__).with_name(name + ".py")
    key = "icax_window_process_" + name + "_" + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        spec.loader.exec_module(module)
    return sys.modules[key]


def frame_plan(model, frame, bends):
    """Invoke the same function once per bend on one allocated raw stock."""
    runtime = _load("assembly_template_runtime")
    profile = frame.profile
    section = {"contours": profile.contours(swap_axes=True)}
    template = model.build()["template"]
    stock = {"id": frame.key, "label": frame.name,
             "profileRef": {"scope": "template", "id": frame.key, "templateId": template["id"]},
             "parameters": {}, "length": float(frame.length), "matrix": list(IDENTITY)}
    bindings = frame.parameters.get("tubeDesignerToolBindings", {})
    binding = bindings.get(frame.tool_role) if isinstance(bindings, dict) else None
    if isinstance(binding, dict):
        reference, values = binding.get("ref"), binding.get("parameters", {})
    else:
        selection = str(frame.parameters.get(frame.tool_role + "Tool", "system:v-notch-sharp"))
        scope, separator, tool_id = selection.partition(":")
        if separator != ":":
            raise ValueError("连续框槽口模具引用无效")
        reference, values = {"scope": scope, "id": tool_id}, {}
    if not isinstance(reference, dict) or not isinstance(values, dict):
        raise ValueError("连续框槽口模具引用无效")
    tool_id = reference.get("id")
    template_id = TOOL_FUNCTIONS.get(tool_id, "node-v-notch-integrated")
    descriptor = runtime._template_by_id(template_id)
    process_id = descriptor["partProcesses"][0]["id"]
    instances = []
    for index, bend in enumerate(bends):
        angle, station, rotation = float(bend["angle"]), float(bend["station"]), float(bend.get("rotation", 0))
        theta = math.radians(rotation)
        local = {"schema": "icax.assembly-process-input", "schemaVersion": 1,
                 "parts": {"stock": {**copy.deepcopy(stock), "section": copy.deepcopy(section)}},
                 "geometry": {"angle": angle, "station": station, "rotation": rotation,
                              "sequence": bend.get("sequence", index + 1),
                              "frame": {"origin": [station, 0, 0], "xAxis": [1, 0, 0],
                                        "yAxis": [0, math.cos(theta), math.sin(theta)],
                                        "zAxis": [0, -math.sin(theta), math.cos(theta)]}},
                 "resources": {process_id: {"ref": copy.deepcopy(reference)}}}
        # Resource drafts are passed unchanged; shared conditional resolution
        # prevents inactive relief options from affecting the actual cutter.
        instances.append({"instanceId": f"{frame.key}.bend.{index + 1:04d}",
                          "templateId": template_id, "processInput": local,
                          "parameters": {}, "processDrafts": {process_id: {tool_id: copy.deepcopy(values)}},
                          "targets": {"stock": stock["id"]}})
    source = {"schema": "icax.assembly-process-source", "schemaVersion": 1,
              "stocks": [stock], "instances": instances}
    plan = runtime.resolve_process_plan(source["stocks"], source["instances"],
        runtime_context={"userMouldRoot": frame.user_mould_root or ""})
    return source, plan


def _point(matrix, point):
    return [sum(matrix[4*i+j]*point[j] for j in range(3))+matrix[4*i+3] for i in range(3)]


def _multiply(a,b):
    return [sum(a[4*i+k]*b[4*k+j] for k in range(4)) for i in range(4) for j in range(4)]


def corner_reserves(fold):
    """Separate incoming/outgoing straight-stock offsets, excluding K amount."""
    if 'materialCornerReserves' in fold:
        values = fold['materialCornerReserves']
        if not isinstance(values, dict) or set(values) != {'incoming', 'outgoing'}:
            raise ValueError('折弯材料边界必须具有明确的前后直段余量')
        result = {}
        for key, value in values.items():
            if (isinstance(value, bool) or not isinstance(value, (int, float))
                    or not math.isfinite(value) or value < -1.e-7):
                raise ValueError('所选槽根超出外框内部折弯基准，不能保持当前成品中心线')
            result[key] = max(0., float(value))
        return result
    frame = fold['frame']
    relative = [fold['hingePoint'][i]-frame['origin'][i] for i in range(3)]
    reserve = -sum(relative[i]*frame['zAxis'][i] for i in range(3))
    if reserve < -1e-7:
        raise ValueError('所选槽根超出外框内部折弯基准，不能保持当前成品中心线')
    return {'incoming': max(0., reserve), 'outgoing': max(0., reserve)}


def corner_reserve(fold):
    """A scalar reserve is valid only for a genuinely symmetric material fold."""
    values = corner_reserves(fold)
    if abs(values['incoming']-values['outgoing']) > 1.e-7:
        raise ValueError('边弧折弯具有不同的前后材料边界，必须逐侧分配直段余量')
    return values['incoming']


def bend_allowances(plan):
    """Return each fold's material requirement, independent of submission order."""
    operations = {}
    for operation in plan['operations']:
        if 'requestFeature' in operation:
            operations.setdefault(operation['instanceId'], []).append(operation)
    amounts = {}
    for fold in plan['forming']:
        identifier = fold['instanceId']
        selected = operations.get(identifier, [])
        if identifier in amounts or len(selected) != 1:
            raise ValueError('连续框折弯必须有唯一对应的槽口材料要求: '+identifier)
        parameters = selected[0]['requestFeature']['toolParameters']
        expected = (math.radians(abs(fold['angle']))*parameters['kFactor']*parameters['wallThickness']
                    if parameters.get('bendCompensation',False) else 0.)
        amount = fold.get('materialLengthAddition', expected)
        if (isinstance(amount, bool) or not isinstance(amount, (int, float))
                or not math.isfinite(amount) or abs(amount-expected) > 1.e-7):
            raise ValueError('折弯材料补偿与实际槽口参数不一致')
        if not math.isfinite(amount) or amount < 0:
            raise ValueError('折弯材料补偿必须是有限非负长度: '+identifier)
        amounts[identifier] = amount
    if not amounts:
        raise ValueError('连续框缺少折弯材料要求')
    return amounts


def bend_allowance(plan):
    """Compatibility for callers which still require one uniform allowance."""
    amounts = list(bend_allowances(plan).values())
    if max(amounts)-min(amounts)>1e-6:
        raise ValueError('该调用需要一致的折弯材料补偿，请使用逐折弯材料要求')
    return amounts[0]


def target_span_check(spans,plan,allowance):
    if allowance:
        return {'status':'not-performed','method':'rigid-centreline-endpoints',
                'reason':'K补偿独立计入材料余量，点铰名义目标不代表该余量的精确成形'}
    first = spans[0]
    placement = first['placement']
    axes = [[placement[name][index] for name in ('xAxis','yAxis','zAxis')] for index in range(3)]
    transform = list(IDENTITY)
    folds = sorted(plan['forming'],key=lambda f:f['station'])
    maximum = 0.
    for index,span in enumerate(spans):
        run = math.dist(span['start'],span['end'])
        start = span['stockStart']+span['startReserve']
        for station,target in ((start,span['start']),(start+run,span['end'])):
            local = _point(transform,[station,0,0])
            actual = [first['start'][i]+sum(axes[j][i]*local[j] for j in range(3)) for i in range(3)]
            maximum = max(maximum,math.dist(actual,target))
        if index<len(folds):
            transform = _multiply(transform,folds[index]['targetTransform'])
    if maximum>1e-4:
        raise ValueError(f'连续母材名义折后中心线偏离成品输入 {maximum:g} mm')
    return {'status':'pass','method':'rigid-centreline-endpoints','maximumDeviation':maximum}


def emit_cutters(model, frame, plan):
    """Pass allocated material and its recipe to the common graph executor."""
    return _load("assembly_geometry_process_runtime").materialize_resolved_tool_plan(
        model, plan, {"contours": frame.profile.contours(swap_axes=True)}, frame.length,
        frame.key + ".export.groove", user_mould_root=frame.user_mould_root or None)


def attach(properties, source, plan, secondary_cutters):
    previous = properties.get('tubeDesigner.assemblyProcessSource')
    source = copy.deepcopy(source)
    if previous:
        source['instances'].extend(copy.deepcopy(previous['instances']))
    properties["tubeDesigner.assemblyProcessStockId"] = source["stocks"][0]["id"]
    properties["tubeDesigner.assemblyProcessSource"] = copy.deepcopy(source)
    properties["tubeDesigner.assemblyProcessPlan"] = copy.deepcopy(plan)
    properties["tubeDesigner.assemblyProcessSecondaryCutters"] = list(secondary_cutters)
    actual = copy.deepcopy(source["instances"][0]["processInput"]["parts"]["stock"]["section"])
    shell = _load("section_geometry").closed_shell_metrics(_load("section_geometry").from_profile(actual))
    bounds = shell["outside"]
    actual.update(width=bounds["max"][0]-bounds["min"][0],
                  depth=bounds["max"][1]-bounds["min"][1], wallThickness=shell["wallThickness"])
    properties["tubeDesigner.assemblyProcessStockProfile"] = actual
