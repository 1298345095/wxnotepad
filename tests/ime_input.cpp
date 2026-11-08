#include "editor_test_helpers.h"
#include <wx/wx.h>
#include <imm.h>
#include <commctrl.h>
#include <chrono>
#include <vector>

namespace {
using Scintilla::Message;
using Clock = std::chrono::steady_clock;

std::string Composition(HIMC context, DWORD field) {
    const LONG bytes = ImmGetCompositionStringW(context, field, nullptr, 0);
    if (bytes <= 0) return {};
    std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    const LONG copied = ImmGetCompositionStringW(context, field, text.data(), static_cast<DWORD>(bytes));
    if (copied <= 0) return {};
    text.resize(static_cast<size_t>(copied) / sizeof(wchar_t));
    return wxString(text).utf8_string();
}

class ImeInputApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (!wxGetenv("WXNOTEPAD_TEST_PINYIN")) {
            std::puts("SKIP: set WXNOTEPAD_TEST_PINYIN=1 with native Pinyin and an available input desktop");
            result_ = 77;
            return true;
        }
        const int count = GetKeyboardLayoutList(0, nullptr);
        std::vector<HKL> layouts(static_cast<size_t>(count));
        GetKeyboardLayoutList(count, layouts.data());
        HKL chinese = nullptr;
        for (const auto layout : layouts) {
            if (LOWORD(reinterpret_cast<uintptr_t>(layout)) == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) {
                chinese = layout;
                break;
            }
        }
        if (!chinese) {
            std::puts("SKIP: native composition requires an installed Simplified Chinese input method");
            result_ = 77;
            return true;
        }
        previous_ = GetForegroundWindow();
        ActivateKeyboardLayout(chinese, 0);
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad native IME composition regression", wxDefaultPosition,
                             wxSize(440, 300));
        editor_ = new EditorControl(frame_);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(editor_, 1, wxEXPAND);
        focusTarget_ = new wxButton(frame_, wxID_ANY, "IME focus target");
        layout->Add(focusTarget_, 0, wxEXPAND);
        frame_->SetSizer(layout);
        window_ = static_cast<HWND>(editor_->GetHandle());
        if (!SetWindowSubclass(window_, Observe, 1, reinterpret_cast<DWORD_PTR>(this))) return false;
        frame_->Show();
        frame_->Raise();
        const auto thread = GetCurrentThreadId();
        const auto foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attached =
            foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
        BringWindowToTop(static_cast<HWND>(frame_->GetHandle()));
        SetForegroundWindow(static_cast<HWND>(frame_->GetHandle()));
        SetActiveWindow(static_cast<HWND>(frame_->GetHandle()));
        editor_->SetFocus();
        if (attached) AttachThreadInput(thread, foregroundThread, FALSE);
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Step(); }, timer_.GetId());
        deadline_ = Clock::now() + std::chrono::seconds(5);
        timer_.Start(30);
        return true;
    }
    int OnRun() override {
        if (result_ == 77) return result_;
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    static LRESULT CALLBACK Observe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto* app = reinterpret_cast<ImeInputApp*>(data);
        if (message == WM_IME_COMPOSITION) {
            const auto context = ImmGetContext(window);
            const auto flags = static_cast<uintptr_t>(lParam);
            if (flags & GCS_COMPSTR) app->preedit_ = Composition(context, GCS_COMPSTR);
            if (flags & GCS_RESULTSTR) app->committed_ = Composition(context, GCS_RESULTSTR);
            ImmReleaseContext(window, context);
        }
        if (message == WM_IME_ENDCOMPOSITION) {
            app->preedit_.clear();
            ++app->ended_;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    void Require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }
    void Key(WORD code, bool control = false) {
        Require(GetForegroundWindow() == frame_->GetHandle() && GetFocus() == window_,
                "physical input remains confined to the owned focused editor");
        std::vector<INPUT> events;
        const auto add = [&events](WORD key, DWORD flags) {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = key;
            input.ki.dwFlags = flags;
            events.push_back(input);
        };
        if (control) add(VK_CONTROL, 0);
        add(code, 0);
        add(code, KEYEVENTF_KEYUP);
        if (control) add(VK_CONTROL, KEYEVENTF_KEYUP);
        Require(SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT)) == events.size(),
                "native keyboard input is delivered");
    }
    void Pinyin() {
        preedit_.clear();
        committed_.clear();
        constexpr WORD keys[] = {'N', 'I', 'H', 'A', 'O'};
        for (const WORD key : keys)
            Key(key);
    }
    void ResetPrefix() {
        const auto end = static_cast<intptr_t>(prefix_.size());
        TestEditor::ResetStream(editor_, prefix_.c_str(), end, end);
    }
    void Next() {
        ++phase_;
        deadline_ = Clock::now() + std::chrono::seconds(5);
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
        if (IsWindow(previous_)) SetForegroundWindow(previous_);
        ExitMainLoop();
    }
    void Step() {
        try {
            Require(Clock::now() < deadline_, "native IME phase completes within its deadline");
            switch (phase_) {
            case 0: {
                if (GetForegroundWindow() != frame_->GetHandle() || GetFocus() != window_) return;
                editor_->Send(Message::SetMarginTypeN, 0, static_cast<intptr_t>(Scintilla::MarginType::Number));
                editor_->Send(Message::SetMarginWidthN, 0, 60);
                editor_->Send(Message::SetWrapMode, static_cast<intptr_t>(Scintilla::Wrap::Word));
                const auto width = editor_->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>("x"));
                Require(width > 0, "the native editor measures the controlled prefix glyph");
                const auto left = editor_->Send(Message::PointXFromPosition, 0, 0);
                const auto available = editor_->GetClientSize().x - left - editor_->Send(Message::GetMarginRight);
                prefix_ = std::string(static_cast<size_t>(std::max<intptr_t>(1, available / width - 2)), 'x');
                ResetPrefix();
                const auto context = ImmGetContext(window_);
                ImmSetOpenStatus(context, TRUE);
                DWORD conversion = 0, sentence = 0;
                ImmGetConversionStatus(context, &conversion, &sentence);
                ImmSetConversionStatus(context, conversion | IME_CMODE_NATIVE, sentence);
                ImmReleaseContext(window_, context);
                Pinyin();
                Next();
                break;
            }
            case 1:
            case 3:
            case 7:
            case 10: {
                auto spelling = preedit_;
                std::erase(spelling, '\'');
                if (spelling != "nihao") return;
            }
                Require(TestEditor::Text(editor_) == prefix_ + preedit_,
                        "uncommitted native preedit participates in editor text layout");
                editor_->Refresh(false);
                editor_->Update();
                Require(editor_->Send(Message::WrapCount, 0) > 1,
                        "uncommitted native preedit wraps through the normal editor layout");
                std::puts("PASS: real native preedit uses editor text and wrapping");
                if (phase_ == 10) {
                    focusTarget_->SetFocus();
                } else if (phase_ == 7) {
                    const int x = static_cast<int>(editor_->Send(Message::PointXFromPosition, 0, 0)) + 1;
                    const int y = static_cast<int>(editor_->Send(Message::PointYFromPosition, 0, 0) +
                                                   editor_->Send(Message::TextHeight, 0) / 2);
                    SendMessageW(window_, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                    SendMessageW(window_, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                } else {
                    Key(phase_ == 1 ? VK_ESCAPE : VK_SPACE);
                }
                Next();
                break;
            case 2:
                if (!ended_) return;
                Require(TestEditor::Text(editor_) == prefix_ && !editor_->Send(Message::GetModify) &&
                            !editor_->Send(Message::CanUndo),
                        "native cancellation restores text, save point and undo history");
                std::puts("PASS: real native preedit cancellation preserves history");
                ended_ = 0;
                Pinyin();
                Next();
                break;
            case 4:
                if (committed_.empty() || !ended_) return;
                Require(TestEditor::Text(editor_) == prefix_ + committed_,
                        "native result is committed exactly once with original Unicode bytes");
                Key('Z', true);
                Next();
                break;
            case 5:
                if (TestEditor::Text(editor_) != prefix_) return;
                Require(!editor_->Send(Message::GetModify) && editor_->Send(Message::CanRedo),
                        "one native Undo restores the document before the committed word");
                Key('Y', true);
                Next();
                break;
            case 6:
                if (TestEditor::Text(editor_) != prefix_ + committed_) return;
                Require(editor_->Send(Message::GetCurrentPos) ==
                                static_cast<intptr_t>(prefix_.size() + committed_.size()) &&
                            editor_->Send(Message::GetAnchor) == editor_->Send(Message::GetCurrentPos),
                        "Redo restores the caret after the committed native word");
                std::puts("PASS: real native commit, single Undo and Redo");
                ResetPrefix();
                ended_ = 0;
                Pinyin();
                Next();
                break;
            case 8:
                if (committed_.empty() || !ended_) return;
                Require(TestEditor::Text(editor_) == prefix_ + committed_ &&
                            editor_->Send(Message::GetCurrentPos) == 0 && editor_->Send(Message::GetAnchor) == 0,
                        "native mouse down completes composition before moving the selection");
                // A directed mouse message bypasses TSF's physical-input path.
                // Clear its remaining keyboard state before the next scenario.
                Key(VK_ESCAPE);
                editor_->Send(Message::Undo);
                Next();
                break;
            case 9:
                if (TestEditor::Text(editor_) != prefix_) return;
                Require(!editor_->Send(Message::GetModify), "one Undo removes the mouse-completed IME word");
                std::puts("PASS: native mouse completion, click selection and single Undo");
                ResetPrefix();
                ended_ = 0;
                Pinyin();
                Next();
                break;
            case 11:
                if (committed_.empty() || !ended_) return;
                Require(TestEditor::Text(editor_) == prefix_ + committed_ && GetFocus() == focusTarget_->GetHandle(),
                        "native focus loss completes composition at its original position");
                editor_->SetFocus();
                editor_->Send(Message::Undo);
                Next();
                break;
            case 12:
                if (TestEditor::Text(editor_) != prefix_) return;
                Require(!editor_->Send(Message::GetModify), "one Undo removes the focus-completed IME word");
                std::puts("PASS: native focus completion and single Undo");
                Finish(0);
                break;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: phase=%d preedit_bytes=%zu committed_bytes=%zu: %s\n", phase_, preedit_.size(),
                         committed_.size(), error.what());
            Finish(1);
        }
    }
    wxFrame* frame_ = nullptr;
    EditorControl* editor_ = nullptr;
    wxButton* focusTarget_ = nullptr;
    HWND window_ = nullptr;
    HWND previous_ = nullptr;
    wxTimer timer_;
    Clock::time_point deadline_;
    std::string prefix_, preedit_, committed_;
    int phase_ = 0, ended_ = 0, result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ImeInputApp);
