"""Hidden process-neutral three-member corner for native contract regression."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model

TEMPLATE_ID = "assembly-orthogonal-corner"
TEMPLATE_VERSION = "1.0.0"
SHARED_ROOT = Path(__file__).resolve().parents[2] / "_shared"


def _shared(name):
    source = SHARED_ROOT / name
    key = "icax_corner_fixture_" + hashlib.sha256(source.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, source)
        if spec is None or spec.loader is None:
            raise RuntimeError("无法加载三向原管验证规则")
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        spec.loader.exec_module(module)
    return sys.modules[key]


def _length(parameters, key):
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) \
            or not math.isfinite(value) or not 100 <= value <= 2000:
        raise ValueError(key + " 必须是 100～2000 mm 的有限数值")
    return float(value)


def _anchor(key, end, length, allowance):
    return {"itemKey": key, "kind": "end", "end": end,
            "anchor": {"kind": "end", "end": end, "stockAllowance": allowance},
            "localAxialStation": length if end == "end" else 0.0,
            "centerlinePoint": [0.0, 0.0, 0.0]}


def _generate_resource_document(parameters, context):
    p = parameters
    if not isinstance(p["productCode"], str) or not p["productCode"].strip():
        raise ValueError("产品编号不能为空")
    catalogue = _shared("tube_profile_catalog.py")
    stock_rules = _shared("assembly_stock_allowance.py")
    geometry = _shared("shared_tube_geometry.py")
    profiles = {role: catalogue.load_profile(p, role) for role in ("a", "b", "c")}
    if any(not profile.hollow for profile in profiles.values()):
        raise ValueError("三向节点验证原管必须中空")
    lengths = {role: _length(p, role + "Length") for role in profiles}
    model = NeutralModel(template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(context.get("template", {}).get("packageDigest", "")),
        parameters=deepcopy(p))
    builder = geometry.SharedTubeGeometry(model)
    members = (
        ("a", [-lengths["a"], 0.0, 0.0], [0.0, 0.0, 0.0],
         [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], "end",
         {"top": [0.0, 0.0, 1.0], "bottom": [0.0, 0.0, -1.0],
          "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}),
        ("b", [0.0, 0.0, 0.0], [0.0, 0.0, lengths["b"]],
         [0.0, 1.0, 0.0], [-1.0, 0.0, 0.0], "start",
         {"top": [-1.0, 0.0, 0.0], "bottom": [1.0, 0.0, 0.0],
          "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}),
        ("c", [0.0, 0.0, 0.0], [0.0, lengths["c"], 0.0],
         [0.0, 0.0, -1.0], [-1.0, 0.0, 0.0], "start",
         {"top": [0.0, 0.0, 1.0], "bottom": [0.0, 0.0, -1.0],
          "right": [-1.0, 0.0, 0.0], "left": [1.0, 0.0, 0.0]}),
    )
    source_sections = {role: {"placement": {"origin": [0.0, 0.0, 0.0],
        "xAxis": x_axis, "yAxis": y_axis}, "contours": profiles[role].contours()}
        for role, _, _, x_axis, y_axis, _, _ in members}
    keys, anchors, rows = [], [], []
    for index, (role, start, end, x_axis, y_axis, node_end, faces) in enumerate(members, 1):
        length = lengths[role]
        axis = [(end[i] - start[i]) / length for i in range(3)]
        # Stock extends to the opposing section's actual long point. This is
        # uncut material, independent of the eventual miter or wrap selection.
        allowance = 0.0 if role == "c" else stock_rules.opposing_half_extent(
            source_sections["b" if role == "a" else "a"], axis,
            node_point=(0.0, 0.0, 0.0))
        stock = stock_rules.stock_span(start, end,
            allowance if node_end == "start" else 0.0,
            allowance if node_end == "end" else 0.0)
        key = "corner." + role + ".0001"
        solid = builder.emit_tube(key, profile_arguments={
            "placement": {"origin": stock["start"], "xAxis": x_axis, "yAxis": y_axis},
            "contours": profiles[role].contours()},
            extrude_arguments={"vector": [stock["end"][i] - stock["start"][i] for i in range(3)]})
        name = role.upper() + " 边原管"
        part_number = f"{p['productCode']}-{index:03d}"
        keys.append(model.item(key, name, representations={"display": solid, "export": solid},
            properties={"partNumber": part_number, "length": stock["length"], "quantity": 1,
                "manufacturing.partKind": "tube", "manufacturing.sourcing": "made",
                "manufacturing.materialCategory": "tube", "manufacturing.materialGrade": "Q235",
                "manufacturing.categoryKey": "assembly-orthogonal-corner.tube",
                "manufacturing.categoryName": name,
                "tubeDesigner.profile": profiles[role].properties(),
                "tubeDesigner.manufacturingAxis": axis,
                "tubeDesigner.manufacturingStartToEnd": axis,
                "tubeDesigner.assemblyPlanning": {"stockState": "uncut", "ready": False},
                "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square",
                                            "lengthBasis": "blank_axial_extent"},
                "assemblyFrame.member": {"start": start, "end": end, "axisLength": length,
                    "stockState": "uncut", "stockInterval": stock["stockInterval"],
                    "sectionFrame": {"originAtStart": start, "xAxis": x_axis, "yAxis": y_axis,
                                     "centerlineUV": [0.0, 0.0], "faceNormals": faces}}}))
        anchors.append(_anchor(key, node_end, length, allowance))
        rows.append({"key": "part." + str(index), "itemKey": key,
                     "values": {"partNumber": part_number, "name": name,
                                "length": stock["length"], "quantity": 1}})
    model.relationship("corner.center", "assembly", keys, properties={
        "topology": "orthogonal-corner", "centerlinePoint": [0.0, 0.0, 0.0],
        "participantAnchors": anchors})
    model.output("display.default", "display", keys)
    model.output("export.manufacturing", "export", keys)
    model.table("parts", "三向节点原管", columns=[
        {"key": "partNumber", "displayName": "编号", "valueType": "string"},
        {"key": "name", "displayName": "名称", "valueType": "string"},
        {"key": "length", "displayName": "原料长", "valueType": "number", "unit": "mm"},
        {"key": "quantity", "displayName": "数量", "valueType": "integer"}], rows=rows)
    return to_resource_model(geometry.finish_geometry_request(model, context))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    return to_display_model(_generate_resource_document(parameter_values, display_context(__file__)))




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        return to_manufacturing_model(
            _generate_resource_document(parameter_values, manufacturing_context(__file__)))
