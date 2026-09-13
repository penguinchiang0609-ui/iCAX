"""Entry point for this independent guardrail product template."""
from __future__ import annotations

import hashlib
from copy import deepcopy
import importlib.util
from pathlib import Path
import sys

_script = Path(__file__).resolve().parent.parent.parent / "_shared" / "modular_guardrail.py"
_name = "icax_modular_guardrail_" + hashlib.sha256(_script.read_bytes()).hexdigest()[:16]
if _name not in sys.modules:
    _spec = importlib.util.spec_from_file_location(_name, _script)
    if _spec is None or _spec.loader is None:
        raise RuntimeError("无法加载组合式护栏生成器")
    _module = importlib.util.module_from_spec(_spec)
    sys.modules[_name] = _module
    _spec.loader.exec_module(_module)

def _parameters(parameters):
    if parameters.get("infillType", "bars") != "bars":
        raise ValueError("此款式的填充构造不可更换，请选择对应护栏款式")
    orientation = parameters.get("barOrientation", "vertical")
    if orientation not in {"vertical", "horizontal"}:
        raise ValueError("杆件方向无效")
    return {**parameters, "infillType": "horizontal" if orientation == "horizontal" else "bars",
            **({"barDistribution": "manual_count", "fixedBarCount": parameters["horizontalRailCount"],
                "guardrailUse": "platform", "spearTipEnabled": False}
               if orientation == "horizontal" else {})}

def generate(parameters, context):
    original = deepcopy(parameters)
    document = sys.modules[_name].generate(_parameters(parameters), context)
    # Internal construction inputs are not public normalized parameters.
    document["parameters"] = original
    return document

def build_layout(parameters):
    return sys.modules[_name].build_layout(_parameters(parameters))
