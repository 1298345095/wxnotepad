"""Exercise host-tool acceptance and rejection at the dependency installer boundary."""

import importlib.util
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ci_dependencies", ROOT / ".github/scripts/ci.py")
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)
GETTEXT = os.environ.get("WXNOTEPAD_GETTEXT_BIN")


class DependencyTools(unittest.TestCase):
    def exercise(self, directory, vcpkg, triplet):
        native_run = CI.run
        installations = []

        def installer_boundary(command, log, env=None, check=True):
            if log == "dependencies.log":
                installations.append([str(item) for item in command])
                return 0
            return native_run(command, log, env, check)

        environment_file = directory / "github-env"
        with (
            mock.patch.multiple(CI, VCPKG=vcpkg, TRIPLET=triplet, WORK=directory, REPORTS=directory / "reports"),
            mock.patch.object(CI, "run", side_effect=installer_boundary),
            mock.patch.dict(os.environ, {"GITHUB_ENV": str(environment_file)}),
        ):
            CI.dependencies()
        self.assertEqual(len(installations), 1)
        command = installations[0]
        self.assertEqual(command[command.index("--host-triplet") + 1], triplet)
        self.assertIn(f"--overlay-ports={ROOT / '.github/ports'}", command)
        return environment_file

    @unittest.skipUnless(GETTEXT, "Requires the selected SDK host gettext tools")
    def test_selected_host_tools_publish_and_validate_real_catalogs(self):
        tools = Path(GETTEXT).resolve()
        with tempfile.TemporaryDirectory(prefix="wxnotepad host tools ") as folder:
            directory = Path(folder)
            environment_file = self.exercise(directory, tools.parents[4], tools.parents[2].name)
            self.assertEqual(environment_file.read_text(encoding="utf-8"), f"WXNOTEPAD_GETTEXT_BIN={tools}\n")
            self.assertIn(
                "Extraction, catalog formats and release translation coverage are current.",
                (directory / "reports/localization-check.log").read_text(encoding="utf-8"),
            )
            self.assertIn("OK", (directory / "reports/localization-cli-tests.log").read_text(encoding="utf-8"))

    @unittest.skipUnless(shutil.which("ninja"), "Requires the native build tool")
    def test_wrong_tool_is_rejected_before_publishing_or_catalog_validation(self):
        with tempfile.TemporaryDirectory(prefix="wxnotepad wrong host tool ") as folder:
            directory = Path(folder)
            vcpkg = directory / "vcpkg"
            tools = vcpkg / "installed" / CI.TRIPLET / "tools/gettext/bin"
            suffix = ".exe" if sys.platform == "win32" else ""
            native_check_output = CI.subprocess.check_output

            def incompatible_tool(command, **kwargs):
                self.assertEqual(command, [tools / ("xgettext" + suffix), "--version"])
                # Keep the real tool at its installed path: hosted Windows may
                # supply a launcher that cannot be copied under another name.
                return native_check_output([shutil.which("ninja"), "--version"], **kwargs)

            with (
                mock.patch.object(CI.subprocess, "check_output", side_effect=incompatible_tool) as version,
                self.assertRaisesRegex(SystemExit, r"Expected pinned GNU gettext 0\.23\.2:"),
            ):
                self.exercise(directory, vcpkg, CI.TRIPLET)
            self.assertEqual(version.call_count, 1)
            self.assertFalse((directory / "github-env").exists())
            self.assertFalse((directory / "reports/localization-check.log").exists())


if __name__ == "__main__":
    unittest.main()
