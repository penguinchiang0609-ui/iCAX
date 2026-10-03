"""Template-owned, read-only local process applicability rules."""
import importlib.util
from pathlib import Path
import math

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_applicability_geometry", _path)
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    host = geometry.part(process_input, "host")
    branch = geometry.part(process_input, "branch")
    h = geometry.rectangular_section(host, "主管母槽")
    b = geometry.rectangular_section(branch, "支管圆弧插舌")
    geometry.centered_section(h, "主管公母配合")
    geometry.centered_section(b, "支管公母配合")
    width, insertion = parameters["tabWidth"], parameters["tabLength"]
    geometry.require(branch["length"] + insertion <= 100000,
                     "支管插舌所需下料补长超出允许的下料长度")
    clearance = parameters["sideClearance"]
    geometry.require(insertion > width / 2 + 0.01, "圆弧插舌长度须大于半宽")
    geometry.require(h["wall"] + 0.01 < insertion < h["depth"] - h["wall"] - 0.01,
                     "插舌长度必须穿过主管近侧壁，且不能碰到对侧内壁")
    branch_flat_width = min(b["flatWidth"], b["innerWidth"] - 2 * max(b["innerRadii"]))
    geometry.require(width < branch_flat_width - 0.02, "圆弧插舌越过支管平直壁")
    opening_width = b["wall"] + 2 * clearance
    opening_length = width + opening_width
    host_flat = min(h["flatWidth"], h["innerWidth"] - 2 * max(h["innerRadii"]))
    geometry.require(opening_length < host_flat - 0.02, "母槽越过主管平直侧壁")
    # pairRotation=90 maps the first opposing Z-wall pair along the host axis.
    axial_extent = (b["depth"] - b["wall"] + opening_width) / 2
    location = geometry.site(host)
    geometry.require(axial_extent < min(location["station"], host["length"] - location["station"]) - 0.01, "母槽完整包络越过主管端部")
    if parameters["pairCount"] == "four":
        branch_flat_depth = min(b["flatDepth"], b["innerDepth"] - 2 * max(b["innerRadii"]))
        geometry.require(width < branch_flat_depth - 0.02, "四边圆弧插舌越过支管侧壁平直区")
        geometry.require(b["width"] - b["wall"] + opening_width < host_flat - 0.02,
                         "四边母槽越过主管平直侧壁")
        geometry.require(opening_length / 2 < min(location["station"], host["length"] - location["station"]) - 0.01, "四边母槽完整包络越过主管端部")
