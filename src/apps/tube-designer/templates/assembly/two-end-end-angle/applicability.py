"""Direct section support and stock reach for matching mitered ends."""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("icax_assembly_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)


@g.checked
def check_applicability(process_input, parameters):
    a, b = (g.part(process_input, key) for key in ("memberA", "memberB"))
    g.same_section(a, b, "斜接的两根管")
    section = g.standard_section(a)
    g.require(section is not None, "斜接示例当前支持圆管和方矩管截面")
    if section["kind"] == "rect":
        g.uniform_section(section, "斜接端面配准")
        radius = a["parameters"].get("cornerRadius", 0)
        g.require(all(abs(value - radius) <= 1e-6 for value in section["outerRadii"]), "斜接端面配准尚不支持独立外圆角")
        g.require(radius < min(section["width"], section["depth"]) / 2, "斜接需要带平直边的方矩管截面")
    product_values = process_input["geometry"]
    extension = g.projection_half_span(section, product_values["planeRotation"]) * math.tan(math.radians(product_values["jointAngle"]) / 2)
    g.require(max(a["length"], b["length"]) + extension <= 100000, "斜接长点所需下料长度超出允许范围")
