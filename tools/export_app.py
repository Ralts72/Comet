#!/usr/bin/env python3
"""增量构建 Release，将所选关卡导出为当前桌面平台的可搬移程序。"""

import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def run(*arguments, cwd=None, stream=False):
    output = ""
    with subprocess.Popen(arguments, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        for line in process.stdout:
            output += line
            if stream:
                print(line, end="", flush=True)
                output = output[-65536:]
        if process.wait():
            raise RuntimeError(f"Command failed: {arguments[0]}\n{output}")
    return output


def stage(message):
    print(f"[Comet export] {message}", flush=True)


def cmake_cache(build):
    entries = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(("//", "#")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        entries[key.split(":", 1)[0]] = value
    return entries


def molten_vk_manifest(cache):
    library = Path(cache["Vulkan_LIBRARY"]).resolve()
    roots = [library.parent.parent, Path("/usr/local")]
    if os.environ.get("VULKAN_SDK"):
        roots.insert(0, Path(os.environ["VULKAN_SDK"]))
    for root in roots:
        manifest = root / "share/vulkan/icd.d/MoltenVK_icd.json"
        if not manifest.is_file():
            continue
        data = json.loads(manifest.read_text())
        driver = (manifest.parent / data["ICD"]["library_path"]).resolve()
        if driver.is_file():
            return data, driver
    raise RuntimeError("Cannot find MoltenVK_icd.json and its driver in the configured Vulkan SDK")


def cmake_string(value):
    text = str(value)
    delimiter = "="
    while f"]{delimiter}]" in text:
        delimiter += "="
    return f"[{delimiter}[{text}]{delimiter}]"


def macho_rpaths(binary):
    lines = run("otool", "-l", str(binary)).splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            path = lines[index + 2].strip()
            yield path.removeprefix("path ").rsplit(" (offset ", 1)[0]


def bundle_libraries(bundle, build, cache, temporary):
    frameworks = bundle / "Contents/Frameworks"
    resources = bundle / "Contents/Resources"
    frameworks.mkdir(exist_ok=True)
    manifest, driver = molten_vk_manifest(cache)
    bundled_driver = frameworks / driver.name
    shutil.copy2(driver, bundled_driver)
    manifest["ICD"]["library_path"] = f"../../../Frameworks/{driver.name}"
    icd = resources / "vulkan/icd.d/MoltenVK_icd.json"
    icd.parent.mkdir(parents=True)
    icd.write_text(json.dumps(manifest, indent=2) + "\n")
    directories = [build / "engine", build / "3rdparty/glfw/src",
                   Path(cache["Vulkan_LIBRARY"]).resolve().parent]
    recipe = temporary / "bundle.cmake"
    recipe.write_text(
        "cmake_minimum_required(VERSION 3.31)\n"
        "include(BundleUtilities)\n"
        "set(BU_CHMOD_BUNDLE_ITEMS TRUE)\n"
        f"fixup_bundle({cmake_string(bundle)} {cmake_string(bundled_driver)} "
        f"{cmake_string(';'.join(map(str, directories)))})\n"
    )
    run(cache["CMAKE_COMMAND"], "-P", str(recipe), stream=True)
    # fixup_bundle 重写依赖；同时清除旧开发 RPATH，缺失产品不能回退到构建机。
    binaries = [bundle / "Contents/MacOS/Comet"]
    binaries.extend(path for path in frameworks.iterdir() if path.is_file() and not path.is_symlink())
    for binary in binaries:
        for rpath in list(macho_rpaths(binary)):
            if not rpath.startswith("@"):
                run("install_name_tool", "-delete_rpath", rpath, str(binary))
    stage("正在签名并验证 App…")
    run("codesign", "--force", "--deep", "--sign", "-", str(bundle), stream=True)
    run("codesign", "--verify", "--deep", "--strict", str(bundle))


def validate_request(project, destination):
    if sys.platform not in ("darwin", "win32", "linux"):
        raise RuntimeError("Desktop export supports macOS, Windows and Linux")
    if sys.platform == "linux" and not shutil.which("patchelf"):
        raise RuntimeError("Linux export requires patchelf to remove development runtime paths")
    if sys.platform == "darwin" and destination.suffix != ".app":
        raise RuntimeError("Destination must end in .app")
    if destination.exists() or destination.is_symlink():
        raise RuntimeError(f"Destination already exists: {destination}")
    manifest = project if project.is_file() else project / "project.json"
    if not manifest.is_file():
        raise RuntimeError(f"Project file not found: {manifest}")
    for reserved in (manifest.parent / "assets", manifest.parent / ".comet"):
        if destination.resolve().is_relative_to(reserved.resolve()):
            raise RuntimeError("Destination must be outside source assets and caches")


def export_app(project, destination, scenes, cmake="cmake"):
    stage("正在检查导出设置…")
    validate_request(project, destination)
    root = Path(__file__).resolve().parent.parent
    stage("正在配置 Release…")
    run(cmake, "--preset", "app-release", cwd=root, stream=True)
    stage("正在构建运行程序…")
    run(cmake, "--build", "--preset", "app-release", "--config", "Release", "--target", "app", "comet_prepare_project",
        cwd=root, stream=True)
    return package_app(project, root / "build-release", destination, scenes)


def package_app(project, build, destination, scenes):
    root = Path(__file__).resolve().parent.parent
    build, project = build.resolve(), project.resolve()
    destination = destination.absolute()
    validate_request(project, destination)
    cache = cmake_cache(build)
    if Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != root:
        raise RuntimeError("Build directory belongs to another source repository")
    if sys.platform != "darwin":
        return package_desktop(project, build, destination, scenes, cache)
    source = build / "app/Comet.app"
    prepare = build / "tools/asset/comet_prepare_project"
    if not (source / "Contents/MacOS/Comet").is_file() or not prepare.is_file():
        raise RuntimeError("Build app and comet_prepare_project before exporting")
    if destination.resolve().is_relative_to(source):
        raise RuntimeError("Destination must be outside the development App")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".comet-app-", dir=destination.parent) as directory:
        temporary = Path(directory)
        bundle = temporary / destination.name
        shutil.copytree(source, bundle, symlinks=True)
        resources = bundle / "Contents/Resources"
        prepare_game_resources(resources, project, prepare, scenes)
        project_data = json.loads((resources / "game/project.json").read_text())
        plist_path = bundle / "Contents/Info.plist"
        with plist_path.open("rb") as stream:
            plist = plistlib.load(stream)
        plist["CFBundleName"] = project_data["name"]
        plist["CFBundleDisplayName"] = project_data["name"]
        plist["CFBundleIdentifier"] = "org.comet.game." + project_data["id"]
        with plist_path.open("wb") as stream:
            plistlib.dump(plist, stream)
        stage("正在打包运行依赖…")
        bundle_libraries(bundle, build, cache, temporary)
        if destination.exists() or destination.is_symlink():
            raise RuntimeError(f"Destination appeared during export: {destination}")
        bundle.rename(destination)
    return destination


def prepare_game_resources(resources, project, prepare, scenes):
    root = Path(__file__).resolve().parent.parent
    stage("正在准备所选关卡与资源…")
    # 开发 Bundle 不携带项目；若用户选错曾打包过的输入，不静默覆盖内容。
    run(str(prepare), str(project), "--export-assets", str(resources / "game"), *scenes, stream=True)
    profile = json.loads((root / "config.json").read_text()).get("app-release", {})
    (resources / "config.json").write_text(json.dumps({"app-release": profile}, indent=2) + "\n")
    for folder in ("fonts", "ui"):
        if not (resources / folder).is_dir():
            raise RuntimeError(f"Development App is missing {folder}; rebuild the app target")
    notices = resources / "licenses"
    for dependency in (root / "3rdparty").iterdir():
        if not dependency.is_dir():
            continue
        for license_file in dependency.iterdir():
            if license_file.is_file() and license_file.stem.lower() in ("license", "copying"):
                output = notices / dependency.name / license_file.name
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(license_file, output)


def desktop_dependencies(executable, build, cache, temporary):
    """Use CMake's native PE/ELF dependency resolver; the GPU driver remains a host requirement."""
    recipe = temporary / "dependencies.cmake"
    manifest = temporary / "dependencies.txt"
    directories = [executable.parent, build / "engine", build / "engine/Release"]
    if cache.get("Vulkan_LIBRARY"):
        directories.append(Path(cache["Vulkan_LIBRARY"]).parent)
    system = (r".*[/\\]([Ww][Ii][Nn][Dd][Oo][Ww][Ss])[/\\].*" if sys.platform == "win32"
              else r".*/(ld-linux[^/]*|lib(c|m|pthread|dl|rt|resolv)\.so[^/]*)$")
    recipe.write_text(
        "cmake_minimum_required(VERSION 3.31)\n"
        "file(GET_RUNTIME_DEPENDENCIES\n"
        f"  EXECUTABLES {cmake_string(executable)}\n"
        f"  DIRECTORIES {cmake_string(';'.join(map(str, directories)))}\n"
        "  RESOLVED_DEPENDENCIES_VAR found UNRESOLVED_DEPENDENCIES_VAR missing\n"
        '  PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"\n'
        f"  POST_EXCLUDE_REGEXES {cmake_string(system)})\n"
        'if(missing)\n  message(FATAL_ERROR "Unresolved runtime libraries: ${missing}")\nendif()\n'
        f'file(WRITE {cmake_string(manifest)} "${{found}}")\n', encoding="utf-8")
    run(cache["CMAKE_COMMAND"], "-P", str(recipe), stream=True)
    return [Path(value) for value in manifest.read_text().split(";") if value]


def package_desktop(project, build, destination, scenes, cache):
    root = Path(__file__).resolve().parent.parent
    windows = sys.platform == "win32"
    suffix = ".exe" if windows else ""
    source = build / "app" / ("app" + suffix)
    prepare = build / "tools/asset" / ("comet_prepare_project" + suffix)
    # Multi-config generators keep executables in the selected configuration directory.
    if not source.is_file():
        source = source.parent / "Release" / source.name
    if not prepare.is_file():
        prepare = prepare.parent / "Release" / prepare.name
    if not source.is_file() or not prepare.is_file():
        raise RuntimeError("Build app and comet_prepare_project before exporting")
    if destination.resolve().is_relative_to(build):
        raise RuntimeError("Destination must be outside the development build")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".comet-app-", dir=destination.parent) as directory:
        temporary = Path(directory)
        bundle = temporary / destination.name
        bundle.mkdir()
        executable = bundle / ("Comet" + suffix)
        shutil.copy2(source, executable)
        resources = bundle / "resources"
        shutil.copytree(source.parent / "resources", resources)
        prepare_game_resources(resources, project, prepare, scenes)
        stage("正在打包运行依赖…")
        libraries = bundle if windows else bundle / "lib"
        libraries.mkdir(exist_ok=True)
        copied = {}
        for dependency in desktop_dependencies(source, build, cache, temporary):
            key = dependency.name.casefold() if windows else dependency.name
            if key in copied and copied[key].resolve() != dependency.resolve():
                raise RuntimeError(f"Conflicting runtime library names: {dependency.name}")
            copied[key] = dependency
            shutil.copy2(dependency, libraries / dependency.name)
        if not windows:
            run("patchelf", "--set-rpath", "$ORIGIN/lib", str(executable))
            for library in libraries.iterdir():
                run("patchelf", "--set-rpath", "$ORIGIN", str(library))
        if destination.exists() or destination.is_symlink():
            raise RuntimeError(f"Destination appeared during export: {destination}")
        bundle.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path, help="project directory or project.json")
    parser.add_argument("destination", type=Path, help="new .app (macOS) or new directory (Windows/Linux)")
    parser.add_argument("--cmake", default="cmake", help="CMake executable (Editor supplies its configured path)")
    parser.add_argument("--scene", action="append", default=[], help="additional scene relative to assets/")
    args = parser.parse_args()
    try:
        output = export_app(args.project, args.destination, args.scene, args.cmake)
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"App export failed: {error}", file=sys.stderr)
        return 1
    print(f"App exported: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
