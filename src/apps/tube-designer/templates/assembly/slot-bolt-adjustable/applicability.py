"""The selected slot direction determines its product-region envelope."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    lower = geometry.part(process_input, "adjuster")
    upper = geometry.part(process_input, "datum")
    diameter = geometry.positive(parameters["boltDiameter"] + parameters["holeClearance"], "螺栓孔直径")
    slot_length = diameter + parameters["adjustment"]
    along, across = ((slot_length, diameter) if parameters["adjustAxis"] == "x" else (diameter, slot_length))
    geometry.local_envelope(lower, along, across, "调节件长孔", radius=diameter / 2)
    geometry.local_envelope(upper, diameter, diameter, "基准件圆孔", radius=diameter / 2)
    geometry.aligned_hole_sites(lower, upper, parameters["holeClearance"], parameters["adjustment"], parameters["adjustAxis"])
