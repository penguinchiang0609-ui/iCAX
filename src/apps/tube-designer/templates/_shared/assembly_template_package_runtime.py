"""Import one encrypted declarative assembly resource package (.itat)."""
from __future__ import annotations

import importlib.util
import copy
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
import zipfile

PASSWORD = b"ICAX_TUBE_DESIGNER"
MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_TOTAL_BYTES = 16 * 1024 * 1024
REQUIRED = ("assembly.json", "applicability.py", "example.py")
TEMPLATE_ROOT = Path(__file__).resolve().parent.parent


def _runtime():
    path = Path(__file__).with_name("assembly_template_runtime.py")
    spec = importlib.util.spec_from_file_location("icax_assembly_import_runtime", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _read(source_path):
    path = Path(source_path)
    if path.suffix.lower() != ".itat":
        raise ValueError("请选择 .itat 装配工艺包")
    if not path.is_file() or path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("itat 文件不存在或超过 16 MB")
    try:
        archive = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as error:
        raise ValueError("itat 不是有效的 ZIP 压缩包") from error
    files, total, names = {}, 0, set()
    with archive:
        archive.setpassword(PASSWORD)
        for info in archive.infolist():
            value = info.filename.replace("\\", "/")
            relative = PurePosixPath(value)
            if (relative.is_absolute() or ":" in value or ".." in relative.parts
                    or any(part in ("", ".") for part in relative.parts)
                    or info.external_attr >> 16 & 0o170000 == 0o120000):
                raise ValueError("itat 包包含不安全的文件路径")
            if info.is_dir():
                continue
            name = relative.as_posix()
            if name.casefold() in names:
                raise ValueError(f"itat 包文件重复：{name}")
            if relative.parts[0] in ("_shared", "mold", "assembly", "finished-product"):
                raise ValueError("itat 只能包含本装配工艺的目录文件")
            if relative.suffix.lower() not in (".json", ".py", ".svg", ".png", ".jpg", ".jpeg", ".webp"):
                raise ValueError(f"itat 包不支持此资源：{name}")
            if not info.flag_bits & 1:
                raise ValueError("itat 必须使用产品固定密码保护")
            if info.file_size > MAX_MEMBER_BYTES:
                raise ValueError(f"itat 包文件超过 4 MB：{name}")
            total += info.file_size
            if total > MAX_TOTAL_BYTES:
                raise ValueError("itat 解压后超过 16 MB")
            try:
                files[name] = archive.read(info)
            except (RuntimeError, zipfile.BadZipFile) as error:
                raise ValueError("itat 密码或文件内容无效") from error
            names.add(name.casefold())
    if any(name not in files for name in REQUIRED):
        raise ValueError("itat 必须包含 assembly.json、applicability.py 和 example.py")
    try:
        descriptor = json.loads(files["assembly.json"].decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("itat 的 assembly.json 无效") from error
    if (not isinstance(descriptor, dict) or descriptor.get("schema") != "icax.assembly-template"
            or descriptor.get("schemaVersion") != 1
            or not isinstance(descriptor.get("id"), str)
            or not re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", descriptor["id"])):
        raise ValueError("itat 装配工艺描述或 ID 无效")
    if descriptor.get("catalogueHidden") is True:
        raise ValueError("导入的装配工艺必须可在资源库选择")
    for name, data in files.items():
        if name.endswith(".py"):
            compile(data.decode("utf-8-sig"), name, "exec")
    return descriptor, files


def _refresh_shared(user_root):
    for name in ("_shared", "finished-product"):
        source = TEMPLATE_ROOT / name
        if source.is_dir():
            shutil.copytree(source, user_root / name, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"))


def _install(source_path, root):
    if not isinstance(root, str) or not root or not Path(root).is_absolute():
        raise ValueError("用户装配目录不可用")
    descriptor, files = _read(source_path)
    identifier = descriptor["id"]
    user_root = Path(root)
    target = user_root / identifier
    if (TEMPLATE_ROOT / "assembly" / identifier).exists() or target.exists():
        raise ValueError(f"装配工艺 ID 已存在：{identifier}")
    user_root.mkdir(parents=True, exist_ok=True)
    _refresh_shared(user_root.parent)
    staging_parent = Path(tempfile.mkdtemp(prefix=".assembly-import-", dir=user_root.parent))
    staging = staging_parent / "assembly" / identifier
    staging.mkdir(parents=True)
    try:
        for name, data in files.items():
            destination = staging / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        _refresh_shared(staging_parent)
        runtime = _runtime()
        validated = runtime._validate_template(descriptor, staging)
        # Descriptors and Python entry points use the same checks as installed
        # templates. Actual BRep processing follows through native preview APIs.
        runtime.USER_ROOT = staging_parent / "assembly"
        runtime._load_applicability_script(identifier)
        runtime._load_example_product_script(identifier)
        if (staging / "assembly.py").is_file():
            runtime._load_assembly_script(identifier)
        runtime.get_example_product(identifier)
        staging.rename(target)
        public = copy.deepcopy(validated)
        public["previewScene"].pop("designParts", None)
        public["parameters"] = [p for p in public["parameters"] if p.get("scope") != "scene"]
        public["libraryScope"] = "user"
        public["sourceFormat"] = "itat"
        return {"template": public}
    finally:
        shutil.rmtree(staging_parent, ignore_errors=True)


def generate(parameters, context):
    if parameters.get("action") != "import":
        raise ValueError("未知装配工艺包操作")
    return _install(parameters.get("sourcePath"), context.get("userAssemblyRoot"))
