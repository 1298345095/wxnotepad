#pragma once

#include <memory>
#include <string_view>
#include <wx/string.h>

namespace DocumentFiles {
struct ReadOnlyState {
    bool readOnly = false;
    bool flag = false;
    wxString changeError;
};

ReadOnlyState ReadReadOnlyState(const wxString& path);
wxString ToggleReadOnly(const wxString& path);
bool UpdateWriteTime(const wxString& path);
// Save Copy deliberately creates its destination before cleanup or an encoding
// decision. A cancelled copy leaves a new empty file and retains existing bytes.
wxString PrepareSaveCopy(const wxString& destination);

// One save owns its safe-write sibling and optional original timestamps.
// Prepare checks write access before any cleanup changes editor text; Commit
// also prepares when no early check was needed. Destruction removes an
// uncommitted sibling. Only a successful Commit replaces the destination.
// The destination is supplied at each phase because an encoding-loss decision
// can enter a modal loop and change the session's live save destination.
class SaveTransaction final {
public:
    explicit SaveTransaction(bool preserveTimes = false);
    ~SaveTransaction();
    SaveTransaction(const SaveTransaction&) = delete;
    SaveTransaction& operator=(const SaveTransaction&) = delete;
    wxString Prepare(const wxString& destination);
    wxString Commit(const wxString& destination, std::string_view bytes);

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace DocumentFiles
