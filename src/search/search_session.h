#pragma once

#include "search/search_options.h"
#include <cstdint>
#include <ScintillaTypes.h>
#include <wx/string.h>
#include <optional>

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
    FindResult Find(SearchDirection direction);
    FindAllResult FindAll();
    std::optional<size_t> MatchCount();
    ReplaceOneResult ReplaceOne();
    ReplaceAllResult ReplaceAll();
    ReplaceAllResult ReplaceInSelection();

private:
    EditorControl& editor_;
    SearchRequest request_;
    std::optional<size_t> matchCount_;
    uint64_t markedRevision_ = 0;
    bool bookmarkForFindAll_ = false;
    void ClearFindAll();
    void OnEditorState(wxCommandEvent& event);
    Scintilla::FindOption SearchFlags() const;
    FindResult SelectTarget(bool wrapped);
    ReplaceAllResult ReplaceRange(bool inSelection);
};
