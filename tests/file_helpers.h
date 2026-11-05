#ifndef WXNOTEPAD_TESTS_FILE_HELPERS_H
#define WXNOTEPAD_TESTS_FILE_HELPERS_H

#include <wx/string.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace TestFiles {
class TemporaryRoot {
public:
    TemporaryRoot() {
        const auto name = wxFileName::CreateTempFileName("wxnotepad-tests-");
        // Reject collisions when converting the reserved name to a directory.
        // wxMkdir creates it atomically; Unix access is restricted to the owner.
        if (name.empty() || !wxRemoveFile(name) || !wxMkdir(name, 0700))
            throw std::runtime_error("create private test directory");
#ifdef __WXMSW__
        path_ = std::filesystem::canonical(std::filesystem::path(name.ToStdWstring()));
#else
        path_ = std::filesystem::canonical(std::filesystem::path(name.ToStdString(wxConvUTF8)));
#endif
    }
    ~TemporaryRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TemporaryRoot(const TemporaryRoot&) = delete;
    TemporaryRoot& operator=(const TemporaryRoot&) = delete;
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

inline std::filesystem::path TemporaryDirectory() {
    // Document Open and native dialogs expand Windows TEMP's 8.3 aliases.
    static const TemporaryRoot root;
    return root.Path();
}
inline wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
inline std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) throw std::runtime_error("saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace TestFiles

#endif // WXNOTEPAD_TESTS_FILE_HELPERS_H
