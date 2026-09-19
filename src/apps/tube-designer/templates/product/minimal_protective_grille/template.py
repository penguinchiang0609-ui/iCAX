"""Extreme-minimal protective grille product entry point."""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys


TEMPLATE_ID = "minimal-protective-grille"
TEMPLATE_VERSION = "1.1.0"


SHARED = Path(__file__).resolve().parents[2] / "_shared" / "minimal_protective_grille.py"
MODULE = "icax_minimal_protective_grille_" + hashlib.sha256(SHARED.read_bytes()).hexdigest()[:16]
if MODULE not in sys.modules:
    spec = importlib.util.spec_from_file_location(MODULE, SHARED)
    if spec is None or spec.loader is None:
        raise RuntimeError("无法加载极简防护网共享规则")
    module = importlib.util.module_from_spec(spec)
    sys.modules[MODULE] = module
    spec.loader.exec_module(module)


def generate(parameters, context):
    return sys.modules[MODULE].generate(parameters, context)
