#pragma once

#include "document/document_session.h"
#include "file_helpers.h"
#include <string>

namespace TestFiles {
struct EncodedFixture {
    const char* name;
    FileEncoding encoding;
    std::string before;
    std::string after;
};
} // namespace TestFiles
