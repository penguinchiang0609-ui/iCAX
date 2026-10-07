"""Compose existing end cuts and receiving tools at a three-ended corner."""
import copy
import importlib.util
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location("icax_corner_geometry_plan", Path(__file__).with_name("geometry.py"))
geometry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)


def _shift(matrix, delta):
    result = list(matrix)
    for at, value in zip((3, 7, 11), delta):
        result[at] += value
    return result


def build_plan(plan):
    parameters = plan["parameters"]
    sizes = geometry.process_sizes(plan["processInput"], parameters)
    if sizes.get("contactRequirements"):
        plan["contactRequirements"] = copy.deepcopy(sizes["contactRequirements"])
    extension, near, insertion = (sizes[key] for key in ("extension", "near", "insertion"))
    designs = {part["role"]: part for part in plan["designParts"]}
    blanks = {part["sourceRole"]: part for part in plan["manufacturingParts"]}
    for role, blank in blanks.items():
        blank["request"]["length"] = designs[role]["request"]["length"] + (extension if role != "memberC" else 0)
        matrix = list(designs[role]["matrix"])
        if role == "memberB":
            matrix = _shift(matrix, [-extension * matrix[at] for at in (0, 4, 8)])
        blank["matrix"] = matrix
        blank["compareMatrix"] = list(matrix)
        away = -1 if role == "memberA" else 1
        blank["explodedMatrix"] = _shift(matrix, [away * 100 * matrix[at] for at in (0, 4, 8)])
    if parameters["abJoint"] == "wrap":
        blanks["memberB"]["request"]["ends"]["start"]["trim"] = 2 * extension
    c_end = blanks["memberC"]["request"]["ends"]["start"]
    c_end["trim"] = near - insertion
    for role in ("memberA", "memberB"):
        actual = plan["processInput"]["parts"]
        placement = (geometry.continuous_receiving_placement(actual[role], actual["memberC"], parameters["cJoint"] == "tabs")
                     if parameters["abJoint"] == "continuous" else
                     {"face": "right", "azimuth": 90, "roll": 90 if role == "memberA" else 180,
                      "pairRotation": 90 if role == "memberA" else 0})
        for feature in blanks[role]["request"]["features"]:
            feature.update(face=placement["face"], reference="start", offset=0,
                           station=designs[role]["request"]["length"] if role == "memberA" else extension)
            if parameters["abJoint"] == "continuous":
                # A/B have no end-cut operation from which the generic local
                # evaluator can infer the sample node end. Express zero
                # distance from its default end datum for both receivers;
                # the explicit host anchor then chooses the real start/end.
                feature.update(reference="end", station=0)
            if feature["toolRef"]["id"] == "branch-profile":
                feature.update(angle=90, azimuth=placement["azimuth"], roll=placement["roll"],
                               direction="positive", length=2 * near + 20)
            else:
                feature["toolParameters"]["pairRotation"] = placement["pairRotation"]
                feature["toolParameters"]["allowSideOpening"] = sizes["allowSideOpening"]
    # Keep the workflow as a precise description of the same standard request.
    for operation in plan["resolvedWorkflow"]["partOperations"]:
        role = next(part["sourceRole"] for part in blanks.values() if part["blankId"] == operation["blankId"])
        request = blanks[role]["request"]
        entry = (request["ends"]["end" if role == "memberA" else "start"]
                 if operation["placement"]["target"] == "end" else
                 next(feature for feature in request["features"] if feature["id"].endswith(operation["processId"])))
        operation["toolParameters"] = copy.deepcopy(entry["toolParameters"])
        for key in operation["operationParameters"]:
            if key in entry:
                operation["operationParameters"][key] = copy.deepcopy(entry[key])
        for key in operation["placement"]:
            if key in entry:
                operation["placement"][key] = copy.deepcopy(entry[key])
        # The native feature reader persists generic operation fields through
        # this declared map; arbitrary top-level azimuth/roll fields alone are
        # not retained when the punch feature is normalized.
        entry["operationParameterValues"] = copy.deepcopy(operation["operationParameters"])
    return plan


def build_bound_operations(context):
    parameters = context["parameters"]
    participants = context["participants"]
    roles = ("memberA", "memberB", "memberC")
    for role in roles:
        geometry.queries.require(set(participants[role].get("nodeFrame", {})) == {
            "selfAwayPart", "selfNodePart", "awayPartsByRole", "sourceToPart", "partToSource"},
            "三向节点必须提供全部真实构件的已核对制造坐标")
        frame = participants[role]["section"].get("sectionFrame", {})
        geometry.queries.require(frame.get("verification") == "committed-source-brep-replay",
                                 "三向节点需要已核对真实原管截面")
        geometry.queries.require(all(abs(geometry.dot(participants[role]["nodeFrame"]["awayPartsByRole"][a],
            participants[role]["nodeFrame"]["awayPartsByRole"][b])) < 1e-6
            for a, b in ((roles[0], roles[1]), (roles[0], roles[2]), (roles[1], roles[2]))),
            "三根构件必须互相垂直")
    geometry.verify_node_frames(participants)
    sections = [geometry.real_size(participants[role]["section"]) for role in roles]
    c = participants["memberC"]
    c_frame = c["section"]["sectionFrame"]
    c_manufacturing_z_world = geometry.vector(c["nodeFrame"]["partToSource"], [0, 0, 1])
    if parameters["abJoint"] == "continuous" and parameters["cJoint"] != "weld":
        frame = c["nodeFrame"]
        geometry.require_continuous_insertion_alignment(
            geometry.vector(frame["sourceToPart"], c_frame["xAxisWorld"]),
            geometry.vector(frame["sourceToPart"], c_frame["yAxisWorld"]),
            [frame["awayPartsByRole"][role] for role in roles[:2]])
    if parameters["cJoint"] == "tabs":
        width_alignment = abs(geometry.dot(c_manufacturing_z_world, c_frame["xAxisWorld"]))
        depth_alignment = abs(geometry.dot(c_manufacturing_z_world, c_frame["yAxisWorld"]))
        geometry.queries.require(max(width_alignment, depth_alignment) > 1-1e-6,
                                 "C 插舌所在制造壁必须与保存截面平直壁对齐")
        if width_alignment > depth_alignment:
            # Normalizing a product stock may quarter-turn its section. The
            # reused tab tool retains physical manufacturing Z walls, so its
            # tab width follows the other actual contour direction.
            for left, right in (("width", "depth"), ("flatWidth", "flatDepth"), ("innerWidth", "innerDepth")):
                sections[2][left], sections[2][right] = sections[2][right], sections[2][left]
    insets = {role: geometry.stock_inset(participants[role]) for role in roles}
    # Continuous A/B receivers may present any actual flat side wall to C.
    # Match dimensions to that wall before checking cavity depth and C trim;
    # the operation below uses the same verified normal to place the cut.
    for index, role in enumerate(roles[:2]):
        part = participants[role]
        frame = part["nodeFrame"]
        width_axis = geometry.vector(frame["sourceToPart"], part["section"]["sectionFrame"]["xAxisWorld"])
        depth_axis = geometry.vector(frame["sourceToPart"], part["section"]["sectionFrame"]["yAxisWorld"])
        c_axis = frame["awayPartsByRole"]["memberC"]
        width_alignment = abs(geometry.dot(width_axis, c_axis))
        depth_alignment = abs(geometry.dot(depth_axis, c_axis))
        if parameters["abJoint"] != "continuous":
            geometry.queries.require(width_alignment > 1-1e-6,
                                     "A / B 截面宽向必须共同朝向 C 轴线")
        else:
            geometry.queries.require(max(width_alignment, depth_alignment) > 1-1e-6,
                                     "C 接近方向必须与 A / B 实际平直侧壁法向一致")
            if depth_alignment > width_alignment:
                for left, right in (("width", "depth"), ("flatWidth", "flatDepth"),
                                    ("innerWidth", "innerDepth"), ("offsetX", "offsetY")):
                    sections[index][left], sections[index][right] = sections[index][right], sections[index][left]
    sizes = geometry.check_sizes(*sections,
        [participants[role]["length"] - insets[role] for role in roles], parameters)
    extension, near, insertion = (sizes[key] for key in ("extension", "near", "insertion"))
    if parameters["abJoint"] != "continuous":
        geometry.queries.require(all(insets[role] >= extension - geometry.EPS for role in roles[:2]),
                                 "A / B 原管需要预留真实节点长点库存余量")
    if parameters["abJoint"] == "wrap":
        geometry.queries.require(abs(insets["memberA"] - extension) < geometry.EPS,
                                 "包接 A 长端库存余量必须与 B 半深一致")
    operations = []
    for process in context["processes"]:
        pid, role = process["id"], process["role"]
        part, values = participants[role], copy.deepcopy(process["values"])
        frame = part["nodeFrame"]
        anchor = {"kind": "end", "end": part["anchor"]["end"], "trim": 0, "rotation": 0}
        section_frame = None
        if pid.startswith("ab-miter-"):
            other = "memberB" if role == "memberA" else "memberA"
            axis = frame["awayPartsByRole"][other]
            values.update(angle=math.copysign(45, frame["selfAwayPart"][0]),
                          rotation=math.degrees(math.atan2(axis[2], axis[1])),
                          trim=insets[role], datum="center")
        elif pid == "ab-wrap-b":
            values.update(angle=0, trim=insets[role] + extension, rotation=0, datum="long")
        elif role == "memberC":
            values.update(trim=insets[role] + near - insertion, rotation=0)
            if process["tool"]["id"] == "end-miter":
                values.update(angle=0, datum="long")
        else:
            axis = geometry.normalized(frame["awayPartsByRole"]["memberC"])
            anchor = {"kind": "side", "face": geometry.face_for(axis), "reference": "start",
                "station": part["length"] / 2 + frame["selfNodePart"][0],
                "offset": 0, "rotation": 0}
            section_frame = copy.deepcopy(c_frame)
            section_frame.update(xAxisToolPart=geometry.vector(frame["sourceToPart"], c_frame["xAxisWorld"]),
                yAxisToolPart=geometry.vector(frame["sourceToPart"], c_frame["yAxisWorld"]),
                nodeToolPart=list(frame["selfNodePart"]), hostFaceNormalToolPart=list(axis),
                branchManufacturingZHostToolPart=geometry.vector(frame["sourceToPart"], c_manufacturing_z_world))
            if process["tool"]["id"] == "branch-profile":
                values.update(angle=math.degrees(math.acos(max(-1, min(1, axis[0])))),
                    azimuth=math.degrees(math.atan2(axis[1], axis[2])), roll=0,
                    direction="positive", length=2*near+20, offsetY=0, offsetZ=0)
            else:
                values.update(pairRotation=0, allowEndOpening=False,
                              allowSideOpening=sizes["allowSideOpening"])
        operation = {"processId": pid, "role": role, "values": values, "anchor": anchor}
        if section_frame is not None:
            operation["sectionFrame"] = section_frame
        operations.append(operation)
    receivers = roles[:2] if parameters["abJoint"] in ("miter", "continuous") else roles[:1]
    contacts = ([] if parameters["abJoint"] == "continuous" else
                [{"roles": [roles[0], roles[1]], "maximumSeparationMm": 0.05}])
    contacts.extend({"roles": [role, "memberC"], "maximumSeparationMm": sizes["gap"] + 0.05} for role in receivers)
    return {"parameters": copy.deepcopy(parameters), "operations": operations, "nodeContacts": contacts}
