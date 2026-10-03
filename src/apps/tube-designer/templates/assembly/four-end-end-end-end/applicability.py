"""Read-only extent checks for four declared long-datum end cuts."""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    slope = abs(math.tan(math.radians(parameters["miterAngle"])))
    for span_id, projection in (("armA", "width"), ("armB", "width"), ("armC", "depth"), ("armD", "depth")):
        part = geometry.part(process_input, span_id)
        section = geometry.standard_section(part)
        if section is not None:
            geometry.require(slope * section[projection] < part["length"] - 1e-7,
                             f"{span_id} 端部斜切会切穿另一端；请减小工艺角度或更换工艺")
