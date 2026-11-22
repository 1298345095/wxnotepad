#include "settings/settings_store.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/ffile.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void WriteSettings(const wxString& path, const wxString& text) {
    wxFFile output(path, "wb");
    Require(output.IsOpened() && output.Write(wxString(wxUniChar(0xfeff)) + text, wxMBConvUTF16LE()) && output.Close(),
            "write settings fixture");
}

wxString ReadSettings(const wxString& path) {
    wxFFile input(path, "rb");
    wxString text;
    Require(input.IsOpened() && input.ReadAll(&text, wxMBConvUTF16LE()), "read saved settings");
    return text;
}

void CheckScalarSettings(const wxString& path) {
    WriteSettings(path, " [sEtTiNgS] \n"
                        "wordwrap=0\nShowLineNumbers= 0suffix\nAutoIndent=\"0\"\n"
                        "TabWidth = +7suffix\nIndentWidth=invalid\n"
                        "[Settings]\nIndentWidth=9\n"
                        "[Other]\nWordWrap=0\nTabWidth=99\n"
                        "[gLoBaL sTyLeS]\n"
                        "default code style=wrong-case\nDefault Code Style = \" font:Fixture; size:12 \" \n"
                        "Default Text Style=\"\"\n"
                        "[Global Styles]\nDefault Text Style=ignored-duplicate\n"
                        "[SETTINGS2]\ndAtEtImEsHoRt = ' format %Y ' \nDATETIMELONG=\"\"\n"
                        "[Settings2]\nDateTimeLong=ignored-duplicate\n");
    const SettingsStore settings(path);
    const OriginalSettings defaults;
    Require(settings.values.wordWrap == defaults.wordWrap && !settings.values.showLineNumbers &&
                settings.values.autoIndent == defaults.autoIndent,
            "boolean keys are exact-case and retain leading 0/1 parsing without removing quotes");
    Require(settings.values.globalTabs.tabWidth == 7 &&
                settings.values.globalTabs.indentWidth == defaults.globalTabs.indentWidth,
            "integer keys use signed decimal prefixes and the first matching field, including invalid values");
    Require(settings.values.fonts.codeStyle == " font:Fixture; size:12 " &&
                settings.values.fonts.textStyle == defaults.fonts.textStyle,
            "string fields remove double quotes, retain enclosed spaces and use the fallback for empty values");
    Require(settings.values.dateTimeShort == " format %Y " && settings.values.dateTimeLong.empty(),
            "date formats use case-insensitive keys, either quote style and the first matching empty field");
}

void CheckHistorySections(const wxString& path) {
    const auto first = TestFiles::Filename(TestFiles::TemporaryDirectory("first.txt"));
    const auto second = TestFiles::Filename(TestFiles::TemporaryDirectory("second.txt"));
    const auto third = TestFiles::Filename(TestFiles::TemporaryDirectory("third.txt"));
    WriteSettings(path, "[Settings]\nSaveSettings=0\nSaveRecentFiles=65\nSaveFindReplace=1\n"
                        "[Settings2]\nRelativeFileMRU=0\n"
                        "[Recent Files]\n01=\"" +
                            first +
                            "\"\n"
                            "[Unrelated]\nKeep=preserved\n"
                            "[recent files]\n02=\"" +
                            second +
                            "\"\n"
                            "[Recent Find]\n01=\" older \"\n"
                            "[recent find]\n02=last\n"
                            "[Recent Replace]\n01=\"replacement\"\n"
                            "[Untouched]\nOther=retained\n");
    SettingsStore settings(path);
    Require(settings.values.recentFiles == std::vector<wxString>({first, second}),
            "file history reads each matching section in order");
    settings.AddRecentFile(third);
    settings.values.recentFind = {"next", " older "};
    settings.values.recentReplace.clear();
    Require(settings.Save(false), "save merged histories");

    const auto saved = ReadSettings(path);
    Require(
        saved.Contains("[Recent Files]\r\n01=\"" + third + "\"\r\n02=\"" + first + "\"\r\n03=\"" + second + "\"\r\n") &&
            !saved.Contains("[recent files]"),
        "file history replaces all matching sections with one numbered, quoted history");
    Require(saved.Contains("[Recent Find]\r\n01=\"next\"\r\n02=\" older \"\r\n03=\"last\"\r\n") &&
                !saved.Contains("[recent find]"),
            "search history retains merge order and whitespace while replacing duplicate sections");
    Require(saved.Contains("[Recent Replace]\r\n01=\"replacement\"\r\n") &&
                saved.Contains("[Unrelated]\r\nKeep=preserved\r\n") &&
                saved.Contains("[Untouched]\r\nOther=retained\r\n"),
            "an empty search history and unrelated sections retain their stored content");

    settings.ClearRecentFiles({});
    const auto cleared = ReadSettings(path);
    Require(settings.values.recentFiles.empty() && !cleared.Lower().Contains("[recent files]"),
            "clearing file history removes the entire section");
    Require(cleared.Contains("[Recent Find]") && cleared.Contains("[Untouched]\r\nOther=retained\r\n"),
            "clearing file history retains adjacent sections and search history");
}
} // namespace

class SettingsStoreApp final : public wxApp {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        try {
            const auto path = TestFiles::Filename(TestFiles::TemporaryDirectory("settings.ini"));
            CheckScalarSettings(path);
            CheckHistorySections(path);
            std::puts("PASS: settings field parsing and history replacement preserve original INI semantics");
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
            return 1;
        }
    }
};
wxIMPLEMENT_APP(SettingsStoreApp);
