from __future__ import annotations

import json
import traceback
from typing import Any

from icax_template_worker import _handle


def invoke(request_text: str) -> str:
    request_id: Any = None
    try:
        request = json.loads(request_text)
        if not isinstance(request, dict):
            raise ValueError("request must be an object")
        request_id = request.get("requestId")
        response, _ = _handle(request)
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
    return json.dumps(
        response,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    )
