#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/timer.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
using Scintilla::EndOfLine;
using Scintilla::Message;
using TestFiles::Bytes;
using TestFiles::Filename;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
class WarningDriver final : public wxTimer {
public:
    bool seen = false;
    int response = wxID_CANCEL;
    int selection = 0;
    int initialSelection = 0;
    bool always = true;
    bool defaultNo = false;
    wxString expectedCounts = "CR+LF: 1 line(s).\nLF: 1 line(s).\nCR: 1 line(s).";
    std::string error;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Inconsistent Line Endings" || !dialog->IsShown()) continue;
            seen = true;
            Stop();
            try {
                auto* mode = wxDynamicCast(wxWindow::FindWindowByName("Line ending conversion", dialog), wxChoice);
                auto* show =
                    wxDynamicCast(wxWindow::FindWindowByLabel("&Always show this dialog.", dialog), wxCheckBox);
                Require(mode && mode->GetCount() == 3 && mode->GetString(0) == "Windows (CR+LF)" &&
                            mode->GetString(1) == "Unix/macOS (LF)" && mode->GetString(2) == "Classic Mac OS (CR)",
                        "warning must offer the original ordered conversion choices");
                Require(mode->GetSelection() == initialSelection && show && show->GetValue(),
                        "warning must select the detected mode and enable Always show");
                Require(wxWindow::FindWindowByLabel(expectedCounts, dialog),
                        "warning must display exact original counts");
                Require(dialog->GetDefaultItem() &&
                            dialog->GetDefaultItem()->GetId() == (defaultNo ? wxID_CANCEL : wxID_OK),
                        "warning must select the original default button");
                mode->SetSelection(selection);
                show->SetValue(always);
                auto* button = dialog->FindWindow(response);
                Require(button, "warning must expose Yes and No buttons");
                wxCommandEvent event(wxEVT_BUTTON, response);
                event.SetEventObject(button);
                button->GetEventHandler()->ProcessEvent(event);
            } catch (const std::exception& failure) {
                error = failure.what();
                dialog->EndModal(wxID_CANCEL);
            }
            break;
        }
    }
    void Check() {
        Stop();
        Require(error.empty(), error.c_str());
        Require(seen, "default Open must show the original mixed-line-ending warning");
    }
};
void CountAndDefault(const std::filesystem::path& directory, SettingsStore& settings) {
    struct Sample {
        const char* name;
        const char* bytes;
        EndOfLine expected[3];
    };
    const Sample samples[] = {
        {"empty", "", {EndOfLine::CrLf, EndOfLine::Lf, EndOfLine::Cr}},
        {"single", "last", {EndOfLine::CrLf, EndOfLine::Lf, EndOfLine::Cr}},
        {"uniform-crlf", "one\r\ntwo\r\nlast", {EndOfLine::CrLf, EndOfLine::CrLf, EndOfLine::CrLf}},
        {"uniform-lf", "one\ntwo\nlast", {EndOfLine::Lf, EndOfLine::Lf, EndOfLine::Lf}},
        {"uniform-cr", "one\rtwo\rlast", {EndOfLine::Cr, EndOfLine::Cr, EndOfLine::Cr}},
        {"three-way-tie", "one\r\ntwo\nthree\rlast", {EndOfLine::CrLf, EndOfLine::Lf, EndOfLine::Cr}},
        {"crlf-lf-tie", "one\r\ntwo\nlast", {EndOfLine::CrLf, EndOfLine::Lf, EndOfLine::CrLf}},
        {"crlf-cr-tie", "one\r\ntwo\rlast", {EndOfLine::CrLf, EndOfLine::CrLf, EndOfLine::Cr}},
        {"lf-cr-tie", "one\ntwo\rthree\nfour\rlast", {EndOfLine::Lf, EndOfLine::Lf, EndOfLine::Cr}},
        {"predominant-crlf", "one\r\ntwo\r\nthree\nfour\rlast", {EndOfLine::CrLf, EndOfLine::CrLf, EndOfLine::CrLf}},
        {"predominant-lf", "one\r\ntwo\nthree\nfour\rlast", {EndOfLine::Lf, EndOfLine::Lf, EndOfLine::Lf}},
        {"predominant-cr", "one\r\ntwo\nthree\rfour\rlast", {EndOfLine::Cr, EndOfLine::Cr, EndOfLine::Cr}},
        {"adjacent-trailing", "\r\r\n\n", {EndOfLine::CrLf, EndOfLine::Lf, EndOfLine::Cr}},
    };
    for (int configured = 0; configured < 3; ++configured) {
        settings.values.defaultEOLMode = configured;
        settings.values.warnLineEndings = false;
        auto* frame = new EditorFrame(&settings);
        frame->ShowWithoutActivating();
        auto* editor = frame->Editor();
        for (const auto& sample : samples) {
            const auto input = directory / (std::string(sample.name) + ".txt");
            const auto output = directory / "saved.txt";
            Write(input, sample.bytes);
            WarningDriver disabled;
            disabled.Start(20);
            Require(frame->Open(Filename(input)).status == OpenStatus::Opened, "configured Open must load the fixture");
            disabled.Stop();
            Require(!disabled.seen && Text(editor) == sample.bytes && !editor->Send(Message::GetModify) &&
                        !editor->Send(Message::CanUndo),
                    "warning-disabled Open must retain bytes and clean history");
            const auto mode = sample.expected[configured];
            Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(mode),
                    "configured tie preference and original counting must choose insertion mode");
            editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
            editor->Send(Message::NewLine);
            const char* ending = mode == EndOfLine::CrLf ? "\r\n" : mode == EndOfLine::Lf ? "\n" : "\r";
            Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved &&
                        Bytes(output) == std::string(sample.bytes) + ending && Bytes(input) == sample.bytes &&
                        !editor->Send(Message::GetModify),
                    "Save As must preserve bytes and append the chosen ending");
        }
        Require(frame->New() &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(configured == 0   ? EndOfLine::CrLf
                                                                               : configured == 1 ? EndOfLine::Lf
                                                                                                 : EndOfLine::Cr),
                "New must restore the configured insertion default");
        frame->Destroy();
        wxYield();
        std::printf("PASS: configured default %d, all count/tie/uniform fixtures and saved bytes\n", configured);
    }
}
void WarningChoices(const std::filesystem::path& directory, SettingsStore& settings) {
    const auto input = directory / "mixed.txt";
    const auto output = directory / "normalized.txt";
    const std::string mixed = "one\r\ntwo\nthree\rlast";
    const char* normalized[] = {"one\r\ntwo\r\nthree\r\nlast", "one\ntwo\nthree\nlast", "one\rtwo\rthree\rlast"};
    for (int selected = 0; selected < 3; ++selected) {
        settings.values.defaultEOLMode = selected;
        settings.values.warnLineEndings = true;
        auto* frame = new EditorFrame(&settings);
        frame->ShowWithoutActivating();
        auto* editor = frame->Editor();
        const auto uniformPath = directory / "uniform.txt";
        Write(uniformPath, "one\ntwo\n");
        WarningDriver uniform;
        uniform.Start(20);
        Require(frame->Open(Filename(uniformPath)).status == OpenStatus::Opened,
                "uniform input must open with the warning preference enabled");
        uniform.Stop();
        Require(!uniform.seen && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(EndOfLine::Lf),
                "uniform endings must not warn and must select their actual insertion mode");
        Write(input, mixed);
        WarningDriver declined;
        declined.initialSelection = selected;
        declined.selection = (selected + 1) % 3;
        declined.Start(20);
        Require(frame->Open(Filename(input), FileEncoding::Utf8).status == OpenStatus::Opened,
                "No must still open the file");
        declined.Check();
        Require(Text(editor) == mixed && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "No must retain loaded mixed bytes, clean save point and empty Undo history");
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(selected == 0   ? EndOfLine::CrLf
                                                                           : selected == 1 ? EndOfLine::Lf
                                                                                           : EndOfLine::Cr),
                "No must ignore a changed selector and retain the detected insertion mode");
        Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == mixed,
                "Save after No must preserve independent mixed disk bytes");
        WarningDriver accepted;
        accepted.initialSelection = selected;
        accepted.selection = (selected + 1) % 3;
        accepted.response = wxID_OK;
        accepted.always = false;
        accepted.Start(20);
        Require(frame->Open(Filename(input), FileEncoding::Utf8).status == OpenStatus::Opened,
                "Yes must complete Open");
        accepted.Check();
        const int target = accepted.selection;
        Require(Text(editor) == normalized[target] && editor->Send(Message::GetModify) &&
                    editor->Send(Message::CanUndo) && Bytes(input) == mixed,
                "Yes must normalize as an undoable dirty edit without writing the source");
        const auto chosen = editor->Send(Message::GetEOLMode);
        editor->Send(Message::Undo);
        Require(Text(editor) == mixed && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && editor->Send(Message::GetEOLMode) == chosen,
                "one Undo must restore loaded bytes and save point while retaining selected insertion mode");
        editor->Send(Message::Redo);
        Require(editor->Send(Message::GetModify) && frame->Save().status == SaveStatus::Saved &&
                    Bytes(input) == normalized[target] && !editor->Send(Message::GetModify),
                "Redo and Save must write exact normalized bytes and establish the save point");
        editor->Send(Message::Undo);
        Require(editor->Send(Message::GetModify) && frame->SaveAs(Filename(output)).status == SaveStatus::Saved &&
                    Bytes(output) == mixed,
                "Undo after Save must dirty text and Save As must write original bytes");
        WarningDriver disabled;
        disabled.Start(20);
        Require(frame->Open(Filename(output)).status == OpenStatus::Opened,
                "mixed file must reopen after disabling warning");
        disabled.Stop();
        Require(!disabled.seen && Text(editor) == mixed && !editor->Send(Message::GetModify),
                "unchecking Always show must suppress subsequent warnings and preserve the loaded save point");
        frame->Destroy();
        wxYield();
        std::printf("PASS: warning choices %d, No/Yes, checkbox, Undo/Redo and independent saved bytes\n", selected);
    }
}
void UnknownFileDefaults(const std::filesystem::path& directory, SettingsStore& settings) {
    settings.values.defaultEOLMode = 0;
    settings.values.warnLineEndings = true;
    settings.values.encoding.defaultEncoding = 0;
    settings.values.encoding.loadANSIasUTF8 = false;
    settings.values.encoding.loadASCIIasUTF8 = false;
    bool nativeAnsi = false;
#ifdef __WXMSW__
    UINT legacy = GetACP();
    if (legacy == CP_UTF8) {
        constexpr LCTYPE legacyAnsi = 0x00000666;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, legacyAnsi | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&legacy), sizeof(legacy) / sizeof(WCHAR));
    }
    nativeAnsi = legacy == 936;
#endif
    // Native ANSI codecs outside the delivered CP936 codec remain unsupported.
    // The UTF-8 case still checks that unknown language alone does not imply No.
    if (!nativeAnsi) settings.values.encoding.defaultEncoding = -1;
    const std::string mixed = nativeAnsi ? "\xd6\xd0one\r\ntwo\nthree\rlast" : "one\r\ntwo\nthree\rlast";
    auto* frame = new EditorFrame(&settings);
    frame->ShowWithoutActivating();
    for (const auto* name : {"known.txt", "known.ini", "unknown.eol-unknown"}) {
        const auto input = directory / name;
        Write(input, mixed);
        WarningDriver warning;
        warning.defaultNo = nativeAnsi && std::string_view(name) == "unknown.eol-unknown";
        warning.Start(20);
        Require(frame->Open(Filename(input)).status == OpenStatus::Opened,
                "native-default fixture must open through the application");
        warning.Check();
        Require(!frame->Editor()->Send(Message::GetModify) && frame->Save().status == SaveStatus::Saved &&
                    Bytes(input) == mixed && !frame->Editor()->Send(Message::GetModify),
                "Save after declining native-default conversion must preserve exact disk bytes and the save point");
        if (std::string_view(name) == "unknown.eol-unknown") {
            WarningDriver reopened;
            reopened.always = false;
            reopened.Start(20);
            Require(frame->Open(Filename(input)).status == OpenStatus::Opened, "same-path unknown file must reopen");
            reopened.Check();
            WarningDriver disabled;
            disabled.Start(20);
            Require(frame->Open(Filename(input)).status == OpenStatus::Opened, "No may disable future warnings");
            disabled.Stop();
            Require(!disabled.seen && !frame->Editor()->Send(Message::GetModify),
                    "unchecking Always show on No must suppress the next warning");
        }
    }
    frame->Destroy();
    wxYield();
    std::puts(nativeAnsi ? "PASS: native ANSI known/unknown defaults, same-path default Yes and No checkbox"
                         : "PASS: UTF-8 known/unknown defaults, same-path default Yes and No checkbox");
}
class EolOpenApp final : public wxApp {
public:
    bool OnInit() override {
        CallAfter([this] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-eol-open");
            try {
                std::filesystem::create_directory(directory);
                SettingsStore settings;
                CountAndDefault(directory, settings);
                WarningChoices(directory, settings);
                UnknownFileDefaults(directory, settings);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            std::filesystem::remove_all(directory);
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(EolOpenApp);
