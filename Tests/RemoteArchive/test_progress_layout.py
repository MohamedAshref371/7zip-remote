"""Check compiled progress-dialog resources without a desktop connection."""
import argparse
import ctypes
from pathlib import Path
import struct


def read_dialog(exe):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    api.LoadLibraryExW.restype = ctypes.c_void_p
    api.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    api.FindResourceW.restype = ctypes.c_void_p
    api.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    api.LoadResource.restype = ctypes.c_void_p
    api.LockResource.argtypes = [ctypes.c_void_p]
    api.LockResource.restype = ctypes.c_void_p
    api.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    api.SizeofResource.restype = ctypes.c_uint32
    api.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = api.LoadLibraryExW(str(exe.resolve()), None, 2)  # data only; no executable code
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        resource = api.FindResourceW(module, 97, 5)  # IDD_PROGRESS, RT_DIALOG
        assert resource, 'Missing progress dialog'
        data = ctypes.string_at(api.LockResource(api.LoadResource(module, resource)),
                                api.SizeofResource(module, resource))
    finally:
        api.FreeLibrary(module)

    style, _, count, _, _, width, height = struct.unpack_from('<IIHhhhh', data)
    offset = 18

    def skip_text():
        nonlocal offset
        value, = struct.unpack_from('<H', data, offset)
        offset += 2
        if value == 0xffff:
            offset += 2
        else:
            while value:
                value, = struct.unpack_from('<H', data, offset)
                offset += 2

    for _ in range(3):  # menu, class, title
        skip_text()
    if style & 0x40:  # DS_SETFONT
        offset += 2
        skip_text()
    items = {}
    for _ in range(count):
        offset = (offset + 3) & ~3
        _, _, x, y, w, h, item_id = struct.unpack_from('<IIhhhhH', data, offset)
        offset += 18
        skip_text()
        skip_text()
        extra, = struct.unpack_from('<H', data, offset)
        offset += 2 + extra
        items[item_id] = (x, y, w, h)
    return width, height, items


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--local-exe', type=Path)
    args = parser.parse_args()
    width, height, items = read_dialog(args.exe)
    previous_bottom = items[126][1] + items[126][3]  # compression ratio
    for item_id in (127, 128, 129, 103):  # network rows, then status
        x, y, w, h = items[item_id]
        assert y >= previous_bottom, (item_id, items[item_id])
        assert x >= 0 and x + w <= width and y + h <= height
        previous_bottom = y + h
    if args.local_exe:
        _, local_height, local_items = read_dialog(args.local_exe)
        assert not any(i in local_items for i in (127, 128, 129))
        assert height - local_height == 33
    print('PASS compiled network rows fit without overlap; local layout retained.')


if __name__ == '__main__':
    main()
