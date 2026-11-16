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
    {wxTRANSLATE("WJ — &Word joiner"), "\x{e2}\x{81}\x{a0}"},
    {wxTRANSLATE("ZWJ — Zero width &joiner"), "\x{e2}\x{80}\x{8d}"},
    {wxTRANSLATE("ZWNJ — Zero width &non-joiner"), "\x{e2}\x{80}\x{8c}"},
    {wxTRANSLATE("LRM — &Left-to-right mark"), "\x{e2}\x{80}\x{8e}"},
    {wxTRANSLATE("RLM — &Right-to-left mark"), "\x{e2}\x{80}\x{8f}"},
    {wxTRANSLATE("LRE — Start of left-to-right &embedding"), "\x{e2}\x{80}\x{aa}"},
    {wxTRANSLATE("RLE — Start of right-to-left e&mbedding"), "\x{e2}\x{80}\x{ab}"},
    {wxTRANSLATE("LRO — Start of left-to-right &override"), "\x{e2}\x{80}\x{ad}"},
    {wxTRANSLATE("RLO — Start of right-to-left o&verride"), "\x{e2}\x{80}\x{ae}"},
    {wxTRANSLATE("LRI — Left-to-&right isolate"), "\x{e2}\x{81}\x{a6}"},
    {wxTRANSLATE("RLI — Right-to-&left isolate"), "\x{e2}\x{81}\x{a7}"},
    {wxTRANSLATE("FSI — &First strong isolate"), "\x{e2}\x{81}\x{a8}"},
    {wxTRANSLATE("PDI — Po&p directional isolate"), "\x{e2}\x{81}\x{a9}"},
    {wxTRANSLATE("PDF — &Pop directional formatting"), "\x{e2}\x{80}\x{ac}"},
    {wxTRANSLATE("NADS — N&ational digit shapes substitution"), "\x{e2}\x{81}\x{ae}"},
    {wxTRANSLATE("NODS — Nominal (European) &digit shapes"), "\x{e2}\x{81}\x{af}"},
    {wxTRANSLATE("ASS — Activate &symmetric swapping"), "\x{e2}\x{81}\x{ab}"},
    {wxTRANSLATE("ISS — Inhibit s&ymmetric swapping"), "\x{e2}\x{81}\x{aa}"},
    {wxTRANSLATE("AAFS — Activate Arabic &form shaping"), "\x{e2}\x{81}\x{ad}"},
    {wxTRANSLATE("IAFS — Inhibit Arabic form s&haping"), "\x{e2}\x{81}\x{ac}"},
    {wxTRANSLATE("ALM — &Arabic letter mark"), "\x{d8}\x{9c}"},
    {wxTRANSLATE("RS — Record Separator (&Block separator)"), "\x{1e}"},
    {wxTRANSLATE("US — Unit Separator (&Segment separator)"), "\x{1f}"},
    {wxTRANSLATE("LS — L&ine Separator"), "\x{e2}\x{80}\x{a8}"},
    {wxTRANSLATE("PS — Para&graph Separator"), "\x{e2}\x{80}\x{a9}"},
    {wxTRANSLATE("ZWSP — &Zero width space"), "\x{e2}\x{80}\x{8b}"},
    {wxTRANSLATE("SHY — Soft &hyphen"), "\x{c2}\x{ad}"},
}};
