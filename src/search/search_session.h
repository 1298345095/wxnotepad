#pragma once

#include "search/search_options.h"
#include <cstdint>
#include <ScintillaTypes.h>
#include <wx/string.h>
#include <optional>
#include <string>
#include <vector>

class EditorControl;
class wxCommandEvent;

struct SearchRequest {
    wxString query;
    wxString replacement;
    SearchOptions options;
    bool wrapAround = true;
    bool bookmarkMatchedLines = false;
};

enum class SearchDirection : uint8_t { Forward, Backward };
enum class FindStatus : uint8_t { Found, NotFound, InvalidPattern, EmptyQuery };
struct FindResult {
    FindStatus status;
    bool wrapped = false;
    intptr_t start = 0;
    intptr_t end = 0;
};
struct FindAllResult {
    FindStatus status;
    size_t count = 0;
};
struct ReplaceOneResult {
    bool changed = false;
    std::optional<FindResult> next;
};
enum class ReplaceAllStatus : uint8_t { Completed, InvalidPattern, ExpansionFailed, RectangularSelection };
struct ReplaceAllResult {
    ReplaceAllStatus status;
    size_t count = 0;
    bool changed = false;
};

// Uses the real editor's matcher, selection and history. Positions are UTF-8 bytes.
class SearchSession final {
public:
    explicit SearchSession(EditorControl& editor);
    ~SearchSession();
    const SearchRequest& Request() const { return request_; }
    void Configure(SearchRequest request);
    void RestoreHistory(std::vector<wxString> find, std::vector<wxString> replace, bool lastReplaceEmpty);
    void Remember(bool replacement);
    const std::vector<wxString>& RecentFind() const { return recentFind_; }
    const std::vector<wxString>& RecentReplace() const { return recentReplace_; }
    bool LastReplaceEmpty() const { return lastReplaceEmpty_; }
    bool HasFindText() const { return hasFindText_; }
    FindResult Find(SearchDirection direction);
    FindAllResult FindAll();
    std::optional<size_t> MatchCount();
    ReplaceOneResult ReplaceOne();
    ReplaceAllResult ReplaceAll();
    ReplaceAllResult ReplaceInSelection();

private:
    EditorControl& editor_;
    SearchRequest request_;
    std::string QueryBytes() const;
    std::string ReplacementBytes() const;
    std::vector<wxString> recentFind_;
    std::vector<wxString> recentReplace_;
    bool lastReplaceEmpty_ = false;
    bool hasFindText_ = false;
    std::optional<size_t> matchCount_;
    uint64_t markedRevision_ = 0;
    bool bookmarkForFindAll_ = false;
    void ClearFindAll();
    void OnEditorState(wxCommandEvent& event);
    Scintilla::FindOption SearchFlags() const;
    bool UsesRegex() const;
    FindResult SelectTarget(bool wrapped);
    ReplaceAllResult ReplaceRange(bool inSelection);
};
