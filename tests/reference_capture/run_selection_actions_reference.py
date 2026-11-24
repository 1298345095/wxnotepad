"""Run the independent original action boundary capture on an owned private desktop."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path

from private_desktop import Process


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    user = c.WinDLL('user32', use_last_error=True)
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    name = 'selection-actions-boundary-' + str(os.getpid())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
    assert desktop, c.WinError(c.get_last_error())
    try:
        process = Process([args.reference.resolve(), args.output_dir.resolve()], name)
        assert process.wait(timeout=30) == 0, 'independent native original action capture'
    finally:
        user.CloseDesktop(desktop)
    print((args.output_dir / 'original-selection-actions.tsv').read_text(encoding='utf-8'))


if __name__ == '__main__':
    main()
