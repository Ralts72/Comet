"""桌面导出事务与平台布局；真正的搬移运行由各平台验收。"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("exporter", ROOT / "tools/export_app.py")
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)


class DesktopExportTest(unittest.TestCase):
    def test_native_layout_and_failed_export_never_replace_output(self):
        for platform in ("win32", "linux"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                temporary = Path(directory)
                project = temporary / "中文 project 'quoted' $HOME"
                project.mkdir()
                (project / "project.json").write_text("{}")
                build = temporary / "build"
                executable = build / "app" / ("app.exe" if platform == "win32" else "app")
                executable.parent.mkdir(parents=True)
                executable.write_bytes(b"executable")
                for folder in ("fonts", "ui"):
                    (executable.parent / "resources" / folder).mkdir(parents=True)
                prepare = build / "tools/asset" / ("comet_prepare_project.exe" if platform == "win32" else "comet_prepare_project")
                prepare.parent.mkdir(parents=True)
                prepare.touch()
                library = build / ("engine.dll" if platform == "win32" else "libengine.so")
                library.write_bytes(b"library")
                cache = {"CMAKE_COMMAND": "cmake"}
                destination = temporary / "exported"
                calls = []

                def run(*arguments, **kwargs):
                    calls.append(arguments)
                    if arguments[0] == str(prepare):
                        game = Path(arguments[3])
                        game.mkdir()
                        (game / "project.json").write_text(json.dumps({"name": "Game"}))
                    elif arguments[0] == "cmake":
                        Path(arguments[2]).with_name("dependencies.txt").write_text(str(library))
                    return ""

                with patch.object(EXPORTER.sys, "platform", platform), patch.object(EXPORTER, "run", run), patch.object(EXPORTER.shutil, "which", return_value="patchelf"):
                    EXPORTER.package_desktop(project, build, destination, ["scenes/second.scene"], cache)
                    output = destination / ("Comet.exe" if platform == "win32" else "Comet")
                    self.assertEqual(output.read_bytes(), b"executable")
                    self.assertTrue((destination / "resources/game/project.json").is_file())
                    self.assertTrue((destination / ("" if platform == "win32" else "lib") / library.name).is_file())
                    self.assertIn("scenes/second.scene", calls[0])
                    self.assertFalse(list(temporary.glob(".comet-app-*")))
                    with self.assertRaisesRegex(RuntimeError, "already exists"):
                        EXPORTER.validate_request(project, destination)
                    self.assertEqual(output.read_bytes(), b"executable")
                    with patch.object(EXPORTER, "desktop_dependencies", side_effect=RuntimeError("missing library")):
                        failed = temporary / "failed"
                        with self.assertRaisesRegex(RuntimeError, "missing library"):
                            EXPORTER.package_desktop(project, build, failed, [], cache)
                        self.assertFalse(failed.exists())
                        self.assertFalse(list(temporary.glob(".comet-app-*")))


if __name__ == "__main__":
    unittest.main()
