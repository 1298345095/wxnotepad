#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax/syntax_catalog.h"
#include <utility>
#include <wx/menu.h>
#include <wx/translation.h>

void EditorFrame::AppendCodeFormatting(wxMenu* web) {
    for (const auto& [label, command] : {std::pair{wxTRANSLATE("CSS/JS/JSON &Compress"), CodeFormatting::Compress},
                                         std::pair{wxTRANSLATE("CSS/JS/JSON &Pretty"), CodeFormatting::Pretty}}) {
        const int id = wxWindow::NewControlId();
        web->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, command](wxCommandEvent&) { editor_->FormatCode(command, SyntaxFor(document_.Language())); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                event.Enable(SyntaxFor(document_.Language()).original.lexerAttributes &
                             SyntaxLexerAttribute::CodePretty);
            },
            id);
    }
}
