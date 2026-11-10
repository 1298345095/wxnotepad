"""Exercise scheme regeneration through its CLI against real Git source fixtures."""
import contextlib
import io
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import scheme_data


class JavaScriptGeneratorTest(unittest.TestCase):
    def test_imports_lexer_lists_attributes_grouped_styles_and_extensions(self):
        with tempfile.TemporaryDirectory(prefix='javascript source ') as temporary:
            source = Path(temporary) / 'original'
            source.mkdir()
            def git(*arguments):
                return subprocess.check_output(['git', '-C', str(source), *arguments], text=True).strip()
            git('init', '--quiet')
            git('config', 'user.name', 'Scheme fixture')
            git('config', 'user.email', 'fixture@example.invalid')
            folder = source / 'src/EditLexers'
            folder.mkdir(parents=True)
            for language, symbol, extensions in [('JavaScript', 'JS', 'js; mjs; jsx'),
                                                   ('TypeScript', 'TS', 'ts; tsx')]:
                (folder / f'stl{language}.cpp').write_text(
                    f'static const char * const Keywords_{symbol}[] = {{\n'
                    '"break const ",\nnullptr,\n"use ",\nnullptr,\nnullptr,\nnullptr,\n"Infinity ",\nnullptr\n'
                    ', // 8 function\n"completion_only( ",\n};\n'
                    f'static const EDITSTYLE Styles_{symbol}[] = {{\n'
                    '{ MULTI_STYLE(SCE_JS_COMMENT, SCE_JS_COMMENTLINE, 0, 0), L"fore:#608060" },\n'
                    '{ SCE_JS_WORD, L"bold; fore:#0000FF; back:#FFFF00" },\n};\n'
                    f'L"{extensions}",\nKeywords_{symbol},\n', encoding='utf-8', newline='\n')
            git('add', '.')
            revision = git('commit-tree', '--no-gpg-sign', git('write-tree'), '-m', 'Original scheme fixture')
            output = Path(temporary) / 'generated'
            output.mkdir()
            arguments = ['update_javascript_scheme.py', '--source', str(source), '--output-dir', str(output)]
            with patch.object(scheme_data, 'REVISION', revision), patch.object(sys, 'argv', arguments):
                runpy.run_path(str(ROOT / 'tools/update_javascript_scheme.py'), run_name='__main__')
            for name in ('javascript', 'typescript'):
                text = (output / f'{name}_scheme_data.h').read_text(encoding='utf-8')
                self.assertIn(revision, text)
                self.assertIn('licenses/Notepad4.txt', text)
                self.assertNotIn('completion_only', text)
                self.assertIn('{SCE_JS_COMMENT, 0x608060, 0xFFFFFF, false}', text)
                self.assertIn('{SCE_JS_COMMENTLINE, 0x608060, 0xFFFFFF, false}', text)
                self.assertIn('{SCE_JS_WORD, 0x0000FF, 0xFFFF00, true}', text)
                self.assertIn(f'{name}Keywords[0] : "", 2U << 8U', text)
                self.assertIn(f'{name}Keywords[5] : "", 0', text)
            self.assertIn('javascriptExtensions[] = {"js", "mjs"}',
                          (output / 'javascript_scheme_data.h').read_text(encoding='utf-8'))
            self.assertIn('typescriptExtensions[] = {"ts"}',
                          (output / 'typescript_scheme_data.h').read_text(encoding='utf-8'))

    def test_source_argument_is_required(self):
        with patch.object(sys, 'argv', ['update_javascript_scheme.py']), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as result:
                runpy.run_path(str(ROOT / 'tools/update_javascript_scheme.py'), run_name='__main__')
        self.assertEqual(result.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
