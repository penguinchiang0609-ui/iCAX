"""Cut a narrow, shallow band following the mating branch's outer contour."""

import copy
import importlib.util
import math
from pathlib import Path
import sys


def _offset(contour, distance):
    script = Path(__file__).resolve().parents[2] / "_shared" / "tube_profile_catalog.py"
    name = "icax_contact_outline_profile_offset"
    module = sys.modules.get(name)
    if module is None or Path(getattr(module, "__file__", "")).resolve() != script:
        spec = importlib.util.spec_from_file_location(name, script)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        try:
            spec.loader.exec_module(module)
        except Exception:
            sys.modules.pop(name, None)
            raise
    return module._offset_outer_contour(contour, distance)


def generate(parameters, context):
    if parameters["markMode"] != "contour":
        raise ValueError("目前只支持轮廓打标")
    width = parameters["lineWidth"]
    if isinstance(width, bool) or not isinstance(width, (int, float)) or not math.isfinite(width) or width <= 0:
        raise ValueError("轮廓打标线宽必须为正数")
    section = context.get("section", {})
    profile = section.get("profile", {}) if isinstance(section, dict) else {}
    contours = profile.get("contours") if isinstance(profile, dict) else None
    if not isinstance(contours, list) or not contours:
        raise ValueError("轮廓打标需要有效的支管截面")
    frame = profile.get("sectionFrame", {})
    outer_index = frame.get("outerContourIndex", 0) if isinstance(frame, dict) else 0
    if isinstance(outer_index, bool) or not isinstance(outer_index, int) or not 0 <= outer_index < len(contours):
        raise ValueError("支管截面的外轮廓索引无效")
    outer = copy.deepcopy(contours[outer_index])
    inner = _offset(outer, -width)
    return {"mode": "profile", "contours": [outer, inner]}
