#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "native_fold_helpers.h"
#include "settings/settings_store.h"
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxChoice* Choice(wxDialog* dialog, const char* name) {
    auto* choice = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxChoice);
    Require(choice, "native wrap choice exists");
    return choice;
}
void Choose(wxDialog* dialog, const char* name, int index) {
    auto* control = Choice(dialog, name);
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(control->GetHandle());
    SendMessageW(handle, CB_SETCURSEL, index, 0);
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(handle));
#else
    gtk_combo_box_set_active(GTK_COMBO_BOX(control->GetHandle()), index);
#endif
    Require(control->GetSelection() == index, "native choice accepts selected index");
}
void Check(wxDialog* dialog, const char* name, bool value) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    Require(control, "native wrap checkbox exists");
    NativeSearch::Toggle(control, value);
}
class DialogDriver final : public wxModalDialogHook {
public:
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Word Wrap Settings")
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool presented) {
                    try {
                        Require(presented, "owned wrap settings dialog is presented");
#ifdef __WXMSW__
                        Require(static_cast<int>(GetDpiForWindow(static_cast<HWND>(dialog->GetHandle()))) ==
                                    dialog->GetDPI().y,
                                "wrap dialog DPI agrees with its actual native window");
#endif
                        Require(dialog->GetDPI() == dialog->GetParent()->GetDPI(),
                                "wrap dialog uses its parent frame's active DPI");
                        const auto area = wxDisplay(wxDisplay::GetFromWindow(dialog)).GetClientArea();
                        Require(area.Contains(dialog->GetScreenRect()),
                                "wrap settings fits the active native work area");
                        for (auto* child : dialog->GetChildren())
                            if (child->IsShown() &&
                                (wxDynamicCast(child, wxChoice) || wxDynamicCast(child, wxCheckBox) ||
                                 wxDynamicCast(child, wxButton)))
                                Require(dialog->GetScreenRect().Contains(child->GetScreenRect()),
                                        "every native wrap control and button fits the dialog at the active DPI");
                        if (!reportedDpi_) {
                            std::printf("wrap dialog dpi=%dx%d size=%dx%d work_area=%dx%d\n", dialog->GetDPI().x,
                                        dialog->GetDPI().y, dialog->GetSize().x, dialog->GetSize().y, area.width,
                                        area.height);
                            reportedDpi_ = true;
                        }
                        action_(dialog);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        return wxID_NONE;
    }
    std::exception_ptr failure;

private:
    std::function<void(wxDialog*)> action_;
    inline static bool reportedDpi_ = false;
};
template <class Action> void Visit(EditorFrame* frame, Action action) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Settings", "Word Wrap Settings...");
    Require(id != wxNOT_FOUND, "Settings exposes Word Wrap Settings");
    bool opened = false;
    DialogDriver driver([&](wxDialog* dialog) {
        opened = true;
        action(dialog);
    });
    NativeSearch::Command(frame, id);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(opened, "wrap settings dialog opens");
}
auto DocumentState(EditorControl* editor) {
    using enum Message;
    return std::tuple(NativeSearch::Text(editor), editor->Send(GetAnchor), editor->Send(GetCurrentPos),
                      editor->Send(GetEOLMode), editor->Send(GetModify), editor->Send(CanUndo), editor->Send(CanRedo),
                      editor->Send(GetLineVisible, 2));
}
auto WrapState(EditorControl* editor) {
    using enum Message;
    return std::tuple(editor->Send(GetWrapMode), editor->Send(GetWrapIndentMode), editor->Send(GetWrapStartIndent),
                      editor->Send(GetWrapVisualFlags), editor->Send(GetWrapVisualFlagsLocation),
                      editor->Send(GetMarginOptions), editor->Send(GetCaretLineHighlightSubLine));
}
void Toggle(EditorFrame* frame) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(frame->Editor(), 'W', {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}}),
            "Ctrl+Shift+W native accelerator is available");
#else
    NativeKeys::Shortcut(frame, 'W', true, false, true);
#endif
    NativeFold::Paint(frame->Editor());
}
void SetMode(EditorFrame* frame, int mode) {
    Visit(frame, [mode](wxDialog* dialog) {
        Choose(dialog, "Wrap mode", mode);
        NativeSearch::Click(dialog, wxID_OK);
    });
}
void MarginClick(EditorControl* editor) {
    using enum Message;
    editor->Send(SetSel, 0, 0);
    editor->Send(ScrollCaret);
    NativeFold::Paint(editor);
    const int x = static_cast<int>(editor->Send(GetMarginWidthN, 0) / 2);
    const int y = static_cast<int>(editor->Send(TextHeight, 0) * 3 / 2);
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
#else
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
    NativeFold::Paint(editor);
}
void Resize(EditorFrame* frame, int width) {
    const auto requested = frame->FromDIP(wxSize(width, 340));
    frame->SetClientSize(requested);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        frame->Layout();
        NativeFold::Paint(frame->Editor());
        if (frame->GetClientSize() == requested && frame->Editor()->GetSize() == requested) return;
        wxYield();
        wxMilliSleep(1);
    } while (std::chrono::steady_clock::now() < deadline);
    std::fprintf(stderr, "Resize requested %dx%d; frame %dx%d; editor %dx%d; minimum %dx%d\n", requested.x, requested.y,
                 frame->GetClientSize().x, frame->GetClientSize().y, frame->Editor()->GetSize().x,
                 frame->Editor()->GetSize().y, frame->GetMinSize().x, frame->GetMinSize().y);
    Require(false, "native frame and editor reach requested geometry before checking wrap settings");
}
void Exercise(EditorFrame* frame, SettingsStore& settings) {
    using enum Message;
    auto* editor = frame->Editor();
    Require(editor->Send(GetWrapMode) == 4, "original default enables Automatic word wrap");
    std::string text = "// \t ";
    for (int repeat = 0; repeat < 30; ++repeat)
        text += "words 中文 longwordwithoutspaces\t ";
    text += "\r\nvoid outer() {\n  body();\n}\rfinal";
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    NativeFold::Paint(editor);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(GetLineVisible, 2), "wrap fixture has a collapsed logical block");
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, 80, 6);
    const auto document = DocumentState(editor);
    const auto wrapping = WrapState(editor);
    const auto preferences =
        std::tuple(settings.values.wordWrap, settings.values.wordWrapMode, settings.values.wordWrapIndent,
                   settings.values.wordWrapSymbols, settings.values.showWordWrapSymbols,
                   settings.values.wordWrapSelectSubLine, settings.values.highlightCurrentLine);
    Visit(frame, [](wxDialog* dialog) {
        Require(Choice(dialog, "Wrap mode")->GetCount() == 5 && Choice(dialog, "Wrap indent")->GetCount() == 8 &&
                    Choice(dialog, "Wrap symbol before")->GetCount() == 3 &&
                    Choice(dialog, "Wrap symbol after")->GetCount() == 4,
                "dialog contains every original mode, continuation indent and symbol placement");
        Choose(dialog, "Wrap mode", 2);
        Choose(dialog, "Wrap indent", 7);
        Choose(dialog, "Wrap symbol before", 1);
        Choose(dialog, "Wrap symbol after", 3);
        Check(dialog, "Wrap select subline", true);
        Check(dialog, "Wrap highlight subline", false);
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    Require(WrapState(editor) == wrapping &&
                preferences == std::tuple(settings.values.wordWrap, settings.values.wordWrapMode,
                                          settings.values.wordWrapIndent, settings.values.wordWrapSymbols,
                                          settings.values.showWordWrapSymbols, settings.values.wordWrapSelectSubLine,
                                          settings.values.highlightCurrentLine),
            "Cancel preserves all active and persisted wrap preferences");
    for (int mode = 1; mode <= 4; ++mode) {
        SetMode(frame, mode);
        Require(editor->Send(GetWrapMode) == mode, "OK immediately applies each original wrap mode");
        Toggle(frame);
        Require(editor->Send(GetWrapMode) == 0, "native shortcut disables selected wrap mode");
        Toggle(frame);
        Require(editor->Send(GetWrapMode) == mode, "native shortcut restores selected mode");
        SetMode(frame, 0);
        Require(editor->Send(GetWrapMode) == 0 && settings.values.wordWrapMode == mode,
                "No Wrap retains the most recent nonzero mode");
        Toggle(frame);
        Require(editor->Send(GetWrapMode) == mode, "toggle restores remembered mode after No Wrap");
    }
    editor->SetIndentationWidths(8, 3);
    constexpr std::array<int, 8> indentModes{0, 0, 0, 0, 0, 1, 2, 3};
    constexpr std::array<int, 5> startIndents{0, 1, 2, 3, 6};
    for (int indent = 0; indent < 8; ++indent) {
        Visit(frame, [indent](wxDialog* dialog) {
            Choose(dialog, "Wrap indent", indent);
            NativeSearch::Click(dialog, wxID_OK);
        });
        Require(editor->Send(GetWrapIndentMode) == indentModes[indent],
                "all eight original indent modes map correctly");
        if (indent < 5)
            Require(editor->Send(GetWrapStartIndent) == startIndents[indent],
                    "fixed continuation indentation uses characters or the effective indent width");
    }
    Visit(frame, [](wxDialog* dialog) {
        Choose(dialog, "Wrap indent", 4);
        NativeSearch::Click(dialog, wxID_OK);
    });
    editor->SetIndentationWidths(8, 0);
    Require(editor->Send(GetWrapStartIndent) == 16, "zero indent width falls back to tab width");
    Require(DocumentState(editor) == document,
            "wrap mode and indentation preserve bytes, endings, direction, folds and undo/redo");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Python"));
    Require(editor->Send(GetWrapStartIndent) == 8, "language settings recalculate continuation indentation");
    const auto restyledDocument = DocumentState(editor);
    // Original SetWrapVisualFlags: the ones digit controls End, the tens digit Start/Margin.
    constexpr std::array<int, 3> beforeFlags{0, 1, 1};
    constexpr std::array<int, 4> afterFlags{0, 2, 2, 4};
    for (int before = 0; before < 3; ++before)
        for (int after = 0; after < 4; ++after) {
            Visit(frame, [before, after](wxDialog* dialog) {
                Choose(dialog, "Wrap symbol before", before);
                Choose(dialog, "Wrap symbol after", after);
                NativeSearch::Click(dialog, wxID_OK);
            });
            Require(editor->Send(GetWrapVisualFlags) == (beforeFlags[before] | afterFlags[after]),
                    "each original symbol pair uses existing visibility and placement state");
            if (before || after)
                Require(editor->Send(GetWrapVisualFlagsLocation) == ((before == 1 ? 1 : 0) | (after == 1 ? 2 : 0)),
                        "near-text wrap indicators preserve original locations");
        }
    Require(DocumentState(editor) == restyledDocument,
            "wrap choices preserve bytes, mixed endings, direction, folds and undo/redo");
    // Match the existing native resize fixture's supported viewports. GTK's
    // menu requisition constrains this frame to at least 351 logical pixels.
    Resize(frame, 640);
    const auto rows = editor->Send(WrapCount, 0);
    Resize(frame, 360);
    Require(editor->Send(WrapCount, 0) > rows && editor->Send(GetWrapStartIndent) == 8,
            "viewport resize recomputes the original wrapping without losing continuation settings");
    editor->Send(SetZoom, 150);
    NativeFold::Paint(editor);
    Require(editor->Send(GetWrapStartIndent) == 8 && editor->Send(GetWrapVisualFlags) == 5,
            "scaled rendering preserves continuation indentation and symbols");
    editor->Send(SetZoom, 100);
    Require(DocumentState(editor) == restyledDocument, "resize and scaled layout preserve logical document state");
    Visit(frame, [](wxDialog* dialog) {
        Check(dialog, "Wrap select subline", true);
        NativeSearch::Click(dialog, wxID_OK);
    });
    MarginClick(editor);
    Require(editor->Send(GetSelectionStart) > 0 && editor->Send(GetSelectionEnd) < editor->Send(GetLineEndPosition, 0),
            "margin click selects just the clicked visual continuation subline");
    Visit(frame, [](wxDialog* dialog) {
        Check(dialog, "Wrap select subline", false);
        NativeSearch::Click(dialog, wxID_OK);
    });
    MarginClick(editor);
    Require(editor->Send(GetSelectionStart) == 0 && editor->Send(GetSelectionEnd) == editor->Send(PositionFromLine, 1),
            "logical-line margin selection includes all visual sublines and the original line ending");
    editor->Send(SetSel, 0, 0);
    editor->Send(SetSavePoint);
    SetMode(frame, 2);
    Require(frame->NewDocument() && editor->Send(GetWrapMode) == 2, "New retains selected wrap mode");
    const auto path = wxFileName::CreateTempFileName("wxnotepad-wrap-settings-");
    {
        wxFFile file(path, "wb");
        file.Write("# -*- truncate-lines: t; tab-width: 7; basic-offset: 3 -*-\nlong line\n");
    }
    Require(frame->Open(path).status == OpenStatus::Opened && editor->Send(GetWrapMode) == 0 &&
                editor->Send(GetWrapStartIndent) == 6,
            "original truncate-lines file variable overrides wrapping and file indentation");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    Require(settings.values.wordWrap && settings.values.wordWrapMode == 2,
            "saving preferences does not promote a document's truncate-lines override to the global setting");
    Require(frame->Revert().status == OpenStatus::Opened && editor->Send(GetWrapMode) == 0,
            "reload retains the original file-variable wrap override");
    Require(frame->NewDocument() && editor->Send(GetWrapMode) == 2,
            "New restores global mode after file-variable override");
    wxRemoveFile(path);
    Visit(frame, [](wxDialog* dialog) {
        Choose(dialog, "Wrap mode", 3);
        Choose(dialog, "Wrap indent", 4);
        Choose(dialog, "Wrap symbol before", 1);
        Choose(dialog, "Wrap symbol after", 3);
        Check(dialog, "Wrap select subline", true);
        Check(dialog, "Wrap highlight subline", false);
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(editor->Send(GetMarginOptions) == 1 && editor->Send(GetCaretLineHighlightSubLine) == 0,
            "OK applies shared margin-selection and current-subline settings");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    std::puts("PASS: original wrap modes, every indent and symbol, Cancel/OK, metadata, file variables and save");
}
void OriginalReference(EditorFrame* frame, const wxString& input) {
    using enum Message;
    auto* editor = frame->Editor();
    auto path = std::filesystem::path(input.ToStdWstring());
    if (std::filesystem::is_directory(path))
        path /= "word_wrap_original_" + std::to_string(editor->GetDPI().y) + ".tsv";
    std::ifstream reference(path);
    Require(reference.is_open(), "independent original wrap capture exists for the active window DPI");
    std::ifstream inputs(path.parent_path() / "word_wrap_inputs.tsv");
    std::map<std::string, std::string> samples;
    std::string sample, hex;
    while (inputs >> sample >> hex)
        samples.emplace(sample, hex);
    Require(samples.size() == 4, "all four original reference input samples exist");
    std::string header;
    std::getline(reference, header);
    int records = 0;
    int mode = 0, indent = 0, width = 0, height = 0, dpi = 0;
    while (reference >> sample >> mode >> indent >> width >> height >> dpi) {
        std::array<intptr_t, 17> expected{};
        for (auto& value : expected)
            reference >> value;
        Require(static_cast<bool>(reference), "original capture row contains every observed field");
        Require(samples.contains(sample), "original reference row identifies an input sample");
        hex = samples.at(sample);
        std::string text;
        for (size_t offset = 0; offset < hex.size(); offset += 2)
            text += static_cast<char>(std::stoi(hex.substr(offset, 2), nullptr, 16));
        TestEditor::ResetStream(editor, text.c_str(), 6, 1, Scintilla::EndOfLine::CrLf);
        editor->Send(SetCodePage, Scintilla::CpUtf8);
        editor->Send(SetLexer, 1); // Original Null lexer; explicit fold levels below.
        editor->Send(SetTechnology, std::to_underlying(Scintilla::Technology::Default));
        editor->Send(SetZoom, 100);
        editor->Send(SetFontQuality, std::to_underlying(Scintilla::FontQuality::QualityDefault));
        editor->Send(SetFontLocale, 0, reinterpret_cast<intptr_t>(""));
        constexpr auto defaultStyle = std::to_underlying(Scintilla::StylesCommon::Default);
        constexpr int referencePointSizeFractional = 1000;
        editor->Send(StyleSetFont, defaultStyle, reinterpret_cast<intptr_t>("Consolas"));
        editor->Send(StyleSetSizeFractional, defaultStyle, referencePointSizeFractional);
        editor->Send(StyleSetWeight, defaultStyle, std::to_underlying(Scintilla::FontWeight::Normal));
        editor->Send(StyleSetItalic, defaultStyle, 0);
        editor->Send(StyleSetCharacterSet, defaultStyle, std::to_underlying(Scintilla::CharacterSet::Default));
        editor->Send(StyleSetStretch, defaultStyle, std::to_underlying(Scintilla::FontStretch::Normal));
        editor->Send(StyleClearAll);
        editor->SetIndentationWidths(8, 3);
        editor->Send(SetExtraAscent, 0);
        editor->Send(SetExtraDescent, 0);
        editor->SetLineNumbersVisible(false);
        editor->SetBookmarkMarginVisible(false);
        editor->SetCodeFoldingVisible(false);
        for (int margin = 0; margin < 5; ++margin)
            editor->Send(SetMarginWidthN, margin, 0);
        editor->Send(SetMarginLeft, 0, 1);
        editor->Send(SetMarginRight, 0, 1);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        editor->Send(SetSel, 6, 1);
        if (sample == "folded") {
            constexpr auto foldBase = std::to_underlying(Scintilla::FoldLevel::Base);
            editor->Send(SetFoldLevel, 0, foldBase | std::to_underlying(Scintilla::FoldLevel::HeaderFlag));
            for (int line = 1; line < 4; ++line)
                editor->Send(SetFoldLevel, line, foldBase + 1);
            editor->Send(SetFoldLevel, 4, foldBase);
            editor->Send(FoldLine, 0, std::to_underlying(Scintilla::FoldAction::Contract));
        }
        Visit(frame, [mode, indent](wxDialog* dialog) {
            Choose(dialog, "Wrap mode", mode);
            Choose(dialog, "Wrap indent", indent);
            Choose(dialog, "Wrap symbol before", 1);
            Choose(dialog, "Wrap symbol after", 3);
            Check(dialog, "Wrap select subline", true);
            Check(dialog, "Wrap highlight subline", true);
            NativeSearch::Click(dialog, wxID_OK);
        });
        // The original's margin-symbol setting enables line numbers. Clear the
        // geometric margins after that host side effect on both reference hosts.
        editor->SetLineNumbersVisible(false);
        for (int margin = 0; margin < 5; ++margin)
            editor->Send(SetMarginWidthN, margin, 0);
        // Normalize the actual client viewport, independently of frame chrome and scrollbars.
        for (int attempt = 0; attempt < 3; ++attempt) {
            frame->SetClientSize(frame->GetClientSize() + wxSize(width, height) - editor->GetClientSize());
            frame->Layout();
            NativeFold::Paint(editor);
            if (editor->GetClientSize() == wxSize(width, height)) break;
        }
        Require(editor->GetClientSize() == wxSize(width, height) && editor->GetDPI().y == dpi,
                "original comparison has identical reported native viewport and window DPI");
        std::string face(static_cast<size_t>(editor->Send(StyleGetFont, defaultStyle)) + 1, '\0');
        editor->Send(StyleGetFont, defaultStyle, reinterpret_cast<intptr_t>(face.data()));
        Require(std::string(face.c_str()) == "Consolas" &&
                    editor->Send(StyleGetSizeFractional, defaultStyle) == referencePointSizeFractional &&
                    editor->Send(GetZoom) == 100 && editor->Send(GetTechnology) == 0 &&
                    editor->Send(GetMarginLeft) == 1 && editor->Send(GetMarginRight) == 1,
                "comparison retains the actual original font, renderer, zoom and edge margins");
        for (int margin = 0; margin < 5; ++margin)
            Require(editor->Send(GetMarginWidthN, margin) == 0, "comparison retains zero native margin columns");
        const std::array<intptr_t, 17> observed{
            editor->Send(GetWrapMode),
            editor->Send(GetWrapIndentMode),
            editor->Send(GetWrapStartIndent),
            editor->Send(GetWrapVisualFlags),
            editor->Send(GetWrapVisualFlagsLocation),
            editor->Send(GetMarginOptions),
            editor->Send(GetCaretLineHighlightSubLine),
            editor->Send(WrapCount, 0),
            editor->Send(GetLineVisible, 2),
            editor->Send(GetAnchor),
            editor->Send(GetCurrentPos),
            editor->Send(GetModify),
            editor->Send(CanUndo),
            editor->Send(CanRedo),
            editor->Send(TextHeight, 0),
            editor->Send(TextWidth, defaultStyle, reinterpret_cast<intptr_t>("W")),
            editor->Send(TextWidth, defaultStyle, reinterpret_cast<intptr_t>("中文"))};
        for (size_t field = 0; field < expected.size(); ++field) {
            if (observed[field] != expected[field])
                std::fprintf(stderr,
                             "original %s mode=%d indent=%d width=%d dpi=%d field=%zu expected=%lld actual=%lld\n",
                             sample.c_str(), mode, indent, width, dpi, field, static_cast<long long>(expected[field]),
                             static_cast<long long>(observed[field]));
        }
        Require(observed == expected, "native wrap result equals independent original executable capture");
        Require(NativeSearch::Text(editor) == text, "original comparison retains exact sample bytes");
        ++records;
    }
    Require(records == 32, "all four original samples, modes and both viewports were compared");
    std::printf("PASS: %d independent original dialog/wrap records at %d DPI\n", records, editor->GetDPI().y);
}
class WrapSettingsApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_WRAP_DPI96")) {
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary emulated 96-DPI wrap window");
            std::puts("Complementary 96-DPI unaware-thread emulation; monitor scaling remains unchanged");
        }
#endif
        settings_ = std::make_unique<SettingsStore>();
        // Keep the original narrow editor viewport independent of the wider
        // native menu requisition after adding Tools. Commands remain native.
        settings_->values.showMenu = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            const bool exitSave = argc > 1 && argv[1] == "--exit";
            try {
                Require(ready, "owned editor frame is presented before native wrap actions");
#ifdef __WXMSW__
                if (wxGetenv("WXNOTEPAD_WRAP_DPI96"))
                    Require(frame->GetDPI().y == 96 && frame->Editor()->GetDPI().y == 96,
                            "complementary native frame and editor both report the requested 96 DPI");
#endif
                if (argc > 1 && argv[1] == "--reference") {
                    OriginalReference(frame, argv[2]);
                } else if (exitSave) {
                    Visit(frame, [](wxDialog* dialog) {
                        Choose(dialog, "Wrap mode", 2);
                        Choose(dialog, "Wrap indent", 3);
                        Choose(dialog, "Wrap symbol before", 1);
                        Choose(dialog, "Wrap symbol after", 3);
                        Check(dialog, "Wrap select subline", true);
                        Check(dialog, "Wrap highlight subline", false);
                        NativeSearch::Click(dialog, wxID_OK);
                    });
                } else if (argc > 1) {
                    Require(frame->Editor()->Send(Message::GetWrapMode) == wxAtoi(argv[1]) &&
                                std::to_underlying(settings_->values.wordWrapIndent) == wxAtoi(argv[2]),
                            "imported and restarted original wrap settings normalize correctly");
                    if (argc > 3) {
                        Require(frame->Editor()->Send(Message::GetWrapVisualFlags) == 5 &&
                                    frame->Editor()->Send(Message::GetWrapVisualFlagsLocation) == 1 &&
                                    frame->Editor()->Send(Message::GetMarginOptions) == 1 &&
                                    frame->Editor()->Send(Message::GetCaretLineHighlightSubLine) == 0,
                                "restart restores complete symbols, subline selection and highlighting");
                    }
                    if (wxAtoi(argv[1]) == 0) {
                        Toggle(frame);
                        Require(frame->Editor()->Send(Message::GetWrapMode) == 2,
                                "restart with WordWrap disabled retains the selected nonzero mode");
                        Toggle(frame);
                    }
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
                } else
                    Exercise(frame, *settings_);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (exitSave)
                frame->Close(true);
            else
                frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 1;
    std::unique_ptr<SettingsStore> settings_;
};
} // namespace
wxIMPLEMENT_APP(WrapSettingsApp);
