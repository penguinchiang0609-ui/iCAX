"""The active fastener hole pattern must fit both product regions."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    lower = geometry.part(process_input, "fastenedPart")
    upper = geometry.part(process_input, "basePart")
    diameter = geometry.positive(parameters["nominalDiameter"] + parameters["holeClearance"], "紧固孔直径")
    count, pitch = parameters["count"], parameters["pitch"]
    along = diameter + (parameters["adjustment"] if parameters["fastenerType"] == "adjustableBolt" else 0)
    geometry.local_envelope(lower, (count - 1) * pitch + along, diameter, "被连接件孔组", radius=diameter / 2)
    geometry.local_envelope(upper, (count - 1) * pitch + diameter, diameter, "基准件孔组", radius=diameter / 2)
    travel = parameters["adjustment"] if parameters["fastenerType"] == "adjustableBolt" else 0
    geometry.aligned_hole_sites(lower, upper, parameters["holeClearance"], travel, count=count)
