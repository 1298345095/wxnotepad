"""Check the shell import's consumed lists and original style attributes."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from update_bash_scheme import generate
from scheme_data import style_initializers


class ShellSchemeData(unittest.TestCase):
    def test_consumed_lists_and_eol_style(self):
        # Original declaration/marker shapes, with distinguishable lexer and
        # completion data so including the completion-only group fails.
        source = b'''static const char * const Keywords_Bash[] = {
"echo if "
, // 1 bash struct
"fi then "
, // 2 variables
"BASH_VERSION "
};
static const EDITSTYLE Styles_Bash[] = {
{ SCE_SH_WORD, NP2StyleX_Keyword, L"bold; fore:#FF8000" },
{ MULTI_STYLE(SCE_SH_HERE_DELIM, SCE_SH_HERE_Q, 0, 0), NP2StyleX_String,
  L"fore:#A46000; back:#FFFFC0; eolfilled" },
};'''
        # Keep each upstream style record on one line, as in pinned stlBash.cpp.
        source = source.replace(b'NP2StyleX_String,\n  L', b'NP2StyleX_String, L')
        output = generate(source)
        self.assertIn('"echo if "', output)
        self.assertIn('"fi then "', output)
        self.assertNotIn('BASH_VERSION', output)
        self.assertIn('{SCE_SH_WORD, 0xFF8000, 0xFFFFFF, true, false, false}', output)
        for style in ('SCE_SH_HERE_DELIM', 'SCE_SH_HERE_Q'):
            self.assertIn('{' + style + ', 0xA46000, 0xFFFFC0, false, false, true}', output)
        self.assertIn('licenses/Notepad4.txt', output)
        self.assertIn('89b8b36e0ee2c23f73be922315f17261ccfbbe1a', output)

    def test_existing_generator_shape_is_preserved(self):
        styles = '{ SCE_JSON_KEYWORD, NP2StyleX_Keyword, L"bold; fore:#FF8000" },'
        self.assertEqual(style_initializers(styles, 'SCE_JSON_'),
                         ['    {SCE_JSON_KEYWORD, 0xFF8000, 0xFFFFFF, true},'])


if __name__ == '__main__':
    unittest.main()
