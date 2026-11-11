"""Original completion preference import and save in independent GUI processes."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

source=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='wxnotepad-completion-') as directory:
    folder=Path(directory)
    executable=folder/source.name
    shutil.copy2(source,executable)
    env=dict(os.environ)
    env['PATH']=str(source.parent)+os.pathsep+env['PATH']
    base='[Settings]\n; preserved\nCustom=retained\n'
    custom=('AutoCIgnoreCase=1\nLaTeXInputMethod=1\nAutoCompleteOption=12\nAutoCompleteScope=83\n'
            'AutoCScanWordsTimeout=75\nAutoCVisibleItemCount=24\nAutoCMinWordLength=2\n'
            'AutoCMinNumberLength=4\nAutoCFillUpMask=5\nAutoCFillUpPunctuation=!?:\n')
    for mode,settings,action in [('defaults',base,'keep'),('custom',base+custom,'keep'),
                                ('bounds',base+'AutoCScanWordsTimeout=0\nAutoCVisibleItemCount=-3\nAutoCMinWordLength=0\nAutoCMinNumberLength=-2\n','keep'),
                                ('custom',base+custom+'SaveSettings=0\n','keep'),
                                ('custom',base+custom+'SaveSettings=0\n','explicit')]:
        ini=folder/'Notepad4.ini'
        ini.write_text(settings,encoding='utf-16')
        result=subprocess.run([str(executable),mode,action],capture_output=True,text=True,timeout=30,env=env)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        saved=ini.read_text(encoding='utf-16')
        assert '; preserved' in saved and 'Custom=retained' in saved
        if mode=='defaults':assert 'AutoCIgnoreCase=1' in saved
        if mode=='custom':
            if action=='keep' and 'SaveSettings=0' in settings:assert 'AutoCIgnoreCase=1' in saved
            else:assert 'AutoCIgnoreCase=' not in saved
            for line in custom.splitlines():
                if not line.startswith('AutoCIgnoreCase'):assert line in saved,line
        if mode=='bounds':
            assert 'AutoCScanWordsTimeout=50' in saved and 'AutoCVisibleItemCount=8' in saved
            assert 'AutoCMinWordLength=' not in saved and 'AutoCMinNumberLength=0' in saved
print('PASS: completion defaults, import, bounds, unknown keys and explicit/exit saves')
