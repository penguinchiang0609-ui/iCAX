"""Native fitting fixtures and a guard against executing forward generators."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

import icax_template_worker as worker

_original_loader = None
_patched = []
_fitting_calls = 0
_negative_catalog = None


def _forbidden(*args, **kwargs):
    raise AssertionError("forward generation is forbidden during native fitting")


def _guarded_loader(path, digest):
    global _fitting_calls
    module = _original_loader(path, digest)
    if Path(path).name == "profile_package_runtime.py" and not any(m is module for m, _ in _patched):
        saved = {name: getattr(module, name) for name in ("_evaluate", "evaluate_section", "_script_context")}
        _patched.append((module, saved))
        module._evaluate = module.evaluate_section = _forbidden

        def inverse_only(*args, **kwargs):
            global _fitting_calls
            if kwargs.get("entry") != "fitting":
                _forbidden()
            _fitting_calls += 1
            return saved["_script_context"](*args, **kwargs)

        module._script_context = inverse_only
    return module


def generate(parameters, context):
    global _original_loader, _fitting_calls, _negative_catalog
    action = parameters["action"]
    if action == "guard":
        if _original_loader is not None:
            raise AssertionError("nested fitting guard")
        _fitting_calls = 0
        _original_loader = worker._load_template
        worker._load_template = _guarded_loader
        return {"enabled": True}
    if action == "unguard":
        if _original_loader is not None:
            worker._load_template = _original_loader
            _original_loader = None
        for module, saved in _patched:
            for name, value in saved.items():
                setattr(module, name, value)
        _patched.clear()
        return {"fittingCalls": _fitting_calls}
    if action == "fixtures":
        assert _original_loader is None, "prepare samples before forbidding forward calls"
        root = Path(parameters["profileRoot"])
        spec = importlib.util.spec_from_file_location("native_fitting_fixtures", root.parent / "_shared/profile_package_runtime.py")
        runtime = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runtime)
        fixtures = []
        for directory in sorted(root.iterdir()):
            if not (directory / "profile.json").is_file():
                continue
            package = runtime._system_package(directory, preview=True)
            fixtures.append({"id": directory.name, "profile": package["previewProfile"]})
        return {"fixtures": fixtures}
    if action == "negative-fixtures":
        _negative_catalog = tempfile.TemporaryDirectory(prefix="icax-native-fitting-status-")
        root = Path(_negative_catalog.name)
        source = Path(parameters["profileRoot"]) / "round"
        for identifier, code in {
                "unsupported": "IMPLEMENTED = False\ndef fitting(section, context):\n    raise AssertionError('unsupported entry executed')\n",
                "no-match": "def fitting(section, context):\n    return False\n",
                "error": "def fitting(section, context):\n    raise RuntimeError('deliberate fitting failure')\n"}.items():
            target = root / identifier
            shutil.copytree(source, target, ignore=shutil.ignore_patterns("__pycache__"))
            descriptor = json.loads((target / "profile.json").read_text(encoding="utf-8-sig"))
            descriptor["id"] = identifier
            (target / "profile.json").write_text(json.dumps(descriptor), encoding="utf-8")
            (target / "fitting.py").write_text(code, encoding="utf-8")
            (target / "profile.py").write_text("raise AssertionError('forward module executed')\n", encoding="utf-8")
        return {"profileRoot": str(root)}
    raise ValueError(action)
