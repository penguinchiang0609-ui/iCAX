"""Entry point for this independent guardrail product template."""
from __future__ import annotations

import hashlib
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

generate = sys.modules[_name].generate
build_layout = sys.modules[_name].build_layout
