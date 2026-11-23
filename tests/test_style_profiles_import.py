"""The shared importer retains original style identities and inheritance text."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from style_profiles import style_records


class StyleProfileImport(unittest.TestCase):
    def test_group_keeps_original_key_and_unresolved_relative_attributes(self):
        source = '''static const EDITSTYLE Styles_CPP[] = {
            EDITSTYLE_DEFAULT,
            { MULTI_STYLE(SCE_C_COMMENT, SCE_C_COMMENTLINE, 0, 0), NP2StyleX_Comment,
                L"font:$(Text); size:+1.5; weight:600; fore:#008000" },
        };'''
        records = style_records(source, 'CPP', {'Comment': 'Comment'})
        self.assertEqual(records, [
            ('Default', '', ['STYLE_DEFAULT']),
            ('Comment', 'font:$(Text); size:+1.5; weight:600; fore:#008000',
             ['SCE_C_COMMENT', 'SCE_C_COMMENTLINE']),
        ])


if __name__ == '__main__':
    unittest.main()
