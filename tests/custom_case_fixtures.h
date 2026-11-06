// Literal Windows reference: Notepad4 89b8b36 EditMapTextCase, zh-SG, Windows 11 build 26200.
#pragma once
struct CustomCaseFixture {
    const char* before;
    const char* command;
    const char* after;
};
inline constexpr CustomCaseFixture CustomCaseFixtures[] = {
    {"aB Straße 𐐀", "Invert Case", "Ab sTRAßE 𐐀"},
    {"aB Straße 𐐀", "Title Case", "Ab Straße 𐐀"},
    {"aB Straße 𐐀", "Sentence Case", "Ab straße 𐐨"},
    {"123ABC DON'T O’NEILL foo_bar FOO-BAR", "Invert Case", "123abc don't o’neill FOO_BAR foo-bar"},
    {"123ABC DON'T O’NEILL foo_bar FOO-BAR", "Title Case", "123Abc Don't O’neill Foo_Bar Foo-Bar"},
    {"123ABC DON'T O’NEILL foo_bar FOO-BAR", "Sentence Case", "123abc don't o’neill foo_bar foo-bar"},
    {"a.b:a;b!b?b/b@b#b", "Invert Case", "A.B:A;B!B?B/B@B#B"},
    {"a.b:a;b!b?b/b@b#b", "Title Case", "A.B:A;B!B?B/B@B#B"},
    {"a.b:a;b!b?b/b@b#b", "Sentence Case", "A.B:a;B!B?B/b@b#b"},
    {"a。B a：B a！B\r\nTHIRD\u0085FOURTH\u2028FIFTH\u2029SIXTH", "Invert Case",
     "A。b A：b A！b\r\nthird\u0085fourth\u2028fifth\u2029sixth"},
    {"a。B a：B a！B\r\nTHIRD\u0085FOURTH\u2028FIFTH\u2029SIXTH", "Title Case",
     "A。B A：B A！B\r\nThird\u0085Fourth\u2028Fifth\u2029Sixth"},
    {"a。B a：B a！B\r\nTHIRD\u0085FOURTH\u2028FIFTH\u2029SIXTH", "Sentence Case",
     "A。b a：b a！B\r\nThird\u0085Fourth\u2028Fifth\u2029Sixth"},
    {"É İ K ΣΩ 𐐀 Á中😀", "Invert Case", "é İ K σω 𐐀 á中😀"},
    {"É İ K ΣΩ 𐐀 Á中😀", "Title Case", "É I K Σω 𐐀 Á中😀"},
    {"É İ K ΣΩ 𐐀 Á中😀", "Sentence Case", "É i k σω 𐐨 á中😀"},
    {"Straße ß ﬃ ΐ ǰ ǳ", "Invert Case", "sTRAßE ß ﬃ ΐ ǰ Ǳ"},
    {"Straße ß ﬃ ΐ ǰ ǳ", "Title Case", "Straße ß ﬃ ΐ ǰ ǲ"},
    {"Straße ß ﬃ ΐ ǰ ǳ", "Sentence Case", "Straße ß ﬃ ΐ ǰ ǳ"},
    {"中😀\r\n", "Invert Case", "中😀\r\n"},
    {"中😀\r\n", "Title Case", "中😀\r\n"},
    {"中😀\r\n", "Sentence Case", "中😀\r\n"},
    {"ABC", "Invert Case", "abc"},
    {"ABC", "Title Case", "Abc"},
    {"ABC", "Sentence Case", "Abc"},
    {"abc", "Invert Case", "ABC"},
    {"abc", "Title Case", "Abc"},
    {"abc", "Sentence Case", "Abc"},
    {"'HELLO' `WORLD` ´WORD´ ‘MIXED’", "Invert Case", "'hello' `world` ´word´ ‘mixed’"},
    {"'HELLO' `WORLD` ´WORD´ ‘MIXED’", "Title Case", "'Hello' `World` ´Word´ ‘Mixed’"},
    {"'HELLO' `WORLD` ´WORD´ ‘MIXED’", "Sentence Case", "'Hello' `world` ´word´ ‘mixed’"},
    {"123ABC 4DEF. 5GHI; 𐐀 ABC", "Invert Case", "123abc 4def. 5ghi; 𐐀 abc"},
    {"123ABC 4DEF. 5GHI; 𐐀 ABC", "Title Case", "123Abc 4Def. 5Ghi; 𐐀 Abc"},
    {"123ABC 4DEF. 5GHI; 𐐀 ABC", "Sentence Case", "123abc 4def. 5ghi; 𐐨 Abc"},
    {"a中B a😀B áB", "Invert Case", "A中b A😀b Áb"},
    {"a中B a😀B áB", "Title Case", "A中b A😀b Áb"},
    {"a中B a😀B áB", "Sentence Case", "A中b a😀b áb"},
};
