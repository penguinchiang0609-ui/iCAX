"""Small, deterministic reader/writer for encrypted iCAX product template (.itpt) files."""
from __future__ import annotations

import json
import binascii
import base64
import os
from pathlib import Path, PurePosixPath
import struct
import time
import zipfile
import zlib

MAX_ARCHIVE_BYTES = 64 * 1024 * 1024
MAX_MEMBER_BYTES = 16 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
REQUIRED = ("template.json", "template.py")
PASSWORD_TEXT = "ICAX_TUBE_DESIGNER"
PASSWORD = PASSWORD_TEXT.encode("ascii")


def _crc32_table() -> tuple[int, ...]:
    table = []
    for value in range(256):
        crc = value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
        table.append(crc & 0xFFFFFFFF)
    return tuple(table)


_CRC32_TABLE = _crc32_table()


def _zipcrypto_update(keys: list[int], value: int) -> None:
    keys[0] = ((keys[0] >> 8) ^ _CRC32_TABLE[(keys[0] ^ value) & 0xFF]) & 0xFFFFFFFF
    keys[1] = ((keys[1] + (keys[0] & 0xFF)) * 134775813 + 1) & 0xFFFFFFFF
    keys[2] = ((keys[2] >> 8) ^ _CRC32_TABLE[(keys[2] ^ (keys[1] >> 24)) & 0xFF]) & 0xFFFFFFFF


def _zipcrypto_transform(data: bytes) -> bytes:
    keys = [0x12345678, 0x23456789, 0x34567890]
    for value in PASSWORD:
        _zipcrypto_update(keys, value)
    output = bytearray()
    for value in data:
        temp = (keys[2] | 2) & 0xFFFFFFFF
        output.append(value ^ ((temp * (temp ^ 1) >> 8) & 0xFF))
        _zipcrypto_update(keys, value)
    return bytes(output)


def _dos_timestamp() -> tuple[int, int]:
    current = time.localtime()
    year = max(1980, min(current.tm_year, 2107))
    return (
        (current.tm_hour << 11) | (current.tm_min << 5) | (current.tm_sec // 2),
        ((year - 1980) << 9) | (current.tm_mon << 5) | current.tm_mday,
    )


def _build_encrypted_zip(entries: list[tuple[str, bytes]]) -> bytes:
    """Write a small ZIP archive using the interoperable legacy ZipCrypto mode.

    Python's stdlib can read ZipCrypto but intentionally cannot write encrypted
    archives.  itpt files contain two required members plus bounded,
    package-local resources, so writing the ZIP records here keeps the runtime
    self-contained and avoids a third-party dependency in the embedded worker.
    """
    local_header = struct.Struct("<IHHHHHIIIHH")
    central_header = struct.Struct("<IHHHHHHIIIHHHHHII")
    output = bytearray()
    central = []
    dos_time, dos_date = _dos_timestamp()
    for name, data in entries:
        name_bytes = name.encode("utf-8")
        compressor = zlib.compressobj(level=6, method=zlib.DEFLATED, wbits=-15)
        compressed = compressor.compress(data) + compressor.flush()
        crc = binascii.crc32(data) & 0xFFFFFFFF
        encrypted = _zipcrypto_transform(os.urandom(11) + bytes((crc >> 24,)) + compressed)
        offset = len(output)
        flags = 0x801  # encrypted + UTF-8 filename; no data descriptor
        output.extend(local_header.pack(
            0x04034B50, 20, flags, 8, dos_time, dos_date, crc,
            len(encrypted), len(data), len(name_bytes), 0,
        ))
        output.extend(name_bytes)
        output.extend(encrypted)
        central.append(central_header.pack(
            0x02014B50, 20, 20, flags, 8, dos_time, dos_date, crc,
            len(encrypted), len(data), len(name_bytes), 0, 0, 0, 0, 0, offset,
        ) + name_bytes)
    central_offset = len(output)
    central_bytes = b"".join(central)
    output.extend(central_bytes)
    output.extend(struct.pack(
        "<IHHHHIIH", 0x06054B50, 0, 0, len(entries), len(entries),
        len(central_bytes), central_offset, 0,
    ))
    return bytes(output)


def _member_name(info: zipfile.ZipInfo) -> str:
    value = info.filename.replace("\\", "/")
    path = PurePosixPath(value)
    if path.is_absolute() or ":" in value or ".." in path.parts:
        raise ValueError("itpt 包包含不安全的文件路径")
    return "/".join(part for part in path.parts if part not in ("", "."))


def _resource_name(value: object) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"itpt 附属资源名称无效：{value}")
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    if path.is_absolute() or ":" in normalized or ".." in path.parts:
        raise ValueError(f"itpt 附属资源名称无效：{value}")
    return "/".join(part for part in path.parts if part not in ("", "."))


def _validate_descriptor(value: object, *, allow_legacy_presets: bool = False) -> dict:
    if not isinstance(value, dict) or value.get("schema") != "icax.template-descriptor":
        raise ValueError("itpt 的 template.json 不是产品模板描述")
    if value.get("schemaVersion") != 1:
        raise ValueError("itpt 的 template.json schemaVersion 不受支持")
    if not isinstance(value.get("id"), str) or not value["id"].strip():
        raise ValueError("itpt 模板 ID 不能为空")
    if not isinstance(value.get("version"), str) or not value["version"].strip():
        raise ValueError("itpt 模板版本不能为空")
    if not value.get("displayName"):
        raise ValueError("itpt 模板名称不能为空")
    # One .itpt file is one product template.  A catalogue preset list would
    # turn one package into several templates, which is intentionally not part
    # of the package format.  Each style gets its own .itpt instead.
    catalog = value.get("extensions", {}).get("catalog") if isinstance(value.get("extensions"), dict) else None
    if isinstance(catalog, dict) and "presets" in catalog and not allow_legacy_presets:
        raise ValueError("itpt 一个压缩包只能包含一个产品模板，请为每个款式分别导出 .itpt")
    return value


def _read(path: Path, *, allow_legacy_presets: bool = False) -> tuple[dict, str, dict[str, str]]:
    if not path.is_file() or path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("itpt 文件不存在或超过 64 MB")
    try:
        archive = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as error:
        raise ValueError("itpt 不是有效的 ZIP 压缩包") from error
    with archive:
        members: dict[str, zipfile.ZipInfo] = {}
        total = 0
        for info in archive.infolist():
            name = _member_name(info)
            if not name or info.is_dir():
                continue
            if name in members:
                raise ValueError(f"itpt 包文件重复：{name}")
            if info.file_size > MAX_MEMBER_BYTES:
                raise ValueError(f"itpt 包文件过大：{name}")
            total += info.file_size
            if total > MAX_TOTAL_BYTES:
                raise ValueError("itpt 解压后超过 64 MB")
            members[name] = info
        for name in REQUIRED:
            if name not in members:
                raise ValueError(f"itpt 包缺少：{name}")
        if any(not (info.flag_bits & 0x1) for info in members.values()):
            raise ValueError("itpt 必须使用产品固定密码保护")
        try:
            archive.setpassword(PASSWORD)
            descriptor = json.loads(archive.read(members["template.json"]).decode("utf-8-sig"))
            script = archive.read(members["template.py"]).decode("utf-8-sig")
            resources = {
                name: base64.b64encode(archive.read(info)).decode("ascii")
                for name, info in members.items() if name not in REQUIRED
            }
        except RuntimeError as error:
            raise ValueError("itpt 密码校验失败或压缩包已损坏") from error
        except (UnicodeDecodeError, json.JSONDecodeError, zipfile.BadZipFile, zlib.error) as error:
            raise ValueError("itpt 内容不是有效 UTF-8 或 JSON") from error
    return _validate_descriptor(descriptor, allow_legacy_presets=allow_legacy_presets), script, resources


def _inspect(source_path: str) -> dict:
    # Import/inspection remains backward compatible with old multi-style
    # packages; migration turns those records into one descriptor per style.
    descriptor, script, resources = _read(Path(source_path), allow_legacy_presets=True)
    return {
        "descriptor": descriptor,
        "scriptSource": script,
        "resources": resources,
        "sourceFileName": Path(source_path).name,
    }


def _inspect_directory(source_path: str) -> dict:
    root = Path(source_path).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("itpt 模板资源目录无效")
    descriptor_path = root / "template.json"
    script_path = root / "template.py"
    if not descriptor_path.is_file() or not script_path.is_file():
        raise ValueError("itpt 模板目录缺少 template.json 或 template.py")
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8-sig"))
    script = script_path.read_text(encoding="utf-8-sig")
    return {
        # Directory inspection also reads legacy development templates so an
        # existing installation can be migrated without blocking startup.
        "descriptor": _validate_descriptor(descriptor, allow_legacy_presets=True),
        "scriptSource": script,
        "resources": _read_directory_resources(root),
        "sourceFileName": root.name + ".itpt",
    }


def _read_directory_resources(source: Path) -> dict[str, str]:
    """Collect package-local assets when exporting an installed template.

    The directory form is the development representation of one .itpt.  Only
    files below that directory are accepted; Python bytecode caches are not
    part of a portable template package.
    """
    root = source.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("itpt 模板资源目录无效")
    result: dict[str, str] = {}
    for file in sorted(root.rglob("*")):
        if not file.is_file() or "__pycache__" in file.parts:
            continue
        resolved = file.resolve(strict=True)
        if not resolved.is_relative_to(root):
            raise ValueError("itpt 模板资源不得通过链接越过模板目录")
        name = resolved.relative_to(root).as_posix()
        if name in REQUIRED:
            continue
        data = resolved.read_bytes()
        if len(data) > MAX_MEMBER_BYTES:
            raise ValueError(f"itpt 包文件过大：{name}")
        result[name] = base64.b64encode(data).decode("ascii")
    return result


def _export(parameters: dict) -> dict:
    descriptor = _validate_descriptor(parameters.get("descriptor"))
    script = parameters.get("scriptSource")
    resources = parameters.get("resources", {})
    source_directory = parameters.get("sourceDirectory")
    target = parameters.get("targetPath")
    if not isinstance(script, str) or not script.strip():
        raise ValueError("itpt 模板脚本不能为空")
    if not isinstance(resources, dict):
        raise ValueError("itpt 附属资源必须是文件名到 Base64 内容的对象")
    if source_directory is not None:
        if not isinstance(source_directory, str) or not source_directory.strip():
            raise ValueError("itpt 模板资源目录无效")
        directory_resources = _read_directory_resources(Path(source_directory))
        directory_resources.update(resources)
        resources = directory_resources
    if not isinstance(target, str) or not target.strip():
        raise ValueError("itpt 导出路径不能为空")
    path = Path(target)
    # Always emit the product-template extension with the product's canonical
    # spelling, even when a host save dialog returns a lower-case suffix.
    if path.suffix.lower() != ".itpt":
        path = path.with_suffix(".itpt")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor_bytes = json.dumps(descriptor, ensure_ascii=False, indent=2).encode("utf-8")
    script_bytes = script.encode("utf-8")
    if len(descriptor_bytes) > MAX_MEMBER_BYTES or len(script_bytes) > MAX_MEMBER_BYTES:
        raise ValueError("itpt 模板内容超过大小限制")
    entries: list[tuple[str, bytes]] = [
        ("template.json", descriptor_bytes),
        ("template.py", script_bytes),
    ]
    seen = set(REQUIRED)
    resource_total = len(descriptor_bytes) + len(script_bytes)
    for raw_name, raw_data in sorted(resources.items(), key=lambda item: str(item[0])):
        name = _resource_name(raw_name)
        if name in seen or not name:
            raise ValueError(f"itpt 附属资源名称无效：{raw_name}")
        if not isinstance(raw_data, str):
            raise ValueError(f"itpt 附属资源不是 Base64 文本：{name}")
        try:
            data = base64.b64decode(raw_data, validate=True)
        except (ValueError, binascii.Error) as error:
            raise ValueError(f"itpt 附属资源不是有效 Base64：{name}") from error
        if len(data) > MAX_MEMBER_BYTES:
            raise ValueError(f"itpt 包文件过大：{name}")
        resource_total += len(data)
        if resource_total > MAX_TOTAL_BYTES:
            raise ValueError("itpt 解压后超过 64 MB")
        seen.add(name)
        entries.append((name, data))
    archive_bytes = _build_encrypted_zip(entries)
    if len(archive_bytes) > MAX_ARCHIVE_BYTES:
        raise ValueError("itpt 压缩包超过 64 MB")
    path.write_bytes(archive_bytes)
    return {"path": str(path), "format": "itpt"}


def main(parameters: dict) -> dict:
    action = parameters.get("action")
    if action == "inspect":
        return {"package": _inspect(str(parameters.get("sourcePath", "")))}
    if action == "inspect-directory":
        return {"package": _inspect_directory(str(parameters.get("sourceDirectory", "")))}
    if action == "export":
        return _export(parameters)
    raise ValueError("未知 itpt 操作")


def generate(parameters: dict, context: dict) -> dict:
    """Entry point expected by the embedded iCAX template worker."""
    del context
    return main(parameters)
