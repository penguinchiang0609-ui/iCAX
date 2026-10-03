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
    inner = geometry.part(process_input, "insertPart")
    outer = geometry.part(process_input, "receivePart")
    a, b = geometry.standard_section(inner), geometry.standard_section(outer)
    geometry.require(a is not None and b is not None and a["kind"] == b["kind"],
                     "此管型组合尚无同轴截面配合校验，当前支持同类圆管或方矩管")
    geometry.centered_section(b, "套接承接管")
    expected = parameters["fitClearance"]
    if a["kind"] == "round":
        actual = (b["innerWidth"] - a["width"]) / 2
        geometry.require(actual >= 0 and abs(actual - expected) <= 1e-6,
                         "套接圆管实际单侧间隙与工艺配合间隙不一致")
    else:
        width_gap = (b["innerWidth"] - a["width"]) / 2
        depth_gap = (b["innerDepth"] - a["depth"]) / 2
        geometry.require(min(width_gap, depth_gap) >= 0
                         and abs(min(width_gap, depth_gap) - expected) <= 1e-6,
                         "套接方矩管实际最小单侧间隙与工艺配合间隙不一致")
        geometry.require(min(a["outerRadii"]) >= max(b["innerRadii"]) - 1e-6,
                         "套接方矩管角部尚未通过保守配合校验")
    depth = parameters["insertDepth"]
    # The input describes the exposed finished length. Hidden engagement is
    # extra stock derived by this process, rather than deducted from the shape.
    geometry.require(depth <= outer["length"], "插入深度超过承接件长度")
    if parameters["connectionMode"] == "telescopic":
        geometry.require(depth - parameters["travelLength"] >= 20,
                         "最大伸出后的剩余啮合深度不足 20 mm")
    geometry.require(parameters["lockMethod"] in ("none", "weld"),
                     "套接当前仅支持配合或焊接锁止")
