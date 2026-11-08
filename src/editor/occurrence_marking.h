#pragma once

#include <cstdint>
#include <ScintillaTypes.h>
#include <wx/timer.h>
#include <optional>
#include <string>

class EditorControl;

// Original EditMarkAll: automatic selections and Find All own the same marks.
class OccurrenceMarking final : public wxEvtHandler {
public:
    explicit OccurrenceMarking(EditorControl& editor);
    ~OccurrenceMarking() override;
    int Options() const { return options_; }
    void SetOptions(int options);
    void IgnoreNextSelectionUpdate() { ignoreSelectionUpdate_ = true; }
    void Update(Scintilla::Update updated);
    void Clear();
    void Stop();
    void FindAll(std::string text, Scintilla::FindOption flags, bool multiline, bool bookmarks);
    std::optional<size_t> FindAllCount() const;

private:
    EditorControl& editor_;
    wxTimer timer_;
    int options_ = 1;
    unsigned flags_ = 0;
    std::string text_;
    size_t matchCount_ = 0;
    intptr_t lastMatch_ = 0;
    intptr_t stopPosition_ = 0;
    intptr_t bookmarkLine_ = -1;
    bool bookmarkForFindAll_ = false;
    bool pending_ = false;
    bool ignoreSelectionUpdate_ = false;
    int incrementSize_ = 1;
    double duration_ = 64;
    void Reset(unsigned flags, std::string text);
    void Start(bool changed, unsigned flags, std::string text);
    void MarkSelection(bool changed);
    void Continue();
};
