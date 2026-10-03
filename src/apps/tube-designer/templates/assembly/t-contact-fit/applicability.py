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
    h = geometry.rectangular_section(host, "主管平面贴合")
    b = geometry.rectangular_section(branch, "支管平面贴合")
    # This matches the conservative complete end-face envelope used by the
    # bound-plan flat-contact check; actual face directions remain native.
    radius = math.hypot(b["width"], b["depth"]) / 2
    location = geometry.site(host)
    geometry.require(abs(location["offset"]) + radius <= h["flatWidth"] / 2 + 1e-6,
                     "支管端面超出主管平直接触面，应改用相贯端切")
    geometry.require(radius <= min(location["station"], host["length"] - location["station"]) + 1e-6,
                     "支管端面靠近主管端部，不能按平面贴合")
    if parameters["markContact"]:
        geometry.require(parameters["markMode"] == "contour", "目前只支持轮廓打标")
        geometry.require(h["wall"] > 0.2, "贴合线打标深度不能穿透主管壁")
