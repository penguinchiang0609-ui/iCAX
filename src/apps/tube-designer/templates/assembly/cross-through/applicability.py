"""Check the fixed orthogonal through-opening against local participant dimensions."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    host = geometry.part(process_input, "host")
    through = geometry.part(process_input, "through")
    host_section = geometry.standard_section(host)
    through_section = geometry.standard_section(through)
    if through_section is not None:
        clearance = parameters["fitGap"]
        geometry.local_envelope(host, through_section["depth"] + 2 * clearance,
                                   through_section["width"] + 2 * clearance, "十字贯穿轮廓口")
    if host_section is not None:
        geometry.require(through["length"] > host_section["depth"], "贯穿区域长度不足以穿过主管两侧壁")
