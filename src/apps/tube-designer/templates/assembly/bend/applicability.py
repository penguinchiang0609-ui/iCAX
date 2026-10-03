"""Cold bending accepts a continuous, unchanged input section."""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("icax_assembly_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)


@g.checked
def check_applicability(process_input, parameters):
    if set(process_input["parts"]) == {"stock"}:
        stock = g.part(process_input, "stock")
        g.require(stock["length"] <= 100000, "折弯母材长度超出允许范围")
        g.standard_section(stock)
        return
    a, b = (g.part(process_input, key) for key in ("segmentA", "segmentB"))
    g.same_section(a, b, "连续冷折母材")
    g.standard_section(a)
    wall = g.positive(a["parameters"]["wallThickness"], "冷折母材壁厚")
    radius = g.positive(parameters["bendRadius"], "冷折半径")
    allowance = (radius + parameters["bendFactor"] * wall) * math.radians(process_input["geometry"]["angle"])
    g.require(a["length"] + b["length"] + allowance <= 100000, "冷折展开长度超出允许的下料长度")
