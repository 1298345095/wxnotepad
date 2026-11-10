#include <cstdint>
#include <cstdio>
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
            std::fprintf(stderr, "Cannot read corpus input: %s\n", argv[index]);
            return 2;
        }
        const std::string bytes(std::istreambuf_iterator<char>{file}, {});
        if (LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) != 0) return 1;
    }
    std::printf("PASS: replayed %d corpus inputs\n", argc - 1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "Corpus replay failed: %s\n", error.what());
    return 1;
}
