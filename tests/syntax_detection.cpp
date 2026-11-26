#include "syntax/syntax_detection.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Check(const wxString& path, std::string_view text, std::optional<SyntaxLanguage> expected,
           SyntaxDetectionOptions options = {}) {
    const auto actual = DetectLanguage(path, text, options);
    if (actual ? expected && actual->schemeLanguage == *expected : !expected) return;
    throw std::runtime_error("syntax detection mismatch for " + path.utf8_string() + ": expected " +
                             std::to_string(expected ? static_cast<int>(*expected) : -1) + ", actual " +
                             std::to_string(actual ? static_cast<int>(actual->schemeLanguage) : -1));
}

void CheckDetection() {
    using enum SyntaxLanguage;
    constexpr auto python = "#!/usr/bin/env python3\n";
    Check("sample.cpp", python, Cpp);
    Check("sample.txt", python, PlainText);
    Check("sample.rb", python, Ruby);
    Check("sample.cgi", python, Python);
    Check("sample.unknown", python, Python);
    Check("sample.conf", python, Python);
    Check("sample.conf", "", PlainText);
    Check("CMakeLists.txt", python, CMake);
    Check("Makefile.in", python, Makefile);
    Check("sample.cpp.in", python, Cpp);
    Check("sample.conf.in", python, Python);
    Check("sample.unknown", "[section]\nkey=value\n", Ini);
    Check(".settings", "#define FEATURE\n[section]\nkey=value\n", Ini);
    Check(".settings", "#notpreprocessor\n[section]\nkey=value\n", std::nullopt);

    SyntaxDetectionOptions options;
    options.noCgiGuess = true;
    Check("sample.unknown", python, std::nullopt, options);
    options.autoSelect = false;
    Check("sample.cpp", python, std::nullopt, options);

    options = {};
    options.extensions = {{Cpp, "shared"}, {Python, "shared"}};
    Check("sample.shared", python, Cpp, options);
    options.favoriteSchemes = "32 1";
    Check("sample.shared", "", Python, options);
    options.favoriteSchemes = "1 32";
    Check("sample.shared", "", Cpp, options);

    const auto markup = DetectLanguage("sample.xml", "<!DOCTYPE html>");
    Require(markup && markup->schemeLanguage == Xml && markup->markupDialect == Html,
            "markup scheme identity and detected dialect remain distinct");
    options = {};
    options.noHtmlGuess = true;
    Check("sample.unknown", "<node/>", std::nullopt, options);
    Check("sample.unknown", "<?xml version=\"1.0\"?><node/>", Xml, options);
    Check("sample.unknown", std::string(255, ' ') + "<html>", std::nullopt);
    Require(!DetectMarkupDocType(std::string(3999, ' ') + "<!DOCTYPE html>"),
            "doctype sniffing retains its original bounded input");
}
} // namespace

class SyntaxDetectionApp final : public wxApp {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        try {
            CheckDetection();
            std::puts("PASS: syntax detection precedence, overrides, dialects and bounded sniffing");
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
            return 1;
        }
    }
};
wxIMPLEMENT_APP(SyntaxDetectionApp);
