#include "settings/settings_store.h"
#include <wx/wx.h>
#include <cstdio>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <sys/stat.h>
#endif

class SettingsFailureApp final : public wxApp {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        wxLogNull quiet;
        SettingsStore settings;
#ifdef __WXMSW__
        const auto lock = CreateFileW(settings.Path().wc_str(), GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) return 2;
#else
        const auto path = settings.Path().ToStdString();
        if (chmod(path.c_str(), S_IWUSR) != 0) return 2;
#endif
        settings.values.wordWrap = false;
        settings.values.schemeTabs[0].tabWidth = 7;
        const bool saved = settings.Save(false);
        const bool explicitSaved = settings.Save(true);
        const bool suppressed = settings.SuppressMessage("OwnedWarning");
        const bool schemeSaved = settings.SaveSchemeTabs(SyntaxLanguage::PlainText);
#ifdef __WXMSW__
        CloseHandle(lock);
#else
        if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) return 2;
#endif
        if (saved || explicitSaved || suppressed || schemeSaved) {
            std::fprintf(stderr, "FAIL: unreadable existing configuration must reject every write\n");
            return 1;
        }
        std::puts("PASS: all settings write operations reject an unreadable existing INI");
        return 0;
    }
};
wxIMPLEMENT_APP(SettingsFailureApp);
