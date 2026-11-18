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
    const char* representation;
};
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
// Original Edit.cpp table and Notepad4.rc menu order/names at 89b8b36e.
// Empty representations retain the original core built-ins (RS/US/LS/PS).
// A normal separator keeps descriptions visible on wxGTK, where tabs mean accelerators.
inline constexpr std::array<UnicodeControlInsertion, 27> UnicodeControlInsertions{{
    {wxTRANSLATE("WJ — &Word joiner"), "\x{e2}\x{81}\x{a0}", "WJ"},
    {wxTRANSLATE("ZWJ — Zero width &joiner"), "\x{e2}\x{80}\x{8d}", "ZWJ"},
    {wxTRANSLATE("ZWNJ — Zero width &non-joiner"), "\x{e2}\x{80}\x{8c}", "ZWNJ"},
    {wxTRANSLATE("LRM — &Left-to-right mark"), "\x{e2}\x{80}\x{8e}", "LRM"},
    {wxTRANSLATE("RLM — &Right-to-left mark"), "\x{e2}\x{80}\x{8f}", "RLM"},
    {wxTRANSLATE("LRE — Start of left-to-right &embedding"), "\x{e2}\x{80}\x{aa}", "LRE"},
    {wxTRANSLATE("RLE — Start of right-to-left e&mbedding"), "\x{e2}\x{80}\x{ab}", "RLE"},
    {wxTRANSLATE("LRO — Start of left-to-right &override"), "\x{e2}\x{80}\x{ad}", "LRO"},
    {wxTRANSLATE("RLO — Start of right-to-left o&verride"), "\x{e2}\x{80}\x{ae}", "RLO"},
    {wxTRANSLATE("LRI — Left-to-&right isolate"), "\x{e2}\x{81}\x{a6}", "LRI"},
    {wxTRANSLATE("RLI — Right-to-&left isolate"), "\x{e2}\x{81}\x{a7}", "RLI"},
    {wxTRANSLATE("FSI — &First strong isolate"), "\x{e2}\x{81}\x{a8}", "FSI"},
    {wxTRANSLATE("PDI — Po&p directional isolate"), "\x{e2}\x{81}\x{a9}", "PDI"},
    {wxTRANSLATE("PDF — &Pop directional formatting"), "\x{e2}\x{80}\x{ac}", "PDF"},
    {wxTRANSLATE("NADS — N&ational digit shapes substitution"), "\x{e2}\x{81}\x{ae}", "NADS"},
    {wxTRANSLATE("NODS — Nominal (European) &digit shapes"), "\x{e2}\x{81}\x{af}", "NODS"},
    {wxTRANSLATE("ASS — Activate &symmetric swapping"), "\x{e2}\x{81}\x{ab}", "ASS"},
    {wxTRANSLATE("ISS — Inhibit s&ymmetric swapping"), "\x{e2}\x{81}\x{aa}", "ISS"},
    {wxTRANSLATE("AAFS — Activate Arabic &form shaping"), "\x{e2}\x{81}\x{ad}", "AAFS"},
    {wxTRANSLATE("IAFS — Inhibit Arabic form s&haping"), "\x{e2}\x{81}\x{ac}", "IAFS"},
    {wxTRANSLATE("ALM — &Arabic letter mark"), "\x{d8}\x{9c}", "ALM"},
    {wxTRANSLATE("RS — Record Separator (&Block separator)"), "\x{1e}", ""},
    {wxTRANSLATE("US — Unit Separator (&Segment separator)"), "\x{1f}", ""},
    {wxTRANSLATE("LS — L&ine Separator"), "\x{e2}\x{80}\x{a8}", ""},
    {wxTRANSLATE("PS — Para&graph Separator"), "\x{e2}\x{80}\x{a9}", ""},
    {wxTRANSLATE("ZWSP — &Zero width space"), "\x{e2}\x{80}\x{8b}", "ZWSP"},
    {wxTRANSLATE("SHY — Soft &hyphen"), "\x{c2}\x{ad}", "SHY"},
}};
