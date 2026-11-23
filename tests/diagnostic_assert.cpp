#include "editor/platform/core_headers.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#ifndef _WIN32
#include <sys/resource.h>
#endif

int main() noexcept {
#ifdef _WIN32
    // This owned abort fixture must not display CRT or crash-reporting dialogs.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    const rlimit limit{0, 0};
    if (setrlimit(RLIMIT_CORE, &limit) != 0) return 84;
#endif
    // A diagnostic exception escaping Assert's noexcept boundary is distinct
    // from its required abort, on both MSVC and POSIX.
    std::set_terminate([] { std::_Exit(83); });
    if (std::setvbuf(stderr, nullptr, _IONBF, 0) != 0) return 84;
    Scintilla::Internal::Platform::Assert("owned assertion", "diagnostic_assert.cpp", 42);
}
