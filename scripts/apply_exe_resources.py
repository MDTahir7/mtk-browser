"""Copies MTK Browser's icon, version and manifest resources onto the shipped
executable.

With the CEF sandbox enabled the executable is CEF's bootstrap binary, so the
resources compiled into MTKBrowser.dll (icon, ProductName, FileVersion) would
otherwise not be visible in Explorer, the taskbar or the shortcut. The
process-wide manifest (per-monitor DPI, long paths, common controls) also has to
live on the executable rather than the DLL.

This is a build step only: no network access and no external dependency. The
resource identifiers are read directly instead of enumerated, so the behaviour
does not depend on callback ordering.

Usage:  python apply_exe_resources.py <source.dll> <target.exe> [manifest.xml]
"""

from __future__ import annotations

import ctypes
import struct
import sys
from ctypes import wintypes

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

LOAD_LIBRARY_AS_DATAFILE = 0x00000002
LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020

RT_ICON = 3
RT_GROUP_ICON = 14
RT_VERSION = 16
RT_MANIFEST = 24
# Must match IDI_MTK_BROWSER in native/src/resource.h.
ICON_GROUP_ID = 101
# VS_VERSION_INFO always uses resource id 1.
VERSION_ID = 1
CREATEPROCESS_MANIFEST_RESOURCE_ID = 1
LANG_NEUTRAL = 0
LANG_EN_US = 0x0409

kernel32.LoadLibraryExW.restype = wintypes.HMODULE
kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE,
                                    wintypes.DWORD]
kernel32.FreeLibrary.restype = wintypes.BOOL
kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
kernel32.FindResourceW.restype = wintypes.HANDLE
kernel32.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR,
                                   wintypes.LPCWSTR]
kernel32.LoadResource.restype = wintypes.HANDLE
kernel32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
kernel32.LockResource.restype = ctypes.c_void_p
kernel32.LockResource.argtypes = [wintypes.HANDLE]
kernel32.SizeofResource.restype = wintypes.DWORD
kernel32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
kernel32.UpdateResourceW.restype = wintypes.BOOL
kernel32.UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR,
                                     wintypes.LPCWSTR, wintypes.WORD,
                                     ctypes.c_void_p, wintypes.DWORD]
kernel32.EndUpdateResourceW.restype = wintypes.BOOL
kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]


def _id(value: int):
    """Wraps an integer resource type/name the way the Win32 APIs expect."""
    return ctypes.cast(ctypes.c_void_p(value), wintypes.LPCWSTR)


def _read(module, resource_type: int, resource_id: int) -> bytes | None:
    handle = kernel32.FindResourceW(module, _id(resource_id),
                                    _id(resource_type))
    if not handle:
        return None
    data = kernel32.LoadResource(module, handle)
    size = kernel32.SizeofResource(module, handle)
    pointer = kernel32.LockResource(data)
    if not pointer or size == 0:
        return None
    return ctypes.string_at(pointer, size)


def _icon_image_ids(group: bytes) -> list[int]:
    """Image ids referenced by a GRPICONDIR structure."""
    if len(group) < 6:
        return []
    _reserved, _type, count = struct.unpack("<HHH", group[:6])
    ids = []
    for index in range(count):
        start = 6 + index * 14
        entry = group[start:start + 14]
        if len(entry) < 14:
            break
        ids.append(struct.unpack("<H", entry[12:14])[0])
    return ids


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    source, target = sys.argv[1], sys.argv[2]
    manifest_path = sys.argv[3] if len(sys.argv) == 4 else None

    manifest_bytes = b""
    if manifest_path:
        with open(manifest_path, "rb") as handle:
            manifest_bytes = handle.read()
        # A byte-order mark makes the loader reject the manifest.
        if manifest_bytes.startswith(b"\xef\xbb\xbf"):
            manifest_bytes = manifest_bytes[3:]
        if not manifest_bytes.strip():
            print("apply_exe_resources: manifest file is empty")
            return 1

    module = kernel32.LoadLibraryExW(
        source, None, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)
    if not module:
        print(f"apply_exe_resources: cannot read resources from {source}"
              f" (error {ctypes.get_last_error()})")
        return 1

    version = _read(module, RT_VERSION, VERSION_ID)
    icon_group = _read(module, RT_GROUP_ICON, ICON_GROUP_ID)
    icons = {}
    if icon_group:
        for image_id in _icon_image_ids(icon_group):
            blob = _read(module, RT_ICON, image_id)
            if blob:
                icons[image_id] = blob
    kernel32.FreeLibrary(module)

    if not version:
        print("apply_exe_resources: source DLL has no version resource")
        return 1
    if not icons:
        print("apply_exe_resources: source DLL has no icon images")
        return 1

    update = kernel32.BeginUpdateResourceW(target, False)
    if not update:
        print(f"apply_exe_resources: cannot open {target}"
              f" (error {ctypes.get_last_error()})")
        return 1

    def write(resource_type: int, resource_id: int, language: int,
              blob: bytes) -> bool:
        buffer = ctypes.create_string_buffer(blob, len(blob))
        return bool(kernel32.UpdateResourceW(update, _id(resource_type),
                                             _id(resource_id), language,
                                             buffer, len(blob)))

    written = 0
    for image_id, blob in icons.items():
        if not write(RT_ICON, image_id, LANG_EN_US, blob):
            print(f"apply_exe_resources: icon {image_id} failed"
                  f" (error {ctypes.get_last_error()})")
            kernel32.EndUpdateResourceW(update, True)
            return 1
        written += 1
    if not write(RT_GROUP_ICON, ICON_GROUP_ID, LANG_EN_US, icon_group):
        print(f"apply_exe_resources: icon group failed"
              f" (error {ctypes.get_last_error()})")
        kernel32.EndUpdateResourceW(update, True)
        return 1
    written += 1
    if not write(RT_VERSION, VERSION_ID, LANG_EN_US, version):
        print(f"apply_exe_resources: version failed"
              f" (error {ctypes.get_last_error()})")
        kernel32.EndUpdateResourceW(update, True)
        return 1
    written += 1
    if manifest_bytes:
        if not write(RT_MANIFEST, CREATEPROCESS_MANIFEST_RESOURCE_ID,
                     LANG_NEUTRAL, manifest_bytes):
            print(f"apply_exe_resources: manifest failed"
                  f" (error {ctypes.get_last_error()})")
            kernel32.EndUpdateResourceW(update, True)
            return 1
        written += 1

    if not kernel32.EndUpdateResourceW(update, False):
        print(f"apply_exe_resources: commit failed"
              f" (error {ctypes.get_last_error()})")
        return 1

    print(f"apply_exe_resources: wrote {written} resources to {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
