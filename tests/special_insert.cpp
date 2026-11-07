#include "native_search_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct ControlFixture {
    wxString label;
    std::string bytes;
    int originalCommand;
};
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoul(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
std::vector<std::string> Fields(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> values;
    for (std::string field; std::getline(input, field, '\t');)
        values.push_back(field);
    return values;
}
std::vector<ControlFixture> ControlFixtures() {
    std::ifstream file{std::filesystem::path(SPECIAL_INSERT_FIXTURES)};
    Require(file.is_open(), "original control-character reference asset is available");
    std::vector<ControlFixture> values;
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line.front() == '#') continue;
        const auto tab = line.find('\t');
        const auto hex = line.substr(tab + 1, line.find('\t', tab + 1) - tab - 1);
        values.push_back({wxString::FromUTF8(line.substr(0, tab)), Unhex(hex),
                          std::stoi(line.substr(line.find('\t', tab + 1) + 1))});
    }
    Require(values.size() == 27, "all 27 original Unicode control entries are represented");
    return values;
}
void RequireGuid(std::string_view guid) {
    Require(guid.size() == 36, "generated GUID has the original 36-byte representation");
    for (size_t index = 0; index < guid.size(); ++index) {
        const bool hyphen = index == 8 || index == 13 || index == 18 || index == 23;
        Require(hyphen ? guid[index] == '-'
                       : (guid[index] >= '0' && guid[index] <= '9') || (guid[index] >= 'a' && guid[index] <= 'f'),
                "GUID uses the original lowercase hexadecimal and hyphen representation");
    }
    Require(guid[14] == '4' && std::string_view("89ab").find(guid[19]) != std::string_view::npos,
            "real GUID has the version and variant observed in independent original generations");
}
void Controls(EditorFrame* frame) {
    auto* editor = frame->Editor();
    size_t index = 0;
    for (const auto& control : ControlFixtures()) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", control.label);
        Require(id != wxNOT_FOUND, "every original control character retains its descriptive native menu name");
        auto* item = frame->GetMenuBar()->FindItem(id);
        Require(item->GetItemLabel() == control.label && item->GetMenu()->FindItemByPosition(index++) == item,
                "original control order and exact mnemonics survive native menu adaptation");
        TestEditor::ResetStream(editor, "AoldZ", 1, 4);
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == "A" + control.bytes + "Z",
                "native control command inserts exact original UTF-8 bytes");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify),
                "control insertion has one Undo restoring text and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "A" + control.bytes + "Z", "Redo restores the exact control bytes");
    }
    std::puts("PASS: all 27 original control names and exact bytes through native menus");
}
void Guid(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "New GUID");
    Require(id != wxNOT_FOUND, "New GUID is available in the native Insert menu");
    TestEditor::ResetStream(frame->Editor(), "AoldZ", 1, 4);
    NativeSearch::Command(frame, id);
    const auto text = TestEditor::Text(frame);
    Require(text.size() == 38 && text.front() == 'A' && text.back() == 'Z',
            "GUID replaces the original selection with exactly 36 ASCII bytes");
    const auto guid = text.substr(1, 36);
    RequireGuid(guid);
    TestEditor::RequireSelection(frame->Editor(), 37, 37, "GUID replacement collapses to its UTF-8 end");
    frame->Editor()->Send(Message::Undo);
    Require(TestEditor::Text(frame) == "AoldZ" && !frame->Editor()->Send(Message::GetModify),
            "one Undo restores original text and save point");
    frame->Editor()->Send(Message::Redo);
    Require(TestEditor::Text(frame) == text, "Redo restores the exact originally generated value");
    std::puts("PASS: native GUID representation, selection boundary and Undo/Redo");
}
std::string Snapshot(EditorControl* editor, int guidOffset) {
    auto text = TestEditor::Text(editor);
    if (guidOffset >= 0) {
        const auto offset = static_cast<size_t>(guidOffset);
        RequireGuid(std::string_view(text).substr(offset, 36));
        text.replace(offset, 36, 36, '@');
    }
    constexpr char digits[] = "0123456789abcdef";
    std::ostringstream output;
    for (unsigned char byte : text)
        output << digits[byte >> 4] << digits[byte & 15];
    for (Message message :
         {Message::GetAnchor, Message::GetCurrentPos, Message::GetModify, Message::CanUndo, Message::CanRedo,
          Message::GetSelections, Message::GetMainSelection, Message::GetSelectionMode})
        output << '\t' << editor->Send(message);
    const auto main = static_cast<uintptr_t>(editor->Send(Message::GetMainSelection));
    output << '\t' << editor->Send(Message::GetSelectionNAnchorVirtualSpace, main) << '\t'
           << editor->Send(Message::GetSelectionNCaretVirtualSpace, main) << '\t' << editor->Send(Message::GetEOLMode);
    return output.str();
}
void PrepareCase(EditorControl* editor, const std::string& name) {
    TestEditor::ResetStream(editor, "AoldZ", 1, 4, Scintilla::EndOfLine::CrLf);
    editor->Send(Message::SetMultipleSelection, 0);
    editor->Send(Message::SetVirtualSpaceOptions, 3);
    if (name == "empty") editor->Send(Message::SetSel, 1, 1);
    if (name == "reverse") editor->Send(Message::SetSel, 4, 1);
    if (name == "multiple" || name == "multiple_first" || name == "rectangle") {
        TestEditor::ResetStream(editor, "abcd\nefgh", 1, 2);
        if (name == "rectangle") {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, 7);
        } else {
            editor->Send(Message::SetMultipleSelection, 1);
            editor->Send(Message::AddSelection, 7, 6);
            if (name == "multiple_first") editor->Send(Message::SetMainSelection, 0);
        }
    }
    if (name == "virtual") {
        TestEditor::ResetStream(editor, "a\nx", 1, 1);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 3);
        editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 1);
        editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 3);
    }
    if (name == "readonly" || name == "pending") {
        const auto end = editor->Send(Message::GetLength);
        editor->Send(Message::SetSel, static_cast<uintptr_t>(end), end);
        editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 4, 1);
        if (name == "readonly") editor->Send(Message::SetReadOnly, 1);
    }
    if (name == "supplementary") TestEditor::ResetStream(editor, "😀old🦊", 4, 7);
}
void InsertCapturedCommand(EditorControl* editor, int command, bool defaultEncoding) {
    switch (command) {
    case 40380:
        editor->InsertGuid();
        break;
    case 40260:
        editor->InsertUnicodeControlCharacter(UnicodeControlCharacter::Lrm, defaultEncoding);
        break;
    case 40275:
        editor->InsertUnicodeControlCharacter(UnicodeControlCharacter::Rs, defaultEncoding);
        break;
    case 40285:
        editor->InsertUnicodeControlCharacter(UnicodeControlCharacter::Alm, defaultEncoding);
        break;
    default:
        throw std::runtime_error("state fixture uses a captured command");
    }
}
void OriginalStates(EditorFrame* frame) {
    auto* editor = frame->Editor();
    std::ifstream file{std::filesystem::path(SPECIAL_INSERT_STATES)};
    Require(file.is_open(), "independent original state fixture is available");
    size_t count = 0;
    bool matched = true;
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = Fields(line);
        Require(fields.size() == 17, "original state row contains every observed field");
        if (fields[3] == "before") PrepareCase(editor, fields[2]);
        if (fields[3] == "after") InsertCapturedCommand(editor, std::stoi(fields[1]), fields[0] == "1");
        if (fields[3] == "undo") {
            editor->Send(Message::SetReadOnly, 0);
            editor->Send(Message::Undo);
        }
        if (fields[3] == "redo") editor->Send(Message::Redo);
        size_t start = 0;
        for (int field = 0; field < 5; ++field)
            start = line.find('\t', start) + 1;
        const auto actual = Snapshot(editor, std::stoi(fields[4]));
        const auto expected = line.substr(start);
        Require(editor->Send(Message::GetCodePage) == 65001,
                "special insertion retains the single UTF-8 editor buffer for every file-encoding identity");
        if (actual != expected) {
            std::fprintf(stderr, "Original state mismatch: %s\nExpected: %s\nActual: %s\n", line.c_str(),
                         expected.c_str(), actual.c_str());
            matched = false;
        }
        ++count;
    }
    Require(count == 296, "complete captured selection/default-ANSI/Undo/Redo matrix is exercised");
    Require(matched, "insertion matches independently captured original editing state");
    std::puts("PASS: 296 independent original states including default ANSI, multiple/rectangular/virtual selections "
              "and redo");
}
void Availability(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto controls = ControlFixtures();
    const int guid = frame->GetMenuBar()->FindMenuItem("Edit", "New GUID");
    for (bool readOnly : {false, true}) {
        TestEditor::ResetStream(editor, "");
        editor->Send(Message::SetReadOnly, readOnly);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(guid),
                "original GUID remains available in empty and read-only documents");
        for (const auto& control : controls)
            Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", control.label)),
                    "original control menus remain available in empty and read-only documents");
    }
    TestEditor::ResetStream(editor, "AoldZ", 4, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    editor->Send(Message::SetReadOnly, 1);
    NativeSearch::Command(frame, guid);
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "native read-only GUID preserves original bytes and history");
    TestEditor::RequireSelection(editor, 1, 1, "native read-only GUID retains original selection collapse");
    editor->Send(Message::SetReadOnly, 0);
    Require(editor->Send(Message::CanRedo), "native read-only GUID preserves the pending redo");
}
int NativeControl(EditorFrame* frame, const std::vector<ControlFixture>& controls, int originalCommand) {
    for (const auto& control : controls) {
        if (control.originalCommand == originalCommand) return frame->GetMenuBar()->FindMenuItem("Edit", control.label);
    }
    throw std::runtime_error("captured control command has an original descriptor");
}
void NondefaultEolMetadata(EditorFrame* frame) {
    auto* editor = frame->Editor();
    constexpr char before[] = "😀\r\nold\nZ\rQ";
    for (auto mode : {Scintilla::EndOfLine::Lf, Scintilla::EndOfLine::Cr}) {
        TestEditor::ResetStream(editor, before, 6, 9, mode);
        editor->InsertUnicodeControlCharacter(UnicodeControlCharacter::Ls, false);
        Require(TestEditor::Text(editor) == "😀\r\n\xe2\x80\xa8\nZ\rQ" &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(mode),
                "line-separator insertion preserves nondefault EOL metadata and unrelated mixed-EOL bytes");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::GetModify),
                "control Undo restores original mixed-EOL bytes and save point");
        editor->InsertGuid();
        const auto text = TestEditor::Text(editor);
        Require(text.size() == 46 && text.starts_with("😀\r\n") && text.ends_with("\nZ\rQ") &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(mode),
                "GUID insertion preserves LF/CR metadata and every neighboring byte");
        RequireGuid(std::string_view(text).substr(6, 36));
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(mode),
                "GUID Undo retains nondefault EOL metadata and restores source bytes");
    }
    std::puts("PASS: GUID/control insertion retains nondefault LF/CR metadata and mixed-EOL neighboring bytes");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto controls = ControlFixtures();
    const auto directory = TestFiles::TemporaryDirectory("special-insert");
    std::filesystem::create_directory(directory);
    std::ifstream file{std::filesystem::path(SPECIAL_INSERT_SAVED)};
    Require(file.is_open(), "independent original saved-byte fixtures are available");
    size_t count = 0;
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = Fields(line);
        const auto encoding = fields[0].starts_with("utf16") ? FileEncoding::Utf16Le
                              : fields[0] == "cp936"         ? FileEncoding::Gbk
                                                             : FileEncoding::Utf8;
        const TestFiles::EncodedFixture fixture{"control", encoding, Unhex(fields[2]), Unhex(fields[3])};
        const auto path = directory / std::filesystem::u8path("控制😀-" + std::to_string(count) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), encoding).status == OpenStatus::Opened,
                "encoded control fixture opens through the actual document command");
        const bool bom = frame->DocumentHasBom();
        const auto eol = editor->Send(Message::GetEOLMode);
        editor->Send(Message::SetSel, 1, 4);
        NativeSearch::Command(frame, NativeControl(frame, controls, std::stoi(fields[1])));
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"Save writes original encoded control bytes",
                                         "Undo Save restores original file bytes",
                                         "Redo Save restores original encoded control bytes"});
        Require(frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(Message::GetEOLMode) == eol,
                "insertion and saved history retain codec, BOM and EOL metadata");
        ++count;
    }
    Require(count == 110, "all controls have original Unicode output and representable CP936 output coverage");
    const auto path = directory / "loss.txt";
    TestFiles::WriteBytes(path, "AoldZ");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "explicit CP936 insertion fixture opens");
    editor->Send(Message::SetSel, 1, 4);
    NativeSearch::Command(frame, NativeControl(frame, controls, 40260));
    Require(TestEditor::Text(editor) == "A\xe2\x80\x8eZ",
            "explicit CP936 accepts the original multibyte control insertion");
    bool asked = false;
    Require(frame->Save([&asked] {
                     asked = true;
                     return GbkLossChoice::Cancel;
                 }).status == SaveStatus::Cancelled &&
                asked && TestFiles::Bytes(path) == "AoldZ" && editor->Send(Message::GetModify),
            "cancelling the existing legacy loss warning retains the inserted control and original disk bytes");
    editor->Send(Message::SetSavePoint);
    std::filesystem::remove_all(directory);
    std::puts("PASS: 110 original encoded byte outcomes, Unicode paths, metadata and explicit CP936 loss cancellation");
}
void NativeAnsi(EditorFrame* frame) {
#ifdef __WXMSW__
    // Native ANSI is supported by this migrated codec set when the actual ACP is CP936.
    if (GetACP() != 936) return;
    auto* editor = frame->Editor();
    const auto path = TestFiles::TemporaryDirectory() / "special-insert-ansi.bat";
    TestFiles::WriteBytes(path, "AoldZ");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Gbk,
            "real automatic ANSI Open retains its supported native CP936 identity");
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    const auto controls = ControlFixtures();
    NativeSearch::Command(frame, NativeControl(frame, controls, 40260));
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
            "actual ANSI identity blocks multibyte control insertion before editing or redo invalidation");
    TestEditor::RequireSelection(editor, 4, 1, "actual ANSI eligibility rejection retains the reversed selection");
    NativeSearch::Command(frame, NativeControl(frame, controls, 40275));
    const TestFiles::EncodedFixture fixture{"ansi", FileEncoding::Gbk, "AoldZ", "A\x1eZ"};
    TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                    {"ANSI Save retains original record-separator byte",
                                     "ANSI Undo Save restores original bytes",
                                     "ANSI Redo Save restores record separator"});
    std::filesystem::remove(path);
    std::puts("PASS: real Windows native ANSI identity and eligible single-byte control output");
#else
    (void)frame;
#endif
}
class SpecialInsertApp final : public wxApp {
public:
    bool OnInit() override {
        settings_.values.encoding.defaultEncoding = 0;
        settings_.values.encoding.loadASCIIasUTF8 = false;
        settings_.values.encoding.loadANSIasUTF8 = false;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        CallAfter([this, frame] {
            try {
                Guid(frame);
                Controls(frame);
                OriginalStates(frame);
                Availability(frame);
                NondefaultEolMetadata(frame);
                SavedBytes(frame);
                NativeAnsi(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Message::SetSavePoint);
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
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SpecialInsertApp);
