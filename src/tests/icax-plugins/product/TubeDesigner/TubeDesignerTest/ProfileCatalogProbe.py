from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "profile-catalog-probe"
TEMPLATE_VERSION = "1.0.0"

_SCRIPT = next(
    parent / "src/apps/tube-designer/templates/_shared/tube_profile_catalog.py"
    for parent in Path(__file__).resolve().parents
    if (parent / "src/apps/tube-designer/templates/_shared/tube_profile_catalog.py").is_file()
)
_MODULE_NAME = "icax_profile_catalog_probe_" + hashlib.sha256(
    _SCRIPT.read_bytes()
).hexdigest()[:16]
if _MODULE_NAME in sys.modules:
    _catalog = sys.modules[_MODULE_NAME]
else:
    _spec = importlib.util.spec_from_file_location(_MODULE_NAME, _SCRIPT)
    if _spec is None or _spec.loader is None:
        raise RuntimeError(f"无法加载管型目录：{_SCRIPT}")
    _catalog = importlib.util.module_from_spec(_spec)
    sys.modules[_MODULE_NAME] = _catalog
    _spec.loader.exec_module(_catalog)


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    profile = _catalog.load_profile(parameters, "probe")
    model = NeutralModel(
        template_id=TEMPLATE_ID,
        template_version=TEMPLATE_VERSION,
        package_digest=str(context["template"].get("packageDigest", "")),
        parameters=parameters,
    )
    placement = {
        "origin": [0.0, 0.0, 0.0],
        "xAxis": [1.0, 0.0, 0.0],
        "yAxis": [0.0, 1.0, 0.0],
    }
    profile_key = model.geometry(
        "probe.profile", "profile2d",
        arguments={"placement": placement, "contours": profile.contours()},
    )
    result = model.geometry(
        "probe.solid", "extrude", inputs=[profile_key],
        arguments={"vector": [0.0, 0.0, 250.0]},
    )
    item = model.item(
        "probe.0001", profile.display_name,
        representations={"display": result, "export": result},
        properties={"tubeDesigner.profile": profile.properties()},
    )
    model.output("display.default", "display", [item])
    return model.build()
