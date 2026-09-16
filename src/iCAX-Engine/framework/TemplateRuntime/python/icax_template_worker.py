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


def _localized_text(value: Any, fallback: str) -> str:
    if isinstance(value, str) and value.strip():
        return value.strip()
    if isinstance(value, dict):
        for locale in ("zh-CN", "zh", "en-US", "en"):
            candidate = value.get(locale)
            if isinstance(candidate, str) and candidate.strip():
                return candidate.strip()
    return fallback


def _scene_specification_annotations(
        result: dict[str, Any], template: dict[str, Any], parameters: dict[str, Any]) -> None:
    """Attach descriptor-owned, envelope-style scene dimensions.

    Complex products can continue to return their own exact anchors from
    ``template.py``.  Straightforward products declare their few principal
    dimensions in the descriptor, keeping the scene overlay generic while the
    package still owns which parameters are exposed and where they are drawn.
    """
    extensions = template.get("extensions")
    specification = extensions.get("sceneSpecificationAnnotations") if isinstance(extensions, dict) else None
    if not isinstance(specification, dict):
        return
    declarations = specification.get("annotations")
    if not isinstance(declarations, list):
        raise ValueError("sceneSpecificationAnnotations.annotations 必须是数组")
    fields = {
        str(field.get("key")): field for field in template.get("parameters", [])
        if isinstance(field, dict) and isinstance(field.get("key"), str)
    }
    annotations: list[dict[str, Any]] = []
    for index, declaration in enumerate(declarations):
        if not isinstance(declaration, dict):
            raise ValueError(f"sceneSpecificationAnnotations.annotations[{index}] 必须是对象")
        parameter = declaration.get("parameter")
        axis = declaration.get("axis")
        if not isinstance(parameter, str) or parameter not in fields:
            raise ValueError(f"sceneSpecificationAnnotations.annotations[{index}] 参数无效")
        if axis not in ("x", "y", "z"):
            raise ValueError(f"sceneSpecificationAnnotations.annotations[{index}] axis 必须为 x、y 或 z")
        value = parameters.get(parameter)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            continue
        origin = declaration.get("origin", [0, 0, 0])
        offset = declaration.get("offset", [0, 0, 0])
        if (not isinstance(origin, list) or len(origin) != 3
                or not isinstance(offset, list) or len(offset) != 3):
            raise ValueError(f"sceneSpecificationAnnotations.annotations[{index}] 原点和偏移必须是三个数值")
        try:
            start = [float(component) for component in origin]
            offset_vector = [float(component) for component in offset]
        except (TypeError, ValueError) as error:
            raise ValueError(f"sceneSpecificationAnnotations.annotations[{index}] 原点和偏移必须是数值") from error
        axis_index = {"x": 0, "y": 1, "z": 2}[axis]
        extent = float(value)
        if declaration.get("centered") is True:
            start[axis_index] -= extent / 2
        end = list(start)
        end[axis_index] += extent
        field = fields[parameter]
        annotation = {
            "id": f"descriptor.{parameter}",
            "parameter": parameter,
            "kind": str(declaration.get("kind", "linear")),
            "start": start,
            "end": end,
            "offset": offset_vector,
            "label": _localized_text(declaration.get("label"), _localized_text(field.get("displayName"), parameter)),
            "editable": declaration.get("editable") is not False,
            "generatedValue": value,
        }
        if isinstance(declaration.get("visibleWhen"), dict):
            annotation["visibleWhen"] = declaration["visibleWhen"]
        annotations.append(annotation)
    if annotations:
        result.setdefault("extensions", {}).setdefault("tubeDesigner.specificationAnnotations", []).extend(annotations)


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
    _scene_specification_annotations(result, template, parameters)

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
    paths = request.get("moduleSearchPaths", [])
    if not isinstance(paths, list) or len(paths) > 16 or any(
            not isinstance(p, str) or not Path(p).is_absolute() or not Path(p).is_dir() for p in paths):
        raise ValueError("fitter module search paths must be existing absolute directories")
    previous = list(sys.path)
    try:
        fitter_path = request.get("fitterPath")
        local = [str(Path(fitter_path).resolve().parent)] if isinstance(fitter_path, str) and fitter_path else []
        sys.path[:0] = local + paths
        return _fit_with_dependencies(request)
    finally:
        sys.path[:] = previous


def _fit_with_dependencies(request: dict[str, Any]) -> dict[str, Any]:
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
