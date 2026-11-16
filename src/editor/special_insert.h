#pragma once

#include <array>
#include <wx/translation.h>
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
    {wxTRANSLATE("WJ — &Word joiner"), "\xe2\x81\xa0"},
    {wxTRANSLATE("ZWJ — Zero width &joiner"), "\xe2\x80\x8d"},
    {wxTRANSLATE("ZWNJ — Zero width &non-joiner"), "\xe2\x80\x8c"},
    {wxTRANSLATE("LRM — &Left-to-right mark"), "\xe2\x80\x8e"},
    {wxTRANSLATE("RLM — &Right-to-left mark"), "\xe2\x80\x8f"},
    {wxTRANSLATE("LRE — Start of left-to-right &embedding"), "\xe2\x80\xaa"},
    {wxTRANSLATE("RLE — Start of right-to-left e&mbedding"), "\xe2\x80\xab"},
    {wxTRANSLATE("LRO — Start of left-to-right &override"), "\xe2\x80\xad"},
    {wxTRANSLATE("RLO — Start of right-to-left o&verride"), "\xe2\x80\xae"},
    {wxTRANSLATE("LRI — Left-to-&right isolate"), "\xe2\x81\xa6"},
    {wxTRANSLATE("RLI — Right-to-&left isolate"), "\xe2\x81\xa7"},
    {wxTRANSLATE("FSI — &First strong isolate"), "\xe2\x81\xa8"},
    {wxTRANSLATE("PDI — Po&p directional isolate"), "\xe2\x81\xa9"},
    {wxTRANSLATE("PDF — &Pop directional formatting"), "\xe2\x80\xac"},
    {wxTRANSLATE("NADS — N&ational digit shapes substitution"), "\xe2\x81\xae"},
    {wxTRANSLATE("NODS — Nominal (European) &digit shapes"), "\xe2\x81\xaf"},
    {wxTRANSLATE("ASS — Activate &symmetric swapping"), "\xe2\x81\xab"},
    {wxTRANSLATE("ISS — Inhibit s&ymmetric swapping"), "\xe2\x81\xaa"},
    {wxTRANSLATE("AAFS — Activate Arabic &form shaping"), "\xe2\x81\xad"},
    {wxTRANSLATE("IAFS — Inhibit Arabic form s&haping"), "\xe2\x81\xac"},
    {wxTRANSLATE("ALM — &Arabic letter mark"), "\xd8\x9c"},
    {wxTRANSLATE("RS — Record Separator (&Block separator)"), "\x1e"},
    {wxTRANSLATE("US — Unit Separator (&Segment separator)"), "\x1f"},
    {wxTRANSLATE("LS — L&ine Separator"), "\xe2\x80\xa8"},
    {wxTRANSLATE("PS — Para&graph Separator"), "\xe2\x80\xa9"},
    {wxTRANSLATE("ZWSP — &Zero width space"), "\xe2\x80\x8b"},
    {wxTRANSLATE("SHY — Soft &hyphen"), "\xc2\xad"},
}};
