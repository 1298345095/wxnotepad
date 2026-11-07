#pragma once

#include <functional>
#include <optional>
#include <string>

class wxWindow;
namespace Scintilla::Internal {
class SelectionText;
}

namespace EditorClipboard {
struct ReadResult {
    std::string text;
    bool rectangular = false;
    bool lineCopy = false;
};

bool Write(wxWindow& owner, const Scintilla::Internal::SelectionText& text);
bool HasText() noexcept;
bool HasData(wxWindow& owner);
// True when a nonempty native clipboard was successfully cleared.
bool Clear(wxWindow& owner);
// Original CF_UNICODETEXT C-string semantics, without our GTK rectangle carrier.
std::optional<std::string> ReadPlainText(wxWindow& owner);
// Native reads can dispatch events. Query the live selection at the same point
// as the original platform host, before interpreting its line-copy format tags.
std::optional<ReadResult> Read(wxWindow& owner, const std::function<bool()>& selectionEmpty);
} // namespace EditorClipboard
