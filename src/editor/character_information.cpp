// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "editor/character_information.h"
#include "editor/editor_control.h"
#include "editor/platform/native_encoding.h"
#include <string>
#include <format>
#include <iterator>
#include <wx/dynlib.h>
#include <wx/log.h>
#ifdef __WXMSW__
#include <windows.h>
#elif __has_include(<unicode/uvernum.h>)
// Optional build-host system ICU version; the application never links or bundles
// ICU. Without the system header or runtime service, omit the optional fields.
#include <unicode/uvernum.h>
#endif

namespace {
class CharacterMetadata final {
public:
    CharacterMetadata() {
        wxLogNull quiet;
        wxString suffix;
#ifdef __WXMSW__
        wchar_t directory[MAX_PATH]{};
        if (!GetSystemDirectoryW(directory, MAX_PATH)) return;
        if (!library_.Load(wxString(directory) + "/icu.dll", wxDL_VERBATIM | wxDL_NOW)) return;
#elif defined(U_ICU_VERSION_MAJOR_NUM)
        suffix = wxString::Format("_%d", U_ICU_VERSION_MAJOR_NUM);
        if (!library_.Load(wxString::Format("libicuuc.so.%d", U_ICU_VERSION_MAJOR_NUM), wxDL_VERBATIM | wxDL_NOW))
            return;
#else
        return;
#endif
        name_ = reinterpret_cast<CharName>(library_.GetSymbol("u_charName" + suffix));
        property_ = reinterpret_cast<IntProperty>(library_.GetSymbol("u_getIntPropertyValue" + suffix));
        propertyName_ = reinterpret_cast<PropertyName>(library_.GetSymbol("u_getPropertyValueName" + suffix));
    }
    void Append(std::string& output, uint32_t character) const {
        // Original U_EXTENDED_CHAR_NAME and UCHAR_GENERAL_CATEGORY. Load all
        // three exports before adding either optional field, as Notepad4 does.
        if (!name_ || !property_ || !propertyName_) return;
        constexpr int extendedCharacterName = 2;
        constexpr int generalCategory = 0x1005;
        constexpr int shortPropertyName = 0;
        constexpr int longPropertyName = 1;
        char name[256]{};
        int error = 0;
        name_(character, extendedCharacterName, name, static_cast<int>(std::size(name)), &error);
        const auto category = property_(character, generalCategory);
        std::format_to(std::back_inserter(output), "\nName: {}\nCategory: {} {}", name,
                       propertyName_(generalCategory, category, shortPropertyName),
                       propertyName_(generalCategory, category, longPropertyName));
    }

private:
    using CharName = int32_t (*)(uint32_t, int, char*, int32_t, int*);
    using IntProperty = int32_t (*)(uint32_t, int);
    using PropertyName = const char* (*)(int, int32_t, int);
    wxDynamicLibrary library_;
    CharName name_ = nullptr;
    IntProperty property_ = nullptr;
    PropertyName propertyName_ = nullptr;
};
} // namespace

bool ShowHexCode(EditorControl& editor, bool nativeDefaultEncoding) {
    using enum Scintilla::Message;
    // Notepad4 89b8b36 EditShowHex: the binary selection getter preserves NUL,
    // and multiple selections retain the original inter-selection EOL bytes.
    const auto length = editor.Send(GetSelText);
    if (!length) return true;
    if (editor.Send(SelectionIsRectangle)) return false;
    std::string bytes(static_cast<size_t>(length) + 1, '\0');
    editor.Send(GetSelText, 1, reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(static_cast<size_t>(length));
    // The migrated core keeps UTF-8. Only CPI_DEFAULT used native ACP bytes in
    // the original; explicit legacy codecs and UTF-16 used UTF-8 editor bytes.
    if (nativeDefaultEncoding) bytes = editor.NativeSelectedBytes();
    std::string output("[");
    for (const unsigned char byte : bytes) {
        output += "0123456789ABCDEF"[byte >> 4];
        output += "0123456789ABCDEF"[byte & 15];
        output += ' ';
    }
    output.back() = ']';
    const auto end = editor.Send(GetSelectionEnd);
    editor.Send(InsertText, end, reinterpret_cast<intptr_t>(output.c_str()));
    editor.Send(SetSel, end, end + static_cast<intptr_t>(output.size()));
    return true;
}

std::string CharacterInformation(EditorControl& editor, bool nativeDefaultEncoding) {
    using enum Scintilla::Message;
    // Adapted from Notepad4 89b8b36 EditShowCharacterInfo. GetCharacterAndWidth
    // supplies the scalar at the *selection start*, including zero at EOF.
    const auto position = editor.Send(GetSelectionStart);
    const auto character = nativeDefaultEncoding ? NativeDefaultEncodingCharacter(editor.NativeBytesAfter(position))
                                                 : static_cast<uint32_t>(editor.Send(GetCharacterAndWidth, position));
    std::string output = character < 0x10000 ? std::format("Unicode: U+{:04X}\nUTF-16:", character)
                                             : std::format("Unicode: U+{:06X}\nUTF-16:", character);
    if (character >= 0x10000) {
        std::format_to(std::back_inserter(output), " {:04X} {:04X}", 0xd800 + ((character - 0x10000) >> 10),
                       0xdc00 + (character & 0x3ff));
    } else {
        std::format_to(std::back_inserter(output), " {:04X}", character);
    }
    output += "\nUTF-8:";
    const auto bytes =
        character <= 0x7f ? std::string(1, static_cast<char>(character)) : wxString(wxUniChar(character)).utf8_string();
    for (const unsigned char byte : bytes)
        std::format_to(std::back_inserter(output), " {:02X}", byte);
    std::format_to(std::back_inserter(output), "\nHTML: &#{};", character);
    static const CharacterMetadata metadata;
    metadata.Append(output, character);
    return output;
}
