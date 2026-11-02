"""Shared design inputs/review for security windows, not a structural certification.

Geometry kernels receive derived frame sizes. User inputs always describe
clear-opening targets; conversion happens on a copy, never on persisted inputs.
"""
from __future__ import annotations

from copy import deepcopy
import importlib.util
import math
from pathlib import Path
from typing import Any


def number(values: dict[str, Any], key: str, default: float) -> float:
    value = values.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{key} 必须是有限数值")
    return float(value)


def center_spacing_positions(
    parameters: dict[str, Any], minimum: float, maximum: float, *,
    start_offset_key: str, end_offset_key: str, maximum_spacing_key: str,
    label: str, boundary_inset: float = 0.0,
) -> list[float]:
    """Equal clear-boundary intervals, or explicitly selected end offsets."""
    maximum_spacing = number(parameters, maximum_spacing_key, 0)
    if maximum_spacing <= 0:
        raise ValueError(f"{label}最大中心距必须大于0")
    if parameters.get('barDistribution', 'automatic') == 'automatic':
        # Equal intervals include both clear frame boundaries. No independent
        # first/last offset drafts participate in this mode.
        minimum += boundary_inset
        maximum -= boundary_inset
        span = maximum - minimum
        if span <= 0:
            raise ValueError(f"{label}没有可用布杆空间")
        intervals = max(2, math.ceil(span / maximum_spacing - 1.e-9))
        if intervals - 1 > 100:
            raise ValueError(f"{label}布置超出支持范围（按最大中心距计算超过100根）")
        return [minimum + span * index / intervals for index in range(1, intervals)]
    if parameters.get('barDistribution') != 'end_offsets':
        raise ValueError('不支持的布杆方式')
    start_offset = number(parameters, start_offset_key, 0)
    end_offset = number(parameters, end_offset_key, 0)
    if start_offset < 0 or end_offset < 0:
        raise ValueError(f"{label}首末端中心留距不能为负数")
    if maximum_spacing <= 0:
        raise ValueError(f"{label}最大中心距必须大于0")
    first = minimum + start_offset
    last = maximum - end_offset
    if first > last + 1.0e-7:
        raise ValueError(f"{label}首末端中心留距之和超过可用范围")
    span = max(0.0, last - first)
    if span <= 1.0e-7:
        return [(first + last) / 2]
    interval_count = max(1, math.ceil(span / maximum_spacing - 1.0e-9))
    if interval_count + 1 > 100:
        raise ValueError(f"{label}布置超出支持范围（按最大中心距计算超过100根）")
    actual_spacing = span / interval_count
    return [first + actual_spacing * index for index in range(interval_count + 1)]


def prepare(parameters: dict[str, Any], *, layout: str, fixed_width: float,
            leaf_width: float, leaf_depth: float, vertical_width: float) -> dict[str, Any]:
    effective = deepcopy(parameters)
    pattern = str(parameters.get("infillPattern", "grid"))
    if pattern not in {"vertical", "horizontal", "grid"}:
        raise ValueError("填充结构不支持")
    # Infill changes only the vertical faces; roof/base closure stays intact.
    if not parameters.get("accessDoorEnabled", False):
        return effective
    clear_width = number(parameters, "doorClearWidth", 800)
    clear_height = number(parameters, "doorClearHeight", 1000)
    if clear_width <= 0 or clear_height <= 0:
        raise ValueError("净开口尺寸必须大于0")
    if str(parameters.get("accessDoorFace", "front")) == "top":
        raise ValueError("逃生窗不支持顶面；请改选立面或底面并核对通路")
    # At the design opening position, reserve the actual leaf thickness.
    # The actual purchased hinge and its swept volume still require verification.
    fixed_clear_width = clear_width + leaf_depth
    frame_allowance = fixed_width * (2 if layout == "single-face" else 1)
    effective["doorWidth"] = fixed_clear_width + frame_allowance
    effective["doorHeight"] = clear_height + frame_allowance
    return effective


def finish(document: dict[str, Any], original: dict[str, Any], effective: dict[str, Any],
           *, layout: str, fixed_width: float, leaf_depth: float) -> dict[str, Any]:
    document["parameters"] = deepcopy(original)
    enabled = bool(original.get("accessDoorEnabled", False))
    allowance = fixed_width * (2 if layout == "single-face" else 1)
    fixed_clear_width = number(effective, "doorWidth", 0) - allowance if enabled else 0
    fixed_clear_height = number(effective, "doorHeight", 0) - allowance if enabled else 0
    single = layout == "single-face"
    frame_mode = str(original.get("frameManufacturingMode","segment_weld"))
    if layout in {"single-face","five-face"} and frame_mode=="spatial_v_notch":
        frame_mode="plane_v_notch"
    outer_join = (str(original.get("frameJoinType", "miter_45"))
                  if single and original.get("frameLayout", "four_sides") == "four_sides"
                  else "open_frame" if single else str(original.get("frameCornerJoin", "post_butt")))
    if (not single or original.get("frameLayout","four_sides")=="four_sides") and frame_mode!="segment_weld":
        outer_join="v_groove_90:tool_library"
    elif single and outer_join.startswith("v_groove"):
        outer_join="miter_45"
    active_joins = [outer_join]
    if enabled:
        active_joins += [str(original.get(key, "miter_45" if single else "butt_90"))
                         for key in ("doorFrameJoinType", "doorLeafFrameJoinType")]
    v_groove_active = any(join.startswith("v_groove") for join in active_joins)
    miter_active = any(join in {"miter_45", "rail_miter"} for join in active_joins)
    construction = "主横杆插入外框定位后焊接；插接不是免焊或承载认证。"
    review = {
        "reviewVersion": 2, "layout": layout, "dimensions": "outside",
        "openingEnabled": enabled, "openingUse": "escape",
        "fixedClearWidth": fixed_clear_width, "fixedClearHeight": fixed_clear_height,
        "designClearWidth": max(0, fixed_clear_width - leaf_depth),
        "designClearHeight": max(0, fixed_clear_height),
        "leafVerticalCount": sum(
            1 for item in document.get("items", [])
            if str(item.get("properties", {}).get("manufacturing.categoryKey", ""))
            == "access_door.leaf.vertical"
        ) if enabled else 0,
        "siteVerification": "required", "hardwareVerification": "required",
        "structuralVerification": "required", "complianceCertified": False,
        "outerFrameConnection": outer_join,
        "outerFrameManufacturing": {
            "mode":frame_mode,
            "members":[{"key":item["key"],"name":item.get("name",""),
                        "length":item.get("properties",{}).get("length"),
                        "bendCount":len(item.get("properties",{}).get("tubeDesigner.cornerProcess",{}).get("bendLocations",[]))}
                       for item in document.get("items",[]) if item["key"].startswith("outer_frame.")],
        },
        "vGrooveTrialRequired": v_groove_active,
        "displayHasUncutMiterStock": miter_active,
        "construction": construction,
        "boundary": ("four_sided_frame" if single and outer_join != "open_frame" else
                     "site_closure_required" if single or layout in {"two-face", "three-face"} else
                     "wall_backed_five_face"),
        "productFamily": "BAR_GRILLE",
        "envelope": {"single-face":"FLAT", "two-face":"L_CORNER", "three-face":"U_BOX", "five-face":"FULL_BOX"}[layout],
        "infillPattern": str(original.get("infillPattern", "grid")),
        "openingSystem": "LOCAL_HINGED_HATCH" if enabled else "FIXED",
        "performance": {key: "not_verified" for key in ("fallProtection", "intrusionResistance", "escape", "anchorage")},
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
        warning("SW_V_GROOVE_TRIAL", "连续框成形须按实际材质、壁厚、加工方向、成形顺序及设备打样，复核补偿、回弹和焊缝；理论展开不等于可直接批量生产。")
    if not enabled:
        warning("SW_NO_OPENING", "未设置应急开启口；住宅和出租房应核对所在房间的逃生及救援条件。", "accessDoorEnabled")
    if review["infillPattern"] == "horizontal":
        warning("SW_HORIZONTAL_CLIMB", "横杆填充具有攀爬风险，须按防坠用途及项目要求复核间距和安装高度。", "infillPattern")
    if layout == "three-face" and abs(number(original, "leftWidth", 0) - number(original, "rightWidth", 0)) > 1e-6:
        warning("SW_UNEVEN_WALL", "左右侧深不同，后端不在同一安装平面，请核对实际墙体。", "leftWidth", "rightWidth")
    spacing_keys = ["horizontalMaximumCenterSpacing", "verticalMaximumCenterSpacing"]
    if not single:
        spacing_keys += ["sideHorizontalMaximumCenterSpacing", "sideVerticalMaximumCenterSpacing"]
        if layout == "five-face":
            spacing_keys += ["topBottomCrossbarMaximumCenterSpacing", "topBottomRodMaximumCenterSpacing"]
    if any(number(original, key, 110) > 110 for key in spacing_keys):
        warning("SW_GRID_REVIEW", "当前杆件中心距较大，须复核全部边缘、窗扇及顶底网孔和儿童攀爬风险。", *spacing_keys)
    for item in document.get("items", []):
        properties = item.setdefault("properties", {})
        properties["manufacturing.installationVerification"] = "required"
    annotations=document.setdefault('extensions',{}).setdefault('tubeDesigner.specificationAnnotations',[])
    existing={a['parameter'] for a in annotations}
    pattern=original.get('infillPattern','grid')
    for key,active in [('horizontalMaximumCenterSpacing',pattern!='vertical'),
                       ('verticalMaximumCenterSpacing',pattern!='horizontal')]:
        if active and key not in existing:
            point=[number(original,'width',1200),0.,number(original,'height',1800)/2]
            annotations.append({'id':'security-window.single-bar.'+key,'parameter':key,'kind':'parameter',
                'start':point,'end':point,'offset':[150.,0.,80. if key.startswith('horizontal') else -80.],
                'generatedValue':original[key]})
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
    document["tables"].append({"key":"security_window_classification", "displayName":"防护窗结构与项目配置",
        "columns":[{"key":"name","displayName":"维度","valueType":"string"},{"key":"value","displayName":"配置","valueType":"string"}],
        "rows":[{"key":key,"values":{"name":label,"value":str(review[key])}} for key,label in (
            ("productFamily","产品族"),("envelope","外包络"),("infillPattern","立面填充"),
            ("openingSystem","开启结构"))]})
    return document


def generate_reviewed(parameters: dict[str, Any], context: dict[str, Any], *,
                      layout: str, load_profile: Any, kernel: Any) -> dict[str, Any]:
    """Translate public clear sizes once, retaining the exact normalized inputs."""
    fixed_width = leaf_depth = leaf_width = vertical_width = 0.0
    if parameters.get("accessDoorEnabled", False):
        fixed_width = load_profile(parameters, "doorFrame").width
        leaf = load_profile(parameters, "doorLeafFrame")
        leaf_width, leaf_depth = leaf.width, leaf.depth
        if parameters.get("infillPattern", "grid") != "horizontal":
            vertical_width = load_profile(parameters, "doorVertical").width
    if layout == 'single-face' and parameters.get('accessDoorEnabled',False):
        frame=load_profile(parameters,'frame')
        pattern=parameters.get('infillPattern','grid')
        horizontal=[] if pattern!='grid' else center_spacing_positions(parameters,0.,float(parameters['height']),
            boundary_inset=frame.width, start_offset_key='lastHorizontalBottomOffset',end_offset_key='firstHorizontalTopOffset',
            maximum_spacing_key='horizontalMaximumCenterSpacing',label='主横杆')
        margin=frame.width if parameters['frameLayout']!='top_bottom' else 0.
        vertical=[] if pattern=='horizontal' else center_spacing_positions(parameters,margin,float(parameters['width'])-margin,
            start_offset_key='verticalLeftCenterOffset',end_offset_key='verticalRightCenterOffset',
            maximum_spacing_key='verticalMaximumCenterSpacing',label='主竖杆')
        spec=importlib.util.spec_from_file_location('icax_opening_cells',Path(__file__).with_name('security_window_opening_layout.py'))
        opening_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(opening_module)
        if pattern=='grid':
            opening=opening_module.plan(parameters,frame=frame,fixed=load_profile(parameters,'doorFrame'),leaf=leaf,
                                        horizontal_positions=horizontal,vertical_positions=vertical)
        else:
            opening=opening_module.plan_single(parameters,frame=frame,fixed=load_profile(parameters,'doorFrame'),leaf=leaf,
                                                   vertical_positions=vertical)
        for name,low,high in (('horizontal','bottom','top'),('vertical','left','right')):
            if pattern==('vertical' if name=='horizontal' else 'horizontal'):continue
            half=load_profile(parameters,'door'+name.title()).width/2
            bounds=opening['leafBounds']
            opening[name+'Axes']=[v for v in opening[name+'Axes']
                if bounds[low]+leaf.width+half<=v<=bounds[high]-leaf.width-half]
        if pattern=='vertical':
            bounds=opening['leafBounds']
            opening['verticalAxes']=center_spacing_positions(parameters,
                bounds['left']+leaf.width,bounds['right']-leaf.width,
                start_offset_key='doorVerticalLeftCenterOffset',end_offset_key='doorVerticalRightCenterOffset',
                maximum_spacing_key='doorVerticalMaximumCenterSpacing',label='窗内竖杆')
        effective=deepcopy(parameters);effective['_escapeGridLayout']=opening
        axes=opening['boundaryAxes']; effective.update(doorLeft=axes['left']-fixed_width/2,
            doorBottom=axes['bottom']-fixed_width/2,doorWidth=axes['right']-axes['left']+fixed_width,
            doorHeight=axes['top']-axes['bottom']+fixed_width)
    else:
        effective = prepare(parameters, layout=layout, fixed_width=fixed_width,
                            leaf_width=leaf_width, leaf_depth=leaf_depth,
                            vertical_width=vertical_width)
    document = kernel(effective, context)
    document=finish(document, parameters, effective, layout=layout,
                    fixed_width=fixed_width, leaf_depth=leaf_depth)
    opening=effective.get('_escapeGridLayout') or document.get('extensions',{}).get('tubeDesigner.escapeWindowLayout')
    if opening:
        opening=deepcopy(opening)
        document['extensions']['tubeDesigner.escapeWindowLayout']=opening
        anchor=next(item for item in document['items'] if item['key'].startswith('access_door.leaf.frame.'))
        anchor['properties']['tubeDesigner.escapeWindowLayout']=deepcopy(opening)
        document['extensions']['tubeDesigner.securityWindowReview'].update({key:opening[key] for key in
            ('fixedClearWidth','fixedClearHeight','designClearWidth','designClearHeight')})
    return document
