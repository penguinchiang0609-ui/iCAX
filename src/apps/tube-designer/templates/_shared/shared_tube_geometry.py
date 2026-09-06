"""Template-declared, per-build sharing of unprocessed tube geometry.

Only callers that explicitly use this builder share prototypes.  The native
evaluator does not compare or deduplicate parts.  Each part keeps its own rigid
placement and all downstream holes, grooves and end cuts remain independent.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import math
from typing import Any, Sequence

from icax_template_sdk import NeutralModel


def request_geometry_purpose(context: dict[str, Any]) -> str | None:
    """Missing purpose retains the legacy dual-output template contract."""
    if "geometryPurpose" not in context:
        return None
    purpose = context["geometryPurpose"]
    if purpose not in ("display", "manufacturing"):
        raise ValueError("geometryPurpose must be display or manufacturing")
    return purpose


def finish_geometry_request(model: NeutralModel, context: dict[str, Any]) -> dict[str, Any]:
    """Return the requested result only, after the template chose what to build.

    This final dependency pass removes unused shared declarations; it is not an
    operator filter. Display templates must not construct manufacturing features
    in the first place. Native code receives one complete, self-contained graph.
    """
    document = model.build()
    purpose = request_geometry_purpose(context)
    if purpose is None:
        return document
    source_purpose = "display" if purpose == "display" else "export"
    selected_keys = list(dict.fromkeys(
        key for output in document["outputs"] if output["purpose"] == source_purpose
        for key in output["items"]
    ))
    manufacturing_keys = {
        key for output in document["outputs"] if output["purpose"] == "export"
        for key in output["items"]
    }
    items = {item["key"]: item for item in document["items"]}
    geometry = {node["key"]: node for node in document["geometry"]}
    needed: set[str] = set()

    def visit(key: str) -> None:
        if key in needed:
            return
        node = geometry[key]
        needed.add(key)
        dependencies = list(node.get("inputs", []))
        if node["operator"] == "boolean":
            arguments = node.get("arguments", {})
            if "target" in arguments:
                dependencies.append(arguments["target"])
            dependencies.extend(arguments.get("tools", []))
        for dependency in dependencies:
            visit(dependency)

    for key in selected_keys:
        item = items[key]
        root = item["representations"][source_purpose]
        visit(root)
        item["representations"] = {"result": root}
    document["items"] = [items[key] for key in selected_keys]
    document["geometry"] = [node for node in document["geometry"] if node["key"] in needed]
    document["outputs"] = [{"key": "result", "purpose": "result", "items": selected_keys, "properties": {}}]
    document.setdefault("extensions", {}).update({
        "tubeDesigner.geometryPurpose": purpose,
        "tubeDesigner.manufacturingPartCount": len(manufacturing_keys),
    })
    return document


def _vector(value: Sequence[float], label: str) -> list[float]:
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(f"{label} must have three coordinates")
    if any(isinstance(component, bool) or not isinstance(component, (int, float))
           or not math.isfinite(component) for component in value):
        raise ValueError(f"{label} must contain finite numbers")
    return [float(component) for component in value]


def _dot(left: Sequence[float], right: Sequence[float]) -> float:
    return sum(a * b for a, b in zip(left, right))


def _cross(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    ]


def _unit(vector: Sequence[float], label: str) -> list[float]:
    length = math.hypot(*vector)
    if length <= 1.0e-12 or not math.isfinite(length):
        raise ValueError(f"{label} must be a nonzero finite axis")
    return [coordinate / length for coordinate in vector]


def _signature(arguments: dict[str, Any]) -> str:
    # No dimension rounding, contour reordering, reflection or material/name key.
    return json.dumps(arguments, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False)


class SharedTubeGeometry:
    """One explicit prototype declaration table owned by one NeutralModel build."""

    def __init__(self, model: NeutralModel) -> None:
        self._model = model
        self._profiles: dict[str, str] = {}
        self._extrusions: dict[str, str] = {}

    def emit_tube(
        self,
        part_key: str,
        *,
        profile_arguments: dict[str, Any],
        extrude_arguments: dict[str, Any],
    ) -> str:
        """Declare an exact local prototype, then place this unprocessed part.

        The original profile x/y directions are retained, not chosen from the
        extrusion direction.  Its signed, full vector is projected into the
        right-handed profile frame; opposite extrusion never mirrors a section.
        All remaining profile and extrusion arguments are included unchanged in
        the corresponding prototype keys, including custom contour data.
        """
        local_profile = deepcopy(profile_arguments)
        source_placement = local_profile["placement"]
        origin = _vector(source_placement["origin"], "profile origin")
        x_axis = _unit(_vector(source_placement["xAxis"], "profile xAxis"), "profile xAxis")
        source_y = _unit(_vector(source_placement["yAxis"], "profile yAxis"), "profile yAxis")
        if abs(_dot(x_axis, source_y)) > 1.0e-7:
            raise ValueError("shared tube profile axes must be orthogonal")
        z_axis = _unit(_cross(x_axis, source_y), "profile zAxis")
        y_axis = _cross(z_axis, x_axis)
        # Preserve additional placement arguments in the declaration as well.
        local_profile["placement"] = {
            **source_placement,
            "origin": [0.0, 0.0, 0.0],
            "xAxis": [1.0, 0.0, 0.0],
            "yAxis": [0.0, 1.0, 0.0],
        }
        local_extrusion = deepcopy(extrude_arguments)
        world_vector = _vector(local_extrusion["vector"], "extrude vector")
        if math.hypot(*world_vector) <= 1.0e-12:
            raise ValueError("shared tube extrusion must have positive length")
        local_extrusion["vector"] = [_dot(world_vector, axis)
                                     for axis in (x_axis, y_axis, z_axis)]

        # Validate both complete declarations before adding anything to the model.
        profile_signature = _signature(local_profile)
        extrusion_signature = _signature({
            "profile": local_profile, "extrusion": local_extrusion,
        })
        profile_key = self._profiles.get(profile_signature)
        if profile_key is None:
            profile_key = "shared.tube.profile." + hashlib.sha256(
                profile_signature.encode("ascii")).hexdigest()
            self._model.geometry(profile_key, "profile2d", arguments=local_profile)
            self._profiles[profile_signature] = profile_key
        extrusion_key = self._extrusions.get(extrusion_signature)
        if extrusion_key is None:
            extrusion_key = "shared.tube.extrude." + hashlib.sha256(
                extrusion_signature.encode("ascii")).hexdigest()
            self._model.geometry(extrusion_key, "extrude", inputs=[profile_key],
                                 arguments=local_extrusion)
            self._extrusions[extrusion_signature] = extrusion_key
        return self._model.geometry(
            f"{part_key}.solid", "transform", inputs=[extrusion_key],
            arguments={"placement": {
                "origin": origin, "xAxis": x_axis, "yAxis": y_axis, "zAxis": z_axis,
            }},
        )
