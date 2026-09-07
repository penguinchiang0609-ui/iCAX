"""Shared design inputs/review for security windows, not a structural certification.

Geometry kernels receive derived frame sizes. User inputs always describe
clear-opening targets; conversion happens on a copy, never on persisted inputs.
"""
from __future__ import annotations

from copy import deepcopy
import math
from typing import Any


def number(values: dict[str, Any], key: str, default: float) -> float:
    value = values.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{key} 必须是有限数值")
    return float(value)


def prepare(parameters: dict[str, Any], *, layout: str, fixed_width: float,
            leaf_width: float, leaf_depth: float, vertical_width: float) -> dict[str, Any]:
    effective = deepcopy(parameters)
    if not parameters.get("accessDoorEnabled", False):
        return effective
    use = str(parameters.get("doorUse", "escape"))
    if use not in {"escape", "maintenance"}:
        raise ValueError("开启口用途不支持")
    clear_width = number(parameters, "doorClearWidth", 800)
    clear_height = number(parameters, "doorClearHeight", 1000)
    hardware = number(parameters, "doorHardwareClearance", 10)
    if clear_width <= 0 or clear_height <= 0 or hardware < 0:
        raise ValueError("净开口尺寸必须大于0，五金预留量不能为负数")
    if parameters.get("accessDoorEnabled", False) and use == "escape":
        if clear_width < 800 or clear_height < 1000:
            raise ValueError("本模板应急开启口设计目标至少为净宽800、净高1000 mm；小型开口请明确选择检修用途，不能作为逃生口")
        if str(parameters.get("accessDoorFace", "front")) in {"top", "bottom"}:
            raise ValueError("应急开启口请选择立面并核对室外通路；顶底开口仅作为检修口设计")
    # At the design opening position, reserve the leaf thickness plus hardware.
    # The actual purchased hinge and its swept volume still require verification.
    fixed_clear_width = clear_width + leaf_depth + hardware
    frame_allowance = fixed_width * (2 if layout == "single-face" else 1)
    effective["doorWidth"] = fixed_clear_width + frame_allowance
    effective["doorHeight"] = clear_height + frame_allowance
    if str(parameters.get("verticalLayoutMode", "maximum_clear_gap")) == "maximum_clear_gap":
        span = fixed_clear_width - 2 * number(parameters, "doorGap", 3) - 2 * leaf_width
        maximum_gap = number(parameters, "maximumVerticalClearGap", 110)
        if span <= 0 or maximum_gap <= 0:
            raise ValueError("窗扇格栅没有可用空间或净间距无效")
        count = max(0, math.ceil(span / (maximum_gap + vertical_width / 2) - 1 - 1e-9))
        if count > 100:
            raise ValueError("按最大净间距计算的窗扇竖杆数量超过100")
        effective["doorVerticalCount"] = count
    return effective


def finish(document: dict[str, Any], original: dict[str, Any], effective: dict[str, Any],
           *, layout: str, fixed_width: float, leaf_depth: float) -> dict[str, Any]:
    document["parameters"] = deepcopy(original)
    enabled = bool(original.get("accessDoorEnabled", False))
    allowance = fixed_width * (2 if layout == "single-face" else 1)
    fixed_clear_width = number(effective, "doorWidth", 0) - allowance if enabled else 0
    fixed_clear_height = number(effective, "doorHeight", 0) - allowance if enabled else 0
    hardware = number(original, "doorHardwareClearance", 10) if enabled else 0
    single = layout == "single-face"
    outer_join = (str(original.get("frameJoinType", "miter_45"))
                  if single and original.get("frameLayout", "four_sides") == "four_sides"
                  else "open_frame" if single else str(original.get("frameCornerJoin", "post_butt")))
    active_joins = [outer_join]
    if enabled:
        active_joins += [str(original.get(key, "miter_45" if single else "butt_90"))
                         for key in ("doorFrameJoinType", "doorLeafFrameJoinType")]
    v_groove_active = any(join.startswith("v_groove") for join in active_joins)
    miter_active = any(join in {"miter_45", "rail_miter"} for join in active_joins)
    connection = str(original.get("mainHorizontalConnection", "insert"))
    construction = ("主横杆插入外框定位后焊接；插接不是免焊或承载认证。"
                    if connection == "insert" else
                    "主横杆与外框平切贴合焊接；竖杆仍穿过横杆。")
    review = {
        "reviewVersion": 2, "layout": layout, "dimensions": "outside",
        "openingEnabled": enabled, "openingUse": str(original.get("doorUse", "escape")),
        "fixedClearWidth": fixed_clear_width, "fixedClearHeight": fixed_clear_height,
        "designClearWidth": max(0, fixed_clear_width - leaf_depth - hardware),
        "designClearHeight": max(0, fixed_clear_height),
        "leafVerticalCount": effective.get("doorVerticalCount", 0) if enabled else 0,
        "siteVerification": "required", "hardwareVerification": "required",
        "structuralVerification": "required", "complianceCertified": False,
        "outerFrameConnection": outer_join, "mainHorizontalConnection": connection,
        "vGrooveTrialRequired": v_groove_active,
        "displayHasUncutMiterStock": miter_active,
        "construction": construction,
        "boundary": ("four_sided_frame" if single and outer_join != "open_frame" else
                     "site_closure_required" if single or layout in {"two-face", "three-face"} else
                     "wall_backed_five_face"),
    }
    document.setdefault("extensions", {})["tubeDesigner.securityWindowReview"] = review
    diagnostics = document.setdefault("diagnostics", [])

    def warning(code: str, message: str, *keys: str) -> None:
        diagnostics.append({"severity": "warning", "code": code, "message": message,
                            "parameterKeys": list(keys), "modelKey": ""})

    warning("SW_SITE_REVIEW", "仅完成模板几何与装配规则校验；实际五金开启、墙体锚固、承载、防坠及当地安装要求须现场核验。")
    if review["boundary"] == "site_closure_required":
        warning("SW_OPEN_BOUNDARY", "当前仅两边框，或仅有转角/三面围护；缺少的边界、顶底须由现场可靠构件封闭，不能单独视为封闭防盗笼。")
    if not single:
        warning("SW_PROJECTION_REVIEW", "外凸尺寸、背面靠墙收口、原窗开启及清洁检修空间须现场核对，不默认当地允许外凸安装。")
    if v_groove_active:
        warning("SW_V_GROOVE_TRIAL", "V槽连续折合须按实际材质、壁厚、槽向、折合顺序及设备打样，复核补偿、回弹和焊缝；理论展开不等于可直接批量生产。")
    if not enabled:
        warning("SW_NO_OPENING", "未设置应急开启口；住宅和出租房应核对所在房间的逃生及救援条件。", "accessDoorEnabled")
    elif original.get("doorUse", "escape") == "maintenance":
        warning("SW_MAINTENANCE_ONLY", "当前开启口用于检修，不能视为已满足人员逃生条件。", "doorUse")
    if enabled and (review["designClearWidth"] < 800 or review["designClearHeight"] < 1000):
        warning("SW_SMALL_OPENING", "按窗扇厚度与五金预留扣除后的开口小于800×1000 mm设计目标，不能作为已验证的逃生口。")
    if layout == "three-face" and abs(number(original, "leftWidth", 0) - number(original, "rightWidth", 0)) > 1e-6:
        warning("SW_UNEVEN_WALL", "左右侧深不同，后端不在同一安装平面，请核对实际墙体。", "leftWidth", "rightWidth")
    if (str(original.get("verticalLayoutMode", "")) == "manual_count"
            or number(original, "maximumVerticalClearGap", 110) > 110
            or (not single and number(original, "sideMaximumVerticalClearGap", 110) > 110)):
        warning("SW_GRID_REVIEW", "当前为手动或较大净距布置，须复核全部边缘、窗扇及顶底网孔和儿童攀爬风险。")
    grade = str(original.get("materialGrade", "unspecified")).strip()
    treatment = str(original.get("surfaceTreatment", "unspecified")).strip()
    if grade in {"", "unspecified"}:
        warning("SW_MATERIAL_UNSPECIFIED", "尚未指定材料牌号；管材尺寸配套不代表材质或承载认证。", "materialGrade")
    if treatment in {"", "unspecified"}:
        warning("SW_FINISH_UNSPECIFIED", "尚未指定表面及焊后防腐处理。", "surfaceTreatment")
    if grade == "Q235B" and treatment == "passivated":
        warning("SW_FINISH_MATERIAL_REVIEW", "Q235B钢不能直接沿用不锈钢的焊斑清理钝化方案，请确认完整防腐体系。", "materialGrade", "surfaceTreatment")
    for item in document.get("items", []):
        properties = item.setdefault("properties", {})
        if grade not in {"", "unspecified"}:
            properties["manufacturing.material"] = grade
        properties["manufacturing.surfaceTreatment"] = treatment
        properties["manufacturing.installationVerification"] = "required"
    process_rows = [{"key": "process.connection", "values": {"status": "工艺说明", "detail": construction}},
                    {"key": "process.assembly", "values": {"status": "工艺说明", "detail": "按零件表下料与开孔，先试装定位并复核外包和对角线，再焊接、清理及防腐；开启口框扇和五金另行试装、实测通行净空。"}}]
    if miter_active:
        process_rows.append({"key": "process.miter_preview", "values": {"status": "显示说明", "detail": "装配预览保留未切角管材；生产零件含实际斜切，毛坯角部的显示重叠不作为焊接成品外形。"}})
    document.setdefault("tables", []).append({
        "key": "security_window_review", "displayName": "防盗窗制作与安装核验",
        "columns": [{"key": "status", "displayName": "状态", "valueType": "string"},
                    {"key": "detail", "displayName": "事项", "valueType": "string"}],
        "rows": process_rows + [{"key": "review." + row["code"], "values": {"status": "待核验", "detail": row["message"]}}
                 for row in diagnostics if row.get("severity") == "warning" and row.get("code", "").startswith("SW_")],
    })
    return document


def generate_reviewed(parameters: dict[str, Any], context: dict[str, Any], *,
                      layout: str, load_profile: Any, kernel: Any) -> dict[str, Any]:
    """Translate public clear sizes once, retaining the exact normalized inputs."""
    fixed_width = leaf_depth = leaf_width = vertical_width = 0.0
    if parameters.get("accessDoorEnabled", False):
        fixed_width = load_profile(parameters, "doorFrame").width
        leaf = load_profile(parameters, "doorLeafFrame")
        leaf_width, leaf_depth = leaf.width, leaf.depth
        vertical_width = load_profile(parameters, "doorVertical").width
    effective = prepare(parameters, layout=layout, fixed_width=fixed_width,
                        leaf_width=leaf_width, leaf_depth=leaf_depth,
                        vertical_width=vertical_width)
    document = kernel(effective, context)
    return finish(document, parameters, effective, layout=layout,
                  fixed_width=fixed_width, leaf_depth=leaf_depth)
