#include "native_key_helpers.h"
#include "app/command_ids.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "editor_test_helpers.h"
#include <imm.h>
#include <commctrl.h>

namespace {
using Clock = std::chrono::steady_clock;
using Scintilla::Message;
constexpr auto PhaseTimeout = std::chrono::seconds(5);

std::string Composition(HIMC context, DWORD field) {
    const LONG bytes = ImmGetCompositionStringW(context, field, nullptr, 0);
    if (bytes <= 0) return {};
    std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    const LONG copied = ImmGetCompositionStringW(context, field, text.data(), static_cast<DWORD>(bytes));
    if (copied <= 0) return {};
    text.resize(static_cast<size_t>(copied) / sizeof(wchar_t));
    return wxString(text).utf8_string();
}

class ImeEscapeApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (!wxGetenv("WXNOTEPAD_TEST_PINYIN")) {
            std::puts("SKIP: set WXNOTEPAD_TEST_PINYIN=1 with native Microsoft Pinyin and an input desktop");
            result_ = 77;
            return true;
        }
        const int count = GetKeyboardLayoutList(0, nullptr);
        std::vector<HKL> layouts(static_cast<size_t>(count));
        GetKeyboardLayoutList(count, layouts.data());
        HKL chinese = nullptr;
        for (const auto layout : layouts)
            if (LOWORD(reinterpret_cast<uintptr_t>(layout)) == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
                chinese = layout;
        if (!chinese) {
            std::puts("SKIP: real composition requires an installed Simplified Chinese input method");
            result_ = 77;
            return true;
        }
        previousForeground_ = GetForegroundWindow();
        previousLayout_ = ActivateKeyboardLayout(chinese, 0);
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "ime.ini"));
        settings_->values.saveSettings = false;
        settings_->values.selectOption &= ~SelectOption_CopyPasteBufferAsFindText;
        requestedTechnology_ = argc == 2 ? wxAtoi(argv[1]) : 0;
        settings_->values.renderingTechnology = 1;
        std::printf("MACTYPE_FORCE_EXCLUDE=%s\n", wxGetenv("MACTYPE_FORCE_EXCLUDE") ? "set" : "unset");
        frame_ = new EditorFrame(settings_.get());
        frame_->SetTitle("wxnotepad real Pinyin Escape regression");
        editor_ = frame_->Editor();
        window_ = static_cast<HWND>(editor_->GetHandle());
        if (!SetWindowSubclass(window_, Observe, 1, reinterpret_cast<DWORD_PTR>(this))) return false;
        frame_->Show();
        NativeFocus::Activate(static_cast<HWND>(frame_->GetHandle()), window_);
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Step(); }, timer_.GetId());
        deadline_ = Clock::now() + PhaseTimeout;
        timer_.Start(30);
        return true;
    }
    int OnRun() override {
        if (result_ == 77) return result_;
        wxApp::OnRun();
        return result_;
    }

private:
    static LRESULT CALLBACK Observe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto* app = reinterpret_cast<ImeEscapeApp*>(data);
        if (message == WM_IME_COMPOSITION) {
            if (const auto context = ImmGetContext(window)) {
                if (lParam & GCS_COMPSTR) app->preedit_ = Composition(context, GCS_COMPSTR);
                if (lParam & GCS_RESULTSTR) app->committed_ = Composition(context, GCS_RESULTSTR);
                ImmReleaseContext(window, context);
            }
        } else if (message == WM_IME_ENDCOMPOSITION) {
            app->preedit_.clear();
            ++app->ended_;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    static void Require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }
    void Key(WORD code, bool control = false) {
        Require(GetForegroundWindow() == frame_->GetHandle() && GetFocus() == window_,
                "physical Pinyin input stays in the owned focused editor");
        for (const int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
            Require(!(GetAsyncKeyState(modifier) & 0x8000), "physical shortcut modifiers are released");
        std::vector<INPUT> input;
        const auto add = [&input](WORD key, DWORD flags) {
            INPUT event{};
            event.type = INPUT_KEYBOARD;
            event.ki.wVk = key;
            event.ki.dwFlags = flags;
            input.push_back(event);
        };
        if (control) add(VK_CONTROL, 0);
        add(code, 0);
        add(code, KEYEVENTF_KEYUP);
        if (control) add(VK_CONTROL, KEYEVENTF_KEYUP);
        Require(SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT)) == input.size(),
                "native Pinyin keyboard input is delivered");
    }
    void Pinyin(std::string_view spelling) {
        preedit_.clear();
        for (const char key : spelling)
            Key(static_cast<WORD>(key));
    }
    bool Spelling(std::string_view expected) const {
        auto text = preedit_;
        std::erase(text, '\'');
        return text == expected;
    }
    void Next() {
        ++phase_;
        deadline_ = Clock::now() + PhaseTimeout;
    }
    void Finish(int result) {
        result_ = result;
        timer_.Stop();
        if (const auto context = ImmGetContext(window_)) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(window_, context);
        }
        RemoveWindowSubclass(window_, Observe, 1);
        frame_->Destroy();
        if (previousLayout_) ActivateKeyboardLayout(previousLayout_, 0);
        if (IsWindow(previousForeground_)) SetForegroundWindow(previousForeground_);
        ExitMainLoop();
    }
    void Step() {
        try {
            Require(Clock::now() < deadline_, "real Pinyin Escape phase completes within its deadline");
            switch (phase_) {
            case 0: {
                if (GetForegroundWindow() != frame_->GetHandle() || GetFocus() != window_) return;
                // The native menu helper yields; do not re-enter this phase.
                timer_.Stop();
                NativeSearch::Command(frame_, frame_->GetMenuBar()->FindMenuItem(
                                                  "Settings", requestedTechnology_ == 0 ? "Legacy GDI" : "Direct2D"));
                NativeSearch::Command(frame_, frame_->GetMenuBar()->FindMenuItem("Settings", "Minimize wxnotepad"));
                Require(editor_->Send(Message::GetTechnology) == requestedTechnology_,
                        "the real Settings menu selects the requested live rendering technology");
                const auto context = ImmGetContext(window_);
                Require(context != nullptr, "owned editor has a native input context");
                ImmSetOpenStatus(context, TRUE);
                DWORD conversion = 0, sentence = 0;
                ImmGetConversionStatus(context, &conversion, &sentence);
                ImmSetConversionStatus(context, conversion | IME_CMODE_NATIVE, sentence);
                ImmReleaseContext(window_, context);
                Pinyin("NIHAO");
                Next();
                timer_.Start(30);
                break;
            }
            case 1:
                if (!Spelling("nihao")) return;
                std::puts("PASS: actual Microsoft Pinyin nihao preedit is active in the application frame");
                Key(VK_SPACE);
                Next();
                break;
            case 2:
                if (committed_.empty() || !ended_ || editor_->InputMethodComposing()) return;
                Require(TestEditor::Text(editor_) == committed_, "Pinyin result commits once in the application");
                ended_ = 0;
                Pinyin("CESHI");
                Next();
                break;
            case 3:
                if (!Spelling("ceshi")) return;
                Require(editor_->InputMethodComposing(), "real second Pinyin composition is active before Escape");
                {
                    const auto composition = preedit_;
                    const auto document = TestEditor::Text(editor_);
                    const auto layout = GetKeyboardLayout(0);
                    const auto context = ImmGetContext(window_);
                    Require(context != nullptr, "real Pinyin has its original native input context");
                    ImmReleaseContext(window_, context);
                    NativeSearch::Command(frame_, std::to_underlying(AppCommand::AlwaysOnTop));
                    NativeSearch::Command(frame_, std::to_underlying(AppCommand::AlwaysOnTop));
                    const auto after = ImmGetContext(window_);
                    const bool sameContext = after == context;
                    if (after) ImmReleaseContext(window_, after);
                    Require(editor_->InputMethodComposing() && preedit_ == composition && !ended_,
                            "topmost toggles preserve actual active Pinyin preedit");
                    Require(TestEditor::Text(editor_) == document,
                            "topmost toggles preserve committed and tentative document bytes during preedit");
                    Require(sameContext && GetKeyboardLayout(0) == layout,
                            "topmost toggles preserve native Pinyin context and keyboard layout");
                    Require(GetFocus() == window_, "topmost toggles preserve actual native Pinyin input focus");
                    std::puts("PASS: Always On Top preserves real Microsoft Pinyin composition and native focus");
                }
                std::puts("PASS: actual Microsoft Pinyin ceshi preedit follows the committed Chinese word");
                Key(VK_ESCAPE);
                Next();
                break;
            case 4:
                if (!ended_) return;
                Require(!frame_->IsIconized(), "candidate-cancelling Escape must not minimize the main frame");
                Require(TestEditor::Text(editor_) == committed_ && editor_->Send(Message::CanUndo),
                        "real candidate cancellation preserves the committed Chinese word and Undo history");
                std::puts("PASS: candidate Escape cancels only Pinyin composition with Minimize configured");
                Key('Z', true);
                Next();
                break;
            case 5:
                if (!TestEditor::Text(editor_).empty()) return;
                Require(!editor_->Send(Message::GetModify), "single native Undo removes only the committed word");
                Key(VK_ESCAPE);
                Next();
                break;
            case 6:
                if (!frame_->IsIconized()) return;
                std::puts("PASS: the next ordinary Escape still performs the configured native minimize");
                Finish(0);
                break;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: technology=%lld phase=%d composing=%d ended=%d iconized=%d: %s\n",
                         static_cast<long long>(editor_->Send(Message::GetTechnology)), phase_,
                         editor_->InputMethodComposing(), ended_, frame_->IsIconized(), error.what());
            Finish(1);
        }
    }
    std::unique_ptr<SettingsStore> settings_;
    EditorFrame* frame_ = nullptr;
    EditorControl* editor_ = nullptr;
    HWND window_ = nullptr;
    HWND previousForeground_ = nullptr;
    HKL previousLayout_ = nullptr;
    wxTimer timer_;
    Clock::time_point deadline_;
    std::string preedit_;
    std::string committed_;
    int phase_ = 0;
    int requestedTechnology_ = 0;
    int ended_ = 0;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ImeEscapeApp);
