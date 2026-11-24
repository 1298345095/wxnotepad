#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "native_dialog_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "font_dialog_helpers.h"
#include "app/localization.h"
#include "native_tree_input.h"
#include <wx/display.h>
#include <wx/colordlg.h>
#include <wx/treectrl.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/convauto.h>
#include <cmath>
#include <SciLexer.h>

namespace {
using Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void SelectStyle(wxTreeCtrl& tree, const wxString& scheme, const wxString& style) {
    Require(NativeDialogs::SelectTreeLabel(tree, wxGetTranslation(scheme)),
            "delivered scheme exists in native style tree");
    wxTreeItemIdValue cookie;
    for (auto item = tree.GetFirstChild(tree.GetSelection(), cookie); item.IsOk();
         item = tree.GetNextChild(tree.GetSelection(), cookie)) {
        if (tree.GetItemText(item) == wxGetTranslation(style)) {
            tree.SelectItem(item);
            return;
        }
    }
    throw std::runtime_error("original style exists beneath its selected scheme");
}
class Picker final : public wxTimer {
public:
    std::function<void(wxDialog&, wxTreeCtrl&, wxTextCtrl&)> action;
    wxString error;
    int shown = 0;
    Picker() { Start(40); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() ||
                (dialog->GetTitle() != wxGetTranslation("Customize Schemes - Default Theme") &&
                 dialog->GetTitle() != wxGetTranslation("Customize Schemes - Dark Theme")))
                continue;
            Stop();
            ++shown;
            try {
                auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Scheme styles", dialog), wxTreeCtrl);
                auto* edit = wxDynamicCast(wxWindow::FindWindowByName("Style settings", dialog), wxTextCtrl);
                Require(tree && edit, "actual customization exposes original tree and editable style text");
                action(*dialog, *tree, *edit);
            } catch (const std::exception& failure) {
                error = failure.what();
                dialog->EndModal(wxID_CANCEL);
            }
        }
    }
};
class SchemeFile final : public wxTimer {
public:
    SchemeFile(wxString title, wxString path) : title_(std::move(title)), path_(std::move(path)) { Start(30); }
    int shown = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<SchemeFile*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (!IsWindowVisible(window) || wxString(title) != driver.title_ ||
                    NativeDialogs::FileTypes(window).empty())
                    return TRUE;
                driver.Stop();
                ++driver.shown;
                NativeDialogs::ChooseFile(window, driver.path_);
                return FALSE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget) ||
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) != title_)
                continue;
            if (path_.empty()) {
                Stop();
                ++shown;
                NativeDialogs::ChooseFile(widget, {});
            } else if (!selected_) {
                NativeDialogs::ChooseFile(widget, path_);
                selected_ = true;
            } else if (NativeDialogs::CanAcceptFile(widget, path_)) {
                Stop();
                ++shown;
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
            }
            break;
        }
        g_list_free(windows);
#endif
    }

private:
    wxString title_, path_;
    [[maybe_unused]] bool selected_ = false;
};
class ColourPicker final : public wxModalDialogHook, public wxTimer {
public:
    ColourPicker(wxColour original, wxColour wanted, bool cancel, bool addPalette = false)
        : original_(original), wanted_(wanted), cancel_(cancel), addPalette_(addPalette) {
        Register();
        Start(30);
    }
    ~ColourPicker() override { Unregister(); }
    bool shown = false;
    wxString error;
    int Enter(wxDialog* dialog) override {
        dialog_ = wxDynamicCast(dialog, wxColourDialog);
        return wxID_NONE;
    }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                if (!IsWindowVisible(window) || !GetDlgItem(window, 706) || !GetDlgItem(window, 707) ||
                    !GetDlgItem(window, 708))
                    return TRUE;
                auto& choice = *reinterpret_cast<ColourPicker*>(data);
                choice.Stop();
                choice.shown = true;
                const std::array<UINT, 3> initial{GetDlgItemInt(window, 706, nullptr, FALSE),
                                                  GetDlgItemInt(window, 707, nullptr, FALSE),
                                                  GetDlgItemInt(window, 708, nullptr, FALSE)};
                if (initial !=
                    std::array<UINT, 3>{choice.original_.Red(), choice.original_.Green(), choice.original_.Blue()})
                    choice.error = "native color chooser initializes from selected raw style";
                SetDlgItemInt(window, 706, choice.wanted_.Red(), FALSE);
                SetDlgItemInt(window, 707, choice.wanted_.Green(), FALSE);
                SetDlgItemInt(window, 708, choice.wanted_.Blue(), FALSE);
                if (choice.addPalette_)
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(712, BN_CLICKED),
                                 reinterpret_cast<LPARAM>(GetDlgItem(window, 712)));
                SendMessageW(window, WM_COMMAND, choice.cancel_ ? IDCANCEL : IDOK, 0);
                return FALSE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        if (!dialog_ || !gtk_widget_get_mapped(GTK_WIDGET(dialog_->GetHandle()))) return;
        Stop();
        shown = true;
        GObject* selection = nullptr;
        g_object_get(dialog_->GetHandle(), "color-selection", &selection, nullptr);
        GdkRGBA* initial = nullptr;
        if (selection) g_object_get(selection, "current-rgba", &initial, nullptr);
        if (!initial || wxColour(*initial) != original_)
            error = "native color chooser initializes from selected raw style";
        if (initial) gdk_rgba_free(initial);
        const GdkRGBA wanted{wanted_.Red() / 255.0, wanted_.Green() / 255.0, wanted_.Blue() / 255.0, 1};
        if (selection) {
            g_object_set(selection, "current-rgba", &wanted, nullptr);
            g_object_unref(selection);
        }
        NativeDialogs::Respond(GTK_WIDGET(dialog_->GetHandle()), cancel_ ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);
#endif
    }

private:
    wxColour original_, wanted_;
    bool cancel_;
    [[maybe_unused]] bool addPalette_;
    [[maybe_unused]] wxColourDialog* dialog_ = nullptr;
};
wxString ReadIni(const std::filesystem::path& path) {
    const auto bytes = TestFiles::Bytes(path);
    return wxString(bytes.data(), wxConvAuto(), bytes.size());
}
void FileButton(wxDialog& dialog, const wxString& name, const wxString& title, const wxString& path) {
    auto* button = wxWindow::FindWindowByName(name, &dialog);
    Require(button, "original scheme import/export button exists");
    SchemeFile chooser(title, path);
    NativeSearch::Click(&dialog, button->GetId());
    chooser.Stop();
    Require(chooser.shown == 1, "scheme IO uses and answers its real native file dialog");
}
void Transaction(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    const int command = frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes...");
    Require(command != wxNOT_FOUND, "original Customize Schemes Ctrl+F12 command exists");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    TestEditor::ResetStream(editor, "int main() {\r\n  // comment\r\n  return 0;\r\n}\r\n", 20, 3);
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("edited"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 20, 3);
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::FoldLine, 0, std::to_underlying(Scintilla::FoldAction::Contract));
    const auto bytes = TestEditor::Text(editor);
    const auto encoding = frame->DocumentEncoding();
    const auto before = settings.values.fonts;
    const auto size = editor->Send(Message::StyleGetSizeFractional, DefaultStyle);
    Picker cancel;
    cancel.action = [editor, size](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(dialog.HasFlag(wxRESIZE_BORDER), "original customization resizes in both axes");
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source") && edit.IsEnabled(),
                "scheme node edits filename extensions");
        Require(!wxWindow::FindWindowByName("Style font", &dialog)->IsEnabled(),
                "font chooser disabled for scheme extensions");
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "global code font exists");
        edit.SetValue("font:$(Code); size:19");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1900 && size != 1900,
                "Apply previews raw style in actual editor");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, command);
    Require(cancel.error.empty(), cancel.error.utf8_str());
    Require(cancel.shown == 1 && settings.values.fonts == before &&
                editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == size,
            "Apply then Cancel restores original style settings and actual rendering");
    Require(TestEditor::Text(editor) == bytes && frame->DocumentEncoding() == encoding &&
                editor->Send(Message::GetAnchor) == 20 && editor->Send(Message::GetCurrentPos) == 3 &&
                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && !editor->Send(Message::GetFoldExpanded, 0),
            "style transaction preserves bytes encoding selection folds save point and Redo");
    Picker accept;
    accept.action = [](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "global code font exists");
        edit.SetValue("font:$(Code); size:17.5");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, command);
    Require(accept.error.empty(), accept.error.utf8_str());
    Require(accept.shown == 1 && editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1750 &&
                settings.values.fonts.codeStyle == "font:$(Code); size:17.5",
            "OK applies and retains the original raw style string");
}
void Extensions(EditorFrame* frame) {
    const int command = frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes...");
    Picker picker;
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source"), "C/C++ scheme extensions editable");
        edit.SetValue("style264");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, command);
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(frame->NewDocument(), "new clears the explicit document scheme override");
    const auto file = TestFiles::TemporaryDirectory("sample.style264");
    TestFiles::WriteBytes(file, "int value;\r\n");
    Require(frame->Open(TestFiles::Filename(file)).status == OpenStatus::Opened &&
                frame->Editor()->Send(Message::GetLexer) == SyntaxFor(SyntaxLanguage::Cpp).lexer,
            "accepted extension edits select the actual C/C++ lexer when opening a file");
}
void ImportCancelWithoutPreview(EditorFrame* frame, SettingsStore& settings) {
    const auto path = TestFiles::TemporaryDirectory("inheritance-only.ini");
    TestFiles::WriteBytes(path, "[C/C++ Source]\nUseDefaultCodeStyle=0\n");
    const auto before = frame->Editor()->Send(Message::StyleGetSizeFractional, DefaultStyle);
    Require(settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode && before == 1750,
            "prepare original no-Preview import with an active customized Code font");
    Picker picker;
    picker.action = [&path](wxDialog& dialog, wxTreeCtrl&, wxTextCtrl&) {
        FileButton(dialog, "Import schemes", "Import Schemes", TestFiles::Filename(path));
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(!settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode &&
                frame->Editor()->Send(Message::StyleGetSizeFractional, DefaultStyle) == before,
            "import then Cancel without Preview retains the original rendered font and unbacked inheritance flag");
}
void ImportExport(EditorFrame* frame, SettingsStore& settings) {
    const auto imported = TestFiles::TemporaryDirectory("style-import.ini");
    const auto exported = TestFiles::TemporaryDirectory("style-export.ini");
    TestFiles::WriteBytes(imported, "[Global Styles]\nDefault Code Style=font:$(Code); size:21\n"
                                    "[C/C++ Source]\nUseDefaultCodeStyle=0\nComment=fore:#AA5533\n"
                                    "[File Extensions]\nC/C++ Source=import264\n[Custom Colors]\n01=#112233\n");
    const auto originalStyle = settings.values.fonts.codeStyle;
    const auto originalExtension = SyntaxExtensionsFor(SyntaxLanguage::Cpp, settings.values.schemeExtensions);
    Picker picker;
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        FileButton(dialog, "Import schemes", "Import Schemes", TestFiles::Filename(imported));
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source") && edit.GetValue() == "import264",
                "original sparse scheme import updates extension text");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetFore, SCE_C_COMMENT) == 0x3355aa &&
                    frame->Editor()->Send(Message::StyleGetFore, SCE_C_COMMENTLINE) == 0x3355aa,
                "import preview applies both original grouped comment style IDs");
        FileButton(dialog, "Export schemes", "Export Schemes", TestFiles::Filename(exported));
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && settings.values.fonts.codeStyle == originalStyle &&
                SyntaxExtensionsFor(SyntaxLanguage::Cpp, settings.values.schemeExtensions) == originalExtension &&
                !settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode,
            "import then Cancel restores strings/extensions while original imported inheritance survives");
    const auto text = ReadIni(exported);
    Require(text.Contains("C/C++ Source=import264") && text.Contains("Comment=fore:#AA5533") &&
                text.Contains("01=#112233") && text.Contains("size:21"),
            "export includes pending styles extensions and the approved original-format custom palette");
    wxString evidence;
    if (wxGetEnv("WXNOTEPAD_STYLE_EVIDENCE", &evidence))
        Require(wxCopyFile(TestFiles::Filename(exported), wxFileName(evidence, "export.ini").GetFullPath()),
                "retain actual scheme export for independent original interoperability capture");
}
void Grammar(EditorFrame* frame) {
    Picker picker;
    picker.action = [frame](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source"), "select actual C/C++ default style");
        wxTreeItemIdValue cookie;
        const auto item = tree.GetFirstChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "Default", "first original scheme style is Default");
        tree.SelectItem(item);
        edit.SetValue("size:12.5junk; weight:600junk; fore:#112233junk; italic");
        NativeSearch::Click(&dialog, wxID_APPLY);
        auto* editor = frame->Editor();
        // Independent pinned original-09 recorded these literal prefix results.
        Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1250 &&
                    editor->Send(Message::StyleGetWeight, DefaultStyle) == 600 &&
                    editor->Send(Message::StyleGetFore, DefaultStyle) == 0x332211 &&
                    editor->Send(Message::StyleGetItalic, DefaultStyle),
                "original style grammar accepts numeric and hexadecimal prefixes before trailing text");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1, "raw style grammar tested through the actual customization dialog/editor");
}
void InvalidInheritedSize(EditorFrame* frame) {
    Picker picker;
    picker.action = [frame](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source"), "current base scheme exists");
        wxTreeItemIdValue cookie;
        tree.SelectItem(tree.GetFirstChild(tree.GetSelection(), cookie));
        edit.SetValue("size:15");
        SelectStyle(tree, "C/C++ Source", "Comment");
        edit.SetValue("size:bogus");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetSizeFractional, SCE_C_COMMENT) == 1500 &&
                    frame->Editor()->Send(Message::StyleGetSizeFractional, SCE_C_COMMENTLINE) == 1500,
                "invalid token size preserves the inherited scheme default size for both comment IDs");
        edit.SetValue("");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetSizeFractional, SCE_C_COMMENT) == 1500 &&
                    frame->Editor()->Send(Message::StyleGetFore, SCE_C_COMMENT) ==
                        frame->Editor()->Send(Message::StyleGetFore, DefaultStyle),
                "explicit empty styles inherit the actual scheme font size and body color");
        SelectStyle(tree, "C/C++ Source", "Default");
        edit.SetValue("font:$(Code); size:15");
        SelectStyle(tree, "C/C++ Source", "Comment");
        edit.SetValue("font:No such wxnotepad 264 font; size:+1.5; fore:invalid");
        NativeSearch::Click(&dialog, wxID_APPLY);
        auto* editor = frame->Editor();
        std::array<char, 256> bodyFace{}, commentFace{};
        editor->Send(Message::StyleGetFont, DefaultStyle, reinterpret_cast<intptr_t>(bodyFace.data()));
        editor->Send(Message::StyleGetFont, SCE_C_COMMENT, reinterpret_cast<intptr_t>(commentFace.data()));
        const auto base = DefaultStyleFont(editor->Fonts(), editor->Fonts().Scheme(SyntaxLanguage::Cpp).useCode)
                              .GetFractionalPointSize();
        Require(
            std::string_view(bodyFace.data()) == commentFace.data() &&
                editor->Send(Message::StyleGetSizeFractional, SCE_C_COMMENT) ==
                    static_cast<int>(std::lround(base * 100)) + 150 &&
                editor->Send(Message::StyleGetFore, SCE_C_COMMENT) == editor->Send(Message::StyleGetFore, DefaultStyle),
            "unavailable fonts fall back to Code while relative size and invalid color follow original inheritance");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
}
void RelativeFontChooser(EditorFrame* frame) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Use Default Code Style"));
    Picker picker;
    picker.action = [frame](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "global code font selectable");
        edit.SetValue("font:$(Code); size:18");
        SelectStyle(tree, "C/C++ Source", "Comment");
        edit.SetValue("size:+2; fore:#123456");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetSizeFractional, SCE_C_COMMENT) == 2000,
                "relative token size renders against the selected global code base");
        {
            NativeFonts::Chooser chooser(frame, 20, DefaultStyleFont(frame->Editor()->Fonts(), true).GetFaceName(),
                                         true, {}, true);
            NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Style font", &dialog)->GetId());
            chooser.Verify();
        }
        Require(edit.GetValue() == "size:+2; fore:#123456" && edit.HasFocus(),
                "native font chooser Cancel preserves relative text and restores edit focus");
        NativeFonts::Chooser accepted(frame, 20, DefaultStyleFont(frame->Editor()->Fonts(), true).GetFaceName(), false,
                                      {}, true);
        NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Style font", &dialog)->GetId());
        accepted.Verify();
        Require(edit.GetValue().Contains("size:15") && edit.GetValue().Contains("fore:#123456") &&
                    edit.GetValue().Contains("bold") && edit.GetValue().Contains("italic"),
                "accepted native font preserves color and serializes the chosen family/size/attributes");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetSizeFractional, SCE_C_COMMENT) == 1500,
                "accepted native font takes effect through actual Preview");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
}
void ColourChoosers(EditorFrame* frame, SettingsStore& settings) {
    const auto palette = settings.values.fonts.customColours;
    Picker picker;
    picker.action = [&settings](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        SelectStyle(tree, "C/C++ Source", "Comment");
        edit.SetValue("size:+1; italic; fore:#123456junk");
        {
            ColourPicker colour(wxColour(0x12, 0x34, 0x56), wxColour(0x44, 0x55, 0x66), false);
            NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Style foreground", &dialog)->GetId());
            Require(colour.shown && colour.error.empty(),
                    colour.error.empty() ? "native foreground chooser appeared" : colour.error.utf8_str().data());
        }
        Require(edit.GetValue().Contains("fore:#445566") && edit.GetValue().Contains("size:+1") &&
                    edit.GetValue().Contains("italic") && edit.HasFocus(),
                "native color acceptance retains other raw style attributes and returns edit focus");
        const auto accepted = edit.GetValue();
        {
            ColourPicker colour(wxColour(0x44, 0x55, 0x66), wxColour(0x33, 0x44, 0x55), true, true);
            NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Style foreground", &dialog)->GetId());
            Require(colour.shown && colour.error.empty(), "native foreground Cancel completed");
        }
        Require(edit.GetValue() == accepted, "native color Cancel retains the selected raw style");
#ifdef __WXMSW__
        Require(std::ranges::find(settings.values.fonts.customColours, 0x334455U) !=
                    settings.values.fonts.customColours.end(),
                "original Windows custom-palette addition survives inner chooser Cancel");
#endif
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(settings.values.fonts.customColours == palette, "outer Cancel restores the opening custom palette");
}
void ResetAndPersistence(EditorFrame* frame, SettingsStore& settings) {
    const int customize = frame->GetMenuBar()->FindMenuItem("Scheme", "Customize Schemes...");
    const auto code = settings.values.fonts.codeStyle;
    const auto extensions = SyntaxExtensionsFor(SyntaxLanguage::Cpp, settings.values.schemeExtensions);
    Picker reset;
    reset.action = [](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "global reset selection exists");
        edit.SetValue("size:99");
        NativeSearch::Click(&dialog, wxID_RESET);
        Require(edit.GetValue() == "font:$(Code)", "Default individual Reset uses the pinned source style");
        NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Reset all styles", &dialog)->GetId());
        Require(NativeDialogs::SelectTreeLabel(tree, "C/C++ Source") && edit.GetValue().StartsWith("c; cpp;"),
                "Reset All restores original source filename extensions");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, customize);
    Require(reset.error.empty(), reset.error.utf8_str());
    Require(settings.values.fonts.codeStyle == code &&
                SyntaxExtensionsFor(SyntaxLanguage::Cpp, settings.values.schemeExtensions) == extensions &&
                !settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode &&
                settings.values.fonts.Scheme(SyntaxLanguage::PlainText).useCode,
            "Reset All then Cancel restores style/extensions but retains the original reset inheritance choices");
    const int save = frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now");
    NativeSearch::Command(frame, save);
    SettingsStore restarted(settings.Path());
    Require(restarted.values.fonts.codeStyle == code &&
                SyntaxExtensionsFor(SyntaxLanguage::Cpp, restarted.values.schemeExtensions) == extensions,
            "ordinary Save Settings Now/restart retains customization despite disabled exit saving");
    const auto ini = ReadIni(TestFiles::Utf8Path(settings.Path().utf8_string()));
    Require(ini.Contains("; keep this comment") && ini.Contains("[Unavailable Scheme]") &&
                ini.Contains("UnknownStyle=fore:#112233"),
            "style persistence retains unavailable configuration and unrelated INI text");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Use Default Code Style"));
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Dark"));
    Picker dark;
    dark.action = [frame](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "Dark profile global style exists");
        edit.SetValue("font:$(Code); size:22; fore:#D4D4D4; back:#1E1E1E");
        NativeSearch::Click(&dialog, wxID_APPLY);
        Require(frame->Editor()->Send(Message::StyleGetSizeFractional, DefaultStyle) == 2200,
                "Dark Preview uses its edited actual native font size");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, customize);
    Require(dark.error.empty(), dark.error.utf8_str());
    NativeSearch::Command(frame, save);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Default"));
    Require(settings.values.fonts.codeStyle == code,
            "switching from edited Dark restores the independent Default profile");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Dark"));
    Require(settings.values.fonts.codeStyle.Contains("size:22"), "switching back retains edited Dark profile");
    Picker darkReset;
    darkReset.action = [](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Default Code Style"), "saved Dark style selectable");
        edit.SetValue("size:29");
        NativeSearch::Click(&dialog, wxID_RESET);
        Require(edit.GetValue().Contains("size:22"), "Dark Reset reloads the saved theme style");
        NativeSearch::Click(&dialog, wxWindow::FindWindowByName("Reset all styles", &dialog)->GetId());
        Require(edit.GetValue().Contains("size:22"), "Dark Reset All reloads saved theme styles");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, customize);
    Require(darkReset.error.empty(), darkReset.error.utf8_str());
    SettingsStore darkRestart(settings.Path());
    Require(darkRestart.values.fonts.theme == StyleTheme::Dark &&
                darkRestart.values.fonts.codeStyle.Contains("size:22"),
            "saved selected Dark profile reopens through the ordinary SettingsStore lifecycle");
}
void LocalizedLayout(EditorFrame* frame) {
    Picker picker;
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree, wxTextCtrl& edit) {
        Require(wxDisplay(&dialog).GetClientArea().Contains(dialog.GetScreenRect()),
                "localized original style dialog fits native work area");
        SelectStyle(tree, "C/C++ Source", "Comment");
        auto* defaults = wxWindow::FindWindowByName("Built-in default value", &dialog);
        auto* import = wxWindow::FindWindowByName("Import schemes", &dialog);
        auto* font = wxWindow::FindWindowByName("Style font", &dialog);
        auto* ok = dialog.FindWindow(wxID_OK);
        Require(defaults && import && font && ok, "localized native original controls are visible");
        for (auto* control : {static_cast<wxWindow*>(&tree), static_cast<wxWindow*>(&edit), defaults, import, font, ok})
            Require(dialog.GetClientRect().Contains(control->GetRect()) && control->IsShown(),
                    "localized controls fit inside the resizable dialog");
        const auto client = dialog.GetClientSize();
        const auto treeSize = tree.GetSize();
        const auto editSize = edit.GetSize();
        const auto defaultSize = defaults->GetSize();
        const auto importPosition = import->GetPosition();
        const auto okPosition = ok->GetPosition();
        dialog.SetClientSize(client + dialog.FromDIP(wxSize(120, 100)));
        dialog.Layout();
        const auto growth = dialog.GetClientSize() - client;
        Require(tree.GetSize() - treeSize == wxSize(0, growth.y) && edit.GetSize().x - editSize.x == growth.x &&
                    (edit.GetSize().y - editSize.y) + (defaults->GetSize().y - defaultSize.y) == growth.y &&
                    import->GetPosition() - importPosition == wxPoint(0, growth.y) &&
                    ok->GetPosition() - okPosition == wxPoint(growth.x, growth.y),
                "original two-axis anchors keep tree width, split edit height, left IO and bottom-right acceptance");
        NativeTreeInput::Present(dialog);
        tree.SetFocus();
        NativeTreeInput::Enter(dialog);
    };
    // Complete queued startup paint/title updates before publishing the unique
    // native focus target used by the unchanged shortcut acknowledgement guard.
    NativePaint::Complete(frame->Editor());
    frame->SetTitle("wxnotepad customize keyboard");
    NativeKeys::Shortcut(frame, WXK_F12, true, false, false, nullptr, "wxnotepad customize keyboard");
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && frame->Editor()->HasFocus(),
            "real Ctrl+F12 opens localized tree and native Enter returns editor focus");
}
class CustomizeApp final : public wxApp {
public:
    bool OnInit() override {
        wxString language;
        const bool localized = wxGetEnv("WXNOTEPAD_CUSTOMIZE_LANGUAGE", &language);
        if (localized) AppLocalization::Initialize(static_cast<uint16_t>(wxAtoi(language)));
        path_ = TestFiles::TemporaryDirectory("style-customization.ini");
        TestFiles::WriteBytes(
            path_,
            "[Settings]\nSaveSettings=0\n; keep this comment\n[Unavailable Scheme]\nUnknownStyle=fore:#112233\n");
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(path_));
        auto* frame = new EditorFrame(settings_.get());
        frame->SetWindowStyleFlag(frame->GetWindowStyleFlag() | wxSTAY_ON_TOP);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, localized](bool ready) {
            try {
                Require(ready, "native customization parent is presented");
                if (localized)
                    LocalizedLayout(frame);
                else {
#ifdef __WXMSW__
                    wxString gdi;
                    if (wxGetEnv("WXNOTEPAD_CUSTOMIZE_GDI", &gdi))
                        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Legacy GDI"));
#endif
                    Transaction(frame, *settings_);
                    ImportCancelWithoutPreview(frame, *settings_);
                    Extensions(frame);
                    ImportExport(frame, *settings_);
                    Grammar(frame);
                    InvalidInheritedSize(frame);
                    RelativeFontChooser(frame);
                    ColourChoosers(frame, *settings_);
                    ResetAndPersistence(frame, *settings_);
                }
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "style customization: {}", error.what());
            }
            frame->Close();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
    std::filesystem::path path_;
    std::unique_ptr<SettingsStore> settings_;
};
} // namespace
wxIMPLEMENT_APP(CustomizeApp);
