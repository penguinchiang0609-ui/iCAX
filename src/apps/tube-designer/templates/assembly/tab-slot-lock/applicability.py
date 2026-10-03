"""Check the tongue and female end shoulder/depth without generating tools."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    nominal_width = parameters["nominalWidth"]
    nominal_depth = parameters["straightDepth"] + nominal_width / 2
    for span_id, clearance in (("tab", 0), ("slot", parameters["fitClearance"])):
        part = geometry.part(process_input, span_id)
        width = nominal_width + 2 * clearance
        depth = nominal_depth + clearance
        geometry.require(depth < part["length"] - 0.001, f"{span_id} 插舌/插槽总深度须小于成品区域长度")
        section = geometry.standard_section(part)
        if section is not None:
            geometry.require(width < section["width"] - 0.002,
                             f"{span_id} 插舌/插槽须在截面两侧保留肩部")
