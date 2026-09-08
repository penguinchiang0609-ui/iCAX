"""Small, deterministic reader/writer for encrypted iCAX product template (.iPT) files."""
from __future__ import annotations

import json
import binascii
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
    archives.  iPT files only contain two bounded members, so writing the ZIP
    records here keeps the runtime self-contained and avoids a third-party
    dependency in the embedded worker.
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
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("iPT 包包含不安全的文件路径")
    return "/".join(part for part in path.parts if part not in ("", "."))


def _validate_descriptor(value: object) -> dict:
    if not isinstance(value, dict) or value.get("schema") != "icax.template-descriptor":
        raise ValueError("iPT 的 template.json 不是产品模板描述")
    if value.get("schemaVersion") != 1:
        raise ValueError("iPT 的 template.json schemaVersion 不受支持")
    if not isinstance(value.get("id"), str) or not value["id"].strip():
        raise ValueError("iPT 模板 ID 不能为空")
    if not isinstance(value.get("version"), str) or not value["version"].strip():
        raise ValueError("iPT 模板版本不能为空")
    if not value.get("displayName"):
        raise ValueError("iPT 模板名称不能为空")
    return value


def _read(path: Path) -> tuple[dict, str]:
    if not path.is_file() or path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("iPT 文件不存在或超过 64 MB")
    try:
        archive = zipfile.ZipFile(path, "r")
    except zipfile.BadZipFile as error:
        raise ValueError("iPT 不是有效的 ZIP 压缩包") from error
    with archive:
        members: dict[str, zipfile.ZipInfo] = {}
        total = 0
        for info in archive.infolist():
            name = _member_name(info)
            if not name or info.is_dir():
                continue
            if name in members:
                raise ValueError(f"iPT 包文件重复：{name}")
            if info.file_size > MAX_MEMBER_BYTES:
                raise ValueError(f"iPT 包文件过大：{name}")
            total += info.file_size
            if total > MAX_TOTAL_BYTES:
                raise ValueError("iPT 解压后超过 64 MB")
            members[name] = info
        for name in REQUIRED:
            if name not in members:
                raise ValueError(f"iPT 包缺少：{name}")
        if any(not (members[name].flag_bits & 0x1) for name in REQUIRED):
            raise ValueError("iPT 必须使用产品固定密码保护")
        unexpected = [name for name in members if name not in REQUIRED]
        if unexpected:
            raise ValueError("iPT 根目录只允许 template.json 和 template.py")
        try:
            archive.setpassword(PASSWORD)
            descriptor = json.loads(archive.read(members["template.json"]).decode("utf-8-sig"))
            script = archive.read(members["template.py"]).decode("utf-8-sig")
        except RuntimeError as error:
            raise ValueError("iPT 密码校验失败或压缩包已损坏") from error
        except (UnicodeDecodeError, json.JSONDecodeError, zipfile.BadZipFile, zlib.error) as error:
            raise ValueError("iPT 内容不是有效 UTF-8 或 JSON") from error
    return _validate_descriptor(descriptor), script


def _inspect(source_path: str) -> dict:
    descriptor, script = _read(Path(source_path))
    return {"descriptor": descriptor, "scriptSource": script, "sourceFileName": Path(source_path).name}


def _export(parameters: dict) -> dict:
    descriptor = _validate_descriptor(parameters.get("descriptor"))
    script = parameters.get("scriptSource")
    target = parameters.get("targetPath")
    if not isinstance(script, str) or not script.strip():
        raise ValueError("iPT 模板脚本不能为空")
    if not isinstance(target, str) or not target.strip():
        raise ValueError("iPT 导出路径不能为空")
    path = Path(target)
    # Always emit the product-template extension with the product's canonical
    # spelling, even when a host save dialog returns a lower-case suffix.
    if path.suffix.lower() != ".ipt" or path.suffix != ".iPT":
        path = path.with_suffix(".iPT")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor_bytes = json.dumps(descriptor, ensure_ascii=False, indent=2).encode("utf-8")
    script_bytes = script.encode("utf-8")
    if len(descriptor_bytes) > MAX_MEMBER_BYTES or len(script_bytes) > MAX_MEMBER_BYTES:
        raise ValueError("iPT 模板内容超过大小限制")
    archive_bytes = _build_encrypted_zip([
        ("template.json", descriptor_bytes),
        ("template.py", script_bytes),
    ])
    if len(archive_bytes) > MAX_ARCHIVE_BYTES:
        raise ValueError("iPT 压缩包超过 64 MB")
    path.write_bytes(archive_bytes)
    return {"path": str(path), "format": "iPT"}


def main(parameters: dict) -> dict:
    action = parameters.get("action")
    if action == "inspect":
        return {"package": _inspect(str(parameters.get("sourcePath", "")))}
    if action == "export":
        return _export(parameters)
    raise ValueError("未知 iPT 操作")


def generate(parameters: dict, context: dict) -> dict:
    """Entry point expected by the embedded iCAX template worker."""
    del context
    return main(parameters)
