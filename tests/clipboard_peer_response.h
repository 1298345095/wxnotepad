#pragma once

#include "file_helpers.h"
#include <wx/wx.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>

namespace ClipboardPeerResponse {
inline std::optional<wxString> Wait(const wxString& path, std::chrono::steady_clock::time_point deadline) {
    constexpr unsigned PollMilliseconds = 10;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            // A published Windows response can briefly deny a reader access.
            // Wait for readable bytes within the caller's existing deadline.
            std::ifstream input(TestFiles::Utf8Path(path.utf8_string()), std::ios::binary);
            if (input) {
                const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                if (!input.bad()) return wxString::FromUTF8(bytes.data(), bytes.size());
            }
        }
        wxYield();
        wxMilliSleep(PollMilliseconds);
    }
    return std::nullopt;
}
} // namespace ClipboardPeerResponse
