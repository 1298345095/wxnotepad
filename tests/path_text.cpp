#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "file_command_driver.h"
#include "settings/settings_store.h"

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int InsertId(EditorFrame* frame, const char* label) {
    auto* edit = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Edit"));
    const auto* insert = edit->FindItem(edit->FindItem("Insert"))->GetSubMenu();
    const int id = insert->FindItem(label);
    Require(id != wxNOT_FOUND, "original filename insertion command exists in the native Insert menu");
    return id;
}
void Insert(EditorFrame* frame, const char* label) {
    NativeSearch::Command(frame, InsertId(frame, label));
}
void CheckInsertion(EditorFrame* frame, const char* label, const std::string& value) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "AoldZ", 1, 4);
    const auto eol = editor->Send(Message::GetEOLMode);
    const auto encoding = frame->DocumentEncoding();
    const bool bom = frame->DocumentHasBom();
    Insert(frame, label);
    const auto expected = "A" + value + "Z";
    Require(TestEditor::Text(editor) == expected && editor->Send(Message::GetModify),
            "native insertion matches independently captured literal name transformation");
    TestEditor::RequireSelection(editor, expected.size() - 1, expected.size() - 1,
                                 "insertion collapses to the UTF-8 byte endpoint");
    Require(editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == encoding &&
                frame->DocumentHasBom() == bom,
            "insertion preserves document encoding, BOM and EOL metadata");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "one Undo restores original bytes and save point");
    TestEditor::RequireSelection(editor, 1, 4, "Undo restores original replacement endpoints");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == expected && editor->Send(Message::GetModify),
            "one Redo restores filename insertion");
}
void NameCases(EditorFrame* frame, const std::filesystem::path& directory) {
    // Literals observed from actual Notepad4 89b8b36 commands; see #142 evidence.
    const std::pair<const char*, const char*> names[] = {{"report.txt", "report"},
                                                         {"archive.part.txt", "archive.part"},
                                                         {"README", "README"},
                                                         {".env", ""},
                                                         {".config.json", ".config"},
                                                         {"file name.txt", "file name"},
                                                         {"dot.before space", "dot.before space"},
                                                         {"no.ext space.txt", "no.ext space"},
                                                         {"é中文😀.txt", "é中文😀"}};
    for (const auto& [name, base] : names) {
        const auto path = directory / std::filesystem::u8path(name);
        TestFiles::WriteBytes(path, "AoldZ");
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "Unicode native filename fixture opens through the actual document interface");
        CheckInsertion(frame, "File Name", base);
        CheckInsertion(frame, "File Name and Extension", name);
        CheckInsertion(frame, "Full Path Name", TestFiles::Filename(path).utf8_string());
        if (std::string_view(name) == "report.txt") {
            std::filesystem::remove(path);
            CheckInsertion(frame, "File Name and Extension", "report.txt");
        }
    }
    std::puts("PASS: literal native names, multi-dot, dotfiles, spaces, Unicode and deleted-file identity");
}
void SelectionCases(EditorFrame* frame, const std::filesystem::path& directory) {
    const auto path = directory / "report.txt";
    TestFiles::WriteBytes(path, "AoldZ");
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open selection identity");
    for (const auto& range : {std::pair{1, 1}, std::pair{1, 4}, std::pair{4, 1}}) {
        TestEditor::ResetStream(editor, "AoldZ", range.first, range.second);
        Insert(frame, "File Name and Extension");
        Require(TestEditor::Text(editor) == (range.first == range.second ? "Areport.txtoldZ" : "Areport.txtZ"),
                "caret and forward/reversed replacements match original bytes");
        TestEditor::RequireSelection(editor, 11, 11, "stream replacement collapses to original byte endpoint");
        editor->Send(Message::Undo);
        TestEditor::RequireSelection(editor, range.first, range.second, "Undo restores directed stream selection");
    }
    for (const bool rectangle : {false, true}) {
        TestEditor::ResetStream(editor, "abcd\nefgh", 1, 2);
        if (rectangle) {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, 7);
        } else
            editor->Send(Message::AddSelection, 7, 6);
        Insert(frame, "File Name and Extension");
        Require(TestEditor::Text(editor) == "acd\nereport.txtgh" && editor->Send(Message::GetSelections) == 1,
                "original boundary deletes selected ranges and inserts once at the surviving main caret");
        TestEditor::RequireSelection(editor, 15, 15, "multiple/rectangle insertion endpoint matches original");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "abcd\nefgh" && editor->Send(Message::GetSelections) == 2 &&
                    !editor->Send(Message::GetModify),
                "Undo restores original multiple/rectangular ranges and save point");
        TestEditor::RequireSelection(editor, rectangle ? 1 : 6, 7, "Undo restores captured main endpoints");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "acd\nereport.txtgh", "Redo restores the single replacement");
    }
    TestEditor::ResetStream(editor, "AoldZ", 4, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    editor->Send(Message::SetReadOnly, 1);
    Insert(frame, "File Name and Extension");
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "read-only filename invocation preserves bytes and history");
    TestEditor::RequireSelection(editor, 1, 1, "read-only replacement retains original selection collapse");
    for (const char* label : {"File Name", "File Name and Extension", "Full Path Name"})
        Require(frame->GetMenuBar()->IsEnabled(InsertId(frame, label)),
                "all original insertion commands remain available while read-only");
    editor->Send(Message::SetReadOnly, 0);
    Require(editor->Send(Message::CanRedo), "read-only invocation preserves pending Redo");
    std::puts("PASS: caret, directed/multiple/rectangular ranges, endpoints, read-only and history");
}
void IdentityCases(EditorFrame* frame, const std::filesystem::path& directory) {
    frame->Editor()->Send(Message::SetSavePoint);
    Require(frame->New(), "create actual untitled document");
    for (const char* label : {"File Name", "File Name and Extension", "Full Path Name"})
        CheckInsertion(frame, label, "Untitled");
    Require(frame->SaveFirst(std::nullopt).status == SaveStatus::Cancelled && frame->DocumentPath().empty(),
            "cancelled first save retains untitled identity");
    Require(frame->SaveFirst(TestFiles::Filename(directory / "missing" / "first.txt")).status == SaveStatus::Failed &&
                frame->DocumentPath().empty(),
            "failed first save retains untitled identity");
    CheckInsertion(frame, "File Name and Extension", "Untitled");
    const auto first = directory / "first.part.txt";
    Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved, "first save commits identity");
    CheckInsertion(frame, "File Name", "first.part");
    {
        NativeDialogs::FileCommandDriver cancel(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL);
        NativeSearch::Command(frame, wxID_SAVEAS);
        cancel.Stop();
        Require(cancel.choosers == 1 && frame->DocumentPath() == TestFiles::Filename(first),
                "cancelled native Save As retains original identity");
    }
    CheckInsertion(frame, "File Name and Extension", "first.part.txt");
    Require(frame->SaveAs(TestFiles::Filename(directory / "missing" / "failed.txt")).status == SaveStatus::Failed &&
                frame->DocumentPath() == TestFiles::Filename(first),
            "failed Save As retains identity");
    CheckInsertion(frame, "Full Path Name", TestFiles::Filename(first).utf8_string());
    const auto renamed = directory / "renamed.txt";
    {
        NativeDialogs::FileCommandDriver save(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL,
                                              TestFiles::Filename(renamed));
        NativeSearch::Command(frame, wxID_SAVEAS);
        save.Stop();
        Require(save.choosers == 1 && frame->DocumentPath() == TestFiles::Filename(renamed),
                "successful native Save As commits the selected native path");
    }
    CheckInsertion(frame, "File Name and Extension", "renamed.txt");
    CheckInsertion(frame, "Full Path Name", TestFiles::Filename(renamed).utf8_string());
    for (const bool shift : {false, true}) {
        TestEditor::ResetStream(frame->Editor(), "AoldZ", 1, 4);
        std::printf("Checking native Ctrl+%sF9 insertion\n", shift ? "Shift+" : "");
        NativeKeys::Shortcut(frame, WXK_F9, true, false, shift);
        Require(TestEditor::Text(frame) ==
                    "A" + (shift ? TestFiles::Filename(renamed).utf8_string() : "renamed.txt") + "Z",
                "native Ctrl+F9 and Ctrl+Shift+F9 insert the current committed identity");
    }
    const auto text = TestEditor::Text(frame);
    NativeKeys::Shortcut(frame, WXK_F9, true, true);
    Require(TestEditor::Text(frame) == text, "extra Alt does not alias filename insertion shortcuts");
    std::puts("PASS: Untitled, first save, native Save As success/cancel, failure and insertion shortcuts");
}
void EncodedCases(EditorFrame* frame, const std::filesystem::path& directory) {
    const TestFiles::EncodedFixture fixtures[] = {{"utf8", FileEncoding::Utf8, "AoldZ\r\n", "A时刻Z\r\n"},
                                                  {"utf16", FileEncoding::Utf16Le,
                                                   std::string("\xff\xfe\x41\0o\0l\0d\0Z\0\r\0\n\0", 16),
                                                   std::string("\xff\xfe\x41\0\xf6\x65\x3b\x52Z\0\r\0\n\0", 14)},
                                                  {"gbk", FileEncoding::Gbk, "AoldZ\r\n", "A\xca\xb1\xbf\xccZ\r\n"}};
    for (const auto& fixture : fixtures) {
        const auto folder = directory / fixture.name;
        std::filesystem::create_directory(folder);
        const auto path = folder / std::filesystem::u8path("时刻.txt");
        TestFiles::WriteBytes(path, fixture.before);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open actual encoded filename fixture");
        frame->Editor()->Send(Message::SetSel, 1, 4);
        Insert(frame, "File Name");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"Save writes captured encoded filename bytes",
                                         "Undo Save restores source bytes", "Redo Save restores filename bytes"});
    }
    const auto path = directory / std::filesystem::u8path("😀.txt");
    TestFiles::WriteBytes(path, "AoldZ");
    frame->Editor()->Send(Message::SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "open explicit GBK identity using UTF-8 editor model");
    frame->Editor()->Send(Message::SetSel, 1, 4);
    Insert(frame, "File Name");
    Require(TestEditor::Text(frame) == "A😀Z" && frame->DocumentEncoding() == FileEncoding::Gbk,
            "explicit GBK insertion retains Unicode until the existing save conversion boundary");
    Require(frame->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                TestFiles::Bytes(path) == "AoldZ" && frame->Editor()->Send(Message::GetModify),
            "legacy-save cancellation preserves the Unicode edit and original disk bytes");
    Require(frame->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "A??Z" && TestEditor::Text(frame) == "A😀Z",
            "approved legacy conversion writes original placeholders without changing editor bytes");
    std::puts("PASS: encoded native saved bytes, EOL, Undo/Redo saves and existing legacy-save decisions");
}
void DefaultEncodingCase(const std::filesystem::path& directory) {
#ifdef __WXMSW__
    if (GetACP() != 936) {
        std::puts("SKIP: captured default-ANSI conversion requires system ACP 936");
        return;
    }
#endif
    SettingsStore settings;
    settings.values.encoding.defaultEncoding = 0;
    settings.values.encoding.loadASCIIasUTF8 = false;
    settings.values.encoding.loadANSIasUTF8 = false;
    auto* frame = new EditorFrame(&settings);
    const auto path = directory / std::filesystem::u8path("é中文😀.txt");
    TestFiles::WriteBytes(path, "AoldZ");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
            "native default encoding opens through configured document detection");
#ifdef __WXMSW__
    CheckInsertion(frame, "File Name and Extension", "é中文??.txt");
#else
    CheckInsertion(frame, "File Name and Extension", "é中文😀.txt");
#endif
    Require(frame->Save().status == SaveStatus::Saved,
            "default encoding insertion saves without a second loss confirmation");
#ifdef __WXMSW__
    Require(TestFiles::Bytes(path) == "A\xa8\xa6\xd6\xd0\xce\xc4??.txtZ",
            "default-ANSI insertion writes independently captured native bytes");
#else
    Require(TestFiles::Bytes(path) == "Aé中文😀.txtZ", "native UTF-8 default preserves Unicode filename bytes");
#endif
    frame->Destroy();
    wxYield();
    std::puts("PASS: native default encoding preserves the original insertion conversion boundary");
}
class PathTextApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                TestEditor::ResetStream(frame->Editor(), "AoldZ", 1, 4);
                Insert(frame, "File Name and Extension");
                Require(TestEditor::Text(frame) == "AUntitledZ", "untitled insertion uses original fallback");
                const auto directory = TestFiles::TemporaryDirectory("path-text") / std::filesystem::u8path("目录😀");
                std::filesystem::create_directories(directory);
                NameCases(frame, directory);
                SelectionCases(frame, directory);
                IdentityCases(frame, directory);
                EncodedCases(frame, directory);
                DefaultEncodingCase(directory);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(PathTextApp);
