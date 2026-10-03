"""Input section support for the segmented V-slot forming script."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("icax_assembly_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)

_layout_spec = importlib.util.spec_from_file_location(
    "icax_segmented_input_layout", Path(__file__).with_name("layout.py"))
layout = importlib.util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)


@g.checked
def check_applicability(process_input, parameters):
    if set(process_input["parts"]) == {"stock"}:
        stock = g.part(process_input, "stock")
        g.require(stock["length"] <= 100000, "折弯母材长度超出允许范围")
        g.standard_section(stock)
        return
    a, b = (g.part(process_input, key) for key in ("segmentA", "segmentB"))
    g.same_section(a, b, "分段开槽折弯")
    section = g.rectangular_section(a, "分段开槽折弯")
    g.centered_section(section, "分段开槽折弯")
    g.uniform_section(section, "分段开槽折弯")
    radius = g.positive(parameters["bendRadius"], "目标弯曲半径")
    measurements = layout.calculate(section["depth"], section["wall"], radius,
                                    process_input["geometry"]["angle"])
    g.require(a["length"] + measurements["patternLength"] + b["length"] <= 100000,
              "分段槽弯曲区与直段所需下料长度超出允许范围")
