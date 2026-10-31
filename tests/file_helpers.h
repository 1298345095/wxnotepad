#pragma once

#include <wx/string.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace TestFiles {
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
