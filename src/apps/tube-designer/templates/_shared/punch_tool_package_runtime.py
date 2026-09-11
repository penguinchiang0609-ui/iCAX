"""Reader for encrypted parametric punch-tool packages (.itmt).

The package deliberately mirrors the other product resources: tool.json is
the declarative descriptor and tool.py is the parametric section generator.
Only the product's fixed magic number is accepted as the ZIP password.
"""
from __future__ import annotations

import hashlib
import base64
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_TOTAL_BYTES = 8 * 1024 * 1024
REQUIRED = ("tool.json", "tool.py")
PASSWORD = b"ICAX_TUBE_DESIGNER"


def _member_name(info: zipfile.ZipInfo) -> str:
    value = info.filename.replace("\\", "/")
    path = PurePosixPath(value)
    if path.is_absolute() or ":" in value or ".." in path.parts:
        raise ValueError("itmt 包包含不安全的文件路径")
    return "/".join(part for part in path.parts if part not in ("", "."))


def _validate_descriptor(value: object) -> tuple[dict, dict]:
    if not isinstance(value, dict) or value.get("schema") != "icax.punch-tool":
        raise ValueError("itmt 的 tool.json 不是模具描述")
    if value.get("schemaVersion") != 1:
        raise ValueError("itmt 的 tool.json schemaVersion 不受支持")
    identifier = value.get("id")
    if not isinstance(identifier, str) or re.fullmatch(r"[a-z][a-z0-9_-]{0,79}", identifier) is None:
        raise ValueError("itmt 模具 ID 无效")
    version = value.get("version")
    if not isinstance(version, str) or not version.strip() or len(version) > 64:
        raise ValueError("itmt 模具版本无效")
    if not isinstance(value.get("displayName"), str) or not value["displayName"].strip():
        raise ValueError("itmt 模具名称不能为空")
    if value.get("kind") != "programmatic":
        raise ValueError("itmt 只能导入程式模具")
    if value.get("target") not in ("side", "end", "part"):
        raise ValueError("itmt 模具适用位置无效")
    definitions = value.get("parameters", [])
    if not isinstance(definitions, list) or len(definitions) > 64:
        raise ValueError("itmt 模具参数最多 64 项")
    defaults: dict = {}
    keys: set[str] = set()
    for index, definition in enumerate(definitions):
        if not isinstance(definition, dict):
            raise ValueError(f"itmt parameters[{index}] 无效")
        key = definition.get("key")
        if not isinstance(key, str) or re.fullmatch(r"[a-z][A-Za-z0-9]{0,79}", key) is None or key in keys:
            raise ValueError(f"itmt parameters[{index}].key 无效或重复")
        keys.add(key)
        if definition.get("valueType") not in ("number", "integer", "string", "boolean"):
            raise ValueError(f"itmt 参数 {key} 的 valueType 无效")
        if "defaultValue" not in definition:
            raise ValueError(f"itmt 参数 {key} 缺少 defaultValue")
        defaults[key] = definition["defaultValue"]
    return value, defaults


def _read(source_path: str) -> dict:
    path = Path(source_path)
    if path.suffix.lower() != ".itmt":
        raise ValueError("请选择 .itmt 程式模具包")
    if not path.is_file() or path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("itmt 文件不存在或超过 16 MB")
    try:
        archive = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as error:
        raise ValueError("itmt 不是有效的 ZIP 压缩包") from error
    with archive:
        members: dict[str, zipfile.ZipInfo] = {}
        total = 0
        for info in archive.infolist():
            name = _member_name(info)
            if not name or info.is_dir():
                continue
            if name in members:
                raise ValueError(f"itmt 包文件重复：{name}")
            if info.file_size > MAX_MEMBER_BYTES:
                raise ValueError(f"itmt 包文件过大：{name}")
            total += info.file_size
            if total > MAX_TOTAL_BYTES:
                raise ValueError("itmt 解压后超过 8 MB")
            members[name] = info
        for name in REQUIRED:
            if name not in members:
                raise ValueError(f"itmt 包缺少：{name}")
        if any(not (members[name].flag_bits & 0x1) for name in REQUIRED):
            raise ValueError("itmt 必须使用产品固定密码保护")
        if any(not (info.flag_bits & 0x1) for info in members.values()):
            raise ValueError("itmt 必须使用产品固定密码保护")
        try:
            archive.setpassword(PASSWORD)
            descriptor_bytes = archive.read(members["tool.json"])
            script_bytes = archive.read(members["tool.py"])
            resources = {
                name: base64.b64encode(archive.read(info)).decode("ascii")
                for name, info in members.items() if name not in REQUIRED
            }
            descriptor, defaults = _validate_descriptor(json.loads(descriptor_bytes.decode("utf-8-sig")))
            script = script_bytes.decode("utf-8-sig")
        except RuntimeError as error:
            raise ValueError("itmt 密码校验失败或压缩包已损坏") from error
        except (UnicodeDecodeError, json.JSONDecodeError, zipfile.BadZipFile) as error:
            raise ValueError("itmt 内容不是有效 UTF-8 或 JSON") from error
    if not script.strip():
        raise ValueError("itmt 的 tool.py 不能为空")
    digest = hashlib.sha256(descriptor_bytes + b"\0" + script_bytes).hexdigest()
    return {
        "schema": "icax.punch-tool-package-record",
        "schemaVersion": 1,
        "kind": "parametric-package",
        "descriptor": descriptor,
        "defaultParameters": defaults,
        "scriptSource": script,
        "resources": resources,
        "packageDigest": digest,
        "sourceFileName": path.name,
    }


def generate(parameters: dict, context: dict) -> dict:
    del context
    if parameters.get("action") != "inspect":
        raise ValueError("未知 itmt 操作")
    return {"package": _read(str(parameters.get("sourcePath", "")))}
