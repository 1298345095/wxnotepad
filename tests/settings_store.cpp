#include "diagnostics/print.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/ffile.h>
#include <cstdio>
#include <print>
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

void CheckStyleFileIsolation(const wxString& path) {
    WriteSettings(path, "[Settings]\nWordWrap=1\n[Main Only]\nKeep=main\n"
                        "[Window Position 1024x768]\nRunDlgSizeX=321\n");
    const auto importFile = TestFiles::TemporaryDirectory("imported-styles.ini");
    const auto exportFile = TestFiles::TemporaryDirectory("exported-styles.ini");
    const auto importPath = TestFiles::Filename(importFile);
    const auto exportPath = TestFiles::Filename(exportFile);
    WriteSettings(importPath, "[Settings]\nWordWrap=0\n[Import Only]\nKeep=import\n"
                              "[Window Position 1024x768]\nRunDlgSizeX=999\n"
                              "[Global Styles]\nDefault Code Style=font:Fixture; size:17\n"
                              "[File Extensions]\nPython Script=fixture\n");
    WriteSettings(exportPath, "[Settings]\nWordWrap=0\n[Export Only]\nKeep=export\n");
    const auto importedBytes = TestFiles::Bytes(importFile);
    SettingsStore settings(path);
    const auto saveMain = [&](bool wordWrap) {
        Require(settings.Path() == path && settings.OriginalRunCommandWidth(1024, 768) == 321 &&
                    settings.OriginalRunCommandWidth(800, 600) == 321,
                "profile operations retain the primary file identity and cached section data");
        settings.values.wordWrap = wordWrap;
        Require(settings.Save(false), "ordinary settings save succeeds after an external profile operation");
        const SettingsStore restarted(path);
        const auto mainText = ReadSettings(path);
        Require(restarted.values.wordWrap == wordWrap && mainText.Contains("[Main Only]\r\nKeep=main\r\n") &&
                    !mainText.Contains("[Import Only]") && !mainText.Contains("[Export Only]"),
                "ordinary settings save updates the primary file and preserves only its unrelated sections");
        Require(TestFiles::Bytes(importFile) == importedBytes,
                "ordinary settings save leaves the import source intact");
    };

    const auto initialMain = ReadSettings(path);
    Require(settings.ImportStyles(importPath) && settings.values.fonts.codeStyle == "font:Fixture; size:17" &&
                settings.values.schemeExtensions.at(SyntaxLanguage::Python) == "fixture" && settings.values.wordWrap,
            "import reads styles and associations without importing ordinary settings");
    Require(ReadSettings(path) == initialMain, "style import leaves primary file bytes pending until settings save");
    saveMain(false);
    Require(SettingsStore(path).values.fonts.codeStyle == "font:Fixture; size:17",
            "imported styles are persisted by the following primary settings save");

    const auto beforeExport = ReadSettings(path);
    Require(settings.ExportStyles(exportPath), "export styles into an existing separate file");
    const auto exportedBytes = TestFiles::Bytes(exportFile);
    Require(ReadSettings(exportPath).Contains("[Export Only]\r\nKeep=export\r\n") &&
                ReadSettings(exportPath).Contains("Default Code Style=font:Fixture; size:17") &&
                ReadSettings(path) == beforeExport,
            "style export preserves destination sections and leaves the primary file untouched");
    saveMain(true);
    Require(TestFiles::Bytes(exportFile) == exportedBytes,
            "ordinary settings save leaves the export destination intact");

    const auto fonts = settings.values.fonts;
    const auto extensions = settings.values.schemeExtensions;
    const auto beforeFailure = ReadSettings(path);
    const auto missingImport = TestFiles::Filename(TestFiles::TemporaryDirectory("missing-styles.ini"));
    Require(!settings.ImportStyles(missingImport) && settings.values.fonts == fonts &&
                settings.values.schemeExtensions == extensions && ReadSettings(path) == beforeFailure,
            "failed import retains the current profile and primary file");
    saveMain(false);
    Require(TestFiles::Bytes(exportFile) == exportedBytes,
            "save after failed import leaves the export destination intact");

    const auto missingExport = TestFiles::Filename(TestFiles::TemporaryDirectory("missing-style-directory/export.ini"));
    Require(!settings.ExportStyles(missingExport) && !wxFileExists(missingExport),
            "export failure in an absent directory leaves no destination file");
    saveMain(true);
    Require(TestFiles::Bytes(exportFile) == exportedBytes, "save after failed export leaves the earlier export intact");
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
            CheckStyleFileIsolation(path);
            std::puts("PASS: settings INI semantics, history replacement and profile file isolation");
            return 0;
        } catch (const std::exception& error) {
            DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
            return 1;
        }
    }
};
wxIMPLEMENT_APP(SettingsStoreApp);
