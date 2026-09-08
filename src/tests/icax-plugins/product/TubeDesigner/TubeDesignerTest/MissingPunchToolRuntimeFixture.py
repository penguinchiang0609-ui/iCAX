"""Test-only empty product catalogue; never moves/deletes installed tool files."""
from pathlib import Path
import runpy

_source = Path(__file__).resolve().parents[6] / "src/apps/tube-designer/templates/_shared/punch_tool_runtime.py"
_runtime = runpy.run_path(str(_source))
_runtime["generate"].__globals__["ROOT"] = Path(__file__).resolve().parent / "nonexistent-test-punch-library"


def generate(parameters, context):
    return _runtime["generate"](parameters, context)
