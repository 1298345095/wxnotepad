"""Check search settings with real native controls in independent processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-find-settings-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    config = folder / "Notepad4.ini"
    seed = ('[Settings]\nSaveFindReplace=1\nSelectOption=1\nFindReplaceOption=66\n'
                      'FindReplaceFlag=8198\nUnrelatedSetting=keep\n'
                      '[Recent Find]\n01="stored"\n02="older"\n'
                      '[Recent Replace]\n01="previous"\n')
    config.write_text(seed, encoding="utf-16")
    subprocess.run([str(executable), "change"], env=environment, check=True, timeout=30)
    saved = config.read_text(encoding="utf-16")
    assert '01="next"' in saved and '01="replacement"' in saved, saved
    assert 'discarded draft' not in saved and 'FindReplaceFlag=8196' in saved, saved
    assert 'UnrelatedSetting=keep' in saved and '"older"' in saved, saved
    subprocess.run([str(executable), "restored"], env=environment, check=True, timeout=30)
    config.write_text(seed.replace('[Settings]', '[Settings]\nSaveSettings=0'), encoding="utf-16")
    subprocess.run([str(executable), "exit-only"], env=environment, check=True, timeout=30)
    saved = config.read_text(encoding="utf-16")
    assert '01="next"' in saved and 'FindReplaceFlag=8198' in saved, saved
    subprocess.run([str(executable), "restored-history"], env=environment, check=True, timeout=30)
    config.write_text(seed.replace('SaveFindReplace=1', 'SaveFindReplace=0'), encoding="utf-16")
    subprocess.run([str(executable), "disabled"], env=environment, check=True, timeout=30)
    saved = config.read_text(encoding="utf-16")
    assert '01="stored"' in saved and '01="previous"' in saved and '"next"' not in saved, saved
    assert 'FindReplaceFlag=' not in saved and 'UnrelatedSetting=keep' in saved, saved
    config.write_text(seed, encoding="utf-16")
    subprocess.run([str(executable), "controls"], env=environment, check=True, timeout=30)
    saved = config.read_text(encoding="utf-16")
    assert '01="next\\nstored"' in saved and '01="replacement\\nline"' in saved, saved
    config.write_text(seed, encoding="utf-16")
    subprocess.run([str(executable), "empty"], env=environment, check=True, timeout=30)
    saved = config.read_text(encoding="utf-16")
    assert 'FindReplaceFlag=40964' in saved and '01="previous"' in saved, saved
    subprocess.run([str(executable), "restored-empty"], env=environment, check=True, timeout=30)
print("Search query, replacement, options, draft discard and settings persistence passed")
