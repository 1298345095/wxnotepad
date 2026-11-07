#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("View", label);
    Require(id != wxNOT_FOUND, "original bookmark/history View command exists");
    NativeSearch::Command(frame, id);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
std::vector<std::vector<std::string>> LoadHistoryFixture(const std::filesystem::path& path) {
    std::ifstream source{path};
    Require(source.is_open(), "pinned native history fixture is available");
    std::vector<std::vector<std::string>> rows;
    for (std::string line; std::getline(source, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::istringstream input(line);
        for (std::string field; std::getline(input, field, '\t');)
            fields.push_back(field);
        Require(fields.size() == 8, "native fixture has eight independent observations");
        rows.push_back(std::move(fields));
    }
    return rows;
}
void CheckHistoryState(EditorControl* editor, const std::vector<std::string>& expected, const char* name) {
    for (const auto [index, message] : {std::pair{2, Message::GetChangeHistory},
                                        {3, Message::GetModify},
                                        {4, Message::CanUndo},
                                        {5, Message::CanRedo}})
        Require(editor->Send(message) == std::stoll(expected[index]), name);
}
void History(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto rows = LoadHistoryFixture(BOOKMARK_HISTORY_REFERENCE);
    size_t row = 0;
    const auto check = [&](const char* name) {
        Require(row < rows.size() && rows[row][0] == name, "native fixture sequence matches named operation");
        const auto& expected = rows[row++];
        const auto text = NativeSearch::Text(editor);
        std::string hex;
        constexpr char digits[] = "0123456789abcdef";
        for (const unsigned char ch : text) {
            hex += digits[ch >> 4];
            hex += digits[ch & 15];
        }
        Require(hex == expected[1], "document bytes agree with independent original fixture");
        CheckHistoryState(editor, expected, name);
        Require(editor->Send(Message::MarkerSymbolDefined, 0) == std::stoll(expected[6]),
                "shared bookmark style agrees with native original");
        std::ostringstream markers;
        for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line) {
            if (line) markers << ',';
            markers << (editor->Send(Message::MarkerGet, line) & 0x1E00001);
        }
        if (markers.str() != expected[7])
            std::fprintf(stderr, "%s marker got %s expected %s\n", name, markers.str().c_str(), expected[7].c_str());
        Require(markers.str() == expected[7], "history marker positions agree with native original");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("View", "Change History Marker")) ==
                    (editor->Send(Message::GetChangeHistory) != 0),
                "native check state follows history preference");
    };
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("aa\r\nbb\r\ncc"));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    check("disabled");
    Menu(frame, "Bookmark Margin");
    check("bookmark-visible");
    Menu(frame, "Bookmark Margin");
    Menu(frame, "Change History Marker");
    check("enabled-clean");
    editor->Send(Message::InsertText, 4, reinterpret_cast<intptr_t>("new\r\n"));
    check("insert");
    editor->Send(Message::SetSavePoint);
    check("saved");
    editor->Send(Message::DeleteRange, 9, 4);
    check("delete");
    editor->Send(Message::Undo);
    check("undo-delete");
    editor->Send(Message::Undo);
    check("undo-insert");
    editor->Send(Message::Redo);
    check("redo-insert");
    Menu(frame, "Change History Marker");
    check("disabled-with-undo");
    Menu(frame, "Change History Marker");
    check("enable-blocked");
    editor->Send(Message::Undo);
    check("undo-disabled");
    Menu(frame, "Change History Marker");
    check("enable-redo-only");
    editor->Send(Message::Redo);
    check("redo-enabled");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode"));
    editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("blocked"));
    check("read-only-insert");
    Menu(frame, "Change History Marker");
    check("read-only-disable");
    Menu(frame, "Change History Marker");
    check("read-only-enable");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode"));
    check("read-only-released");
    editor->Send(Message::SetSavePoint);
    Require(frame->New(), "native clean New succeeds");
    check("new");
    editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("abc"));
    check("modified-before-delete");
    editor->Send(Message::DeleteRange, 1, 1);
    check("deleted-modified");
    editor->Send(Message::SetSavePoint);
    check("saved-deletion");
    editor->Send(Message::Undo);
    check("undo-saved-deletion");
    editor->Send(Message::Redo);
    check("redo-saved-deletion");
    NativeSearch::Command(frame, wxID_NEW);
    check("new-after-reverted");
    Require(row == rows.size(), "all original native fixture states were checked");
    std::puts("PASS: original insert/delete/savepoint/Undo/Redo/read-only/New history fixtures and undo gate");
}
void MarginClick(EditorControl* editor, int line) {
    const int x =
        static_cast<int>(editor->Send(Message::GetMarginWidthN, 0) + editor->Send(Message::GetMarginWidthN, 1) / 2);
    const int y =
        static_cast<int>(editor->Send(Message::PointYFromPosition, 0, editor->Send(Message::PositionFromLine, line)) +
                         editor->Send(Message::TextHeight, line) / 2);
#ifdef __WXMSW__
    const auto window = static_cast<HWND>(editor->GetHandle());
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    for (const auto type : {GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE}) {
        auto* event = gdk_event_new(type);
        event->button.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.x = x;
        event->button.y = y;
        event->button.button = 1;
        if (type == GDK_BUTTON_RELEASE) event->button.state = GDK_BUTTON1_MASK;
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    wxYield();
}
void Geometry(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Menu(frame, "Change History Marker");
    Menu(frame, "No Highlight");
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("first\r\nsecond\r\nlast"));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::GotoLine, 1);
    NativeKeys::Shortcut(frame, WXK_F2, true);
    Require(editor->Send(Message::MarkerGet, 1) & 1, "native bookmark command shares marker zero");
    Menu(frame, "Bookmark Margin");
    const auto bookmarkCount = [&] {
        const int height = static_cast<int>(editor->Send(Message::TextHeight, 0));
        const int width = static_cast<int>(editor->Send(Message::GetMarginWidthN, 1));
        const int left = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0));
        const auto pixels = NativePaint::Read(editor, wxRect(left, height, width, height));
        return std::count(pixels.begin(), pixels.end(), wxColour(64, 128, 64));
    };
    Require(bookmarkCount() > 0, "actual native margin paints original green vertical bookmark");
    editor->Send(Message::SetSel, 3, 0);
    MarginClick(editor, 1);
    Require(!(editor->Send(Message::MarkerGet, 1) & 1), "native visible bookmark margin click toggles shared bookmark");
    Require(editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 0,
            "native bookmark margin clicks preserve caret and selection direction");
    Require(bookmarkCount() == 0, "native margin repaint removes bookmark pixels");
    editor->Send(Message::GotoLine, 1);
    for (const int zoom : {50, 100, 150, 200}) {
        editor->Send(Message::SetZoom, zoom);
        Require(editor->Send(Message::GetMarginWidthN, 1) == editor->Send(Message::TextHeight, 0) - 2,
                "bookmark margin follows independently measured native text height through zoom");
        Menu(frame, "Line Numbers");
        Require(editor->Send(Message::GetMarginWidthN, 1) == editor->Send(Message::TextHeight, 0) - 2,
                "line-number visibility leaves shared margin width tied to native text height");
    }
    editor->Send(Message::SetZoom, 100);
    NativeKeys::Shortcut(frame, WXK_F2, true);
    const auto sharedWidth = editor->Send(Message::GetMarginWidthN, 1);
    const auto foldingWidth = editor->Send(Message::GetMarginWidthN, 2);
    const auto textX = editor->Send(Message::PointXFromPosition, 0, 0);
    const auto bookmarkPixels = bookmarkCount();
    Require(foldingWidth > 0 && bookmarkPixels > 0, "original independent bookmark and folding margins are visible");
    Menu(frame, "Show Code Folding");
    Require(editor->Send(Message::GetMarginWidthN, 2) == 0 &&
                editor->Send(Message::GetMarginWidthN, 1) == sharedWidth &&
                editor->Send(Message::PointXFromPosition, 0, 0) == textX - foldingWidth &&
                bookmarkCount() == bookmarkPixels,
            "native folding toggle shifts text by its own margin while bookmark geometry and paint stay fixed");
    Menu(frame, "Show Code Folding");
    Require(editor->Send(Message::GetMarginWidthN, 2) == foldingWidth &&
                editor->Send(Message::PointXFromPosition, 0, 0) == textX && bookmarkCount() == bookmarkPixels &&
                NativeSearch::Text(editor) == "first\r\nsecond\r\nlast" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "restored folding margin retains bookmark pixels, text bytes and clean undo/save state");
    Menu(frame, "Change History Marker");
    Require(editor->Send(Message::GetMarginWidthN, 1) == sharedWidth && bookmarkCount() == bookmarkPixels,
            "enabling history alongside visible bookmarks preserves shared geometry and bookmark appearance");
    Menu(frame, "Bookmark Margin");
    Require(editor->Send(Message::GetMarginWidthN, 1) == editor->Send(Message::TextHeight, 0) - 2,
            "history alone keeps bookmark margin visible while bookmark preference stays hidden");
    Menu(frame, "Show Code Folding");
    Require(editor->Send(Message::GetMarginWidthN, 2) == 0 && editor->Send(Message::GetMarginWidthN, 1) == sharedWidth,
            "history-only shared margin remains visible with folding hidden");
    Menu(frame, "Show Code Folding");
    MarginClick(editor, 1);
    Require(editor->Send(Message::MarkerGet, 1) & 1, "history-only margin click preserves hidden bookmarks");
    const int textLeft =
        static_cast<int>(editor->Send(Message::GetMarginWidthN, 0) + editor->Send(Message::GetMarginWidthN, 1) +
                         editor->Send(Message::GetMarginWidthN, 2));
    const auto background = NativePaint::PixelReader(editor)(
        textLeft + 180, static_cast<int>(editor->Send(Message::TextHeight, 0) * 3 / 2));
    Require(background.Red() >= 214 && background.Red() <= 216 && background.Green() == 255 &&
                background.Blue() >= 214 && background.Blue() <= 216,
            "hidden bookmarks keep original green alpha40 background while history margin stays visible");
    editor->Send(Message::InsertText, 7, reinterpret_cast<intptr_t>("new"));
    const int height = static_cast<int>(editor->Send(Message::TextHeight, 0));
    const int width = static_cast<int>(editor->Send(Message::GetMarginWidthN, 1));
    const int left = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0));
    auto pixels = NativePaint::Read(editor, wxRect(left, height, width, height));
    Require(std::count(pixels.begin(), pixels.end(), wxColour(0, 128, 255)) > 0,
            "actual native history margin paints original modified blue marker");
    editor->Send(Message::SetSavePoint);
    pixels = NativePaint::Read(editor, wxRect(left, height, width, height));
    Require(std::count(pixels.begin(), pixels.end(), wxColour(0, 128, 0)) > 0,
            "native save-point repaint switches modified blue marker to original saved green");
    std::puts("PASS: native margin clicks, independent paint colors, shared width, zoom and line-number geometry");
}
void FileLifecycle(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Menu(frame, "Change History Marker");
    const auto rows = LoadHistoryFixture(BOOKMARK_HISTORY_FILES);
    const auto decode = [](const std::string& hex) {
        std::string bytes;
        for (size_t i = 0; i < hex.size(); i += 2)
            bytes += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
        return bytes;
    };
    const auto path = TestFiles::TemporaryDirectory("bookmark-history-utf16.txt");
    const auto filename = TestFiles::Filename(path);
    Require(rows.size() == 10, "all original file transitions were captured");
    TestFiles::WriteBytes(path, decode(rows[0][7]));
    size_t row = 0;
    const auto check = [&](const char* name) {
        Require(row < rows.size() && rows[row][0] == name, "original file transition order matches");
        const auto& expected = rows[row++];
        Require(NativeSearch::Text(editor) == decode(expected[1]),
                "file operation text agrees with original UTF-16 fixture");
        CheckHistoryState(editor, expected, name);
        std::ostringstream markers;
        for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line) {
            if (line) markers << ',';
            markers << (editor->Send(Message::MarkerGet, line) & 0x1E00001);
        }
        Require(markers.str() == expected[6], "native file operation marker locations agree with original");
        Require(TestFiles::Bytes(path) == decode(expected[7]),
                "independently read UTF-16LE BOM disk bytes agree with original");
    };
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_NO, filename);
        NativeSearch::Command(frame, wxID_OPEN);
        Require(driver.choosers == 1 && driver.errors == 0 && driver.prompts == 0, "real native Open loads fixture");
    }
    check("opened");
    Menu(frame, "Change History Marker");
    check("enabled-opened");
    editor->Send(Message::InsertText, 7, reinterpret_cast<intptr_t>("new"));
    check("inserted");
    NativeSearch::Command(frame, wxID_SAVE);
    check("native-saved");
    NativeKeys::Shortcut(frame, 'Z', true);
    check("undo-save");
    NativeKeys::Shortcut(frame, 'Y', true);
    check("redo-save");
    TestFiles::WriteBytes(path, decode(rows[6][7]));
    Require(frame->Open(filename).status == OpenStatus::Opened, "application same-path reload succeeds");
    check("native-reloaded");
    editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("x"));
    check("reloaded-insert");
    NativeKeys::Shortcut(frame, 'Z', true);
    check("reloaded-undo");
    NativeSearch::Command(frame, wxID_NEW);
    check("native-new");
    Require(row == rows.size(), "every original native file transition was checked");
    std::puts(
        "PASS: native Open/Save/New, native Undo/Redo, application reload and independently read UTF-16 disk bytes");
}
class BookmarkHistoryApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_BOOKMARK_DPI96")) {
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary 96-DPI owned window");
            std::puts("Complementary 96-DPI thread emulation; monitor scale unchanged");
        }
#endif
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "native view is presented");
                auto* editor = frame->Editor();
                std::printf("native bookmark/history dpi=%d\n", editor->GetDPI().y);
                Require(editor->Send(Message::GetMarginWidthN, 1) == 0 && editor->Send(Message::GetChangeHistory) == 0,
                        "original defaults hide bookmark margin and disable change history");
                Menu(frame, "Bookmark Margin");
                Require(editor->Send(Message::GetMarginWidthN, 1) == editor->Send(Message::TextHeight, 0) - 2 &&
                            editor->Send(Message::MarkerSymbolDefined, 0) ==
                                static_cast<intptr_t>(Scintilla::MarkerSymbol::VerticalBookmark),
                        "native menu shows original vertical bookmark at text-height minus two width");
                NativeKeys::Shortcut(frame, 'M', true, false, true);
                Require(editor->Send(Message::GetMarginWidthN, 1) == 0 &&
                            editor->Send(Message::MarkerSymbolDefined, 0) ==
                                static_cast<intptr_t>(Scintilla::MarkerSymbol::Background),
                        "native Ctrl+Shift+M restores background bookmark and hides margin");
                std::puts("PASS: native bookmark margin menu/shortcut and original geometry/styles");
                History(frame);
                Geometry(frame);
                FileLifecycle(frame);
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
};
} // namespace
wxIMPLEMENT_APP(BookmarkHistoryApp);
