"""Weld openings are checked only for the active interface branch."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_assembly_input_geometry", Path(__file__).resolve().parents[2] / "_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    lower = geometry.part(process_input, "primary")
    upper = geometry.part(process_input, "secondary")
    common_start, common_end = geometry.overlap_interval(lower, upper)
    geometry.require(common_end > common_start, "成品两区域没有可焊接的轴向重合区")
    if parameters["weldType"] == "lap":
        return
    if parameters["weldType"] == "plug":
        along = across = parameters["openingDiameter"]
    else:
        along, across = parameters["slotLength"], parameters["slotWidth"]
        geometry.require(along >= across, "槽焊孔长度不能小于宽度")
    extent = (parameters["weldCount"] - 1) * parameters["weldPitch"] + along
    geometry.local_envelope(upper, extent, across, "焊接孔组")
    center = geometry.dot([b - a for a, b in zip(geometry.origin(lower), geometry.site(upper)["point"])], geometry.axis(lower))
    geometry.require(center - extent / 2 >= common_start - 1e-7
                     and center + extent / 2 <= common_end + 1e-7,
                     "焊接孔组完整包络超出成品两区域的实际重合区")
    lower_section = geometry.standard_section(lower)
    if lower_section is not None:
        geometry.require(across <= lower_section["width"] + 1e-7, "焊接孔宽度超出承接区域截面范围")
