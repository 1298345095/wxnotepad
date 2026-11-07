#pragma once

#include <array>
#include <cstdint>

enum class UnicodeControlCharacter : std::uint8_t {
    Wj,
    Zwj,
    Zwnj,
    Lrm,
    Rlm,
    Lre,
    Rle,
    Lro,
    Rlo,
    Lri,
    Rli,
    Fsi,
    Pdi,
    Pdf,
    Nads,
    Nods,
    Ass,
    Iss,
    Aafs,
    Iafs,
    Alm,
    Rs,
    Us,
    Ls,
    Ps,
    Zwsp,
    Shy
};
struct UnicodeControlInsertion {
    const char* label;
    const char* utf8;
};
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
// Original Edit.cpp table and Notepad4.rc menu order/names at 89b8b36e.
// A normal separator keeps descriptions visible on wxGTK, where tabs mean accelerators.
inline constexpr std::array<UnicodeControlInsertion, 27> UnicodeControlInsertions{{
    {"WJ — &Word joiner", "\xe2\x81\xa0"},
    {"ZWJ — Zero width &joiner", "\xe2\x80\x8d"},
    {"ZWNJ — Zero width &non-joiner", "\xe2\x80\x8c"},
    {"LRM — &Left-to-right mark", "\xe2\x80\x8e"},
    {"RLM — &Right-to-left mark", "\xe2\x80\x8f"},
    {"LRE — Start of left-to-right &embedding", "\xe2\x80\xaa"},
    {"RLE — Start of right-to-left e&mbedding", "\xe2\x80\xab"},
    {"LRO — Start of left-to-right &override", "\xe2\x80\xad"},
    {"RLO — Start of right-to-left o&verride", "\xe2\x80\xae"},
    {"LRI — Left-to-&right isolate", "\xe2\x81\xa6"},
    {"RLI — Right-to-&left isolate", "\xe2\x81\xa7"},
    {"FSI — &First strong isolate", "\xe2\x81\xa8"},
    {"PDI — Po&p directional isolate", "\xe2\x81\xa9"},
    {"PDF — &Pop directional formatting", "\xe2\x80\xac"},
    {"NADS — N&ational digit shapes substitution", "\xe2\x81\xae"},
    {"NODS — Nominal (European) &digit shapes", "\xe2\x81\xaf"},
    {"ASS — Activate &symmetric swapping", "\xe2\x81\xab"},
    {"ISS — Inhibit s&ymmetric swapping", "\xe2\x81\xaa"},
    {"AAFS — Activate Arabic &form shaping", "\xe2\x81\xad"},
    {"IAFS — Inhibit Arabic form s&haping", "\xe2\x81\xac"},
    {"ALM — &Arabic letter mark", "\xd8\x9c"},
    {"RS — Record Separator (&Block separator)", "\x1e"},
    {"US — Unit Separator (&Segment separator)", "\x1f"},
    {"LS — L&ine Separator", "\xe2\x80\xa8"},
    {"PS — Para&graph Separator", "\xe2\x80\xa9"},
    {"ZWSP — &Zero width space", "\xe2\x80\x8b"},
    {"SHY — Soft &hyphen", "\xc2\xad"},
}};
