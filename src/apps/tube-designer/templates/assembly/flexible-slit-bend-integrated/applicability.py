"""Check the section and avoid a self-intersecting flexible target sweep."""
import importlib.util
import json
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("icax_assembly_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)

_tool = json.loads((Path(__file__).resolve().parents[2] / "mold" / "flexible-slit-bend" /
                    "tool.json").read_text(encoding="utf-8"))
_defaults = {item["key"]: item["defaultValue"] for item in _tool["parameters"]}


@g.checked
def check_applicability(process_input, parameters):
    if set(process_input["parts"]) == {"stock"}:
        stock = g.part(process_input, "stock")
        g.require(stock["length"] <= 100000, "折弯母材长度超出允许范围")
        g.standard_section(stock)
        return
    a, b = (g.part(process_input, key) for key in ("segmentA", "segmentB"))
    g.same_section(a, b, "柔性缝折弯")
    section = g.rectangular_section(a, "柔性缝折弯")
    g.centered_section(section, "柔性缝折弯")
    g.uniform_section(section, "柔性缝折弯")
    radius = g.positive(parameters["bendRadius"], "目标中心线半径")
    g.require(radius > section["depth"] / 2, "目标中心线半径过小，方矩管内侧可能自交")
    flexible_length = radius * math.radians(process_input["geometry"]["angle"])
    p = _defaults
    opening = p["slitWidth"] + (p["uSpan"] if p["slitMode"] == "u" else 0)
    pitch = flexible_length / (p["slitCount"] + 1)
    g.require(pitch - opening + 1e-9 >= p["minimumLand"],
              "柔性槽间剩余筋宽不足，请增大半径、减少槽数或减小割缝/U 槽开口")
    height = section["depth"] - section["wall"] - p["rootClearance"]
    g.require(height > 1e-6, "柔性槽根超出主管顶部")
    if p["slitMode"] == "narrow":
        g.require(height > p["slitWidth"] / 2, "窄缝高度不足以形成圆头")
    elif p["slitMode"] == "u":
        g.require(height > p["slitWidth"] + 1e-6, "U 槽底横缝超出主管顶部")
    length = a["length"] + b["length"] + flexible_length
    g.require(length <= 100000, "柔性缝弯曲区展开长度超出允许的下料长度")
