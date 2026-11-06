#pragma once

#include "document/document_session.h"
#include "file_helpers.h"
#include "app/editor_frame.h"
#include <array>
#include <string>

namespace TestFiles {
struct EncodedFixture {
    const char* name;
    FileEncoding encoding;
    std::string before;
    std::string after;
};
inline void RequireSavedUndoRedo(EditorFrame* frame, const std::filesystem::path& path, const EncodedFixture& fixture,
                                 const std::array<const char*, 3>& messages) {
    const auto save = [frame, &path](const std::string& expected, const char* message) {
        if (frame->Save().status != SaveStatus::Saved || Bytes(path) != expected) throw std::runtime_error(message);
    };
    save(fixture.after, messages[0]);
    frame->Editor()->Send(Scintilla::Message::Undo);
    save(fixture.before, messages[1]);
    frame->Editor()->Send(Scintilla::Message::Redo);
    save(fixture.after, messages[2]);
}
} // namespace TestFiles
