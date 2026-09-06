from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
from typing import Any


SHARED_SCRIPT = Path(__file__).resolve().parent.parent / "_shared" / "multi_face_security_window.py"
SHARED_MODULE_NAME = "icax_tube_designer_multi_face_" + hashlib.sha256(SHARED_SCRIPT.read_bytes()).hexdigest()[:16]
if SHARED_MODULE_NAME in sys.modules:
    _shared_module = sys.modules[SHARED_MODULE_NAME]
else:
    _shared_spec = importlib.util.spec_from_file_location(SHARED_MODULE_NAME, SHARED_SCRIPT)
    if _shared_spec is None or _shared_spec.loader is None:
        raise RuntimeError(f"无法加载多面防盗窗共用规则：{SHARED_SCRIPT}")
    _shared_module = importlib.util.module_from_spec(_shared_spec)
    sys.modules[SHARED_MODULE_NAME] = _shared_module
    _shared_spec.loader.exec_module(_shared_module)
generate_multi_face = _shared_module.generate_multi_face


TEMPLATE_ID = "three-face-security-window"
TEMPLATE_VERSION = "2.0.0"


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    return generate_multi_face(
        parameters,
        context,
        template_id=TEMPLATE_ID,
        template_version=TEMPLATE_VERSION,
        layout="three-face",
    )
