"""Material-path target poses for visual checking, never finished BRep geometry.

Folds are composed in material-station order. Shop execution order is metadata;
changing it must not move an earlier material span. Curved targets are sampled
as labelled chords and do not replace the unchanged cutting/allowance models.
"""
import copy
import math


IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def _multiply(a, b):
    return [sum(a[4*i+k]*b[4*k+j] for k in range(4)) for i in range(4) for j in range(4)]


def _translation(station):
    result = list(IDENTITY)
    result[3] = station
    return result


def _rotation(axis, angle):
    x, y, z = axis
    c, s, t = math.cos(angle), math.sin(angle), 1-math.cos(angle)
    return [t*x*x+c, t*x*y-s*z, t*x*z+s*y, 0,
            t*x*y+s*z, t*y*y+c, t*y*z-s*x, 0,
            t*x*z-s*y, t*y*z+s*x, t*z*z+c, 0,
            0, 0, 0, 1]


def _request(stock, length):
    return {"profileRef": copy.deepcopy(stock["profileRef"]),
            "parameters": copy.deepcopy(stock["parameters"]), "length": length,
            "features": [], "ends": {"start": {"type": "keep"}, "end": {"type": "keep"}}}


def build_target_parts(plan):
    """Return independent tube poses tagged as nominal forming targets.

    Source intervals stay in the original stock. Target poses are composed only
    for calibration display; physical cutting solids and finished members are
    never replaced. A pure point-fold chain yields exactly N+1 straight spans.
    """
    if not isinstance(plan, dict) or not isinstance(plan.get("stocks"), list) or not isinstance(plan.get("forming"), list):
        raise ValueError("折弯校核需提供原始母材及局部成形信息")
    parts = []
    for stock in plan["stocks"]:
        stock_id, length = stock["id"], stock["length"]
        world = stock.get("matrix", IDENTITY)
        folds = [fold for fold in plan["forming"] if fold["stockId"] == stock_id]
        if not folds:
            continue
        events = []
        for fold in folds:
            kind, calculation = fold["kind"], fold["calculation"]
            if kind in ("continuous-cold-bend", "flexible-slit-bend"):
                zone = calculation.get("bendZoneLength", calculation.get("flexibleLength"))
                radius = calculation.get("bendRadius", calculation.get("targetCenterlineRadius"))
                if not isinstance(zone, (int, float)) or zone <= 0 or not isinstance(radius, (int, float)) or radius <= 0:
                    raise ValueError("曲线目标缺少明确弯曲区长度或半径")
                events.append({"station": fold["station"]-zone/2,
                               "end": fold["station"]+zone/2,
                               "fold": fold, "zone": zone, "radius": radius,
                               "transform": fold["targetTransform"], "kind": "arc"})
            else:
                local_folds = fold.get("localFolds") or [fold]
                events.extend({"station": step["station"], "fold": fold,
                               "transform": step["targetTransform"], "kind": "point"}
                              for step in local_folds)
        events.sort(key=lambda event: (event["station"], event["fold"]["instanceId"]))
        previous, cursor, index = list(IDENTITY), 0., 0

        def append(start, end, matrix, target_length=None, curved=False):
            nonlocal index
            if end-start <= 1e-8:
                return
            index += 1
            part_length = end-start if target_length is None else target_length
            if not math.isfinite(part_length) or part_length <= 0:
                raise ValueError("折弯校核目标区段长度无效")
            # Native profile requests require at least 1 mm. This display-only
            # axial scale preserves a shorter target's endpoints and section;
            # it is never an input stock pose or a physical material request.
            request_length = max(1., part_length)
            display_matrix = _multiply(world, matrix)
            for row in range(3):
                display_matrix[4*row] *= part_length/request_length
            parts.append({"id": f"target-{stock_id}-{index}", "stockId": stock_id,
                          "sourceRole": stock_id, "label": "折弯目标校核（名义形态）",
                          "materialInterval": [start, end], "request": _request(stock, request_length),
                          "matrix": display_matrix, "targetLength": part_length,
                          "displayAffine": part_length < 1., "previewKind": "target-shape",
                          "approximate": True, "formingValidation": "target-preview",
                          "curvedApproximation": curved})

        for event in events:
            station, fold = event["station"], event["fold"]
            if station < cursor-1e-7 or station < -1e-7 or station > length+1e-7:
                raise ValueError("目标折弯区站位重叠或超出母材")
            append(cursor, station, _multiply(previous, _translation(cursor)))
            if event["kind"] == "arc":
                end, zone, radius = event["end"], event["zone"], event["radius"]
                if end > length+1e-7:
                    raise ValueError("曲线校核区超出母材")
                beta = math.radians(abs(fold["angle"]))
                samples = max(8, math.ceil(math.degrees(beta)/6))
                direction, axis = fold["frame"]["zAxis"], fold["axis"]
                # Straight stock coordinates are X=axial. The effective
                # cutting frame already encodes signed bend direction.
                for sample in range(samples):
                    a, b = beta*sample/samples, beta*(sample+1)/samples
                    matrix = _rotation(axis, (a+b)/2)
                    position = [station+radius*math.sin(a),
                                direction[1]*radius*(1-math.cos(a)),
                                direction[2]*radius*(1-math.cos(a))]
                    for row in range(3):
                        matrix[4*row+3] = position[row]
                    start_s, end_s = station+zone*sample/samples, station+zone*(sample+1)/samples
                    append(start_s, end_s, _multiply(previous, matrix),
                           2*radius*math.sin((b-a)/2), True)
                cursor = end
            else:
                cursor = station
            previous = _multiply(previous, event["transform"])
        append(cursor, length, _multiply(previous, _translation(cursor)))
    return parts
