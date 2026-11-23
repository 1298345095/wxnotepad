#include "editor_test_helpers.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "file_command_driver.h"
#include "file_helpers.h"
#include "app/localization.h"
#include <wx/filename.h>
#include <wx/ffile.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <array>
#include <memory>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
constexpr uint16_t EnglishLanguage = 1033;
constexpr uint16_t SimplifiedChineseLanguage = 2052;
constexpr uint16_t TraditionalChineseLanguage = 1028;
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}
wxString NativeTitle(EditorFrame* frame) {
#ifdef __WXMSW__
    const auto window = static_cast<HWND>(frame->GetHandle());
    std::wstring title(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    return wxString(title.c_str());
#elif defined(__WXGTK__)
    return wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(frame->GetHandle())));
#else
    return frame->GetTitle();
#endif
}
void Menu(EditorFrame* frame, const char* label) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Settings", label);
    Require(id != wxNOT_FOUND, "Settings offers original window title commands");
    NativeSearch::Command(frame, id);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    wxYield();
}
void NativeRadioChoices(EditorFrame* frame) {
    for (const auto* label : {"File Name Only", "File Name and Directory", "Full Path Name", "Text Excerpt"}) {
        const auto* item = frame->GetMenuBar()->FindItem(frame->GetMenuBar()->FindMenuItem("Settings", label));
        Require(item, "title choice exists");
#ifdef __WXMSW__
        MENUITEMINFOW information{};
        information.cbSize = sizeof(information);
        information.fMask = MIIM_FTYPE;
        Require(GetMenuItemInfoW(static_cast<HMENU>(item->GetMenu()->GetHMenu()), static_cast<UINT>(item->GetMSWId()),
                                 FALSE, &information) &&
                    (information.fType & MFT_RADIOCHECK),
                "original title choices use native mutually exclusive radio marks");
#elif defined(__WXGTK__)
        Require(gtk_check_menu_item_get_draw_as_radio(GTK_CHECK_MENU_ITEM(item->GetMenuItem())),
                "original title choices use native mutually exclusive radio marks");
#endif
    }
}
void Modes(EditorFrame* frame) {
    const auto path = TestFiles::Filename(TestFiles::TemporaryDirectory() / TestFiles::Utf8Path("title 中文.txt"));
    wxFFile file(path, "wb");
    Require(file.IsOpened() && file.Write("document") && file.Close(), "write title fixture");
    Require(frame->Open(path).status == OpenStatus::Opened, "open named document");
    const wxFileName name(path);
    Require(NativeTitle(frame) == name.GetFullName() + " [" + name.GetPath() + "] - wxnotepad",
            "default actual title shows file name then directory");
    Menu(frame, "File Name Only");
    Require(NativeTitle(frame) == name.GetFullName() + " - wxnotepad", "name only applies immediately");
    Menu(frame, "Full Path Name");
    Require(NativeTitle(frame) == path + " - wxnotepad", "full native path applies immediately");
    Menu(frame, "File Name and Directory");
    Require(NativeTitle(frame) == name.GetFullName() + " [" + name.GetPath() + "] - wxnotepad",
            "name and directory restores original title");
    wxRemoveFile(path);
    const auto root = TestFiles::Filename(TestFiles::TemporaryDirectory("title-path"));
    const wxString directory = root + wxFILE_SEP_PATH + wxString::FromUTF8(std::string(175, 'a'));
    const wxFileName longPath(directory, wxString::FromUTF8("文档.txt"));
    Require(wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL), "create long native Unicode path");
    wxFFile longFile(longPath.GetFullPath(), "wb");
    Require(longFile.Write("long path") && longFile.Close(), "write long path fixture");
    Require(frame->Open(longPath.GetFullPath()).status == OpenStatus::Opened, "open long Unicode path");
    Menu(frame, "Full Path Name");
    Require(NativeTitle(frame) == longPath.GetFullPath() + " - wxnotepad", "long native title is not truncated");
    Menu(frame, "File Name Only");
    Require(NativeTitle(frame) == wxString::FromUTF8("文档.txt - wxnotepad"),
            "long directory keeps Unicode file identity");
    Menu(frame, "File Name and Directory");
    wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
}
std::string Unhex(const std::string& value) {
    std::string result;
    for (size_t index = 0; index < value.size(); index += 2)
        result += static_cast<char>(std::stoul(value.substr(index, 2), nullptr, 16));
    return result;
}
auto State(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    std::array<intptr_t, 11> state{};
    size_t index = 0;
    for (const auto message : {GetAnchor, GetCurrentPos, GetSelectionMode, GetSelections, GetMainSelection,
                               GetRectangularSelectionAnchorVirtualSpace, GetRectangularSelectionCaretVirtualSpace,
                               GetModify, CanUndo, CanRedo, GetReadOnly})
        state[index++] = editor->Send(message);
    return std::pair{TestEditor::Text(frame), state};
}
void Excerpts(EditorFrame* frame) {
    using enum Scintilla::Message;
    Require(frame->NewDocument(), "create excerpt fixture");
    auto* editor = frame->Editor();
    std::ifstream reference(std::filesystem::path(WINDOW_TITLE_REFERENCE));
    Require(reference.is_open(), "open independently captured original excerpt fixtures");
    std::string line;
    size_t fixtureIndex = 0;
    while (std::getline(reference, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        ++fixtureIndex;
        std::istringstream row(line);
        std::array<std::string, 6> columns;
        for (auto& column : columns)
            Require(static_cast<bool>(std::getline(row, column, '\t')), "complete original excerpt row");
        const auto text = Unhex(columns[1]);
        TestEditor::ResetStream(editor, text.c_str(), std::stoll(columns[2]), std::stoll(columns[3]));
        if (columns[4] == "1") {
            editor->Send(SetSelectionMode, 1);
            editor->Send(SetRectangularSelectionAnchor, 0);
            editor->Send(SetRectangularSelectionCaret, 5);
            editor->Send(SetRectangularSelectionAnchorVirtualSpace, 2);
            editor->Send(SetRectangularSelectionCaretVirtualSpace, 3);
        }
        const auto before = State(frame);
        Menu(frame, "Text Excerpt");
        const auto expected = wxString::FromUTF8(Unhex(columns[5])) + " - wxnotepad";
        if (NativeTitle(frame) != expected)
            std::fprintf(stderr, "excerpt row %zu: title mismatch (actual length=%zu, expected length=%zu)\n",
                         fixtureIndex, NativeTitle(frame).length(), expected.length());
        Require(NativeTitle(frame) == expected, "actual title equals independent original excerpt");
        Require(State(frame) == before, "title command preserves exact text, directed selections and undo/redo");
        const bool excerpt = columns[0] != "empty" && columns[0] != "rectangle";
        Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Settings", "Text Excerpt")) ==
                    excerpt,
                "empty and rectangular selection revert to the path mode check");
    }
}
void Lifecycle(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "captured text", 0, 8);
    Menu(frame, "Text Excerpt");
    editor->Send(SetSel, 0, 0);
    editor->Send(InsertText, 0, reinterpret_cast<intptr_t>("dirty "));
    wxYield();
    Require(NativeTitle(frame) == "* \"captured\" - wxnotepad", "later edits and selection movement retain capture");
    editor->Send(Undo);
    wxYield();
    Require(NativeTitle(frame) == "\"captured\" - wxnotepad", "Undo removes only the modified marker");
    editor->Send(Redo);
    wxYield();
    Require(NativeTitle(frame) == "* \"captured\" - wxnotepad", "Redo restores modified marker without recapture");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode"));
    Require(NativeTitle(frame) == "* \"captured\" (Read Only Mode) - wxnotepad",
            "editor protection retains captured title and existing annotation");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode"));
    editor->Send(SetSavePoint);
    Require(frame->NewDocument(), "New succeeds without discarding unsaved content");
    Require(NativeTitle(frame) == "Untitled - wxnotepad", "New clears a transient captured excerpt");
    const auto first = wxFileName::CreateTempFileName("excerpt first ");
    const auto second = wxFileName::CreateTempFileName("excerpt second ");
    const auto saved = wxFileName::CreateTempFileName("excerpt saved ");
    for (const auto& path : {first, second}) {
        wxFFile file(path, "wb");
        Require(file.Write("captured text") && file.Close(), "write excerpt lifecycle input");
    }
    Require(frame->Open(first).status == OpenStatus::Opened, "open excerpt lifecycle file");
    editor->Send(SetSel, 0, 8);
    Menu(frame, "Text Excerpt");
    Require(frame->Revert().status == OpenStatus::Opened && NativeTitle(frame) == "\"captured\" - wxnotepad",
            "Revert preserves the captured excerpt");
    Require(frame->Open(first).status == OpenStatus::Opened && NativeTitle(frame) == "\"captured\" - wxnotepad",
            "opening the current file preserves its excerpt");
    {
        const auto before = State(frame);
        NativeDialogs::FileCommandDriver chooser(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Open..."));
        Require(chooser.choosers == 1 && NativeTitle(frame) == "\"captured\" - wxnotepad" && State(frame) == before,
                "Cancel Open preserves the capture, exact document and history");
    }
    {
        NativeDialogs::FileCommandDriver chooser(NativeDialogs::FileDialogProfile::Open, wxID_OK, first);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Open..."));
        Require(chooser.choosers == 1 && !NativeTitle(frame).StartsWith("\""),
                "native Open chooser clears the excerpt even when choosing the same file");
    }
    Require(frame->Open(second).status == OpenStatus::Opened && !NativeTitle(frame).StartsWith("\""),
            "opening another file clears the excerpt");
    editor->Send(SetSel, 0, 8);
    Menu(frame, "Text Excerpt");
    Require(frame->Save().status == SaveStatus::Saved && NativeTitle(frame) == "\"captured\" - wxnotepad",
            "ordinary Save retains capture and normal saved-document identity");
    {
        const auto before = State(frame);
        NativeDialogs::FileCommandDriver chooser(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Save As..."));
        Require(chooser.choosers == 1 && NativeTitle(frame) == "\"captured\" - wxnotepad" && State(frame) == before,
                "Cancel Save As preserves the capture, exact document and history");
    }
    const int readOnlyFile = frame->GetMenuBar()->FindMenuItem("File", "Read Only File");
    NativeSearch::Command(frame, readOnlyFile);
    Require(NativeTitle(frame) == "\"captured\" (Read Only File) - wxnotepad",
            "native file protection retains excerpt and existing annotation");
    NativeSearch::Command(frame, readOnlyFile);
    Require(frame->SaveAs(saved).status == SaveStatus::Saved && !NativeTitle(frame).StartsWith("\""),
            "Save As clears the captured excerpt after success");
    for (const auto& path : {first, second, saved})
        wxRemoveFile(path);
}
void Localized(EditorFrame* frame, bool traditional) {
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const auto caption = wxString::FromUTF8(traditional ? "視窗標題顯示(&Y)" : "窗口标题显示(&Y)");
    const std::array<const char*, 4> captions =
        traditional
            ? std::array<const char*, 4>{"只檔名(&F)", "檔案名稱與資料夾(&D)", "完整路徑名稱(&P)", "選取的文字(&T)"}
            : std::array<const char*, 4>{"仅文件名(&F)", "文件名和目录(&D)", "完整路径名称(&P)", "文本摘选(&T)"};
    wxMenu* title = nullptr;
    for (auto* item : frame->GetMenuBar()->GetMenu(5)->GetMenuItems())
        if (item->GetItemLabel() == caption) title = item->GetSubMenu();
    if (!title) {
        std::fprintf(stderr, "expected title menu '%s'\n", caption.utf8_str().data());
        for (auto* item : frame->GetMenuBar()->GetMenu(5)->GetMenuItems())
            std::fprintf(stderr, "observed '%s'\n", item->GetItemLabel().utf8_str().data());
    }
    Require(title && title->GetMenuItemCount() == 4, "localized native title submenu matches pinned resources");
    for (size_t index = 0; index < captions.size(); ++index) {
        const auto* item = title->FindItemByPosition(index);
        Require(item->GetItemLabel() == wxString::FromUTF8(captions[index]) && item->IsChecked() == (index == 1),
                "localized title commands retain original order, wording and default check");
    }
    Require(NativeTitle(frame) == wxString::FromUTF8("未命名 - wxnotepad"),
            "actual untitled title keeps existing translation");
    TestEditor::ResetStream(frame->Editor(), "  中文\t😀\r\n  café  ", 24, 0);
    NativeSearch::Command(frame, title->FindItemByPosition(3)->GetId());
    const auto quoted = wxString::FromUTF8(traditional ? "「中文 😀 café 」" : "“中文 😀 café ”");
    Require(NativeTitle(frame) == quoted + " - wxnotepad", "actual title uses independently captured locale quotes");
    const int protection =
        frame->GetMenuBar()->FindMenuItem(wxGetTranslation("&File"), wxGetTranslation("&Read Only Mode\tF10"));
    NativeSearch::Command(frame, protection);
    Require(NativeTitle(frame) ==
                quoted + wxString::FromUTF8(traditional ? " (唯讀模式)" : " (只读模式)") + " - wxnotepad",
            "localized title keeps existing protection annotation");
    NativeSearch::Command(frame, protection);
    for (size_t mode = 0; mode < 3; ++mode) {
        NativeSearch::Command(frame, title->FindItemByPosition(mode)->GetId());
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(NativeTitle(frame) == wxString::FromUTF8("未命名 - wxnotepad"),
                "localized path command clears excerpt immediately");
        for (size_t index = 0; index < 4; ++index)
            Require(title->FindItemByPosition(index)->IsChecked() == (index == mode),
                    "native title menu has exactly one selected mode");
    }
}
void NativeAnsi(EditorFrame* frame) {
#ifdef __WXMSW__
    using enum Scintilla::Message;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "ANSI"));
    const bool chinese = GetACP() == 936;
    std::string text;
    for (size_t index = 0; index < 200; ++index)
        text += chinese ? "中" : "a";
    TestEditor::ResetStream(frame->Editor(), text.c_str(), 0, text.size());
    const auto before = State(frame);
    Menu(frame, "Text Excerpt");
    wxString expected;
    // Original ACP936 capture: 255 native bytes yields 127 Chinese characters.
    // Other host pages use the independently captured 200-ASCII excerpt rule.
    for (size_t index = 0; index < (chinese ? 127 : 124); ++index)
        expected += wxUniChar(chinese ? 0x4e2d : 'a');
    if (!chinese) expected += "...";
    Require(NativeTitle(frame) == "\"" + expected + "\" - wxnotepad", "native ANSI excerpt keeps original byte bound");
    Require(State(frame) == before, "native ANSI excerpt preserves exact text, directed selection and history");
#else
    (void)frame;
#endif
}
} // namespace

class WindowTitleApp final : public wxApp {
public:
    bool OnInit() override {
        const bool localized = argc == 3 && wxString(argv[1]) == "--localized";
        const bool traditional = localized && wxString(argv[2]) == "zh_TW";
        const bool nativeAnsi = argc == 2 && wxString(argv[1]) == "--native-ansi";
        AppLocalization::Initialize(localized ? (traditional ? TraditionalChineseLanguage : SimplifiedChineseLanguage)
                                              : EnglishLanguage);
        frame_ = std::make_unique<EditorFrame>();
        auto* frame = frame_.get();
        frame->Show();
        CallAfter([this, frame, localized, traditional, nativeAnsi] {
            try {
                if (nativeAnsi) {
                    NativeAnsi(frame);
                } else if (localized) {
                    Localized(frame, traditional);
                } else {
                    NativeRadioChoices(frame);
                    Modes(frame);
                    Excerpts(frame);
                    Lifecycle(frame);
                }
                std::puts("PASS: native original window title modes");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                status_ = 1;
            }
            frame_.release()->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return status_;
    }

private:
    std::unique_ptr<EditorFrame> frame_;
    int status_ = 0;
};
wxIMPLEMENT_APP(WindowTitleApp);
