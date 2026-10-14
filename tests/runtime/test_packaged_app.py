"""运行搬移后的真实 App；子进程不能读取仓库、开发缓存或 SDK 的全局驱动。"""

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    spec = importlib.util.spec_from_file_location("export_app", root / "tools/export_app.py")
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)
    project_id = str(uuid.uuid4())
    user_data = Path.home() / "Library/Application Support/Comet/players" / project_id
    assert not user_data.exists()
    try:
        with tempfile.TemporaryDirectory(prefix="comet-bundle-test-") as directory:
            temporary = Path(directory).resolve()
            # 通过真实命令行注入失败的 CMake，确认不会回退打包 build-release 中的旧程序。
            for fail_at in ("--preset", "--build"):
                cmake = temporary / "failing cmake"
                cmake.write_text(f"#!{sys.executable}\nimport sys\n"
                                 f"if sys.argv[1] == {fail_at!r}:\n"
                                 "    print('COMET_TEST_BUILD_FAILED', flush=True)\n"
                                 "    sys.exit(23)\n")
                cmake.chmod(0o755)
                destination = temporary / "BuildFailure.app"
                failed_build = subprocess.run(
                    [sys.executable, str(root / "tools/export_app.py"), str(root / "demo"),
                     str(destination), "--cmake", str(cmake)],
                    text=True, capture_output=True, timeout=30)
                assert failed_build.returncode != 0
                assert "COMET_TEST_BUILD_FAILED" in failed_build.stdout, failed_build.stdout
                assert "Command failed" in failed_build.stderr, failed_build.stderr
                assert "正在准备所选关卡" not in failed_build.stdout
                assert ("正在构建运行程序" in failed_build.stdout) == (fail_at == "--build")
                assert not destination.exists()
            source = temporary / "source"
            shutil.copytree(root / "demo/assets", source / "assets")
            project = json.loads((root / "demo/project.json").read_text())
            project["id"] = project_id
            (source / "project.json").write_text(json.dumps(project))
            script = source / "assets/scripts/character.lua"
            script.write_text("local controller = (function()\n" + script.read_text() + "\nend)()\n" + """
                local previous = controller.update
                local frames = 0
                local returning = false
                local stage_key = 'packaged_test.stage'
                local first_goal = 'character.level1.goal1'
                local second_goal = 'character.level2.goal1'
                controller.update = function(self, ...)
                    if previous then previous(self, ...) end
                    frames = frames + 1
                    -- 每次进关先推进真实帧，再调用项目自己的切关／继续入口。
                    if frames == 8 then
                        local stage = comet.session_get(stage_key) or 0
                        if stage == 0 then
                            assert(self.parameters.level == 1)
                            local progress = require('scripts.character_progress')
                            for goal = 1, 3 do progress.collect(1, goal) end
                            comet.session_set(stage_key, 1)
                            self:next_level()
                        elseif stage == 1 then
                            assert(self.parameters.level == 2)
                            assert(comet.session_get(first_goal) == true)
                            comet.session_set(second_goal, true)
                            comet.session_set(stage_key, 3)
                            comet.save_checkpoint('export_smoke', {stage_key, first_goal, second_goal})
                            returning = true
                        elseif stage == 2 then
                            assert(self.parameters.level == 1)
                            assert(comet.session_get(second_goal) == true)
                            comet.session_set(second_goal, false)
                            self:continue_progress()
                        elseif stage == 3 then
                            assert(self.parameters.level == 2)
                            assert(comet.session_get(first_goal) == true)
                            assert(comet.session_get(second_goal) == true)
                            assert(comet.session_get('character.score') == 1)
                            comet.log('COMET_PACKAGED_LEVELS_AND_CHECKPOINT_READY')
                        end
                    elseif frames == 12 and returning then
                        -- 保存请求已处理，返回第一关后验证继续会恢复第二关及进度。
                        comet.session_set(stage_key, 2)
                        self:return_to_first()
                    end
                end
                return controller
            """)
            # 用本次 CTest 的已构建程序验证打包，避免测试启动另一轮全量 Release 构建。
            built = exporter.package_app(source, args.build, temporary / "Built.app",
                                        ["scenes/character_second.scene"])
            original_executable = (built / "Contents/MacOS/Comet").read_bytes()
            try:
                exporter.package_app(source, args.build, built, [])
            except RuntimeError as error:
                assert "already exists" in str(error)
            else:
                raise AssertionError("Existing App was overwritten")
            assert (built / "Contents/MacOS/Comet").read_bytes() == original_executable
            failed = temporary / "MissingScene.app"
            try:
                exporter.package_app(source, args.build, failed, ["scenes/missing.scene"])
            except RuntimeError as error:
                assert "Command failed" in str(error)
            else:
                raise AssertionError("Missing scene was silently omitted")
            assert not failed.exists()
            assert not list(temporary.glob(".comet-app-*"))
            bundle = temporary / "搬移目录/Game.app"
            bundle.parent.mkdir()
            shutil.move(built, bundle)
            shutil.rmtree(source)
            resources = bundle / "Contents/Resources"
            (resources / "config.json").write_text(json.dumps({"app-release": {"diagnostics": {
                "log_level": "info", "enable_file_logging": True, "enable_validation": False}}}))
            exporter.run("codesign", "--force", "--deep", "--sign", "-", str(bundle))
            executable = bundle / "Contents/MacOS/Comet"
            # 默认允许系统框架与 Metal；明确拒绝所有开发文件和对产品目录的写入。
            profile = temporary / "isolated.sb"
            denied = [root, args.build.resolve(), Path("/usr/local/lib"),
                      Path("/usr/local/share/vulkan"), Path.home() / "VulkanSDK"]
            profile.write_text("(version 1)\n(allow default)\n"
                               + "(deny file-read* " + " ".join(
                                   f"(subpath {json.dumps(str(path))})" for path in denied) + ")\n"
                               + f"(deny file-write* (subpath {json.dumps(str(bundle), ensure_ascii=False)}))\n")
            log = temporary / "app.log"
            with log.open("w") as output:
                process = subprocess.Popen(["/usr/bin/sandbox-exec", "-f", str(profile), str(executable)],
                                           cwd=temporary, stdout=output, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 60
                    while time.monotonic() < deadline:
                        text = log.read_text(errors="replace")
                        if "COMET_PACKAGED_LEVELS_AND_CHECKPOINT_READY" in text:
                            break
                        if process.poll() is not None:
                            raise AssertionError(f"Packaged App exited: {process.returncode}\n{text}")
                        time.sleep(0.05)
                    else:
                        raise AssertionError("Packaged App did not finish level/checkpoint round trip:\n"
                                             + log.read_text())
                    assert not (resources / "game/.comet").exists()
                    assert list((user_data / "default/logs").glob("*.log"))
                    checkpoint = json.loads((user_data / "default/saves/checkpoint.json").read_text())
                    assert checkpoint["project"] == project_id
                    assert checkpoint["checkpoint"] == "export_smoke"
                    assert "Application failed" not in text
                    print("Relocated App rendered both levels, returned and restored a checkpoint "
                          "outside its read-only bundle")
                finally:
                    # 此项验证启动与运行；关闭交互由用户验收，不注入生产用测试入口。
                    if process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            shutil.rmtree(resources / "ui")
            missing = subprocess.run([str(executable)], cwd=temporary, text=True,
                                     capture_output=True, timeout=30)
            assert missing.returncode != 0
            assert "Packaged" in missing.stderr or "packaged" in missing.stderr, missing.stderr
            assert "ui" in missing.stderr.lower(), missing.stderr
    finally:
        shutil.rmtree(user_data, ignore_errors=True)


if __name__ == "__main__":
    main()
