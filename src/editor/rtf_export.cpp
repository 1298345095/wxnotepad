#include "editor/editor_control.h"
#include "editor/platform/clipboard.h"
#include "editor/platform/native_encoding.h"
#include "Scintilla.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <wx/strconv.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
using enum Scintilla::Message;
constexpr size_t StyleCount = static_cast<size_t>(Scintilla::StylesCommon::Max) + 1;
constexpr unsigned DefaultStyle = static_cast<unsigned>(Scintilla::StylesCommon::Default);
constexpr unsigned DefaultBackground = 2;
constexpr size_t FontFaceCapacity = 32;
constexpr auto Paragraph = R"(\pard\box\cbpat)";
constexpr auto ParagraphEnd = "\\par\n";

struct StyledRange {
    std::string packed;
    size_t length;
    std::vector<size_t> lineStarts;
    unsigned codePage;
};

StyledRange ReadRange(EditorControl& editor, Sci_Position start, Sci_Position end) {
    std::string packed(2 * static_cast<size_t>(end - start) + 2, '\0');
    const Sci_TextRangeFull range{{start, end}, packed.data()};
    const auto length = static_cast<size_t>(editor.Send(GetStyledTextFull, 0, reinterpret_cast<intptr_t>(&range)));
    std::vector<size_t> lineStarts{0};
    for (auto line = editor.Send(LineFromPosition, start) + 1;; ++line) {
        const auto position = editor.Send(PositionFromLine, line);
        if (position >= end || position <= start) break;
        lineStarts.push_back(static_cast<size_t>(position - start));
    }
    return {std::move(packed), length, std::move(lineStarts), static_cast<unsigned>(editor.Send(GetCodePage))};
}

#ifdef __WXMSW__
void ConvertAnsi(StyledRange& range, Sci_Position start, unsigned codePage, const NativeByteRanges& rawBytes) {
    const std::string_view styles(range.packed.data(), range.length);
    const std::string_view text(range.packed.data() + range.length + 1, range.length);
    std::string converted;
    std::string convertedStyles;
    size_t offset = 0;
    auto raw = rawBytes.begin();
    const auto append = [&](size_t boundary) {
        while (offset < boundary) {
            auto end = boundary;
            const auto position = start + static_cast<Sci_Position>(offset);
            while (raw != rawBytes.end() && raw->end <= position)
                ++raw;
            const bool inRaw = raw != rawBytes.end() && raw->start <= position;
            if (raw != rawBytes.end())
                end = std::min(end, static_cast<size_t>((inRaw ? raw->end : raw->start) - start));
            const auto next = styles.substr(offset, end - offset).find_first_not_of(styles[offset], 1);
            if (next != std::string_view::npos) end = offset + next;
            const auto bytes = text.substr(offset, end - offset);
            const auto before = converted.size();
            if (inRaw)
                converted += bytes;
            else
                converted += NativeDefaultEncodingBytes(bytes);
            convertedStyles.append(converted.size() - before, styles[offset]);
            offset = end;
        }
    };
    for (auto& boundary : range.lineStarts) {
        append(boundary);
        boundary = converted.size();
    }
    append(range.length);
    range.length = converted.size();
    range.packed = std::move(convertedStyles);
    range.packed += '\0';
    range.packed += converted;
    range.packed += '\0';
    range.codePage = codePage;
}
#endif

unsigned RtfAnsiCodePage() {
#ifdef __WXMSW__
    UINT page = ::GetACP();
    if (page == CP_UTF8) {
        constexpr LCTYPE legacyAnsi = 0x00000666;
        ::GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, legacyAnsi | LOCALE_RETURN_NUMBER,
                          reinterpret_cast<LPWSTR>(&page), sizeof(page) / sizeof(WCHAR));
    }
    return page;
#else
    constexpr unsigned westernAnsiCodePage = 1252;
    return westernAnsiCodePage;
#endif
}

std::string EncodeFont(const std::string& font, unsigned codePage) {
    const auto face = wxString::FromUTF8(font);
#ifdef __WXMSW__
    const auto wide = face.ToStdWstring();
    std::array<char, FontFaceCapacity> result{};
    if (::WideCharToMultiByte(codePage, 0, wide.c_str(), -1, result.data(), static_cast<int>(result.size()), nullptr,
                              nullptr))
        return result.data();
    return {};
#else
    const wxCSConv converter(wxString::Format("CP%u", codePage));
    std::string result;
    for (const auto character : face) {
        const wxString glyph(character);
        const auto bytes = glyph.mb_str(converter);
        if (bytes)
            result.append(bytes.data(), bytes.length());
        else
            result += '?';
    }
    return result.size() < FontFaceCapacity ? result : std::string();
#endif
}

struct Style {
    std::string font;
    unsigned foreground;
    unsigned background;
    int size;
    int weight;
    Scintilla::CharacterSet charset;
    bool italic;
    bool underline;
    bool strike;
    bool eolFilled;

    bool operator==(const Style&) const = default;
};

Style ReadStyle(EditorControl& editor, unsigned id) {
    const auto fontLength = static_cast<size_t>(editor.Send(StyleGetFont, id));
    std::string font(fontLength + 1, '\0');
    editor.Send(StyleGetFont, id, reinterpret_cast<intptr_t>(font.data()));
    font.resize(fontLength);
    return {std::move(font),
            static_cast<unsigned>(editor.Send(StyleGetFore, id)),
            static_cast<unsigned>(editor.Send(StyleGetBack, id)),
            static_cast<int>(editor.Send(StyleGetSizeFractional, id)),
            static_cast<int>(editor.Send(StyleGetWeight, id)),
            static_cast<Scintilla::CharacterSet>(editor.Send(StyleGetCharacterSet, id)),
            editor.Send(StyleGetItalic, id) != 0,
            editor.Send(StyleGetUnderline, id) != 0,
            editor.Send(StyleGetStrike, id) != 0,
            editor.Send(StyleGetEOLFilled, id) != 0};
}

using StyleMap = std::array<uint8_t, StyleCount>;

std::vector<Style> ReadUsedStyles(EditorControl& editor, std::string_view buffer, StyleMap& indices) {
    std::array<bool, StyleCount> used{};
    for (const unsigned char style : buffer)
        used[style] = true;
    std::vector<Style> styles{ReadStyle(editor, DefaultStyle)};
    used[DefaultStyle] = false;
    for (unsigned style = 0; style < used.size(); ++style) {
        if (!used[style]) continue;
        auto value = ReadStyle(editor, style);
        const auto existing = std::ranges::find(styles, value);
        indices[style] = static_cast<uint8_t>(existing - styles.begin());
        if (existing == styles.end()) styles.push_back(std::move(value));
    }
    return styles;
}

struct RtfStyle {
    std::array<std::string, 7> controls;
    unsigned background;
    bool eolFilled;
};

unsigned ColorIndex(std::vector<unsigned>& colors, unsigned value, size_t first) {
    const auto found = std::find(colors.begin() + static_cast<std::ptrdiff_t>(first), colors.end(), value);
    const auto index = static_cast<unsigned>(found - colors.begin()) + 1;
    if (found == colors.end()) colors.push_back(value);
    return index;
}
std::vector<RtfStyle> AppendTables(std::string& output, const std::vector<Style>& styles, unsigned codePage) {
    std::vector<wxString> fonts;
    std::vector<unsigned> colors;
    std::vector<RtfStyle> records;
    output += R"({\rtf1\ansi\ansicpg)" + std::to_string(codePage) + R"(\deff0\deftab420{\fonttbl)";
    for (const auto& style : styles) {
        const auto font = wxString::FromUTF8(style.font);
        const auto existing =
            std::ranges::find_if(fonts, [&](const wxString& other) { return other.CmpNoCase(font) == 0; });
        const auto index = std::to_string(existing - fonts.begin());
        if (existing == fonts.end()) {
            fonts.push_back(font);
            output += R"({\f)" + index + R"(\fnil)";
            if (style.charset != Scintilla::CharacterSet::Ansi && style.charset != Scintilla::CharacterSet::Default)
                output += R"(\fcharset)" + std::to_string(static_cast<int>(style.charset));
            output += ' ' + EncodeFont(style.font, codePage) + ";}";
        }
        const auto foreground = ColorIndex(colors, style.foreground, 0);
        const auto background = ColorIndex(colors, style.background, 1);
        const auto halfPoints = style.size / (Scintilla::FontSizeMultiplier / 2);
        records.push_back(
            {{R"(\f)" + index, R"(\fs)" + std::to_string(halfPoints), R"(\cf)" + std::to_string(foreground),
              style.weight >= static_cast<int>(Scintilla::FontWeight::SemiBold) ? R"(\b)" : R"(\b0)",
              style.italic ? R"(\i)" : R"(\i0)", style.underline ? R"(\ul)" : R"(\ulnone)",
              style.strike ? R"(\strike)" : R"(\strike0)"},
             background,
             style.eolFilled});
    }
    output += R"(}{\colortbl;)";
    for (const auto color : colors) {
        constexpr unsigned componentMask = 0xff;
        output += R"(\red)" + std::to_string(color & componentMask) + R"(\green)" +
                  std::to_string((color >> 8) & componentMask) + R"(\blue)" +
                  std::to_string((color >> 16) & componentMask) + ';';
    }
    output += "}\n";
    return records;
}

void AppendStyle(std::string& output, const RtfStyle* previous, const RtfStyle& current) {
    const auto before = output.size();
    for (size_t index = 0; index < current.controls.size(); ++index) {
        if (!previous || current.controls[index] != previous->controls[index]) output += current.controls[index];
    }
    if (output.size() != before) output += ' ';
}

void AppendUnicode(std::string& output, unsigned scalar) {
    const auto appendUnit = [&output](unsigned unit) {
        output += R"(\u)" + std::to_string(static_cast<int16_t>(unit)) + '?';
    };
    constexpr unsigned supplementary = 0x10000;
    if (scalar < supplementary) {
        appendUnit(scalar);
    } else {
        appendUnit(0xd800 + ((scalar - supplementary) >> 10));
        appendUnit(0xdc00 + (scalar & 0x3ff));
    }
}

class ParagraphStyles {
public:
    explicit ParagraphStyles(const RtfStyle* initial)
        : background_(initial && initial->eolFilled ? initial->background : DefaultBackground),
          eolFilled_(initial && initial->eolFilled) {}

    void Start(std::string& output) const { output += Paragraph + std::to_string(background_); }

    void Apply(std::string& output, const RtfStyle& current) {
        if (&current == previous_) return;
        AppendStyle(output, previous_, current);
        previous_ = &current;
        const auto next = current.background == background_ ? 0 : current.background;
        if (highlight_ == next) return;
        output += R"(\highlight)" + std::to_string(next) + ' ';
        highlight_ = next;
    }

    void EndLine(std::string& output, const RtfStyle* nextLine) {
        if (!UpdateBackground(nextLine)) {
            output += "\\line\n";
            return;
        }
        previous_ = nullptr;
        highlight_ = 0;
        output += ParagraphEnd;
        Start(output);
    }

private:
    bool UpdateBackground(const RtfStyle* nextLine) {
        if (!nextLine) return false;
        const bool previouslyFilled = std::exchange(eolFilled_, nextLine->eolFilled);
        if (eolFilled_) return std::exchange(background_, nextLine->background) != nextLine->background;
        if (!previouslyFilled) return false;
        background_ = DefaultBackground;
        return true;
    }

    unsigned background_;
    bool eolFilled_;
    unsigned highlight_ = 0;
    const RtfStyle* previous_ = nullptr;
};
std::string SerializeRtf(EditorControl& editor, const StyledRange& range, Sci_Position start) {
    const std::string_view packed = range.packed;
    const auto length = range.length;
    const bool unicode = range.codePage == Scintilla::CpUtf8;
    const auto codePage = unicode || !range.codePage ? RtfAnsiCodePage() : range.codePage;
    StyleMap indices{};
    const auto styles = ReadUsedStyles(editor, packed.substr(0, length), indices);
    std::string output;
    const auto rtf = AppendTables(output, styles, codePage);
    const auto styleAt = [&](size_t offset) -> const RtfStyle& {
        return rtf[indices[static_cast<unsigned char>(packed[offset])]];
    };
    const auto paragraphAt = [&](size_t line) -> const RtfStyle* {
        if (line >= range.lineStarts.size() || !range.lineStarts[line]) return nullptr;
        return &styleAt(2 * range.lineStarts[line] - 1);
    };
    ParagraphStyles paragraphs(paragraphAt(1));
    paragraphs.Start(output);
    const auto text = packed.substr(length + 1, length);
    const bool useTabs = editor.Send(GetUseTabs);
    const auto tabWidth = static_cast<size_t>(editor.Send(GetTabWidth));
    size_t column = 0;
    for (size_t offset = 0; offset < length; ++offset) {
        paragraphs.Apply(output, styleAt(offset));
        const auto character = text[offset];
        ++column;
        if (character == '\t') {
            if (useTabs) {
                output += R"(\tab )";
            } else {
                const auto padding = tabWidth - ((column - 1) % tabWidth);
                output.append(padding, ' ');
                column += padding;
            }
        } else if (character == '\r' || character == '\n') {
            column = 0;
            if (character == '\r' && offset + 1 < length && text[offset + 1] == '\n') ++offset;
            const auto nextLine = std::ranges::upper_bound(range.lineStarts, offset) - range.lineStarts.begin();
            paragraphs.EndLine(output, paragraphAt(static_cast<size_t>(nextLine) + 1));
        } else if (unicode && static_cast<unsigned char>(character) >= 0x80) {
            Sci_Position width = 0;
            const auto scalar = editor.Send(GetCharacterAndWidth, start + offset, reinterpret_cast<intptr_t>(&width));
            AppendUnicode(output, static_cast<unsigned>(scalar));
            offset += static_cast<size_t>(std::max<Sci_Position>(width, 1) - 1);
        } else {
            if (character == '\\' || character == '{' || character == '}') output += '\\';
            output += character;
        }
    }
    output += ParagraphEnd;
    output += '}';
    return output;
}
} // namespace

void EditorControl::CopyAsRtf([[maybe_unused]] bool nativeAnsi) {
    auto start = Send(GetSelectionStart);
    auto end = Send(GetSelectionEnd);
    if (start == end) {
        start = 0;
        end = Send(GetLength);
    }
    if (end == start) return;
    Send(Colourise, 0, end);
    auto range = ReadRange(*this, start, end);
#ifdef __WXMSW__
    const auto ansi = ::GetACP();
    if (nativeAnsi && range.codePage == Scintilla::CpUtf8 && ansi != Scintilla::CpUtf8)
        ConvertAnsi(range, start, ansi, *RawNativeBytes());
#endif
    EditorClipboard::WriteRtf(*GetParent(), SerializeRtf(*this, range, start));
}
