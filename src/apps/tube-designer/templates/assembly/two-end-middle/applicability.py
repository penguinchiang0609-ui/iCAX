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
    h = geometry.standard_section(host)
    geometry.standard_section(branch)
    projection = abs(math.sin(math.radians(process_input["geometry"]["intersectionAngle"])))
    geometry.require(projection > 1e-6, "端—中连接的两条轴线不能平行")
    # The insert dimensions only apply to the single-face insertion branch.
    # Saddle/through branches must ignore saved, inactive insertion drafts.
    if parameters["interfaceMode"] == "singleInsert":
        insertion = geometry.positive(parameters["insertDepth"], "支管插入深度")
        if h is not None:
            geometry.require(insertion > h["wall"] / projection,
                             "支管插入深度不足以越过主管近侧壁")
            geometry.require(insertion < (h["depth"] - h["wall"]) / projection,
                             "支管插入深度会碰到主管对侧内壁")
    # The underlying branch-profile/end-profile cutters accept arbitrary
    # closed contours; material intersection and assembly fit remain native.
