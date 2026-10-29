"""Build this checkout's Windows x64 engine/CLI using the portable Zig compiler.

Reads source lists from the upstream MinGW makefiles, selecting their Windows,
multithreaded, C-only (no MASM) configuration. This is a development build, not
the optimized upstream release build. No tools are downloaded or installed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def source_list(directory, http=False):
    variables = {"SystemDrive": "C:", "IS_MINGW": "1", "O": "obj"}
    if http:
        variables["Z7_HTTP"] = "1"

    def expand(value):
        for _ in range(30):
            result = re.sub(r"\$\(([^)]+)\)|\$([A-Za-z_])",
                            lambda m: variables.get(m[1] or m[2], ""), value)
            if result == value:
                return result
            value = result
        raise ValueError("Recursive make variable")

    def read(path):
        active = [True]
        text = path.read_text().replace("\\\n", " ")
        for raw in text.splitlines():
            line = raw.split("#", 1)[0].strip()
            if line.startswith(("ifdef ", "ifndef ")):
                kind, name = line.split()
                condition = bool(variables.get(name))
                active.append(active[-1] and (condition if kind == "ifdef" else not condition))
            elif line == "else":
                active[-1] = active[-2] and not active[-1]
            elif line == "endif":
                active.pop()
            elif not active[-1]:
                continue
            elif line.startswith("include "):
                include = line.split(None, 1)[1]
                if include.endswith("7zip_gcc.mak"):
                    continue  # Only source-selection variables are evaluated.
                read((directory / include).resolve())
            elif line:
                match = re.fullmatch(r"([\w]+)\s*(\+?=)\s*(.*)", line)
                if not match:
                    raise ValueError(f"Unsupported source-list syntax in {path}: {line}")
                variables[match[1]] = (variables.get(match[1], "") + " " + match[3]
                                       if match[2] == "+=" else match[3])
        assert len(active) == 1, path

    read(directory / "makefile.gcc")
    # The shared rules use explicit source paths. Select the portable C versions
    # of optimized objects; assembler rules are deliberately excluded.
    rules = (ROOT / "CPP/7zip/7zip_gcc.mak").read_text()
    paths = {}
    for name, source in re.findall(r"^\$O/([\w]+)\.o:\s+(\S+\.(?:cpp|c))\b", rules, re.M):
        paths[name] = (directory / source).resolve()
    result = []
    for obj in expand(variables["OBJS"]).split():
        name = Path(obj).stem
        if name == "resource":
            continue
        source = paths[name]
        if not source.is_file():
            raise FileNotFoundError(source)
        result.append((name, source))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=["engine", "console", "all"], default="all")
    parser.add_argument("--zig", type=Path, default=ROOT / ".build/tools/ziglang/zig.exe")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--http", action="store_true", help="Enable HTTP in the console build")
    parser.add_argument("--out", type=Path, default=ROOT / ".build/native")
    args = parser.parse_args()
    out = args.out.resolve()
    temp = ROOT / ".build/tmp"
    out.mkdir(parents=True, exist_ok=True)
    temp.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / ".build/zig-cache"),
               ZIG_LOCAL_CACHE_DIR=str(ROOT / ".build/zig-local"),
               TMP=str(temp), TEMP=str(temp), TMPDIR=str(temp))
    zig = str(args.zig.resolve())
    targets = ["engine", "console"] if args.target == "all" else [args.target]
    for target in targets:
        directory = ROOT / ("CPP/7zip/Bundles/Format7zF" if target == "engine"
                            else "CPP/7zip/UI/Console")
        sources = source_list(directory, args.http and target == "console")
        objdir = out / target
        objdir.mkdir(exist_ok=True)
        flags = ["-target", "x86_64-windows-gnu", "-O2", "-DNDEBUG",
                 "-D_REENTRANT", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE"]
        flags += ["-DZ7_EXTERNAL_CODECS"]
        if target == "console":
            flags += ["-DZ7_DEVICE_FILE"]
            if args.http:
                flags += ["-DZ7_HTTP"]

        def compile_one(item):
            name, source = item
            obj = objdir / (name + ".o")
            command = [zig, "c++" if source.suffix == ".cpp" else "cc", *flags]
            if source.suffix == ".cpp":
                command += ["-std=c++17"]
            command += ["-c", str(source), "-o", str(obj)]
            result = subprocess.run(command, cwd=directory, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                raise RuntimeError(f"{source}\n{result.stdout}")
            return str(obj)

        print(f"Building {target}: {len(sources)} translation units", flush=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            objects = list(pool.map(compile_one, sources))
        resource = objdir / "resource.res"
        subprocess.run([zig, "rc", "/fo", str(resource), "resource.rc"],
                       cwd=directory, env=env, check=True)
        binary = out / ("7z.dll" if target == "engine" else "7z.exe")
        command = [zig, "c++", *flags, *objects, str(resource), "-o", str(binary)]
        if target == "engine":
            command += ["-shared", str(ROOT / "CPP/7zip/Archive/Archive2.def")]
        command += ["-loleaut32", "-luuid", "-ladvapi32", "-luser32", "-lole32",
                    "-lgdi32", "-lcomctl32", "-lcomdlg32", "-lshell32"]
        if target == "console" and args.http:
            command += ["-lwinhttp"]
        subprocess.run(command, cwd=directory, env=env, check=True)
        (objdir / "sources.json").write_text(json.dumps(
            [str(source.relative_to(ROOT)) for _, source in sources], indent=2) + "\n")
        print(binary, flush=True)


if __name__ == "__main__":
    main()
