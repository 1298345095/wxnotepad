#include <cstdint>
#include <cstdio>
#include <print>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) try {
    if (argc < 2) {
        std::fputs("Usage: fuzz_*_replay INPUT...\n", stderr);
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        if (!file) {
            std::println(stderr, "Cannot read corpus input: {}", argv[index]);
            return 2;
        }
        const std::string bytes(std::istreambuf_iterator<char>{file}, {});
        if (LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) != 0) return 1;
    }
    std::println("PASS: replayed {:d} corpus inputs", argc - 1);
    return 0;
} catch (const std::exception& error) {
    std::println(stderr, "Corpus replay failed: {}", error.what());
    return 1;
}
