"""Read-only checks for the paired central through-bolt pattern."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    lower = geometry.part(process_input, "partA")
    upper = geometry.part(process_input, "partB")
    diameter = geometry.positive(parameters["boltDiameter"] + parameters["holeClearance"], "螺栓孔直径")
    extent = (parameters["boltCount"] - 1) * parameters["pitch"] + diameter
    for part, label in ((lower, "下方螺栓孔组"), (upper, "上方螺栓孔组")):
        geometry.local_envelope(part, extent, diameter, label, radius=diameter / 2)
    geometry.aligned_hole_sites(lower, upper, parameters["holeClearance"], count=parameters["boltCount"])
