#pragma once

#include "search/search_options.h"
#include <cstdint>
#include <ScintillaTypes.h>
#include <wx/string.h>
#include <optional>

class EditorControl;

struct SearchRequest {
    wxString query;
    wxString replacement;
    SearchOptions options;
    bool wrapAround = true;
};

enum class SearchDirection : uint8_t { Forward, Backward };
enum class FindStatus : uint8_t { Found, NotFound, InvalidPattern, EmptyQuery };
struct FindResult {
    FindStatus status;
    bool wrapped = false;
    intptr_t start = 0;
    intptr_t end = 0;
};
struct ReplaceOneResult {
    bool changed = false;
    std::optional<FindResult> next;
};
enum class ReplaceAllStatus : uint8_t { Completed, InvalidPattern, ExpansionFailed };
struct ReplaceAllResult {
    ReplaceAllStatus status;
    size_t count = 0;
    bool changed = false;
};

// Uses the real editor's matcher, selection and history. Positions are UTF-8 bytes.
class SearchSession final {
public:
    explicit SearchSession(EditorControl& editor) : editor_(editor) {}
    const SearchRequest& Request() const { return request_; }
    void Configure(SearchRequest request);
    FindResult Find(SearchDirection direction);
    ReplaceOneResult ReplaceOne();
    ReplaceAllResult ReplaceAll();

private:
    EditorControl& editor_;
    SearchRequest request_;
    Scintilla::FindOption SearchFlags() const;
    FindResult SelectTarget(bool wrapped);
};
