"""Exercise each original insertion bit through owned native dialogs and reopened settings."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
source=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='wxnotepad-auto-insertion-') as name:
    folder=Path(name); executable=folder/source.name
    shutil.copy2(source,executable)
    env=dict(os.environ,PATH=str(source.parent)+os.pathsep+os.environ['PATH'])
    config=folder/'Notepad4.ini'
    config.write_text('[Settings]\nAutoInsertMask=33279\nUnrelated=keep\n',encoding='utf-16')
    original_config = config.read_bytes()
    for mode in ('--disable', '--reopen'):
        for bit in (0, 3, -1, 512, 2147483647):
            result = subprocess.run([executable, mode, str(bit)], cwd=folder, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            output = result.stdout.decode('utf-8', errors='replace')
            print(output, end='', flush=True)
            if result.returncode != 1 or 'FAIL: insertion setting bit must be a power of two from 1 through 256' not in output:
                raise AssertionError(f'Invalid insertion bit {bit} must fail normally: {result.returncode}: {output}')
            if config.read_bytes() != original_config:
                raise AssertionError('Rejecting an invalid insertion bit must preserve the settings file')
    print('PASS: invalid insertion bits fail before shifts, narrowing or settings changes')
    for bit in (1,2,4,8,16,32,64,128,256):
        config.write_text('[Settings]\nAutoInsertMask=33279\nUnrelated=keep\n',encoding='utf-16')
        for mode in ('--disable','--reopen'):
            result=subprocess.run([executable,mode,str(bit)],cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            print(result.stdout.decode('utf-8',errors='replace'),end='',flush=True)
            result.check_returncode()
        saved=config.read_text(encoding='utf-16')
        assert f'AutoInsertMask={32768 | (511 ^ bit)}' in saved.splitlines(),saved
        assert 'AutoIndent=0' in saved.splitlines() and 'Unrelated=keep' in saved,saved
print('PASS: all nine independent original bits survive save/reopen with unrelated settings retained')

with tempfile.TemporaryDirectory(prefix='wxnotepad-auto-close-tags-') as name:
    folder=Path(name); executable=folder/source.name
    shutil.copy2(source,executable)
    env=dict(os.environ,PATH=str(source.parent)+os.pathsep+os.environ['PATH'])
    config=folder/'Notepad4.ini'
    config.write_text('[Settings]\nAutoCompleteOption=32775\nAutoInsertMask=33279\nUnrelated=keep\n',encoding='utf-16')
    for mode in ('--tags','--tags-reopen'):
        result=subprocess.run([executable,mode,'0'],cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        print(result.stdout.decode('utf-8',errors='replace'),end='',flush=True)
        result.check_returncode()
    saved=config.read_text(encoding='utf-16')
    assert 'AutoCompleteOption=32774' in saved.splitlines(),saved
    assert 'AutoInsertMask=33279' in saved.splitlines() and 'Unrelated=keep' in saved,saved
print('PASS: tag closure uses the original shared completion key and preserves unrelated settings')

with tempfile.TemporaryDirectory(prefix='wxnotepad-word-preferences-') as name:
    folder=Path(name); executable=folder/source.name
    shutil.copy2(source,executable)
    env=dict(os.environ,PATH=str(source.parent)+os.pathsep+os.environ['PATH'])
    config=folder/'Notepad4.ini'
    config.write_text(chr(10).join(('[Settings]', 'Unrelated=keep', '')),encoding='utf-16')
    for mode in ('--words','--words-reopen'):
        result=subprocess.run([executable,mode,'0'],cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        print(result.stdout.decode('utf-8',errors='replace'),end='',flush=True)
        result.check_returncode()
    saved=config.read_text(encoding='utf-16')
    for line in ('AutoCompleteOption=24', 'AutoCompleteScope=136', 'AutoCScanWordsTimeout=75', 'AutoCVisibleItemCount=8',
                 'AutoCMinWordLength=2', 'AutoCMinNumberLength=0', 'AutoCFillUpMask=0', 'AutoCFillUpPunctuation=!?:!'):
        assert line in saved.splitlines(),saved
    assert 'Unrelated=keep' in saved,saved
print('PASS: all original word completion preferences survive independent save/reopen')
