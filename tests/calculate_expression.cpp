#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "encoded_file_helpers.h"
#include "calculate_expression_reference.h"
#include "calculate_expression_encoding_reference.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++shown;
        return wxID_OK;
    }
    int shown = 0;
};
void SelectionModes(EditorFrame* frame, int id) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "1+2\n3+4", 0, 3);
    editor->Send(SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->FindItem(id)->IsEnabled(), "original read-only selection keeps calculation enabled");
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(frame) == "1+2\n3+4" && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                editor->Send(GetAnchor) == 3 && editor->Send(GetCurrentPos) == 5,
            "original read-only insertion is blocked while result-range selection is still applied");
    TestEditor::ResetStream(editor, "1+2\n3+4", 0, 3);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    {
        RectangleFeedback feedback;
        NativeSearch::Command(frame, id);
        Require(feedback.shown == 1 && TestEditor::Text(frame) == "1+2\n3+4" && !editor->Send(GetModify) &&
                    !editor->Send(CanUndo),
                "rectangular selection warns once without editing or history");
    }
    TestEditor::ResetStream(editor, "1+2\n3+4", 0, 3);
    editor->Send(AddSelection, 7, 4);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(frame) == "1+2\n3+4 7" && editor->Send(GetSelections) == 1 &&
                editor->Send(GetAnchor) == 7 && editor->Send(GetCurrentPos) == 9,
            "original multiple-selection evaluation uses main expression and leaves one result selection");
    editor->Send(Undo);
    Require(TestEditor::Text(frame) == "1+2\n3+4" && editor->Send(GetSelections) == 2 &&
                editor->Send(GetMainSelection) == 1 && editor->Send(GetAnchor) == 4 &&
                editor->Send(GetCurrentPos) == 7 && !editor->Send(GetModify),
            "Undo restores original multiple selections and main selection");
    editor->Send(Redo);
    Require(TestEditor::Text(frame) == "1+2\n3+4 7" && editor->Send(GetSelections) == 1,
            "Redo restores original main-expression result and single selection");
    std::puts("PASS: original read-only, rectangle and main/multiple selection semantics");
}
void EncodedFiles(EditorFrame* frame, int id) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const auto& fixture : CalculationEncodingReferences) {
        const auto path = TestFiles::TemporaryDirectory() / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open real encoded expression through application");
        const auto originalBom = frame->DocumentHasBom();
        const auto originalEol = editor->Send(GetEOLMode);
        const auto expressionEnd = editor->Send(GetLineEndPosition, 0) - 5;
        editor->Send(SetSel, expressionEnd, 4);
        NativeSearch::Command(frame, id);
        Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == originalBom &&
                    editor->Send(GetEOLMode) == originalEol,
                "evaluation preserves current encoding, BOM and EOL metadata");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"saved expression/result has exact independent encoded bytes",
                                         "Undo and Save restore original encoded bytes",
                                         "Redo and Save restore result encoded bytes"});
    }
    std::puts("PASS: all ten delivered encodings, Unicode results, BOM/EOL and saved Undo/Redo bytes");
}
void Clipboard(bool seed) {
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    Require(!!lock, "open owned clipboard");
    const wxString text = wxString::FromUTF8("calculate-owned 中文😀");
    const wxDataFormat format("application/x-wxnotepad-calculation292");
    const std::string opaque("\0\1\x7f\xff", 4);
    if (seed) {
        auto* composite = new wxDataObjectComposite;
        composite->Add(new wxTextDataObject(text), true);
        auto* custom = new wxCustomDataObject(format);
        custom->SetData(opaque.size(), opaque.data());
        composite->Add(custom);
        Require(wxTheClipboard->SetData(composite), "seed independent clipboard data");
    } else {
        wxTextDataObject observed;
        wxCustomDataObject custom(format);
        Require(wxTheClipboard->GetData(observed) && observed.GetText() == text && wxTheClipboard->GetData(custom) &&
                    std::string(static_cast<const char*>(custom.GetData()), custom.GetSize()) == opaque,
                "calculation preserves clipboard text and opaque bytes");
    }
}
class CalculateExpressionApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "calc.ini"));
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "owned calculation frame presented");
                const int id = frame->GetMenuBar()->FindMenuItem("Tools", "Calculate Expression");
                Require(id != wxNOT_FOUND, "Tools Action on Selection exposes original Calculate Expression");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->FindItem(id)->IsEnabled(), "empty selection disables calculation");
                std::puts("PASS: original Calculate Expression menu and empty-selection availability");
                Clipboard(true);
                TestEditor::ResetStream(frame->Editor(), "pre 1+2*3 tail", 4, 9);
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(frame) == "pre 1+2*3 7 tail",
                        "original JavaScript precedence inserts result after expression");
                Require(frame->Editor()->Send(Scintilla::Message::GetAnchor) == 9 &&
                            frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 11,
                        "inserted selection includes original leading space");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(TestEditor::Text(frame) == "pre 1+2*3 tail" &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "single Undo restores original document save point");
                for (const auto& sample : CalculationReferences) {
                    for (const bool reversed : {false, true}) {
                        const std::string expression(sample.expression);
                        const std::string before = "pre " + expression + " tail";
                        const auto end = static_cast<intptr_t>(4 + expression.size());
                        auto* editor = frame->Editor();
                        editor->Send(Scintilla::Message::CallTipCancel);
                        TestEditor::ResetStream(editor, before.c_str(), reversed ? end : 4, reversed ? 4 : end);
                        NativeSearch::Command(frame, id);
                        const std::string expected = "pre " + expression + sample.inserted + " tail";
                        if (TestEditor::Text(frame) != expected) {
                            DiagnosticPrint::Print(stderr, "Expression {} expected {} actual {}\n", expression,
                                                   expected, TestEditor::Text(frame));
                            throw std::runtime_error("independent original expression bytes");
                        }
                        Require((editor->Send(Scintilla::Message::CallTipActive) != 0) == sample.error,
                                "syntax and evaluation errors show native notification; empty values do not");
                        if (std::string_view(sample.inserted).empty()) {
                            Require(!editor->Send(Scintilla::Message::GetModify) &&
                                        !editor->Send(Scintilla::Message::CanUndo) &&
                                        editor->Send(Scintilla::Message::GetAnchor) == (reversed ? end : 4) &&
                                        editor->Send(Scintilla::Message::GetCurrentPos) == (reversed ? 4 : end),
                                    "empty results and errors preserve selection, document and history");
                        } else {
                            Require(editor->Send(Scintilla::Message::GetAnchor) == end &&
                                        editor->Send(Scintilla::Message::GetCurrentPos) ==
                                            end + static_cast<intptr_t>(std::string_view(sample.inserted).size()),
                                    "forward result selection follows original for both source directions");
                            editor->Send(Scintilla::Message::Undo);
                            Require(TestEditor::Text(frame) == before && !editor->Send(Scintilla::Message::GetModify) &&
                                        editor->Send(Scintilla::Message::GetAnchor) == (reversed ? end : 4) &&
                                        editor->Send(Scintilla::Message::GetCurrentPos) == (reversed ? 4 : end),
                                    "single Undo restores original selection direction and save point");
                            editor->Send(Scintilla::Message::Redo);
                            Require(TestEditor::Text(frame) == expected && editor->Send(Scintilla::Message::GetModify),
                                    "single Redo restores complete result and modified document");
                        }
                    }
                }
                std::puts(
                    "PASS: independent original JavaScript/Math, types, objects, fresh state, errors and Undo/Redo");
                SelectionModes(frame, id);
                std::string originalLongExpression;
                for (int term = 0; term < 550; ++term)
                    originalLongExpression += "1+";
                originalLongExpression += '1';
                TestEditor::ResetStream(frame->Editor(), originalLongExpression.c_str(), 0,
                                        static_cast<intptr_t>(originalLongExpression.size()));
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(frame) == originalLongExpression &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify) &&
                            frame->Editor()->Send(Scintilla::Message::CallTipActive),
                        "original wsprintf Math wrapper limit reports error without document/history changes");
                EncodedFiles(frame, id);
                Clipboard(false);
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
            }
            frame->Editor()->Send(Scintilla::Message::SetSavePoint);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(CalculateExpressionApp);
