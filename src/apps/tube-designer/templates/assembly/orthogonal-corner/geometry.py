"""Direct measurements and feasibility rules for the three-ended corner.

This module performs no section generation, tool generation or candidate search.
"""
import importlib.util
import math
from pathlib import Path


def load_shared(name):
    path = Path(__file__).resolve().parents[2] / "_shared" / (name + ".py")
    spec = importlib.util.spec_from_file_location("icax_corner_" + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


queries = load_shared("assembly_applicability_geometry")
sections = load_shared("section_geometry")
EPS = 0.01


def check_sizes(a, b, c, lengths, parameters, contact_requirements=None):
    for label, section in zip("ABC", (a, b, c)):
        queries.require(section["kind"] == "rect", label + " 当前需要方矩管")
        queries.centered_section(section, label)
        queries.uniform_section(section, label)
    queries.require(a == b, "A / B 必须采用相同有效截面")
    # A/B are rotated around their common section-width direction. Their
    # coplanar near walls jointly cover the C end around the miter seam.
    gap = parameters["fitGap"] if parameters["cJoint"] == "insert" else (
        parameters["sideClearance"] if parameters["cJoint"] == "tabs" else 0)
    footprint = max(c["width"], c["depth"]) + 2 * gap
    if parameters["cJoint"] == "weld" and not contact_requirements:
        # A/B jointly provide the projected corner envelope. A weld end
        # stops at their near outside surface; it does not enter the cavity.
        # Equal outside sizes may meet at the edge, including rounded-wall
        # weld gaps. This is not a full planar-contact or weld-strength check.
        queries.require(footprint <= a["depth"] + 1e-6,
                        "C 贴焊端口超出 A / B 共同角面外包络")
    elif parameters["cJoint"] == "tabs" and parameters["abJoint"] == "continuous":
        # The body stops at the near wall; only the retained two Z skins enter.
        # An equal-size skin pair needs shallow edge sockets, not an opening
        # large enough to insert the complete C section through the cavity.
        queries.require(c["depth"] <= a["depth"] + 1e-6,
                        "C 插舌壁面超出 A / B 共同角面外包络")
    elif parameters["cJoint"] != "weld":
        queries.require(footprint < min(a["flatDepth"], a["innerDepth"]) - 2 * EPS,
                        "C 端口须小于 A / B 共同角面及内孔，保留连续平直壁料")
    extension = 0 if parameters["abJoint"] == "continuous" else a["depth"] / 2
    queries.require(all(lengths[index] + extension <= 100000 for index in (0, 1)),
                    "A / B 加工库存余量后长度超出允许范围")
    queries.require(min(lengths[:2]) > extension + footprint / 2 + EPS,
                    "A / B 太短，节点加工会贯通远端")
    near = a["width"] / 2
    insertion = (parameters["insertDepth"] if parameters["cJoint"] == "insert" else
                 parameters["tabLength"] if parameters["cJoint"] == "tabs" else 0)
    queries.require(lengths[2] > near + EPS, "C 长度必须越过共同角面，并保留外露直管")
    if parameters["cJoint"] != "weld":
        queries.require(insertion > a["wall"] + EPS, "C 插入量必须穿过承接近侧壁")
        queries.require(insertion < near - EPS, "C 插入量须停留在近侧半孔内，不能越过节点中心")
    if parameters["cJoint"] == "tabs":
        width, clearance = parameters["tabWidth"], parameters["sideClearance"]
        queries.require(insertion > width / 2 + EPS, "圆弧插舌长度须大于半宽")
        # A committed stock can be quarter-turned by native normalization.
        # Both possible retained-wall pairs must satisfy the same constraints.
        c_flat = min(c["flatWidth"], c["flatDepth"],
                     c["innerWidth"] - 2*max(c["innerRadii"]),
                     c["innerDepth"] - 2*max(c["innerRadii"]))
        queries.require(width < c_flat - 2 * EPS, "圆弧插舌宽度超出 C 内外共同平直壁面")
        # The reused end-tab tool currently uses the receiving contour's
        # depth as its conservative penetration bound, even for a side face.
        queries.require(insertion < a["depth"] - a["wall"] - EPS,
                        "插舌长度超出端切模具的承接截面深度上限")
        opening = c["wall"] + 2 * clearance
        queries.require(width + opening < a["flatDepth"] - 2 * EPS,
                        "圆弧母槽超出承接平直壁面")
        if parameters["abJoint"] == "miter":
            # Each ear must be entirely on one side of x+z=0. A capsule
            # crossing the seam would have no continuous retaining wall.
            queries.require(min(c["width"], c["depth"]) - c["wall"] > width + 2 * opening + 2 * EPS,
                            "公母槽与 A / B 斜接缝冲突，应增大 C 深度或减小插舌宽度及间隙")
    result = {"extension": extension, "near": near, "insertion": insertion, "gap": gap,
              "allowSideOpening": parameters["abJoint"] == "continuous" and parameters["cJoint"] == "tabs"}
    if contact_requirements:
        result["contactRequirements"] = contact_requirements
    return result


def product_sizes(product, parameters):
    spans = [queries.span(product, "arm" + letter) for letter in "ABC"]
    sizes = [queries.rectangular_section(span, letter) for span, letter in zip(spans, "ABC")]
    return check_sizes(*sizes, [span["length"] for span in spans], parameters)


def process_sizes(process_input, parameters):
    parts = [queries.part(process_input, "member" + letter) for letter in "ABC"]
    sizes = []
    for part, letter in zip(parts, "ABC"):
        section = queries.standard_section(part)
        if section is None:
            actual = part.get("section", {})
            profile = actual.get("profile", actual)
            queries.require(isinstance(profile.get("contours"), list),
                            letter + " 必须提供真实方矩管截面")
            section = real_size(profile)
        queries.require(section["kind"] == "rect", letter + " 当前需要方矩管")
        sizes.append(section)
    c_axis = queries.axis(parts[2])
    generalized_wall = parameters["abJoint"] == "continuous"
    c_away = [(-1 if parts[2].get("anchor",{}).get("end") == "end" else 1)*value
              for value in c_axis]
    for index in (0,1):
        if not generalized_wall:
            # The retained receiving tools place their cut on the positive
            # width face. Do not broaden their input range by merely rotating
            # the feasibility dimensions while leaving the actual cut there.
            queries.require(queries.dot(c_away,queries.axis(parts[index],1))>1-1e-6,
                            "当前三向节点加工工具需要 C 朝向 A / B 的正宽向侧面")
        along_y = abs(queries.dot(c_axis,queries.axis(parts[index],1)))
        along_z = abs(queries.dot(c_axis,queries.axis(parts[index],2)))
        queries.require(max(along_y,along_z)>1-1e-6,
                        "C 接近方向必须与 A / B 实际平直侧壁法向一致")
        if along_z>along_y:
            current = dict(sizes[index])
            for left,right in (("width","depth"),("flatWidth","flatDepth"),("innerWidth","innerDepth"),
                               ("offsetX","offsetY")):
                current[left],current[right] = current[right],current[left]
            sizes[index] = current
    if generalized_wall:
        _, away = continuous_node_frames(parts)
        if parameters["cJoint"] != "weld":
            require_continuous_insertion_alignment(queries.axis(parts[2], 1), queries.axis(parts[2], 2), away[:2])
    contacts = continuous_weld_contacts(parts,sizes) if generalized_wall and parameters["cJoint"] == "weld" else None
    return check_sizes(*sizes, [part["length"] for part in parts], parameters, contacts)


def continuous_node_frames(parts):
    """Check the physical common node before choosing a receiving wall."""
    nodes, away = [], []
    for part in parts:
        anchor = part.get("anchor", {})
        queries.require(anchor.get("kind") == "end" and anchor.get("end") in ("start", "end"),
                        "连续三向节点必须明确选择实际管段端点")
        at_end = anchor["end"] == "end"
        nodes.append(point(part["matrix"], [part["length"] if at_end else 0, 0, 0]))
        away.append([(-1 if at_end else 1) * value for value in queries.axis(part)])
    queries.require(all(math.dist(nodes[0], node) < 1e-6 for node in nodes[1:]),
                    "连续三向节点的三根管段必须共用实际端点")
    queries.require(all(abs(dot(away[a], away[b])) < 1e-6 for a, b in ((0, 1), (0, 2), (1, 2))),
                    "连续三向节点必须采用真实互相垂直的管段")
    return nodes, away


def require_continuous_insertion_alignment(width_axis, depth_axis, receiving_axes):
    # The corner's insertion feasibility uses the two declared rectangle
    # dimensions. An oblique rectangle needs its complete projected envelope;
    # reject that case rather than treating max(width, depth) as its span.
    queries.require(all(max(abs(dot(axis, receiving)) for receiving in receiving_axes) > 1 - 1e-6
                        for axis in (width_axis, depth_axis)),
                    "连续三向插接的 C 截面壁面必须与 A / B 轴向对齐")


def continuous_receiving_placement(host, branch, tabs=False):
    """Derive a cut in the actual host coordinates from the branch axes."""
    sign = -1 if branch["anchor"]["end"] == "end" else 1
    axis = [sign * dot(queries.axis(branch), queries.axis(host, column)) for column in range(3)]
    face = face_for(axis)
    azimuth = math.atan2(axis[1], axis[2])
    # A perpendicular branch has the tool's unrolled U along the host axis.
    # Project its actual contour U into that plane to preserve a rectangular
    # branch's physical width and depth instead of guessing a quarter-turn.
    u = [1., 0., 0.]
    v = [0., math.cos(azimuth), -math.sin(azimuth)]
    branch_u = [dot(queries.axis(branch, 1), queries.axis(host, column)) for column in range(3)]
    roll = math.degrees(math.atan2(dot(branch_u, v), dot(branch_u, u)))
    tabs_z = [dot(queries.axis(branch, 2), queries.axis(host, column)) for column in range(3)]
    normal_index, across_index = (2, 1) if face in ("top", "bottom") else (1, 2)
    if tabs:
        queries.require(abs(tabs_z[normal_index]) < 1e-6
                        and max(abs(tabs_z[0]), abs(tabs_z[across_index])) > 1 - 1e-6,
                        "C 插舌壁面必须与实际承接面的轴向或横向对齐")
    return {"face": face, "azimuth": math.degrees(azimuth), "roll": roll,
            "pairRotation": 90 if abs(tabs_z[0]) > 1 - 1e-6 else 0}


def _clip_rectangle(polygon, bounds):
    """Intersect a convex material patch with an actual straight host wall."""
    for axis, limit, sign in ((0,bounds[0],1),(0,bounds[1],-1),
                             (1,bounds[2],1),(1,bounds[3],-1)):
        clipped = []
        if not polygon:
            break
        previous = polygon[-1]
        previous_inside = sign*(previous[axis]-limit) >= 0
        for current in polygon:
            inside = sign*(current[axis]-limit) >= 0
            if inside != previous_inside:
                fraction = (limit-previous[axis])/(current[axis]-previous[axis])
                clipped.append([previous[i]+fraction*(current[i]-previous[i]) for i in (0,1)])
            if inside:
                clipped.append(current)
            previous,previous_inside = current,inside
        polygon = clipped
    return polygon


def _polygon_area(polygon):
    return abs(sum(point[0]*polygon[(index+1)%len(polygon)][1]
                   -point[1]*polygon[(index+1)%len(polygon)][0]
                   for index,point in enumerate(polygon)))/2 if polygon else 0


def continuous_weld_contacts(parts, sizes):
    """Prove positive flat-wall contact using actual endpoint and section axes.

    The four patches are contained in C's measured annular section. Rounded
    corners are deliberately excluded, so this does not approximate their arcs
    or accept an overlap of hollow envelopes as material contact. The executor
    also verifies contact and penetration against the final processed solids.
    """
    _,away = continuous_node_frames(parts)
    # process_sizes has already rotated A/B measurements into the C-normal
    # direction. The two near wall planes therefore coincide at this distance.
    queries.require(abs(sizes[0]["width"]-sizes[1]["width"])<1e-6,
                    "A / B 朝向 C 的近壁面必须位于同一平面")
    c = sizes[2]
    horizontal = max(0,min(c["flatWidth"],c["innerWidth"]-2*max(c["innerRadii"])))/2
    vertical = max(0,min(c["flatDepth"],c["innerDepth"]-2*max(c["innerRadii"])))/2
    w,d,t = c["width"]/2,c["depth"]/2,c["wall"]
    rectangles = [(-horizontal,horizontal,d-t,d),(-horizontal,horizontal,-d,-d+t),
                  (w-t,w,-vertical,vertical),(-w,-w+t,-vertical,vertical)]
    c_y,c_z = queries.axis(parts[2],1),queries.axis(parts[2],2)
    patches = []
    for left,right,bottom,top in rectangles:
        if right<=left or top<=bottom:
            continue
        patches.append([[dot([x*y+y0*z for y,z in zip(c_y,c_z)],axis)
                         for axis in away[:2]]
                        for x,y0 in ((left,bottom),(right,bottom),(right,top),(left,top))])
    bounds = [(0,parts[0]["length"],-sizes[0]["flatDepth"]/2,sizes[0]["flatDepth"]/2),
              (-sizes[1]["flatDepth"]/2,sizes[1]["flatDepth"]/2,0,parts[1]["length"])]
    contacts = []
    for index,host in enumerate(bounds):
        area = sum(_polygon_area(_clip_rectangle(patch,host)) for patch in patches)
        queries.require(area>1e-6,
                        "C 贴焊端面的实际壁料与 "+("A" if index==0 else "B")+" 近壁面没有正面积接触")
        contacts.append({"roles":["member"+("A" if index==0 else "B"),"memberC"],
                         "kind":"positive-area-contact",
                         "minimumContactAreaMm2":min(1e-4,area*1e-6),
                         "maximumIntersectionVolumeMm3":1e-5})
    return contacts


def real_size(profile):
    """Read the committed boundary, including actual inner and outer flats."""
    shell = sections.closed_shell_metrics(sections.from_profile(profile))
    queries.require(not shell.get("circularShell"), "三向节点当前需要平直方矩管壁面")
    outside, inside = shell["outside"], shell["inside"]
    for axis in (0, 1):
        queries.require(abs(outside["min"][axis] + outside["max"][axis]) < EPS
                        and abs(inside["min"][axis] + inside["max"][axis]) < EPS,
                        "真实方矩管内外轮廓必须居中")
    widths = [outside["max"][axis] - outside["min"][axis] for axis in (0, 1)]
    wall = shell["wallThickness"]
    queries.require(all(abs(outside["max"][axis] - inside["max"][axis] - wall) < EPS
                        and abs(inside["min"][axis] - outside["min"][axis] - wall) < EPS
                        for axis in (0, 1)), "真实方矩管四壁必须等厚")
    flat = []
    radii = []
    for contour in shell["contours"]:
        arcs = [edge["radius"] for edge in contour["edges"] if edge["kind"] == "circleArc"]
        queries.require(all(edge["kind"] in ("line", "circleArc") for edge in contour["edges"]),
                        "三向节点当前不支持非方矩管边界")
        queries.require(not arcs or max(arcs) - min(arcs) < EPS, "真实方矩管当前需要统一圆角")
        radii.append(arcs[0] if arcs else 0)
    for axis in (0, 1):
        lengths = [abs(edge["end"][axis] - edge["start"][axis]) for edge in shell["contours"][0]["edges"]
                   if edge["kind"] == "line" and abs(edge["end"][1-axis] - edge["start"][1-axis]) < EPS]
        queries.require(len(lengths) == 2 and min(lengths) > EPS, "真实方矩管需要四边平直壁面")
        flat.append(min(lengths))
    return {"kind": "rect", "width": widths[0], "depth": widths[1], "wall": wall,
            "offsetX": 0., "offsetY": 0., "outerRadii": [radii[0]] * 4,
            "innerRadii": [radii[1]] * 4, "flatWidth": flat[0], "flatDepth": flat[1],
            "innerWidth": widths[0] - 2 * wall, "innerDepth": widths[1] - 2 * wall}


def dot(a, b): return sum(x*y for x, y in zip(a, b))
def vector(matrix, value): return [sum(matrix[row*4+column]*value[column] for column in range(3)) for row in range(3)]
def point(matrix, value): return [component + matrix[at] for component, at in zip(vector(matrix, value), (3, 7, 11))]
def normalized(value):
    magnitude = math.sqrt(dot(value, value))
    queries.require(abs(magnitude - 1) < 1e-6, "已核对节点方向必须是单位向量")
    return list(value)


def face_for(axis):
    normalized(axis)
    index = max((1, 2), key=lambda i: abs(axis[i]))
    queries.require(abs(axis[index]) > 1 - 1e-6 and abs(axis[0]) < 1e-6,
                    "C 轴线必须垂直承接方矩管平直侧面")
    return ("right" if axis[1] > 0 else "left") if index == 1 else ("top" if axis[2] > 0 else "bottom")


def stock_inset(part):
    frame, length = part["nodeFrame"], part["length"]
    away, node = frame["selfAwayPart"], frame["selfNodePart"]
    queries.require(abs(abs(away[0])-1) < 1e-6 and abs(node[1]) < EPS and abs(node[2]) < EPS,
                    "实际节点必须位于管材轴线上")
    queries.require((away[0] > 0) == (part["anchor"]["end"] == "start"), "实际端点与节点远离方向不一致")
    inset = length / 2 + away[0] * node[0]
    queries.require(inset >= -EPS and abs(inset - part["anchor"].get("stockAllowance", inset)) < EPS,
                    "已核对节点位置与产品库存余量不一致")
    return max(0., inset)


def verify_node_frames(participants):
    nodes, axes = [], {}
    for role, part in participants.items():
        frame = part["nodeFrame"]
        forward, inverse = frame["sourceToPart"], frame["partToSource"]
        queries.require(all(abs(forward[row*4+col] - (1 if col == 3 else 0)) < 1e-6
                            and abs(inverse[row*4+col] - (1 if col == 3 else 0)) < 1e-6
                            for row in (3,) for col in range(4)), "节点映射必须是仿射刚体矩阵")
        for row in range(3):
            for col in range(3):
                queries.require(abs(sum(forward[row*4+k]*inverse[k*4+col] for k in range(3)) - (1 if row == col else 0)) < 1e-6,
                                "节点制造映射与逆映射不一致")
        queries.require(math.dist(point(forward, point(inverse, [0,0,0])), [0,0,0]) < EPS,
                        "节点制造映射的平移不一致")
        nodes.append(point(inverse, frame["selfNodePart"]))
        current = {key: normalized(vector(inverse, value)) for key, value in frame["awayPartsByRole"].items()}
        if axes:
            queries.require(all(math.dist(value, axes[key]) < 1e-6 for key, value in current.items()),
                            "三根真实构件的共同节点方向不一致")
        axes = current
    queries.require(all(math.dist(nodes[0], node) < EPS for node in nodes[1:]),
                    "三根真实构件不共用同一端点节点")
