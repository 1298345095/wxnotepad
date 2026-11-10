#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int InsertId(EditorFrame* frame, const char* label) {
    auto* edit = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Edit"));
    auto* insert = edit->FindItem(edit->FindItem("Insert"))->GetSubMenu();
    const int id = insert->FindItem(label);
    Require(id != wxNOT_FOUND, "original header command exists in the native Insert menu");
    return id;
}
void Encoding(EditorFrame* frame) {
    NativeSearch::Command(frame, InsertId(frame, "Encoding Identifier"));
}
void BasicEncoding(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "AoldZ", 1, 4);
    Encoding(frame);
    Require(TestEditor::Text(editor) == "AUTF-8Z",
            "encoding insertion replaces selection with the original preferred name");
    TestEditor::RequireSelection(editor, 6, 6, "encoding insertion collapses to its UTF-8 endpoint");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "one Undo restores input and save point");
    TestEditor::RequireSelection(editor, 1, 4, "Undo restores the directed selection");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "AUTF-8Z", "one Redo restores the header insertion");
}
void BasicShebang(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "A\nB", 0, 0, Scintilla::EndOfLine::Lf);
    NativeSearch::Command(frame, InsertId(frame, "Script Shebang Line"));
    Require(TestEditor::Text(editor) == "#!/usr/bin/env A\nB",
            "unsupported syntax inserts the original generic prefix without an appended EOL");
}
std::string Unhex(const std::string& value) {
    std::string bytes;
    for (size_t offset = 0; offset < value.size(); offset += 2)
        bytes += static_cast<char>(std::stoi(value.substr(offset, 2), nullptr, 16));
    return bytes;
}
std::vector<std::string> Fields(const std::string& row) {
    std::istringstream stream(row);
    std::vector<std::string> fields;
    for (std::string field; std::getline(stream, field, '\t');)
        fields.push_back(field);
    return fields;
}
void State(EditorControl* editor, const std::vector<std::string>& fields, size_t start) {
    Require(TestEditor::Text(editor) == Unhex(fields[start]),
            "header text matches independently captured original bytes");
    const Message messages[] = {Message::GetAnchor, Message::GetCurrentPos, Message::GetModify, Message::CanUndo,
                                Message::CanRedo};
    for (size_t index = 0; index < std::size(messages); ++index)
        Require(editor->Send(messages[index]) == std::stoll(fields[start + index + 1]),
                "header selection, save point and history match the original capture");
}
void PrepareSelection(EditorControl* editor, const std::vector<std::string>& fields) {
    const auto data = Unhex(fields[5]);
    TestEditor::ResetStream(editor, data.c_str(), std::stoll(fields[6]), std::stoll(fields[7]),
                            static_cast<Scintilla::EndOfLine>(std::stoi(fields[2])));
    if (fields[8] == "1") {
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, std::stoll(fields[6]));
        editor->Send(Message::SetRectangularSelectionCaret, std::stoll(fields[7]));
    }
    if (fields[3] == "protected_redo") {
        editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("redo"));
        editor->Send(Message::Undo);
        Require(editor->Send(Message::CanRedo), "protected reference scenario begins with a redo branch");
    }
    editor->Send(Message::SetReadOnly, fields[9] == "1");
}
void RejectedAnsiOpen(EditorFrame* frame, const std::filesystem::path& path, const std::vector<std::string>& fields,
                      unsigned nativePage) {
    auto* editor = frame->Editor();
    PrepareSelection(editor, fields);
    const auto text = TestEditor::Text(editor);
    const auto codec = frame->DocumentEncoding();
    const bool bom = frame->DocumentHasBom();
    const auto oldPath = frame->DocumentPath();
    const Message messages[] = {Message::GetAnchor,        Message::GetCurrentPos, Message::GetModify,
                                Message::CanUndo,          Message::CanRedo,       Message::GetReadOnly,
                                Message::GetSelectionMode, Message::GetSelections, Message::GetEOLMode,
                                Message::GetLexer};
    std::vector<intptr_t> before;
    for (const auto message : messages)
        before.push_back(editor->Send(message));
    const auto bytes = Unhex(fields[5]);
    TestFiles::WriteBytes(path, bytes);
    const auto opened = frame->Open(TestFiles::Filename(path));
    Require(opened.status == OpenStatus::Failed &&
                opened.error.Contains(wxString::Format("code page %u", nativePage)) &&
                opened.error.Contains("not migrated"),
            "unmigrated native ANSI is rejected explicitly by public Open");
    Require(TestEditor::Text(editor) == text && frame->DocumentEncoding() == codec && frame->DocumentHasBom() == bom &&
                frame->DocumentPath() == oldPath && TestFiles::Bytes(path) == bytes,
            "rejected native ANSI Open preserves text, codec, BOM, file identity and disk bytes");
    for (size_t index = 0; index < std::size(messages); ++index)
        Require(editor->Send(messages[index]) == before[index],
                "rejected native ANSI Open preserves selection, history, protection, EOL and language");
}
void OpenReferenceEncoding(EditorFrame* frame, const std::filesystem::path& directory,
                           const std::vector<std::string>& fields, bool promotedAnsi) {
    const auto& encoding = fields[1];
    const auto path = directory / "reference.txt";
    std::string bytes = "A\r\nB\r\nC";
    if (encoding == "utf8bom") bytes = "\xef\xbb\xbf" + bytes;
    if (encoding.starts_with("utf16")) bytes = std::string("A\0\r\0\n\0B\0\r\0\n\0C\0", 14);
    if (encoding == "utf16bom") bytes = "\xff\xfe" + bytes;
    TestFiles::WriteBytes(path, bytes);
    auto* editor = frame->Editor();
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSavePoint);
    const auto opened =
        frame->Open(TestFiles::Filename(path), encoding == "ansi"
                                                   ? std::nullopt
                                                   : std::optional{encoding.starts_with("utf16") ? FileEncoding::Utf16Le
                                                                   : encoding == "gbk"           ? FileEncoding::Gbk
                                                                                                 : FileEncoding::Utf8});
    if (opened.status != OpenStatus::Opened) {
#ifdef __WXMSW__
        std::fprintf(stderr, "Header host ACP=%u OEM=%u\n", GetACP(), GetOEMCP());
#else
        std::fprintf(stderr, "Header host locale=%s\n", wxLocale::GetSystemEncodingName().utf8_string().c_str());
#endif
        std::fprintf(stderr, "Header Open: %s/%s/%s/%s/%s status=%d error=%s\n", fields[0].c_str(), encoding.c_str(),
                     fields[2].c_str(), fields[3].c_str(), fields[4].c_str(), static_cast<int>(opened.status),
                     opened.error.utf8_string().c_str());
    }
    Require(opened.status == OpenStatus::Opened,
            "captured encoding identity opens through the application document interface");
    if (promotedAnsi)
        Require(frame->DocumentEncoding() == FileEncoding::Utf8 && !frame->DocumentHasBom(),
                "UTF-8 native context promotes the ANSI load choice to explicit UTF-8 without BOM");
}
void SelectReferenceLanguage(EditorFrame* frame, const std::string& language) {
    const char* label = language == "Python" ? "Python"
                        : language == "Cpp"  ? "C/C++"
                        : language == "Json" ? "JSON"
                                             : "Plain Text";
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", label);
    Require(id != wxNOT_FOUND, "captured language is available in the native menu");
    NativeSearch::Command(frame, id);
}
void ReferenceInsertion(EditorFrame* frame, const std::vector<std::string>& fields,
                        const std::vector<std::string>& expected) {
    auto* editor = frame->Editor();
    PrepareSelection(editor, fields);
    const auto font =
        editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
    const auto zoom = editor->Send(Message::GetZoom);
    const auto codec = frame->DocumentEncoding();
    const auto bom = frame->DocumentHasBom();
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const int id = InsertId(frame, fields[4] == "encoding" ? "Encoding Identifier" : "Script Shebang Line");
    Require(frame->GetMenuBar()->FindItem(id)->IsEnabled(),
            "original header entries stay enabled for protected and rectangular selections");
    NativeSearch::Command(frame, id);
    try {
        State(editor, expected, 10);
        editor->Send(Message::Undo);
        State(editor, expected, 16);
        editor->Send(Message::Redo);
        State(editor, expected, 22);
    } catch (...) {
        std::fprintf(stderr, "Header reference: %s/%s/%s/%s/%s\n", fields[0].c_str(), fields[1].c_str(),
                     fields[2].c_str(), fields[3].c_str(), fields[4].c_str());
        throw;
    }
    Require(editor->Send(Message::GetEOLMode) == std::stoi(fields[2]) && frame->DocumentEncoding() == codec &&
                frame->DocumentHasBom() == bom &&
                editor->Send(Message::StyleGetSizeFractional,
                             static_cast<uintptr_t>(Scintilla::StylesCommon::Default)) == font &&
                editor->Send(Message::GetZoom) == zoom,
            "header insertion retains encoding, BOM, EOL, font and zoom metadata");
}
void ReferenceCases(EditorFrame* frame, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ifstream stream(std::filesystem::path(HEADER_REFERENCE));
    Require(stream.good(), "original header fixtures are available");
    std::string row, previousEncoding, previousLanguage;
    std::getline(stream, row);
    // Original Edit.cpp 1106-1119 promotes ANSI to its explicit legacy-code-page
    // identity. The Linux port resolves a UTF-8 native locale to UTF-8 for both
    // native and legacy pages. Keep the Windows ANSI capture context unchanged.
#ifdef __WXMSW__
    constexpr bool nativeUtf8 = false;
    const unsigned nativePage = GetACP();
    const bool unmigratedNativeAnsi = nativePage != 936 && nativePage != CP_UTF8;
#else
    const bool nativeUtf8 = wxLocale::GetSystemEncodingName().CmpNoCase("UTF-8") == 0;
    constexpr unsigned nativePage = 0;
    constexpr bool unmigratedNativeAnsi = false;
#endif
    using ReferenceKey = std::array<std::string, 4>;
    const auto key = [](const std::vector<std::string>& fields) {
        return ReferenceKey{fields[0], fields[2], fields[3], fields[4]};
    };
    std::map<ReferenceKey, std::vector<std::string>> utf8References;
    const auto recordsStart = stream.tellg();
    while (std::getline(stream, row)) {
        auto fields = Fields(row);
        if (fields[1] == "utf8") utf8References.emplace(key(fields), std::move(fields));
    }
    stream.clear();
    stream.seekg(recordsStart);
    size_t count = 0;
    size_t rejected = 0;
    while (std::getline(stream, row)) {
        const auto fields = Fields(row);
        Require(fields.size() == 28, "original header record has all captured states");
        const auto& encoding = fields[1];
        const bool promotedAnsi = nativeUtf8 && encoding == "ansi";
        const auto& expected = promotedAnsi ? utf8References.at(key(fields)) : fields;
        if (unmigratedNativeAnsi && encoding == "ansi") {
            try {
                RejectedAnsiOpen(frame, directory / "unmigrated-ansi.txt", fields, nativePage);
            } catch (...) {
                std::fprintf(stderr, "Header refused Open: %s/%s/%s/%s/%s ACP=%u\n", fields[0].c_str(),
                             encoding.c_str(), fields[2].c_str(), fields[3].c_str(), fields[4].c_str(), nativePage);
                throw;
            }
            ++rejected;
            continue;
        }
        if (encoding != previousEncoding) {
            OpenReferenceEncoding(frame, directory, fields, promotedAnsi);
            previousEncoding = encoding;
            previousLanguage.clear();
        }
        if (previousLanguage != fields[0]) {
            SelectReferenceLanguage(frame, fields[0]);
            previousLanguage = fields[0];
        }
        ReferenceInsertion(frame, fields, expected);
        ++count;
    }
    Require(count + rejected == 1248 && rejected == (unmigratedNativeAnsi ? 216U : 0U),
            "all original records run in their supported insertion or explicit native-codec refusal context");
    frame->Editor()->Send(Message::SetReadOnly, 0);
    std::printf("PASS: %zu native original header reference cases; %zu native ANSI Open refusal records (ACP=%u)\n",
                count, rejected, nativePage);
}
void KeyCases(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "fresh UTF-8 document is available for the native Ctrl+F8 shortcut");
    TestEditor::ResetStream(editor, "AoldZ", 4, 1);
    NativeKeys::Shortcut(frame, WXK_F8, true);
    Require(TestEditor::Text(editor) == "AUTF-8Z", "native Ctrl+F8 invokes encoding insertion on the owned editor");
    editor->Send(Message::Undo);
    TestEditor::RequireSelection(editor, 4, 1, "native Ctrl+F8 Undo restores the reversed selection");
    for (const auto modifiers :
         {std::array{false, false, false}, std::array{false, false, true}, std::array{false, true, false},
          std::array{true, false, true}, std::array{true, true, false}}) {
        NativeKeys::Shortcut(frame, WXK_F8, modifiers[0], modifiers[1], modifiers[2]);
        Require(TestEditor::Text(editor) == "AoldZ" && editor->Send(Message::CanRedo),
                "F8 neighbors and extra modifiers preserve text and the existing redo branch");
    }
    editor->Send(Message::SetReadOnly, 1);
    NativeKeys::Shortcut(frame, WXK_F8, true);
    Require(TestEditor::Text(editor) == "AoldZ" && !editor->Send(Message::CanRedo),
            "protected Ctrl+F8 retains text and clears redo like the original ReplaceSel command");
    TestEditor::RequireSelection(editor, 1, 1, "protected Ctrl+F8 collapses the selection like the original command");
    editor->Send(Message::SetReadOnly, 0);
    TestEditor::ResetStream(editor, "");
    NativeKeys::Shortcut(frame, WXK_F8, true);
    Require(TestEditor::Text(editor) == "UTF-8", "native Ctrl+F8 is available in an empty document");
    std::puts("PASS: native Ctrl+F8 exact modifiers, neighbors, empty and protected documents");
}
void LossDecision(EditorFrame* frame, const std::filesystem::path& directory) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    const auto path = directory / "loss.txt";
    TestFiles::WriteBytes(path, "AoldZ");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "explicit CP936 document opens for the existing loss decision");
    TestEditor::ResetStream(editor, "😀old中", 4, 7);
    Encoding(frame);
    Require(TestEditor::Text(editor) == "😀GBK中", "encoding insertion preserves neighboring Unicode bytes");
    bool asked = false;
    Require(frame->Save([&asked] {
                     asked = true;
                     return EncodingLossChoice::Cancel;
                 }).status == SaveStatus::Cancelled &&
                asked && TestFiles::Bytes(path) == "AoldZ" && editor->Send(Message::GetModify),
            "header insertion retains the existing legacy loss decision and cancelled Save bytes");
}
class HeaderInsertApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        settings_.values.encoding.defaultEncoding = 0;
        settings_.values.encoding.loadASCIIasUTF8 = false;
        settings_.values.encoding.loadANSIasUTF8 = false;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor presents the owned header test editor");
                BasicEncoding(frame);
                BasicShebang(frame);
                ReferenceCases(frame, TestFiles::TemporaryDirectory("headers"));
                KeyCases(frame);
                LossDecision(frame, TestFiles::TemporaryDirectory("headers"));
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
    int result_ = 1;
    SettingsStore settings_;
};
} // namespace
wxIMPLEMENT_APP(HeaderInsertApp);
