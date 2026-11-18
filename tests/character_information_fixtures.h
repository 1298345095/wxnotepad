// Captured from original Notepad4 89b8b36; original-03/samples.json in #214 evidence.
#pragma once
#include <array>
#include <string_view>
#include <cstdint>

struct ShowHexFixture {
    const char* name;
    std::string_view before;
    intptr_t anchor, caret;
    std::string_view after;
    intptr_t resultAnchor, resultCaret;
};
inline constexpr auto ShowHexFixtures = std::to_array<ShowHexFixture>({
    {"ascii", std::string_view{"AB", 2}, 0, 2, std::string_view{"AB[41 42]", 9}, 2, 9},
    {"ascii", std::string_view{"AB", 2}, 2, 0, std::string_view{"AB[41 42]", 9}, 2, 9},
    {"cjk", std::string_view{"中", 3}, 0, 3, std::string_view{"中[E4 B8 AD]", 13}, 3, 13},
    {"cjk", std::string_view{"中", 3}, 3, 0, std::string_view{"中[E4 B8 AD]", 13}, 3, 13},
    {"supplementary", std::string_view{"😀", 4}, 0, 4, std::string_view{"😀[F0 9F 98 80]", 17}, 4, 17},
    {"supplementary", std::string_view{"😀", 4}, 4, 0, std::string_view{"😀[F0 9F 98 80]", 17}, 4, 17},
    {"controls-nul", std::string_view{"A\x{00}\t\r\nB", 6}, 0, 6,
     std::string_view{"A\x{00}\t\r\nB[41 00 09 0D 0A 42]", 25}, 6, 25},
    {"controls-nul", std::string_view{"A\x{00}\t\r\nB", 6}, 6, 0,
     std::string_view{"A\x{00}\t\r\nB[41 00 09 0D 0A 42]", 25}, 6, 25},
});

struct CharacterInfoFixture {
    const char* name;
    std::string_view text;
    intptr_t anchor, caret;
    std::string_view payload;
};
inline constexpr auto CharacterInfoFixtures = std::to_array<CharacterInfoFixture>({
    {"ascii", std::string_view{"A", 1}, 0, 1,
     std::string_view{"Unicode: U+0041\nUTF-16: 0041\nUTF-8: 41\nHTML: &#65;\nName: LATIN CAPITAL LETTER A\nCategory: "
                      "Lu Uppercase_Letter",
                      109}},
    {"cjk", std::string_view{"中", 3}, 3, 0,
     std::string_view{"Unicode: U+4E2D\nUTF-16: 4E2D\nUTF-8: E4 B8 AD\nHTML: &#20013;\nName: CJK UNIFIED "
                      "IDEOGRAPH-4E2D\nCategory: Lo Other_Letter",
                      118}},
    {"supplementary", std::string_view{"😀", 4}, 0, 4,
     std::string_view{"Unicode: U+01F600\nUTF-16: D83D DE00\nUTF-8: F0 9F 98 80\nHTML: &#128512;\nName: GRINNING "
                      "FACE\nCategory: So Other_Symbol",
                      116}},
    {"tab", std::string_view{"\t", 1}, 0, 0,
     std::string_view{
         "Unicode: U+0009\nUTF-16: 0009\nUTF-8: 09\nHTML: &#9;\nName: <control-0009>\nCategory: Cc Control", 91}},
    {"nul", std::string_view{"\x{00}", 1}, 0, 0,
     std::string_view{
         "Unicode: U+0000\nUTF-16: 0000\nUTF-8: 00\nHTML: &#0;\nName: <control-0000>\nCategory: Cc Control", 91}},
    {"end", std::string_view{"A", 1}, 1, 1,
     std::string_view{
         "Unicode: U+0000\nUTF-16: 0000\nUTF-8: 00\nHTML: &#0;\nName: <control-0000>\nCategory: Cc Control", 91}},
});
