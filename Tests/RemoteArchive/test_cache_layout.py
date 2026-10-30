"""Inspect compiled Open URL/cache resources without operating the desktop."""
import argparse
from pathlib import Path
from test_progress_layout import read_dialog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--local-exe', type=Path)
    args = parser.parse_args()
    for dialog, controls, combos in [
        (2998, [101, 103, 102, 104, 105, 106, 1, 2], {101, 103, 105}),
        (2997, [110, 111, 112, 113, 114, 115, 116, 1, 2], set()),
    ]:
        width, height, items = read_dialog(args.exe, dialog)
        rects = []
        for control in controls:
            x, y, w, h = items[control]
            if control in combos: h = 14 # dropdown popup height is not the closed control
            assert x >= 0 and y >= 0 and x + w <= width and y + h <= height, (dialog, control, items[control])
            for other, a, b, c, d in rects:
                assert x + w <= a or a + c <= x or y + h <= b or b + d <= y, (dialog, control, other)
            rects.append((control, x, y, w, h))
        if args.local_exe:
            try: read_dialog(args.local_exe, dialog)
            except AssertionError: pass
            else: raise AssertionError(f'HTTP-only dialog {dialog} exists in local build')
    print('PASS Open URL and cache settings controls fit without overlaps.')


if __name__ == '__main__':
    main()
