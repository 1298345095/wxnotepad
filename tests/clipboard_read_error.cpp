#include "clipboard_read_error.h"
#include <wx/init.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
class DeliveredRecords final : public wxLog {
public:
    DeliveredRecords() : previous_(wxLog::SetActiveTarget(this)) {}
    ~DeliveredRecords() override { wxLog::SetActiveTarget(previous_); }
    unsigned records = 0;

private:
    void DoLogRecord(wxLogLevel, const wxString&, const wxLogRecordInfo&) override { ++records; }
    wxLog* const previous_;
};
} // namespace

int main() {
    const wxInitializer initialized;
    if (!initialized) return 1;
    try {
        DeliveredRecords delivered;
        {
            ClipboardReadErrors errors;
            Require(!errors.HasError() && !errors.BusyOnly(), "a false read without busy evidence is not retriable");
            wxLogSysError(static_cast<long>(CLIPBRD_E_CANT_OPEN), wxS("arbitrary translated clipboard error"));
            Require(errors.BusyOnly() && errors.LastCode() == static_cast<DWORD>(CLIPBRD_E_CANT_OPEN),
                    "actual wxLogSysError retains its signed HRESULT in structured context");
        }
        Require(wxLog::GetActiveTarget() == &delivered && delivered.records == 1,
                "the scoped observer forwards the record and restores its borrowed target");
        {
            ClipboardReadErrors errors;
            wxLogRecordInfo info;
            info.StoreValue(wxLOG_KEY_SYS_ERROR_CODE, static_cast<wxUIntPtr>(static_cast<DWORD>(CLIPBRD_E_CANT_OPEN)));
            wxLog::OnLog(wxLOG_Error, wxS("zero-extended error"), info);
            Require(errors.BusyOnly(), "zero-extended HRESULT has the same Windows error identity");
            wxLogSysError(static_cast<long>(E_ACCESSDENIED), wxS("another native error"));
            Require(errors.HasError() && !errors.BusyOnly(), "mixed native errors must not qualify as busy-only");
        }
        {
            ClipboardReadErrors errors;
            wxLogError(wxS("OpenClipboard text without structured busy code"));
            Require(errors.HasError() && !errors.BusyOnly(), "localized-looking text cannot authorize another read");
        }
        Require(wxLog::GetActiveTarget() == &delivered && delivered.records == 4,
                "all observed errors remain delivered to the unchanged borrowed log target");
        std::puts("PASS: clipboard read errors retain numeric identity, forwarding and scoped target ownership");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
