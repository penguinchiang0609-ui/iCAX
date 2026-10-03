from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


SHARED_SCRIPT = Path(__file__).resolve().parent.parent.parent / "_shared" / "steel_staircase.py"
SHARED_MODULE_NAME = "icax_tube_designer_steel_staircase_" + hashlib.sha256(SHARED_SCRIPT.read_bytes()).hexdigest()[:16]
if SHARED_MODULE_NAME in sys.modules:
    _shared_module = sys.modules[SHARED_MODULE_NAME]
else:
    _shared_spec = importlib.util.spec_from_file_location(SHARED_MODULE_NAME, SHARED_SCRIPT)
    if _shared_spec is None or _shared_spec.loader is None:
        raise RuntimeError(f"无法加载钢楼梯共用规则：{SHARED_SCRIPT}")
    _shared_module = importlib.util.module_from_spec(_shared_spec)
    sys.modules[SHARED_MODULE_NAME] = _shared_module
    _shared_spec.loader.exec_module(_shared_module)


def _generate_resource_document(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    return to_resource_model(_shared_module.generate(parameters, context))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    return to_display_model(_generate_resource_document(parameter_values, display_context(__file__)))




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        return to_manufacturing_model(
            _generate_resource_document(parameter_values, manufacturing_context(__file__)))
