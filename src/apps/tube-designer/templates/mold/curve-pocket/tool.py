"""Exact closed-curve side cutter, including all saved inner islands.

The native side-cutter placement owns face, position, rotation and cut depth.
Curves stay in the saved local profile coordinates throughout this template.
"""
import copy


def generate(parameters, context):
    section = context.get("section", {})
    profile = section.get("profile", {}) if isinstance(section, dict) else {}
    contours = profile.get("contours") if isinstance(profile, dict) else None
    if not isinstance(contours, list) or not contours:
        raise ValueError("曲线切除需要有效的闭合切除轮廓")
    # Never rebuild, sample, reverse or discard curves here. Native profile
    # evaluation validates exact curves and preserves every inner island.
    return {"mode": "profile", "contours": copy.deepcopy(contours)}
