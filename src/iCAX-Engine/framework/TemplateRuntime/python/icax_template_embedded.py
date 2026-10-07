from __future__ import annotations

import json
import os
import sys
from time import perf_counter
import traceback
from typing import Any

from icax_template_worker import _handle


def invoke(request_text: str) -> str:
    profiling = bool(os.environ.get("ICAX_PROFILE_DISASSEMBLY"))
    calls = bool(os.environ.get("ICAX_PROFILE_PYTHON_TEMPLATE_CALLS"))
    profiler = None
    if calls:
        import cProfile
        profiler = cProfile.Profile()
    started = perf_counter()
    decoded = started
    handled = started
    request_id: Any = None
    operation = "unknown"
    try:
        request = json.loads(request_text)
        decoded = perf_counter()
        if not isinstance(request, dict):
            raise ValueError("request must be an object")
        request_id = request.get("requestId")
        operation = str(request.get("operation", "unknown"))
        if profiler is not None:
            profiler.enable()
        try:
            response, _ = _handle(request)
        finally:
            if profiler is not None:
                profiler.disable()
            handled = perf_counter()
    except Exception as error:
        response = {
            "requestId": request_id,
            "ok": False,
            "error": {
                "type": type(error).__name__,
                "message": str(error),
                "traceback": traceback.format_exc(limit=20),
            },
        }
    response_text = json.dumps(
        response,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    )
    finished = perf_counter()
    if profiling:
        print(
            f"Disassembly/python-bridge operation={operation} "
            f"loads={(decoded - started) * 1000:.3f} "
            f"handle={(handled - decoded) * 1000:.3f} "
            f"dumps={(finished - handled) * 1000:.3f} "
            f"total={(finished - started) * 1000:.3f} ms",
            file=sys.stderr,
        )
    if profiler is not None:
        import pstats
        print(f"Disassembly/python-calls operation={operation}", file=sys.stderr)
        pstats.Stats(profiler, stream=sys.stderr).strip_dirs().sort_stats("cumulative").print_stats(35)
    return response_text
