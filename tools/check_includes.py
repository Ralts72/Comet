#!/usr/bin/env python3
"""使用真实编译参数检查 PCH 覆盖的源码，保留头文件独立性检查。"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess


def without_pch(arguments):
    command = []
    uses_pch = False
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if (
            argument == "-Xclang"
            and arguments[index + 1] in ("-include-pch", "-include")
            and "cmake_pch" in arguments[index + 3]
        ):
            uses_pch = True
            index += 4
        elif argument in ("-include", "-include-pch") and "cmake_pch" in arguments[index + 1]:
            uses_pch = True
            index += 2
        elif argument in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
        elif argument in ("-c", "-MD", "-MMD", "-MP", "-Winvalid-pch", "-fpch-preprocess"):
            index += 1
        else:
            command.append(argument)
            index += 1
    if command and Path(command[0]).name in ("ccache", "sccache"):
        command.pop(0)
    return command + ["-fsyntax-only", "-O0"] if uses_pch else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument(
        "--base", help="只检查已提交的改动；头文件或构建规则改动时检查全部 PCH 源码"
    )
    parser.add_argument("--jobs", type=int, default=4)
    options = parser.parse_args()
    if options.jobs < 1:
        parser.error("--jobs must be positive")
    root = Path(__file__).resolve().parents[1]
    changed = None
    if options.base:
        output = subprocess.check_output(
            ["git", "diff", "--name-only", "--no-renames", "-z", options.base, "HEAD"], cwd=root
        )
        changed = set(output.decode().rstrip("\0").split("\0")) - {""}
        # 头文件、子仓或编译规则会影响未改动的调用方，必须扩大检查范围。
        if any(
            Path(path).suffix
            in (".h", ".hh", ".hpp", ".hxx", ".inl", ".inc", ".ipp", ".tpp", ".cmake")
            or Path(path).name in ("CMakeLists.txt", "CMakePresets.json")
            or path.startswith("3rdparty/")
            or path == ".github/workflows/ci.yml"
            for path in changed
        ):
            changed = None
    entries = json.loads((options.build_directory / "compile_commands.json").read_text())
    checks = []
    for entry in entries:
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if not source.is_relative_to(root) or source.relative_to(root).parts[0] not in (
            "engine",
            "editor",
            "app",
            "tools",
            "tests",
        ):
            continue
        if changed is not None and source.relative_to(root).as_posix() not in changed:
            continue
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        command = without_pch(arguments)
        if command:
            checks.append((entry, command))

    def check(item):
        entry, command = item
        result = subprocess.run(
            command, cwd=entry["directory"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        if result.stdout:
            print(f"{entry['file']}:\n{result.stdout}", flush=True)
        return result.returncode != 0

    print(f"Checking {len(checks)} sources without PCH", flush=True)
    with ThreadPoolExecutor(max_workers=options.jobs) as executor:
        failures = sum(executor.map(check, checks))
    print(f"Checked {len(checks)} sources; failures={failures}", flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
