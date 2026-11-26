#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "editor/platform/core_headers.h"
#include "measurement_workers.h"
#include "native_paint_helpers.h"
#include "file_helpers.h"
#include <SciLexer.h>
#include <array>
#include <barrier>
#include <cstdio>
#include <stdexcept>
#ifdef __WXMSW__
#include "font_quality_measurement.h"
#include <wx/dcmemory.h>
#include <set>
#include <imm.h>
#include <wx/vlbox.h>
#include <wx/dcclient.h>
#endif

namespace {
using Scintilla::Message;
constexpr std::array QualityLabels{"Default", "None", "Standard", "ClearType"};
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int QualityCommand(EditorFrame& frame, int quality) {
    auto* bar = frame.GetMenuBar();
    const int submenu = bar->FindMenuItem("Settings", "Font Quality");
    return submenu == wxNOT_FOUND ? wxNOT_FOUND
                                  : bar->FindItem(submenu)->GetSubMenu()->FindItem(QualityLabels[quality]);
}
#ifdef __WXMSW__
void Capture(wxWindow& window, const wxString& name) {
    wxString directory;
    if (!wxGetEnv("WXNOTEPAD_FONT_QUALITY_EVIDENCE", &directory)) return;
    const auto handle = static_cast<HWND>(wxGetTopLevelParent(&window)->GetHandle());
    const bool topmost = (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    constexpr UINT Flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    Require(SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, Flags), "expose only the owned capture window");
    try {
        window.Update();
        wxYield();
        const auto size = window.GetClientSize();
        wxBitmap bitmap(size.x, size.y, 32);
        wxMemoryDC memory(bitmap);
        wxClientDC client(&window);
        Require(memory.Blit(0, 0, size.x, size.y, &client, 0, 0), "copy the actual owned native raster");
        memory.SelectObject(wxNullBitmap);
        Require(bitmap.ConvertToImage().SaveFile(wxFileName(directory, name + ".png").GetFullPath(), wxBITMAP_TYPE_PNG),
                "save controlled font quality evidence");
    } catch (...) {
        SetWindowPos(handle, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, Flags);
        throw;
    }
    Require(SetWindowPos(handle, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, Flags),
            "restore the owned capture window's z-order policy");
}
wxVListBox* CompletionList(wxWindow& parent) {
    for (auto* child : parent.GetChildren()) {
        if (auto* list = wxDynamicCast(child, wxVListBox)) return list;
        if (auto* list = CompletionList(*child)) return list;
    }
    return nullptr;
}
wxWindow* CallTipWindow(wxWindow& parent) {
    for (auto* child : parent.GetChildren()) {
        if (child->GetName() == "Color CallTip") return child;
        if (auto* popup = CallTipWindow(*child)) return popup;
    }
    return nullptr;
}
std::string ConfiguredFamily(EditorControl& editor) {
    std::string family(static_cast<size_t>(editor.Send(Message::StyleGetFont, 0)) + 1, '\0');
    editor.Send(Message::StyleGetFont, 0, reinterpret_cast<intptr_t>(family.data()));
    family.pop_back();
    return family;
}
void DocumentAndPopups(EditorFrame& frame) {
    constexpr std::array<BYTE, 4> Qualities{DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY,
                                            CLEARTYPE_QUALITY};
    auto* editor = frame.Editor();
    const std::string first = "Plain text: English office ffi Café 中文 😀 👩‍💻\r\n";
    const std::string comment = "// Comment: English office ffi Café 中文 😀 👩‍💻\r\n";
    const std::string emphasis = "Bold and italic: English office ffi Café 中文 😀 👩‍💻\r\n";
    std::string text = first + comment + emphasis + "Wrapped text: ";
    for (int repeat = 0; repeat < 14; ++repeat)
        text += "English 中文 Café 😀 ";
    text += "\r\n";
    for (const auto* backend : {"Legacy GDI", "Direct2D"}) {
        NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("Settings", backend));
        for (int quality = 0; quality < 4; ++quality) {
            NativeSearch::Command(&frame, QualityCommand(frame, quality));
            TestEditor::ResetStream(editor, text.c_str(), 0, 0);
            editor->Send(Message::SetLexer, SCLEX_CONTAINER);
            editor->Send(Message::SetCaretPeriod, 0);
            editor->Send(Message::StyleSetFont, 0, reinterpret_cast<intptr_t>("Consolas"));
            editor->Send(Message::StyleSetSizeFractional, 0, 1200);
            editor->Send(Message::StyleSetFore, 0, 0x141414);
            editor->Send(Message::StyleSetBack, 0, 0xffffff);
            editor->Send(Message::StyleClearAll);
            editor->Send(Message::StyleSetFore, 1, 0x287828);
            editor->Send(Message::StyleSetWeight, 2, 700);
            editor->Send(Message::StyleSetItalic, 2, 1);
            editor->Send(Message::StartStyling, 0);
            editor->Send(Message::SetStyling, first.size(), 0);
            editor->Send(Message::SetStyling, comment.size(), 1);
            editor->Send(Message::SetStyling, emphasis.size(), 2);
            editor->Send(Message::SetStyling, text.size() - first.size() - comment.size() - emphasis.size(), 0);
            editor->Send(Message::SetWrapMode, std::to_underlying(Scintilla::Wrap::Word));
            NativePaint::Complete(editor);
            Require(TestEditor::Text(editor) == text && editor->Send(Message::WrapCount, 3) > 1,
                    "each quality draws unchanged mixed-script plain, comment, bold-italic and wrapped text");
            Require(editor->Send(Message::GetStyleIndexAt, first.size()) == 1 &&
                        editor->Send(Message::GetStyleIndexAt, first.size() + comment.size()) == 2 &&
                        editor->Send(Message::StyleGetWeight, 2) == 700 && editor->Send(Message::StyleGetItalic, 2),
                    "actual painted comment and bold-italic spans retain their explicit styles");
            for (const auto position :
                 {text.find("English"), text.find("中"), text.find("Cafe") + 3, text.find("😀")}) {
                const auto x = editor->Send(Message::PointXFromPosition, 0, position);
                const auto y =
                    editor->Send(Message::PointYFromPosition, 0, position) + editor->Send(Message::TextHeight, 0) / 2;
                Require(editor->Send(Message::PositionFromPointClose, x + 1, y) == static_cast<intptr_t>(position),
                        "caret hit testing retains ASCII, Chinese, combining-cluster and emoji boundaries");
            }
            const auto prefix = wxString::Format("wx-dpi%d-technology%lld-quality%d", editor->GetDPI().y,
                                                 static_cast<long long>(editor->Send(Message::GetTechnology)), quality);
            Capture(*editor, prefix);
            TestEditor::ResetStream(editor, "al", 2, 2);
            editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
            auto* list = CompletionList(*editor);
            LOGFONTW native{};
            Require(list && GetObjectW(list->GetFont().GetHFONT(), sizeof(native), &native) &&
                        native.lfQuality == Qualities[quality],
                    "actual completion HFONT uses the selected original quality on either renderer");
            Capture(*list, prefix + "-completion");
            editor->Send(Message::AutoCCancel);
            editor->Send(Message::CallTipShow, 2, reinterpret_cast<intptr_t>("quality(English 中文 Café 😀)"));
            wxYield();
            auto* popup = CallTipWindow(*editor);
            Require(editor->Send(Message::CallTipActive) && popup && popup->IsShownOnScreen(),
                    "CallTip remains visible with every selected quality on either renderer");
            Capture(*popup, prefix + "-calltip");
            editor->Send(Message::CallTipCancel);
        }
    }
    NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    const std::string code = "int main() {\r\n // 中文 Café 😀\r\n return 1;\r\n}\r\n";
    TestEditor::ResetStream(editor, code.c_str(), 6, 3);
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("edit"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 6, 3);
    editor->Send(Message::FoldLine, 0, std::to_underlying(Scintilla::FoldAction::Contract));
    const auto size = editor->Send(Message::StyleGetSizeFractional, 0);
    const auto family = ConfiguredFamily(*editor);
    const auto fonts = editor->Fonts();
    for (int quality = 0; quality < 4; ++quality) {
        NativeSearch::Command(&frame, QualityCommand(frame, quality));
        for (const auto* backend : {"Legacy GDI", "Direct2D", "Legacy GDI"}) {
            NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("Settings", backend));
            NativePaint::Complete(editor);
            Require(editor->Send(Message::GetFontQuality) == quality && TestEditor::Text(editor) == code &&
                        editor->Send(Message::GetAnchor) == 6 && editor->Send(Message::GetCurrentPos) == 3 &&
                        !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                        editor->Send(Message::CanRedo) && !editor->Send(Message::GetFoldExpanded, 0) &&
                        editor->Send(Message::StyleGetSizeFractional, 0) == size &&
                        ConfiguredFamily(*editor) == family && editor->Fonts() == fonts,
                    "quality/backend round trips retain fonts, bytes, selection, fold, save point and history");
        }
    }
    for (const auto* scheme : {"Plain Text", "C/C++", "Dark", "Default"}) {
        NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("Scheme", scheme));
        Require(editor->Send(Message::GetFontQuality) == 3 && TestEditor::Text(editor) == code &&
                    editor->Send(Message::GetAnchor) == 6 && editor->Send(Message::GetCurrentPos) == 3 &&
                    editor->Send(Message::CanRedo),
                "scheme and Default/Dark round trips retain quality, bytes, selection and redo history");
    }
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == code.substr(0, 3) + "edit" + code.substr(6),
            "Redo remains usable after all quality, backend, scheme and theme round trips");
    editor->Send(Message::Undo);
}
void CompositionFont(EditorFrame& frame) {
    constexpr std::array<BYTE, 4> Qualities{DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY,
                                            CLEARTYPE_QUALITY};
    auto* editor = frame.Editor();
    NativeFocus::Activate(static_cast<HWND>(frame.GetHandle()), static_cast<HWND>(editor->GetHandle()));
    wxYield();
    Require(editor->HasFocus() && GetForegroundWindow() == frame.GetHandle(), "editor owns native IME focus");
    for (const auto* backend : {"Legacy GDI", "Direct2D"}) {
        NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("Settings", backend));
        for (int quality = 0; quality < 4; ++quality) {
            NativeSearch::Command(&frame, QualityCommand(frame, quality));
            NativePaint::Complete(editor);
            const auto context = ImmGetContext(static_cast<HWND>(editor->GetHandle()));
            LOGFONTW font{};
            const bool read = context && ImmGetCompositionFontW(context, &font);
            if (context) ImmReleaseContext(static_cast<HWND>(editor->GetHandle()), context);
            Require(read && font.lfQuality == Qualities[quality],
                    "actual IMM composition font uses original quality on either renderer");
            std::printf("IME backend=%s quality=%d native_quality=%u height=%ld\n", backend, quality,
                        static_cast<unsigned>(font.lfQuality), font.lfHeight);
        }
    }
}
void NativeFonts(wxWindow& window) {
    namespace core = Scintilla::Internal;
    // Independent pinned PlatWin.h values, rather than the production mapper.
    constexpr std::array<BYTE, 4> Qualities{DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY,
                                            CLEARTYPE_QUALITY};
    constexpr std::array Samples{"English office ffi", "中文", "Café", "😀 👩‍💻"};
    constexpr int WorkerIterations = 64;
    for (size_t quality = 0; quality < Qualities.size(); ++quality) {
        for (const bool emphasis : {false, true}) {
            wxBitmap bitmap;
            Require(bitmap.CreateWithLogicalSize(640, 100, window.GetContentScaleFactor()),
                    "create native font raster");
            wxMemoryDC dc(bitmap);
            auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
            surface->Init(&dc, &window);
            surface->SetMode({Scintilla::CpUtf8});
            const auto font = core::Font::Allocate(core::FontParameters(
                "Consolas", 12, emphasis ? Scintilla::FontWeight::Bold : Scintilla::FontWeight::Normal,
                Scintilla::FontStretch::Normal, emphasis, static_cast<Scintilla::FontQuality>(quality)));
            surface->FillRectangle(core::PRectangle(0, 0, 640, 100), core::Fill(core::ColourRGBA(255, 255, 255)));
            surface->DrawTextTransparentUTF8(core::PRectangle(0, 0, 640, 100), font.get(), 40, "English 中文 Café 😀",
                                             core::ColourRGBA(0, 0, 0));
            LOGFONTW drawing{};
            Require(GetObjectW(GetCurrentObject(static_cast<HDC>(dc.GetHDC()), OBJ_FONT), sizeof(drawing), &drawing),
                    "read the actually selected GDI drawing font");
            Require(drawing.lfQuality == Qualities[quality], "GDI drawing HFONT uses the original quality mapping");
            std::array<double, Samples.size()> widths{};
            for (size_t sample = 0; sample < Samples.size(); ++sample) {
                widths[sample] = surface->WidthTextUTF8(font.get(), Samples[sample]);
                Require(widths[sample] == dc.GetTextExtent(wxString::FromUTF8(Samples[sample])).x,
                        "private font measurement agrees with the drawing font");
            }
            BeginNativeFontObservation();
            std::barrier measured(MeasurementWorkers::Count);
            MeasurementWorkers::Run([&](int) {
                try {
                    for (int iteration = 0; iteration < WorkerIterations; ++iteration)
                        for (size_t sample = 0; sample < Samples.size(); ++sample)
                            Require(surface->WidthTextUTF8(font.get(), Samples[sample]) == widths[sample],
                                    "all four simultaneous workers preserve native drawing widths");
                } catch (...) {
                    measured.arrive_and_drop();
                    throw;
                }
                // Keep every thread's private context alive until all are observed.
                measured.arrive_and_wait();
            });
            const auto selections = EndNativeFontObservation();
            std::set<DWORD> threads;
            std::set<HDC> contexts;
            std::set<HFONT> fonts;
            for (const auto& selected : selections) {
                if (wxString(selected.description.lfFaceName) != "Consolas") continue; // Restored system font.
                Require(selected.description.lfQuality == drawing.lfQuality &&
                            selected.description.lfHeight == drawing.lfHeight &&
                            selected.description.lfWeight == drawing.lfWeight &&
                            selected.description.lfItalic == drawing.lfItalic,
                        "each actual worker HFONT retains the drawing font's native description");
                threads.insert(selected.thread);
                contexts.insert(selected.dc);
                fonts.insert(selected.font);
            }
            Require(threads.size() == MeasurementWorkers::Count && contexts.size() == MeasurementWorkers::Count &&
                        fonts.size() == MeasurementWorkers::Count,
                    "parallel workers retain four separate native threads, measurement HDCs and HFONTs");
            std::printf("GDI dpi=%d quality=%zu native_quality=%u height=%ld weight=%ld italic=%u workers=%zu HDCs=%zu "
                        "HFONTs=%zu\n",
                        window.GetDPI().y, quality, static_cast<unsigned>(drawing.lfQuality), drawing.lfHeight,
                        drawing.lfWeight, static_cast<unsigned>(drawing.lfItalic), threads.size(), contexts.size(),
                        fonts.size());
            dc.SelectObject(wxNullBitmap);
            const auto image = bitmap.ConvertToImage();
            const size_t bytes = static_cast<size_t>(image.GetWidth()) * image.GetHeight() * 3;
            Require(std::any_of(image.GetData(), image.GetData() + bytes,
                                [](unsigned char channel) { return channel != 255; }),
                    "every GDI quality, including None, draws visible text ink");
        }
    }
}
#endif
class QualityApp final : public wxApp {
    SettingsStore settings_;
    std::unique_ptr<SettingsStore> localSettings_;
    int result_ = 1;

public:
    bool OnInit() override {
        wxInitAllImageHandlers();
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_FONT_QUALITY_DPI96")) {
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary DPI-unaware96 emulation");
            std::puts("Complementary DPI-unaware96 emulation; monitor scaling unchanged");
        }
#endif
        const bool composition = argc == 2 && argv[1] == "--composition";
        if (argc == 1)
            localSettings_ =
                std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory("quality.ini")));
        auto* frame = new EditorFrame(argc > 1 ? &settings_ : localSettings_.get());
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame, composition](bool ready) {
            try {
                Require(ready, "font quality window is presented");
                auto* editor = frame->Editor();
#ifdef __WXMSW__
                const int expected = argc > 1 && !composition ? wxAtoi(argv[1]) : 3;
                Require(editor->Send(Message::GetFontQuality) == expected,
                        "Windows applies original quality default and imported bounds");
                if (argc > 2 && argv[2] != "keep") {
                    const int command = QualityCommand(*frame, wxAtoi(argv[2]));
                    Require(command != wxNOT_FOUND, "original font quality menu command exists");
                    NativeSearch::Command(frame, command);
                }
                if (argc == 1) {
                    for (int quality = 0; quality < 4; ++quality) {
                        const int command = QualityCommand(*frame, quality);
                        Require(command != wxNOT_FOUND, "all four original quality choices exist");
                        NativeSearch::Command(frame, command);
                        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                        Require(editor->Send(Message::GetFontQuality) == quality,
                                "quality menu immediately changes actual editor quality");
                        for (int item = 0; item < 4; ++item)
                            Require(frame->GetMenuBar()->IsChecked(QualityCommand(*frame, item)) == (item == quality),
                                    "font quality menu is an exclusive radio group");
                    }
                    NativeFonts(*editor);
                    DocumentAndPopups(*frame);
                }
                if (composition) CompositionFont(*frame);
#else
                Require(editor->Send(Message::GetFontQuality) == 0, "Linux retains system text rendering");
                Require(QualityCommand(*frame, 3) == wxNOT_FOUND, "Linux hides Windows ClearType commands");
#endif
                if (argc > 3 && argv[3] == "explicit")
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
                result_ = 0;
                frame->Close();
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                frame->Destroy();
                ExitMainLoop();
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
};
} // namespace
wxIMPLEMENT_APP(QualityApp);
