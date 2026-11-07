#include "editor/editor_control.h"
#include "syntax/syntax_catalog.h"
#include "ScintillaTypes.h"

#ifdef __WXMSW__
#include <objbase.h>
#include <cstdio>
#else
#include <glib.h>
#endif

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
void EditorControl::InsertGuid() {
#ifdef __WXMSW__
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK) return;
    char text[40]{};
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
                  guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
#else
    char* text = g_uuid_string_random();
#endif
    Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text));
#ifndef __WXMSW__
    g_free(text);
#endif
}

void EditorControl::InsertUnicodeControlCharacter(UnicodeControlCharacter character, bool defaultEncoding) {
    const char* text = UnicodeControlInsertions[static_cast<size_t>(character)].utf8;
    if (defaultEncoding && text[1] != '\0') return;
    Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text));
}

void EditorControl::InsertEncodingIdentifier(std::string_view name, const SyntaxDefinition& syntax) {
    using Scintilla::Message;
    std::string text(name);
    if (syntax.encodingHeader) {
        const auto position = Send(Message::GetCurrentPos);
        const auto line = Send(Message::LineFromPosition, position);
        if (line < 2 && position == Send(Message::PositionFromLine, line)) text = "#-*- coding: " + text + " -*-";
    }
    Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text.c_str()));
}

void EditorControl::InsertScriptShebangLine(const SyntaxDefinition& syntax) {
    using Scintilla::Message;
    std::string text = "#!/usr/bin/env ";
    if (syntax.scriptInterpreter) {
        text += syntax.scriptInterpreter;
        if (Send(Message::GetCurrentPos) == 0) {
            const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
            text += mode == Scintilla::EndOfLine::Cr ? "\r" : mode == Scintilla::EndOfLine::Lf ? "\n" : "\r\n";
        }
    }
    Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text.c_str()));
}
