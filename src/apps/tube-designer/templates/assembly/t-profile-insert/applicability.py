"""Template-owned, read-only local process applicability rules."""
import importlib.util
from pathlib import Path
import math

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_applicability_geometry", _path)
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    host = geometry.part(process_input, "host")
    branch = geometry.part(process_input, "branch")
    host_section = geometry.standard_section(host)
    branch_section = geometry.standard_section(branch)
    insertion = geometry.positive(parameters["insertDepth"], "支管插入深度")
    geometry.require(branch["length"] + insertion <= 100000,
                     "支管插入段所需下料补长超出允许的下料长度")
    angle = math.radians(process_input["geometry"]["intersectionAngle"])
    projection = abs(math.sin(angle))
    geometry.require(projection > 1e-6, "支管轴线不能与主管平行")
    if host_section is not None:
        # Same near/opposite-wall limits as this template's dimensionChecks.
        geometry.require(insertion - host_section["wall"] / projection >= 0.01,
                         "支管插入深度不足以越过主管近侧壁")
        geometry.require((host_section["depth"] - host_section["wall"]) / projection - insertion >= 0.01,
                         "支管插入深度会碰到主管对侧内壁")
        if host_section["kind"] == "round":
            # branch-profile removes the filled outer contour, enlarged by
            # fitGap, along (cos(theta), 0, sin(theta)). A depth check alone
            # can accept an opening that separates the circular host.
            geometry.require(branch_section is not None,
                             "圆主管单面插入口当前需要可直接测量的圆管或方矩管支管截面")
            branch_radius = (branch_section["width"] / 2 if branch_section["kind"] == "round"
                             else math.hypot(branch_section["width"], branch_section["depth"]) / 2)
            opening_radius = branch_radius + parameters["fitGap"]
            outer_radius = host_section["width"] / 2
            # Leave complete wall bands beyond both sides of the opening.
            # Inner-hole eccentricity reduces the smaller available band.
            inner_radius = outer_radius - host_section["wall"]
            geometry.require(opening_radius < inner_radius - abs(host_section["offsetX"]) - 0.01,
                             "圆主管轮廓插入口过宽，不能保留两侧连续壁厚材料；请减小支管截面")
            axial_half_span = (opening_radius + outer_radius * abs(math.cos(angle))) / projection
            location = geometry.site(host)
            geometry.require(axial_half_span < min(location["station"], host["length"] - location["station"]) - 0.01,
                             "圆主管轮廓插入口的完整轴向包络越过主管端部")
