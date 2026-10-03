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
    host = geometry.part(process_input, "memberA")
    branch = geometry.part(process_input, "memberB")
    geometry.standard_section(host)
    geometry.standard_section(branch)
    if not parameters["maleFemale"]:
        # Plain end-miter cuts accept general sections. Inactive tab drafts
        # cannot make this butt-contact branch inapplicable.
        return
    h = geometry.rectangular_section(host, "主件母槽")
    b = geometry.rectangular_section(branch, "支件圆弧插舌")
    geometry.centered_section(h, "主件公母配合")
    geometry.centered_section(b, "支件公母配合")
    width, insertion, clearance = (parameters[key] for key in
                                  ("tabWidth", "tabLength", "sideClearance"))
    geometry.require(insertion > width / 2 + 0.01, "圆弧插舌长度须大于半宽")
    geometry.require(h["wall"] + 0.01 < insertion < h["depth"] - h["wall"] - 0.01,
                     "插舌长度必须穿过主件近侧壁，且不能碰到对侧内壁")
    branch_flat_width = min(b["flatWidth"], b["innerWidth"] - 2 * max(b["innerRadii"]))
    geometry.require(width < branch_flat_width - 0.02, "圆弧插舌越过支件平直壁")
    opening_width = b["wall"] + 2 * clearance
    opening_length = width + opening_width
    host_flat = min(h["flatWidth"], h["innerWidth"] - 2 * max(h["innerRadii"]))
    geometry.require(b["depth"] - b["wall"] + opening_width < host_flat - 0.02,
                     "对边母槽越过主件平直侧壁")
    anchor = host.get("anchor", {})
    if anchor.get("kind") == "side":
        station = geometry.site(host)["station"]
    else:
        geometry.require(anchor.get("kind") == "end" and anchor.get("end") in ("start", "end"),
                         "端邻母槽需要明确指定主件起端或末端")
        station = b["width"] / 2 if anchor["end"] == "start" else host["length"] - b["width"] / 2

    def within_end_opening(center, half_span):
        geometry.require(0.01 < center < host["length"] - 0.01,
                         "端部开口母槽中心须位于主件内")
        geometry.require(not (center - half_span <= 0.01
                              and center + half_span >= host["length"] - 0.01),
                         "端部开口母槽不能贯通主件两端")

    within_end_opening(station, opening_length / 2)
    if parameters["pairCount"] == "four":
        branch_flat_depth = min(b["flatDepth"], b["innerDepth"] - 2 * max(b["innerRadii"]))
        geometry.require(width < branch_flat_depth - 0.02, "四边插舌越过支件侧壁平直区")
        geometry.require(opening_length < host_flat - 0.02, "四边母槽越过主件平直侧壁")
        for sign in (-1, 1):
            within_end_opening(station + sign * (b["width"] - b["wall"]) / 2,
                               opening_width / 2)
