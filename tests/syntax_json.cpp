#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "native_dialog_helpers.h"
#include <SciLexer.h>
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include <wx/wx.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Paint(EditorFrame* frame) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        frame->Editor()->Refresh();
        frame->Editor()->Update();
        wxYield();
        if (frame->Editor()->Send(Message::GetEndStyled) >= frame->Editor()->Send(Message::GetLength)) return;
        wxMilliSleep(10);
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "native menu command must be handled");
    Paint(frame);
}
void Choose(EditorFrame* frame, const wxString& label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Language", label);
    Require(id != wxNOT_FOUND, "Language menu must offer JSON for any document");
    Command(frame, id);
    auto* menu = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Language"));
    for (const auto* item : menu->GetMenuItems()) {
        if (item->IsRadio()) Require(item->IsChecked() == (item->GetId() == id), "language choices must be exclusive");
    }
}
void Delete(EditorControl* editor) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = WXK_DELETE;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
class FileDriver final : public wxTimer {
public:
    explicit FileDriver(wxString destination) : destination_(std::move(destination)) { Start(100); }
    int choosers = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<FileDriver*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                const wxString name(title);
                if (IsWindowVisible(window) && (name == "Open file" || name == "Save As") && !driver.stage_++) {
                    ++driver.choosers;
                    NativeDialogs::ChooseFile(window, driver.destination_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            if (!stage_++) {
                ++choosers;
                NativeDialogs::ChooseFile(widget, destination_);
            } else if (stage_ == 2 && !destination_.empty())
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
        }
        g_list_free(windows);
#endif
    }

private:
    wxString destination_;
    int stage_ = 0;
};
void FileCommand(EditorFrame* frame, int command, const std::filesystem::path& path) {
    FileDriver driver(Filename(path));
    Command(frame, command);
    driver.Stop();
    Require(driver.choosers == 1, "file command must use its native chooser");
}
void Style(EditorFrame* frame, const std::string& text, const char* token, int style) {
    const auto position = text.find(token);
    Require(position != std::string::npos, "fixture token must exist");
    const auto actual = frame->Editor()->Send(Message::GetStyleIndexAt, position);
    if (actual != style) {
        std::fprintf(stderr, "token=%s position=%zu expected=%d actual=%zd\n", token, position, style, actual);
        throw std::runtime_error("JSON token must have the pinned lexer style");
    }
}
class JsonApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-json-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const std::string source = "{\r\n  \"name\": \"中文\\nvalue\", \"count\": -12.5e2,\r\n"
                                           "  \"flags\": [true, false, null]\r\n}\r\n";
                const auto path = directory / "fixture.JsOn";
                {
                    std::ofstream output(path, std::ios::binary);
                    output << source;
                }
                FileCommand(frame, wxID_OPEN, path);
                auto* editor = frame->Editor();
                Require(frame->GetStatusBar()->GetStatusText().Contains("JSON") &&
                            frame->GetStatusBar()->GetStatusText().Contains("UTF-8") &&
                            !frame->GetStatusBar()->GetStatusText().Contains("override"),
                        "mixed-case .json Open must select JSON automatically and retain encoding status");
                Style(frame, source, "name", SCE_JSON_PROPERTYNAME);
                Style(frame, source, "中文", SCE_JSON_STRING_DQ);
                Style(frame, source, "\\n", SCE_JSON_ESCAPECHAR);
                Style(frame, source, "12.5", SCE_JSON_NUMBER);
                for (const auto* keyword : {"true", "false", "null"})
                    Style(frame, source, keyword, SCE_JSON_KEYWORD);
                for (const auto* punctuation : {"{", ":", ",", "-", "[", "]", "}"})
                    Style(frame, source, punctuation, SCE_JSON_OPERATOR);
                Require(editor->Send(Message::StyleGetFore, SCE_JSON_PROPERTYNAME) == 0x0060A4 &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_STRING_DQ) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_ESCAPECHAR) == 0xC08000 &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_KEYWORD) == 0xFF0000 &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_NUMBER) == 0x0000FF &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_OPERATOR) == 0xB000B0,
                        "JSON styles must use the pinned default light palette");
                for (const int style : {SCE_JSON_PROPERTYNAME, SCE_JSON_STRING_DQ, SCE_JSON_ESCAPECHAR,
                                        SCE_JSON_KEYWORD, SCE_JSON_NUMBER, SCE_JSON_OPERATOR}) {
                    Require(editor->Send(Message::StyleGetSizeFractional, style) ==
                                    editor->Send(Message::StyleGetSizeFractional, 32) &&
                                editor->Send(Message::StyleGetBack, style) == 0xFFFFFF &&
                                !editor->Send(Message::StyleGetBold, style),
                            "JSON styles must inherit native size and use the light background");
                }
                Command(frame, wxID_SAVE);
                Require(Bytes(path) == source && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "automatic JSON styling and Save must preserve exact UTF-8 CRLF bytes and clean history");
                std::puts("PASS: native mixed-case JSON Open, token styles, palette and exact saved bytes");
                editor->Send(Message::SetSel, 18, 4);
                Choose(frame, "Plain Text");
                Require(editor->Send(Message::GetStyleIndexAt, source.find("name")) == 0 &&
                            editor->Send(Message::StyleGetFore, SCE_JSON_ESCAPECHAR) == 0 &&
                            editor->Send(Message::GetAnchor) == 18 && editor->Send(Message::GetCurrentPos) == 4 &&
                            !editor->Send(Message::GetModify),
                        "leaving JSON must clear its styles without changing selection or save point");
                Choose(frame, "C/C++");
                Style(frame, source, "name", SCE_C_STRING);
                Choose(frame, "JSON");
                Style(frame, source, "name", SCE_JSON_PROPERTYNAME);
                const auto plain = directory / "manual.txt";
                FileCommand(frame, wxID_SAVEAS, plain);
                Require(Bytes(plain) == source && frame->GetStatusBar()->GetStatusText().Contains("JSON") &&
                            frame->GetStatusBar()->GetStatusText().Contains("override"),
                        "explicit JSON must survive native Save As to .txt with exact bytes");
                Choose(frame, "Automatic");
                Require(editor->Send(Message::GetLexer) == SCLEX_NULL,
                        "Automatic must follow the .txt path immediately");
                const auto automatic = directory / "automatic.JSON";
                FileCommand(frame, wxID_SAVEAS, automatic);
                Style(frame, source, "name", SCE_JSON_PROPERTYNAME);
                Require(Bytes(automatic) == source && !frame->GetStatusBar()->GetStatusText().Contains("override"),
                        "Automatic Save As must adopt JSON without creating an override");
                Command(frame, wxID_NEW);
                Require(editor->Send(Message::GetLexer) == SCLEX_NULL, "New must reset JSON to automatic Plain Text");
                Choose(frame, "JSON");
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                Paint(frame);
                Style(frame, source, "true", SCE_JSON_KEYWORD);
                const auto escape = source.find("\\n");
                editor->Send(Message::SetSel, escape, static_cast<intptr_t>(escape));
                Delete(editor);
                Paint(frame);
                auto edited = source;
                edited.erase(escape, 1);
                Require(Text(editor) == edited && editor->Send(Message::GetStyleIndexAt, escape) == SCE_JSON_STRING_DQ,
                        "deleting an escape must immediately restyle it as string content");
                Command(frame, wxID_UNDO);
                Style(frame, source, "\\n", SCE_JSON_ESCAPECHAR);
                Command(frame, wxID_REDO);
                Require(Text(editor) == edited && editor->Send(Message::GetStyleIndexAt, escape) == SCE_JSON_STRING_DQ,
                        "Redo must restore the escape edit and its syntax");
                Command(frame, wxID_UNDO);
                const auto quote = source.find("value\"") + 5;
                editor->Send(Message::SetSel, quote, static_cast<intptr_t>(quote));
                Delete(editor);
                Paint(frame);
                edited = source;
                edited.erase(quote, 1);
                Require(Text(editor) == edited && editor->Send(Message::GetStyleIndexAt, quote) == SCE_JSON_STRING_DQ,
                        "a removed closing quote must restyle punctuation as an incomplete string");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == source &&
                            editor->Send(Message::GetStyleIndexAt, quote + 1) == SCE_JSON_OPERATOR,
                        "Undo must restore the quote and punctuation syntax");
                Command(frame, wxID_REDO);
                Require(Text(editor) == edited && editor->Send(Message::GetStyleIndexAt, quote) == SCE_JSON_STRING_DQ,
                        "Redo must restore the incomplete string syntax");
                const auto incomplete = directory / "incomplete.txt";
                Require(frame->SaveAs(Filename(incomplete)).status == SaveStatus::Saved && Bytes(incomplete) == edited,
                        "incomplete JSON must remain freely editable and save its exact bytes");
                Choose(frame, "Plain Text");
                Require(editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                        "leaving JSON must preserve edit history and the saved incomplete document");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == source && editor->Send(Message::CanRedo) && editor->Send(Message::GetModify),
                        "view-only language switching must leave quote edit Undo and Redo intact");
                std::puts("PASS: native JSON choice, Save As rules, scheme reset, escape/quote edits and Undo/Redo");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(directory);
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
wxIMPLEMENT_APP(JsonApp);
