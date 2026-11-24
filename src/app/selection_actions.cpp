#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/translation.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
wxString FormatSelectionCommand(const wxString& commandTemplate, const wxString& selected) {
    // Original EditSelectionAction uses wsprintfW, whose formatted result is
    // limited to 1024 UTF-16 units, including the template's literal text.
    constexpr size_t OriginalFormatLimit = 1024;
#ifdef __WXMSW__
    wchar_t command[OriginalFormatLimit + 1]{};
    wsprintfW(command, commandTemplate.wc_str(), selected.wc_str());
    return command;
#else
    const auto formatted = wxString::Format(commandTemplate, selected);
    wxString command;
    size_t units = 0;
    for (const wxUniChar character : formatted) {
        if (units == OriginalFormatLimit) break;
        const size_t count = character.GetValue() >= 0x10000 ? 2 : 1;
        if (units + count > OriginalFormatLimit) {
            command += wxUniChar(0xfffd);
            break;
        }
        command += character;
        units += count;
    }
    return command;
#endif
}
} // namespace

bool EditorFrame::HandleSelectionActionKey(const wxKeyEvent& event) {
    if (!event.ControlDown() || !event.ShiftDown() || event.AltDown() || event.MetaDown()) return false;
    const int key = event.GetKeyCode();
    // Like the existing display shortcuts, keep the original number keys when
    // GTK reports shifted punctuation instead of matching the menu accelerator.
    if (key != '1' && key != '!' && key != '2' && key != '@') return false;
    if (!editor_->Send(Scintilla::Message::GetSelectionEmpty))
        RunSelectionAction(key == '1' || key == '!' ? SelectionAction::Custom1 : SelectionAction::Custom2);
    return true;
}

void EditorFrame::RunSelectionAction(SelectionAction action) {
    auto commandTemplate = settings_ ? settings_->SelectionActionTemplate(action) : wxString{};
    if (commandTemplate.empty()) {
        switch (action) {
        case SelectionAction::Google:
            commandTemplate = "https://www.google.com/search?q=%s";
            break;
        case SelectionAction::Bing:
            commandTemplate = "https://www.bing.com/search?q=%s";
            break;
        case SelectionAction::Wikipedia:
            // TRANSLATORS: Original IDS_WIKI_SEARCH_URL default; preserve the literal %s placeholder.
            commandTemplate = _("https://en.wikipedia.org/wiki/Special:Search?search=%s");
            break;
        case SelectionAction::Custom1:
        case SelectionAction::Custom2:
            return;
        }
    }
    const auto selected = editor_->UrlEncodedSelection();
    if (selected.empty()) return;
    const auto command = FormatSelectionCommand(commandTemplate, wxString::FromUTF8(selected));
    // Original startup changes cwd to the executable directory. An untitled
    // EditSelectionAction inherits that directory; named documents supply their own.
    const auto directory = document_.Path().empty() ? wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()
                                                    : wxFileName(document_.Path()).GetPath();
    wxString error;
    if (!LaunchSelectionCommand(command, directory, error))
        wxMessageBox(wxString::Format(_("Could not run command: %s"), error), _("Could not open target"),
                     wxOK | wxICON_ERROR, this);
}
