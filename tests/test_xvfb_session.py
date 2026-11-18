"""Exercise private display authentication through the real parallel CI entry point."""
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONCURRENT_SESSIONS = 40  # The overlapping X11/Wayland startup that reproduced the failure.
NATIVE_TOOLS = ('ctest', 'Xvfb', 'xauth', 'xdpyinfo', 'dbus-run-session', 'xdotool')


@unittest.skipUnless(sys.platform == 'linux' and all(shutil.which(tool) for tool in NATIVE_TOOLS),
                     'Requires the native Linux GUI validation tools')
class PrivateXvfb(unittest.TestCase):
    def test_parallel_sessions_authenticate_and_keep_displays_independent(self):
        with tempfile.TemporaryDirectory(prefix='wxnotepad display sessions ') as temporary:
            directory = Path(temporary)
            probe = directory / 'probe.py'
            probe.write_text(
                'import json,os,subprocess,sys,time\nfrom pathlib import Path\n'
                'root,index,count=sys.argv[1:]\nroot=Path(root)\n'
                'subprocess.run(["xdpyinfo"],check=True,stdout=subprocess.DEVNULL)\n'
                '(root/(index+".json")).write_text(json.dumps(dict(display=os.environ["DISPLAY"],'
                'authority=os.environ["XAUTHORITY"])),encoding="utf-8")\n'
                'deadline=time.monotonic()+15\n'
                'while len(list(root.glob("*.json")))<int(count):\n'
                '    assert time.monotonic()<deadline, "private displays did not start concurrently"\n'
                '    time.sleep(.01)\n', encoding='utf-8')
            results = directory / 'results'
            results.mkdir()
            definitions = []
            for index in range(CONCURRENT_SESSIONS):
                arguments = ' '.join(f'[==[{value}]==]' for value in
                                     (sys.executable, probe, results, index, CONCURRENT_SESSIONS))
                definitions.append(f'add_test(display_{index} {arguments})')
            (directory / 'CTestTestfile.cmake').write_text('\n'.join(definitions), encoding='utf-8')
            command = (
                [sys.executable, '-X', 'utf8', ROOT / '.github/scripts/ci.py', 'test', '--backend', 'x11',
                 '--cmake-dir', directory, '--jobs', str(CONCURRENT_SESSIONS),
                 '--report-dir', directory / 'reports'])
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, encoding='utf-8', start_new_session=True) as process:
                try:
                    output, _ = process.communicate(timeout=60)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    output, _ = process.communicate()
                    self.fail('Private display sessions exceeded their 60-second deadline:\n' + output)
            self.assertEqual(process.returncode, 0, output)
            observations = [json.loads(path.read_text(encoding='utf-8')) for path in results.glob('*.json')]
            self.assertEqual(len(observations), CONCURRENT_SESSIONS)
            self.assertEqual(len({item['display'] for item in observations}), CONCURRENT_SESSIONS)
            self.assertEqual(len({item['authority'] for item in observations}), CONCURRENT_SESSIONS)
            self.assertTrue(all(not Path(item['authority']).exists() for item in observations),
                            'private authentication files are removed after their command exits')

    def test_failed_child_keeps_status_and_removes_private_authority(self):
        result = subprocess.run(
            [sys.executable, ROOT / 'tools/xvfb_session.py', sys.executable, '-c',
             'import os,sys; print(os.environ["XAUTHORITY"]); sys.exit(23)'],
            capture_output=True, text=True, encoding='utf-8', timeout=15)
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertFalse(Path(result.stdout.strip()).exists())


if __name__ == '__main__':
    unittest.main()
