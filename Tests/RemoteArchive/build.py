"""Build the Windows-only prototype with a portable Zig/Clang toolchain.

No downloads or package installation happen in this script.
"""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", type=Path, default=ROOT / ".build/tools/ziglang/zig.exe")
    parser.add_argument("--cache-check", action="store_true")
    args = parser.parse_args()
    out = ROOT / ".build/remote"
    out.mkdir(parents=True, exist_ok=True)
    temporary = ROOT / ".build/tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / ".build/zig-cache"),
               ZIG_LOCAL_CACHE_DIR=str(ROOT / ".build/zig-local"),
               TMP=str(temporary), TEMP=str(temporary), TMPDIR=str(temporary))
    sources = [
        "Tests/RemoteArchive/DiskCacheCheck.cpp" if args.cache_check else "CPP/7zip/UI/Remote7z/Remote7z.cpp",
        "CPP/7zip/Common/HttpInStream.cpp",
        "CPP/7zip/Common/TracingInStream.cpp",
        "CPP/Windows/HttpRangeSource.cpp",
        "CPP/7zip/Common/FileStreams.cpp",
        "CPP/Common/MyString.cpp", "CPP/Common/MyVector.cpp",
        "CPP/Common/StringConvert.cpp", "CPP/Common/IntToString.cpp",
        "CPP/Windows/FileIO.cpp", "CPP/Windows/FileFind.cpp",
        "CPP/Windows/FileName.cpp", "CPP/Windows/FileDir.cpp", "CPP/Windows/TimeUtils.cpp",
        "CPP/Windows/PropVariant.cpp",
    ]
    command = [str(args.zig.resolve()), "c++", "-target", "x86_64-windows-gnu",
               "-std=c++17", "-O2", "-g", "-municode", "-DUNICODE", "-D_UNICODE",
               "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               *sources, "-o", str(out / ("cachecheck.exe" if args.cache_check else "7zremote.exe")),
               "-lwinhttp", "-loleaut32", "-lole32", "-luuid", "-ladvapi32", "-lshell32"]
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    print(out / ("cachecheck.exe" if args.cache_check else "7zremote.exe"))


if __name__ == "__main__":
    main()
