"""The edge arc groove needs a straight rectangular reference wall."""
import importlib.util
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
    g.same_section(a, b, "边弧槽连续母材")
    g.rectangular_section(a, "边弧槽开槽折弯")
    g.require(a["length"] + b["length"] <= 100000, "边弧槽母材长度超出允许的下料长度")
