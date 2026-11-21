"""Read JSON bytes with permissive Windows sharing; active indexes are gated upstream."""
from contextlib import contextmanager
from pathlib import Path
import os


@contextmanager
def open_json_snapshot(path):
    path = Path(path)
    if os.name != "nt":
        with path.open("rb") as reader:
            yield reader
        return
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    read_path = os.path.abspath(os.fspath(path))
    handle = kernel.CreateFileW(read_path, 0x80000000, 0x1 | 0x2 | 0x4, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(descriptor, "rb") as reader:
        yield reader


def read_json_bytes(path):
    with open_json_snapshot(path) as reader:
        return reader.read()
