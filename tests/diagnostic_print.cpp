#include "diagnostics/print.h"
#include "completion_allocation_fault.h"
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
struct AllocatingText {
    std::string_view value;
};
} // namespace

template <> struct std::formatter<AllocatingText> : std::formatter<std::string_view> {
    template <typename FormatContext> auto format(const AllocatingText& text, FormatContext& context) const {
        // Exercise a real allocation during formatting even when the standard
        // library streams its own output directly to FILE without a buffer.
        const std::string copy(text.value);
        return std::formatter<std::string_view>::format(copy, context);
    }
};

namespace {
class DiagnosticFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const char* message) {
    if (!condition) throw DiagnosticFailure(message);
}

struct CloseFile {
    void operator()(FILE* file) const noexcept { std::fclose(file); }
};
using File = std::unique_ptr<FILE, CloseFile>;
File Open(const std::filesystem::path& path, const char* mode) {
    File file(std::fopen(path.string().c_str(), mode));
    Require(file != nullptr, "open the owned diagnostic fixture file");
    Require(std::setvbuf(file.get(), nullptr, _IONBF, 0) == 0, "observe each real file-write result immediately");
    return file;
}

void SuccessfulOutput(FILE* file) {
    Require(DiagnosticPrint::Print(file, "{} {:d}", "print", 42), "print a prefix without adding a newline");
    Require(DiagnosticPrint::Print(file, " {:04x} {:d}\n", 42, true), "retain typed hex and numeric boolean output");
    Require(DiagnosticPrint::Print(file, "{}\n", "中文"), "retain UTF-8 output bytes");
}

void WriteFailure(const std::filesystem::path& directory, FILE* recovered) {
    auto input = Open(directory / "input.txt", "rb");
    Require(!DiagnosticPrint::Print(input.get(), "unwritable {:d}\n", 42),
            "a real write to an input-only FILE reports failure without escaping");
    Require(std::ferror(input.get()) != 0, "the native FILE records the actual write failure");
    Require(DiagnosticPrint::Print(recovered, "recovered\n"), "a later valid diagnostic still succeeds");
}

void AllocationFailure(FILE* file) {
    // Build the input before arming the injector; the formatter owns the copy
    // whose allocation must fail inside the production diagnostic boundary.
    const std::string payload(8192, 'x');
    const AllocatingText text{payload};
    bool printed = true;
    bool injected = false;
    {
        const CompletionAllocationFault::ScopedFailure failure;
        printed = DiagnosticPrint::Print(file, "{}\n", text);
        injected = failure.Injected();
    }
    Require(injected && !printed, "a real formatting allocation failure reaches the diagnostic boundary");
    Require(DiagnosticPrint::Print(file, "{}\n", text),
            "the same formatter and payload recover after allocation failure");
    Require(DiagnosticPrint::Print(file, "recovered\n"), "diagnostics recover after the failed allocation");
}
} // namespace

int main(int argc, char** argv) try {
    Require(argc == 3, "provide the scenario and its private fixture directory");
    const std::string_view scenario(argv[1]);
    const std::filesystem::path directory(argv[2]);
    auto output = Open(directory / "output.txt", "wb");
    if (scenario == "success")
        SuccessfulOutput(output.get());
    else if (scenario == "write-failure")
        WriteFailure(directory, output.get());
    else if (scenario == "allocation-failure")
        AllocationFailure(output.get());
    else
        throw DiagnosticFailure("unknown diagnostic-print scenario");
    return 0;
} catch (const std::exception& error) {
    DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
    return 1;
}
