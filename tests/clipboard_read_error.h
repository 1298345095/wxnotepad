#pragma once

#include <wx/log.h>
#include <wx/msw/wrapwin.h>

// Observe the real wx GetData call without changing its error delivery.
class ClipboardReadErrors final : public wxLog {
public:
    ClipboardReadErrors() : previous_(wxLog::SetActiveTarget(this)) {}
    ~ClipboardReadErrors() override { wxLog::SetActiveTarget(previous_); }
    bool BusyOnly() const { return busy_ && !other_; }
    bool HasError() const { return busy_ || other_; }
    DWORD LastCode() const { return lastCode_; }

private:
    void DoLogRecord(wxLogLevel level, const wxString& message, const wxLogRecordInfo& info) override {
        if (level == wxLOG_Error) {
            wxUIntPtr code = 0;
            const bool hasCode = info.GetNumValue(wxLOG_KEY_SYS_ERROR_CODE, &code);
            // wx stores a signed Windows long in wxUIntPtr. Compare HRESULT's
            // 32 bits for both sign-extended and zero-extended record values.
            lastCode_ = static_cast<DWORD>(code);
            if (hasCode && lastCode_ == static_cast<DWORD>(CLIPBRD_E_CANT_OPEN))
                busy_ = true;
            else
                other_ = true;
        }
        if (previous_) previous_->LogRecord(level, message, info);
    }
    wxLog* const previous_;
    bool busy_ = false;
    bool other_ = false;
    DWORD lastCode_ = 0;
};
