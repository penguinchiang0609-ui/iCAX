from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import traceback
from types import ModuleType
from typing import Any


RUNTIME_ROOT = Path(__file__).resolve().parent
if str(RUNTIME_ROOT) not in sys.path:
    sys.path.insert(0, str(RUNTIME_ROOT))

PROTOCOL = "icax.template-runtime"
PROTOCOL_VERSION = 1
_modules: dict[tuple[str, str], ModuleType] = {}


def _load_template(template_path: str, package_digest: str) -> ModuleType:
    path = Path(template_path).resolve()
    if not path.is_file():
        raise FileNotFoundError(f"template script was not found: {path}")
    cache_key = (str(path), package_digest)
    cached = _modules.get(cache_key)
    if cached is not None:
        return cached

    module_name = f"icax_template_{abs(hash(cache_key))}"
    specification = importlib.util.spec_from_file_location(module_name, path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load template module: {path}")
    module = importlib.util.module_from_spec(specification)
    sys.modules[module_name] = module
    template_directory = str(path.parent)
    sys.path.insert(0, template_directory)
    try:
        specification.loader.exec_module(module)
    except Exception:
        sys.modules.pop(module_name, None)
        raise
    finally:
        if sys.path and sys.path[0] == template_directory:
            sys.path.pop(0)
    _modules[cache_key] = module
    return module


def _evaluate(request: dict[str, Any]) -> dict[str, Any]:
    template = request.get("template")
    parameters = request.get("parameters")
    context = request.get("context")
    if not isinstance(template, dict):
        raise ValueError("template must be an object")
    if not isinstance(parameters, dict):
        raise ValueError("parameters must be an object")
    if not isinstance(context, dict):
        raise ValueError("context must be an object")

    captured_stdout = io.StringIO()
    captured_stderr = io.StringIO()
    template_context = dict(context)
    template_context["template"] = dict(template)
    with contextlib.redirect_stdout(captured_stdout), contextlib.redirect_stderr(captured_stderr):
        module = _load_template(
            str(request.get("templatePath", "")),
            str(template.get("packageDigest", "")),
        )
        generator = getattr(module, "generate", None)
        if not callable(generator):
            raise RuntimeError("template module must define generate(parameters, context)")
        result = generator(dict(parameters), template_context)
    if not isinstance(result, dict):
        raise TypeError("template generate() must return an object")

    # Force full JSON validation before the protocol writer touches stdout.
    json.dumps(result, ensure_ascii=False, allow_nan=False)
    messages = [text for text in (captured_stdout.getvalue(), captured_stderr.getvalue()) if text]
    if messages:
        diagnostics = result.setdefault("diagnostics", [])
        diagnostics.append({
            "severity": "info",
            "code": "template.output",
            "message": "\n".join(messages).strip(),
        })
    return result


def _fit(request: dict[str, Any]) -> dict[str, Any]:
    fitter_path = request.get("fitterPath")
    contours = request.get("contours")
    context = request.get("context", {})
    if not isinstance(fitter_path, str) or not fitter_path:
        raise ValueError("fitterPath must be a non-empty string")
    if not isinstance(contours, list):
        raise ValueError("contours must be an array")
    if not isinstance(context, dict):
        raise ValueError("context must be an object")

    module = _load_template(
        fitter_path,
        str(request.get("packageDigest", "")),
    )
    fitter = getattr(module, "fitter", None)
    if not callable(fitter):
        raise RuntimeError("section fitter module must define fitter(contours, context)")
    result = fitter(list(contours), dict(context))
    if isinstance(result, bool):
        result = {"matched": result}
    if not isinstance(result, dict):
        raise TypeError("section fitter must return an object or boolean")
    if "matched" not in result or not isinstance(result["matched"], bool):
        raise ValueError("section fitter result must contain boolean matched")
    json.dumps(result, ensure_ascii=False, allow_nan=False)
    return result


def _handle(request: dict[str, Any]) -> tuple[dict[str, Any], bool]:
    request_id = request.get("requestId")
    if request.get("protocol") != PROTOCOL:
        raise ValueError("unsupported template runtime protocol")
    if request.get("protocolVersion") != PROTOCOL_VERSION:
        raise ValueError("unsupported template runtime protocol version")
    operation = request.get("operation")
    if operation == "shutdown":
        return {"requestId": request_id, "ok": True, "result": {}}, True
    if operation == "fit":
        return {"requestId": request_id, "ok": True, "result": _fit(request)}, False
    if operation != "evaluate":
        raise ValueError(f"unsupported template runtime operation: {operation}")
    return {"requestId": request_id, "ok": True, "result": _evaluate(request)}, False


def main() -> int:
    for line in sys.stdin:
        if not line.strip():
            continue
        should_stop = False
        request_id: Any = None
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("request must be an object")
            request_id = request.get("requestId")
            response, should_stop = _handle(request)
        except Exception as error:  # The error is data; stderr is never part of the protocol.
            response = {
                "requestId": request_id,
                "ok": False,
                "error": {
                    "type": type(error).__name__,
                    "message": str(error),
                    "traceback": traceback.format_exc(limit=20),
                },
            }
        sys.stdout.write(json.dumps(response, ensure_ascii=False, separators=(",", ":"), allow_nan=False))
        sys.stdout.write("\n")
        sys.stdout.flush()
        if should_stop:
            return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
