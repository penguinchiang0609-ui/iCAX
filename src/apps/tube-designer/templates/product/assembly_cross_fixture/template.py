"""Hidden, process-neutral pair of continuous tubes with one real X node."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


TEMPLATE_ID = "assembly-cross-fixture"
TEMPLATE_VERSION = "1.0.0"
SHARED_ROOT = Path(__file__).resolve().parents[2] / "_shared"


def _shared(name):
    source = SHARED_ROOT / name
    module_name = "icax_assembly_cross_" + hashlib.sha256(source.read_bytes()).hexdigest()[:16]
    if module_name not in sys.modules:
        spec = importlib.util.spec_from_file_location(module_name, source)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"无法加载共享管材规则：{name}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
    return sys.modules[module_name]


def _side_anchor(key, length, face, normal, contact):
    station = length / 2
    return {
        "itemKey": key, "kind": "side", "face": face,
        "anchor": {"kind": "side", "face": face, "reference": "start", "station": station},
        "localAxialStation": station, "centerlinePoint": [0.0, 0.0, 0.0],
        "faceNormal": normal, "contactPoint": contact,
    }


def _generate_resource_document(parameters, context):
    p = parameters
    if not isinstance(p["productCode"], str) or not p["productCode"].strip():
        raise ValueError("产品编号不能为空")
    host_length, through_length = float(p["hostLength"]), float(p["throughLength"])
    if any(not math.isfinite(length) or length < 100 for length in (host_length, through_length)):
        raise ValueError("两根连续原管的长度无效")
    catalogue = _shared("tube_profile_catalog.py")
    host = catalogue.load_profile(p, "host")
    through = catalogue.load_profile(p, "through")
    if not host.hollow or not through.hollow:
        raise ValueError("十字贯穿需要两根中空管")
    if through.width >= host.width - 2 * host.wall:
        raise ValueError("贯穿管宽度必须小于主管宽度")
    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(context.get("template", {}).get("packageDigest", "")),
        parameters=deepcopy(p))
    shared_geometry = _shared("shared_tube_geometry.py")
    builder = shared_geometry.SharedTubeGeometry(model)
    members = (
        ("cross.host.0001", "主管", host, host_length,
         [-host_length / 2, 0.0, 0.0], [host_length / 2, 0.0, 0.0],
         [0.0, 1.0, 0.0], [0.0, 0.0, 1.0],
         {"top": [0.0, 0.0, 1.0], "bottom": [0.0, 0.0, -1.0],
          "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}),
        ("cross.through.0001", "贯穿管", through, through_length,
         [0.0, 0.0, -through_length / 2], [0.0, 0.0, through_length / 2],
         [1.0, 0.0, 0.0], [0.0, 1.0, 0.0],
         {"top": [-1.0, 0.0, 0.0], "bottom": [1.0, 0.0, 0.0],
          "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}),
    )
    item_keys = []
    for index, (key, name, profile, length, start, end, x_axis, y_axis, faces) in enumerate(members, 1):
        axis = [(end[i] - start[i]) / length for i in range(3)]
        stock = builder.emit_tube(key, profile_arguments={
            "placement": {"origin": start, "xAxis": x_axis, "yAxis": y_axis},
            "contours": profile.contours(),
        }, extrude_arguments={"vector": [end[i] - start[i] for i in range(3)]})
        item_keys.append(model.item(key, name,
            representations={"display": stock, "export": stock},
            properties={
                "partNumber": f"{p['productCode']}-{index:03d}",
                "length": length, "quantity": 1,
                "manufacturing.partKind": "tube", "manufacturing.sourcing": "made",
                "manufacturing.materialCategory": "tube",
                "manufacturing.materialGrade": "Q235",
                "manufacturing.categoryKey": "assembly-cross-fixture.tube",
                "manufacturing.categoryName": name,
                "tubeDesigner.profile": profile.properties(),
                "tubeDesigner.manufacturingAxis": axis,
                "tubeDesigner.manufacturingStartToEnd": axis,
                "tubeDesigner.assemblyPlanning": {"stockState": "uncut", "ready": False},
                "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square",
                                            "lengthBasis": "blank_axial_extent"},
                "assemblyFrame.member": {
                    "start": start, "end": end, "stockState": "uncut", "axisLength": length,
                    "stockInterval": {"startStation": 0.0, "endStation": length},
                    "sectionFrame": {"originAtStart": start, "xAxis": x_axis,
                                     "yAxis": y_axis, "centerlineUV": [0.0, 0.0],
                                     "faceNormals": faces},
                },
            }))
    model.relationship("cross.center", "assembly", item_keys,
        properties={"topology": "X", "centerlinePoint": [0.0, 0.0, 0.0],
                    "participantAnchors": [
                        _side_anchor("cross.host.0001", host_length, "top",
                                     [0.0, 0.0, 1.0], [0.0, 0.0, host.depth / 2]),
                        _side_anchor("cross.through.0001", through_length, "top",
                                     [-1.0, 0.0, 0.0], [-through.width / 2, 0.0, 0.0]),
                    ]})
    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    return to_resource_model(shared_geometry.finish_geometry_request(model, context))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    return to_display_model(_generate_resource_document(parameter_values, display_context(__file__)))




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        return to_manufacturing_model(
            _generate_resource_document(parameter_values, manufacturing_context(__file__)))
