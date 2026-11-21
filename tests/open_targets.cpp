#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "SciLexer.h"
#include "editor_test_helpers.h"
#include "native_key_helpers.h"
#include "file_helpers.h"
#include "open_target_windows.h"
#include "open_target_manager.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/filename.h>
#include <wx/modalhook.h>
#include <wx/stdpaths.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstring>
#include <vector>
#include <wx/weakref.h>
#ifdef __WXMSW__
#include <objbase.h>
#include "native_published_file.h"
#endif
#ifdef __WXGTK__
#include <gio/gio.h>
#endif
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class TargetFeedback final : public wxModalDialogHook {
public:
    int shown = 0;
    TargetFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Could not open target") {
            ++shown;
            return wxID_OK;
        }
        return wxID_NONE;
    }
};
std::filesystem::path NativePath(const wxString& name) {
#ifdef __WXMSW__
    return std::filesystem::path(name.ToStdWstring());
#else
    return std::filesystem::path(name.utf8_string());
#endif
}
std::filesystem::path Directory() {
    wxString name;
    Require(wxGetEnv("WXNOTEPAD_OPEN_TARGET_ROOT", &name), "owned fixture directory must be configured");
    return NativePath(name);
}
void FileTarget(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto folder = Directory();
    const std::string contents = "first\nabcd中文\nlast\n";
    for (const auto& filename : {"target.txt", "目标 file.txt", "cwd-only.txt", "target.cpp", "name)"})
        std::ofstream(folder / NativePath(wxString::FromUTF8(filename)), std::ios::binary) << contents;
    std::filesystem::create_directory(folder / "documents");
    std::ofstream(folder / "documents" / "parent.txt") << "source\n";
    std::ofstream(folder / "documents" / "doc-only.txt", std::ios::binary) << contents;
    Require(frame->Open(TestFiles::Filename(folder / "parent.txt")).status == OpenStatus::Opened,
            "source document opens normally");
    const int command = frame->GetMenuBar()->FindMenuItem("Tools", "Open File, Folder, Link, etc.");
    const auto run = [&](const wxString& input, intptr_t anchor, intptr_t caret, const std::string& filename,
                         int expectedPosition, bool hotspot = false) {
        std::printf("BEGIN: native file target %s\n", input.utf8_str().data());
        std::filesystem::remove(folder / "child-received");
        const auto source = input.utf8_string();
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        if (!hotspot) editor->Send(Message::AppendText, 2, reinterpret_cast<intptr_t>("\n!"));
        editor->Send(Message::BeginUndoAction);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("?"));
        editor->Send(Message::EndUndoAction);
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, anchor, caret);
        if (hotspot) {
            editor->Send(Message::StyleSetHotSpot, 0, true);
        }
        NativeSearch::Command(frame, command);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!std::filesystem::exists(folder / "child-received") && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(std::filesystem::exists(folder / "child-received"),
                "selection action must launch an independent editor");
        Require(TestFiles::Bytes(folder / "child-received") ==
                    filename + "\n" + std::to_string(expectedPosition) + "\n",
                "child loads the independently specified disk file and caret location");
        const auto retained = source + (hotspot ? "" : "\n!");
        Require(TestEditor::Text(frame) == retained && (editor->Send(Message::GetModify) != 0) == !hotspot &&
                    editor->Send(Message::CanRedo),
                "launch retains source bytes, dirty state and pending redo");
        TestEditor::RequireSelection(editor, anchor, caret, "launch retains source selection");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(frame) == retained + "?", "launch preserves usable source undo/redo history");
        if (hotspot) editor->Send(Message::StyleSetHotSpot, 0, false);
        std::printf("PASS: file target %s -> %s position %d\n", source.c_str(), filename.c_str(), expectedPosition);
    };
    run("target.txt:2:4", 0, 14, "target.txt", 9);
    run("target.txt(2,4)", 0, 15, "target.txt", 9);
    run("target.txt:2", 0, 12, "target.txt", 6);
    run("target.txt(2)", 0, 13, "target.txt", 6);
    run("target.txt:2:6.", 0, 15, "target.txt", 13);
    run("target.txt:2:4\tignored", 0, 22, "target.txt", 9);
    run("target.txt:2:4\nignored", 0, 22, "target.txt", 9);
    run("prefix target.txt(2,4): warning", 10, 10, "target.txt", 9);
    run("prefix target.txt:2:4 suffix", 10, 10, "target.txt", 9);
    run("[alt](./target.txt)", 12, 12, "target.txt", 0);
    run("target.cpp", 0, 10, "target.cpp", 0);
    run("name)", 0, 5, "name)", 0);
    const auto quoted = "  '" + TestFiles::Filename(folder / NativePath(wxString::FromUTF8("目标 file.txt"))) + "'  ";
    run(quoted, 0, static_cast<intptr_t>(quoted.utf8_string().size()), "目标 file.txt", 0);
    run(wxString::FromUTF8("目标 file.txt"), 3, 3, "目标 file.txt", 0, true);
    wxSetEnv("WXNOTEPAD_OPEN_DIR", TestFiles::Filename(folder));
#ifdef __WXMSW__
    const wxString variable = "%WXNOTEPAD_OPEN_DIR%/target.txt";
#else
    const wxString variable = "$WXNOTEPAD_OPEN_DIR/target.txt";
#endif
    run(variable, 0, static_cast<intptr_t>(variable.utf8_string().size()), "target.txt", 0);
    editor->Send(Message::SetSavePoint);
    Require(frame->Open(TestFiles::Filename(folder / "documents" / "parent.txt")).status == OpenStatus::Opened,
            "relative-target source opens in its own directory");
    run("doc-only.txt", 0, 12, "doc-only.txt", 0);
    run("cwd-only.txt", 0, 12, "cwd-only.txt", 0);
    std::puts("PASS: native file targets, original location shapes, Unicode, selected boundaries, "
              "caret/Markdown/hotspot and native environment");
}
void Anchor(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int open = frame->GetMenuBar()->FindMenuItem("Tools", "Open File, Folder, Link, etc.");
    Require(open != wxNOT_FOUND, "Tools selection action must exist");
    const int folder = frame->GetMenuBar()->FindMenuItem("File", "Open Containing Folder");
    Require(folder != wxNOT_FOUND, "File containing-folder action must exist");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->FindItem(open)->IsEnabled() && !frame->GetMenuBar()->FindItem(folder)->IsEnabled(),
            "empty untitled document disables original path actions");
    const std::string text = "#target\n<element id=\"target\">\n<a name='target'>\n";
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::SetSel, 0, 7);
    NativeSearch::Command(frame, open);
    TestEditor::RequireSelection(editor, 33, 46, "name anchor precedes id anchor in original search order");
    editor->Send(Message::SetSel, 3, 3);
    NativeSearch::Command(frame, open);
    TestEditor::RequireSelection(editor, 33, 46, "caret-local anchor uses original target extraction");
    Require(TestEditor::Text(frame) == text && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "anchor action preserves all text and history");
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("#target\n<element id=\"target\">\n"));
    editor->Send(Message::SetSel, 0, 7);
    NativeSearch::Command(frame, open);
    TestEditor::RequireSelection(editor, 17, 28, "id anchor is used when no name anchor exists");
    editor->Send(Message::SetSavePoint);
    std::puts("PASS: native selection action, name/id anchor priority, source state and original menu availability");
}
void DesktopTargetCase(EditorFrame* frame, const std::string& input, const std::string& expected, intptr_t caret = -1) {
    auto* editor = frame->Editor();
    const auto marker = Directory() / "desktop-received";
    std::filesystem::remove(marker);
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(input.c_str()));
    editor->Send(Message::SetSel, caret < 0 ? 0 : caret, caret < 0 ? static_cast<intptr_t>(input.size()) : caret);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Open File, Folder, Link, etc."));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(std::filesystem::exists(marker), "desktop association must receive the target through a real child");
    if (TestFiles::Bytes(marker) != expected + "\n")
        std::fprintf(stderr, "desktop received [%s] expected [%s]\n", TestFiles::Bytes(marker).c_str(),
                     expected.c_str());
    Require(TestFiles::Bytes(marker) == expected + "\n",
            "desktop association receives exact independently specified target");
    Require(TestEditor::Text(frame) == input, "desktop action preserves source bytes");
    TestEditor::RequireSelection(editor, caret < 0 ? 0 : caret, caret < 0 ? static_cast<intptr_t>(input.size()) : caret,
                                 "desktop action preserves source selection");
    std::printf("PASS: desktop input %s -> %s\n", input.c_str(), expected.c_str());
}
void MailTargets(EditorFrame* frame) {
    DesktopTargetCase(frame, "reader@example.invalid", "mailto:reader@example.invalid");
    DesktopTargetCase(frame, "mailto:reader@example.invalid?subject=test%20value",
                      "mailto:reader@example.invalid?subject=test%20value");
    std::puts("PASS: real mailto handler preserves bare email and explicit mail URI bytes");
}
void DesktopTarget(EditorFrame* frame, const wxString& scheme, const wxString& extension,
                   bool standardSchemes = false) {
    const auto run = [frame](const std::string& input, const std::string& expected, intptr_t caret = -1) {
        DesktopTargetCase(frame, input, expected, caret);
    };
    const auto protocol = scheme.utf8_string();
    run(protocol + "://host:4321/path%20name", protocol + "://host:4321/path%20name");
    run(protocol + "://host/path(one)", protocol + "://host/path(one)");
    run("[alt](" + protocol + "://host/path(one))", protocol + "://host/path(one)", 12);
    run("prefix(" + protocol + "://host/target", protocol + "://host/target");
    if (standardSchemes) {
        run("https://example.invalid:4321/path%20name", "https://example.invalid:4321/path%20name");
        run("https://example.invalid/path(one)", "https://example.invalid/path(one)");
        run("[alt](https://example.invalid/path(one))", "https://example.invalid/path(one)", 12);
        run("prefix(https://example.invalid/target", "https://example.invalid/target");
        run("//example.invalid/path%20name", "http://example.invalid/path%20name");
        run("www.example.invalid/path", "http://www.example.invalid/path");
        MailTargets(frame);
        std::puts(
            "PASS: real standard-scheme handlers preserve explicit URLs, Markdown, mail, www and relative protocols");
    }
    const auto unsupported = NativePath("external" + extension);
    std::ofstream(Directory() / unsupported) << "owned unsupported target\n";
    const auto file = TestFiles::Filename(Directory() / unsupported);
    run(file.utf8_string(), file.utf8_string());
#ifdef __WXGTK__
    const auto directory = TestFiles::Filename(Directory() / "documents");
    run(directory.utf8_string(), directory.utf8_string());
#endif
    std::puts("PASS: controlled real desktop handlers preserve URI ports, encoding and unsupported files");
}
#ifdef __WXMSW__
void PublishExplorerRequest(const std::filesystem::path& path, const std::string& bytes) {
    auto staging = path;
    staging += ".tmp";
    std::ofstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.open(staging, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    // The observer must never see the final name while its writer is open.
    std::filesystem::rename(staging, path);
}
void PublishedExplorerRequest() {
    const auto path = Directory() / "request-publication.check";
    auto staging = path;
    staging += ".tmp";
    const std::string bytes = "directory/目标\n\n1234\ncancelled\n";
    const auto raw =
        CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(raw != INVALID_HANDLE_VALUE, "owned request staging lock must open");
    std::unique_ptr<void, decltype(&CloseHandle)> lock(raw, &CloseHandle);
    bool rejected = false;
    try {
        PublishExplorerRequest(path, bytes);
    } catch (const std::ios_base::failure&) {
        rejected = true;
    }
    Require(rejected && !std::filesystem::exists(path), "failed staging write must not publish a request name");
    lock.reset();
    std::filesystem::remove(staging);
    PublishExplorerRequest(path, bytes);
    Require(!std::filesystem::exists(staging) && TestFiles::Bytes(path) == bytes,
            "closed request publication must preserve the complete UTF-8 payload and empty item line");
    std::puts("PASS: closed staging publishes exact request bytes and failed staging leaves no request");
}
void PublishedAcknowledgement() {
    const auto root = Directory();
    const auto staging = root / "acknowledgement.tmp";
    const auto done = root / "acknowledgement.ready";
    TestFiles::WriteBytes(staging, "ok\n");
    const auto raw = CreateFileW(staging.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(raw != INVALID_HANDLE_VALUE, "owned marker rename handle must open");
    const std::unique_ptr<void, decltype(&CloseHandle)> rename(raw, &CloseHandle);
    const auto destination = done.wstring();
    const auto nameBytes = static_cast<DWORD>(destination.size() * sizeof(wchar_t));
    std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + nameBytes);
    auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    information->ReplaceIfExists = FALSE;
    information->RootDirectory = nullptr;
    information->FileNameLength = nameBytes;
    std::memcpy(information->FileName, destination.data(), nameBytes);
    Require(SetFileInformationByHandle(rename.get(), FileRenameInfo, information, static_cast<DWORD>(storage.size())),
            "owned marker must publish while its rename handle remains open");
    Require(std::filesystem::exists(done), "published marker name must already be visible");
    Require(NativePublishedFile::Read(done) == "ok\n",
            "published acknowledgement must be readable before the rename handle closes");
    std::puts("PASS: exact acknowledgement bytes while its publication DELETE handle remains open");
}
void WaitExplorerRequests(const std::filesystem::path& root, std::initializer_list<int> requests) {
    const auto finished = [&] {
        for (const int request : requests)
            if (!std::filesystem::exists(root / ("folder-done-" + std::to_string(request)))) return false;
        return true;
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!finished() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(finished(), "pending Explorer requests must complete within the original acknowledgement deadline");
    for (const int request : requests)
        Require(NativePublishedFile::Read(root / ("folder-done-" + std::to_string(request))) == "ok\n",
                "pending request acknowledgement must retain its exact bytes");
}
void PendingFolderTargets(EditorFrame* frame, int& request) {
    const auto root = Directory();
    const auto prepare = [&](const char* directory, bool cancelled) {
        const auto parent = root / directory;
        std::filesystem::create_directory(parent);
        std::ofstream(parent / "target.txt") << "owned pending target\n";
        const auto target = TestFiles::Filename(parent / "target.txt");
        const auto bytes = TestFiles::Filename(parent).utf8_string() + "\n" + target.utf8_string() + "\n" +
                           std::to_string(GetCurrentProcessId()) + "\n" + (cancelled ? "cancelled\n" : "");
        PublishExplorerRequest(root / ("folder-request-" + std::to_string(++request)), bytes);
        return target;
    };
    const auto first = prepare("pending-first", false);
    const int firstRequest = request;
    const auto second = prepare("pending-second", false);
    const int secondRequest = request;
    for (const auto& path : {first, second}) {
        const auto text = path.utf8_string();
        frame->Editor()->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
        frame->Editor()->Send(Message::SetSel, 0, static_cast<intptr_t>(text.size()));
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        // Keep both explicit menu requests pending before returning to the loop.
        NativeSearch::ActivateCommand(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Open Containing Folder"));
    }
    const auto responsive = std::make_shared<bool>(false);
    frame->CallAfter([responsive] { *responsive = true; });
    WaitExplorerRequests(root, {firstRequest, secondRequest});
    Require(*responsive, "the GUI loop must continue servicing callbacks while folder requests are pending");
    std::puts("PASS: two explicit native menu requests coexist while the GUI remains responsive");

    const auto cancelledPath = prepare("cancelled-owner", true);
    auto* cancelled = new EditorFrame;
    wxWeakRef<EditorFrame> weak(cancelled);
    Require(cancelled->Open(cancelledPath).status == OpenStatus::Opened, "cancellation source must open normally");
    cancelled->Show();
    cancelled->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    NativeSearch::ActivateCommand(cancelled, cancelled->GetMenuBar()->FindMenuItem("File", "Open Containing Folder"));
    cancelled->Destroy();
    WaitExplorerRequests(root, {request});
    Require(!weak, "closed native owner must be destroyed while the surviving source loop continues");
    std::puts("PASS: destroying the owner cancels pending selection and leaves the other frame usable");
}
#endif
void FolderTarget(EditorFrame* frame) {
    const auto root = Directory();
#ifdef __WXMSW__
    int request = 0;
    const auto run = [&](const std::filesystem::path& item, const char* menu, bool openDirectory = false) {
        ++request;
        const auto target = TestFiles::Filename(item);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        const auto apartmentResult = CoGetApartmentType(&apartment, &qualifier);
        std::printf("BEGIN: native Explorer request %d directory=%d target=%s cwd=%s\n", request, openDirectory,
                    target.utf8_str().data(), wxGetCwd().utf8_str().data());
        std::printf(
            "Native Explorer caller pid=%lu thread=%lu com_result=%08lx apartment=%d qualifier=%d attributes=%08lx\n",
            GetCurrentProcessId(), GetCurrentThreadId(), static_cast<unsigned long>(apartmentResult),
            static_cast<int>(apartment), static_cast<int>(qualifier), GetFileAttributesW(target.wc_str()));
        PublishExplorerRequest(root / ("folder-request-" + std::to_string(request)),
                               (openDirectory ? target : wxFileName(target).GetPath()).utf8_string() + "\n" +
                                   (openDirectory ? wxString{} : target).utf8_string() + "\n" +
                                   std::to_string(GetCurrentProcessId()) + "\n");
        NativeSearch::Command(frame,
                              frame->GetMenuBar()->FindMenuItem(menu, openDirectory ? "Open File, Folder, Link, etc."
                                                                                    : "Open Containing Folder"));
        const auto done = root / ("folder-done-" + std::to_string(request));
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(20);
        while (!std::filesystem::exists(done) && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        const bool acknowledged = std::filesystem::exists(done);
        const auto response = acknowledged ? NativePublishedFile::Read(done) : std::string{};
        if (!acknowledged || response != "ok\n") {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            std::fprintf(stderr,
                         "Explorer acknowledgement request=%d exists=%d elapsed_ms=%lld bytes=%zu hex=", request,
                         acknowledged, static_cast<long long>(elapsed.count()), response.size());
            for (const unsigned char byte : response)
                std::fprintf(stderr, "%02x", byte);
            std::fputc('\n', stderr);
        }
        Require(acknowledged && response == "ok\n",
                "native Explorer must display the owned directory and select the exact owned item");
    };
    run(root / "documents" / "parent.txt", "File");
    const std::string file = "target.txt:2:4";
    frame->Editor()->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(file.c_str()));
    frame->Editor()->Send(Message::SetSel, 0, static_cast<intptr_t>(file.size()));
    run(root / "target.txt", "Tools");
    const std::string directory = "documents";
    frame->Editor()->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(directory.c_str()));
    frame->Editor()->Send(Message::SetSel, 0, static_cast<intptr_t>(directory.size()));
    run(root / "documents", "Tools");
    run(root / "documents", "Tools", true);
    const auto unicodeName = wxString::FromUTF8("目标, selected file.txt");
    std::ofstream(root / NativePath(unicodeName)) << "owned Unicode selection target\n";
    const auto unicodeInput = (unicodeName + ":2:4").utf8_string();
    frame->Editor()->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(unicodeInput.c_str()));
    frame->Editor()->Send(Message::SetSel, 0, static_cast<intptr_t>(unicodeInput.size()));
    run(root / NativePath(unicodeName), "Tools");
    Require(TestEditor::Text(frame) == unicodeInput, "Unicode folder action retains exact source bytes");
    TestEditor::RequireSelection(frame->Editor(), 0, static_cast<intptr_t>(unicodeInput.size()),
                                 "Unicode folder action retains source selection");
    PendingFolderTargets(frame, request);
#elif defined(__WXGTK__)
    const auto marker = root / "filemanager-received";
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(std::filesystem::exists(marker), "native file manager must receive the selection request");
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Open Containing Folder"));
    wait();
    Require(TestFiles::Bytes(marker) == "file://" + frame->DocumentPath().utf8_string() + "\n",
            "File containing folder requests selection of the named current document");
    std::filesystem::remove(marker);
    const std::string target = "target.txt:2:4";
    frame->Editor()->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(target.c_str()));
    frame->Editor()->Send(Message::SetSel, 0, static_cast<intptr_t>(target.size()));
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Open Containing Folder"));
    wait();
    Require(TestFiles::Bytes(marker) == "file://" + TestFiles::Filename(root / "target.txt").utf8_string() + "\n",
            "selection containing folder requests the resolved file without its location suffix");
    std::ofstream(root / "filemanager-reject") << "reject";
    std::filesystem::remove(root / "desktop-received");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Open Containing Folder"));
    const auto fallback = root / "desktop-received";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::exists(fallback) && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(std::filesystem::exists(fallback) &&
                TestFiles::Bytes(fallback) == TestFiles::Filename(root / "documents").utf8_string() + "\n",
            "desktop association opens the parent folder when item selection is unsupported");
#endif
}
void NoopAndFailure(EditorFrame* frame, TargetFeedback& feedback) {
    auto* editor = frame->Editor();
    for (const std::string input : {"", "   ", "missing-target-name.txt", "'目标 file.txt'"}) {
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(input.c_str()));
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        editor->Send(Message::SetSel, 3, 3);
        const auto caret = editor->Send(Message::GetCurrentPos);
        Require(frame->OpenSelection(), "original unsupported caret shapes remain a no-op");
        Require(TestEditor::Text(frame) == input && editor->Send(Message::GetCurrentPos) == caret &&
                    !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "no-op preserves source, caret and history");
    }
    const std::string target = "target.txt\n!";
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(target.c_str()));
    editor->Send(Message::SetSel, 0, 10);
    const auto executable = wxStandardPaths::Get().GetExecutablePath();
#ifdef __WXMSW__
    const auto moved = executable + ".moved";
    Require(wxRenameFile(executable, moved), "temporarily move only the owned copied executable");
#else
    const auto binary = std::filesystem::path(executable.utf8_string());
    const auto permissions = std::filesystem::status(binary).permissions();
    std::filesystem::permissions(binary, std::filesystem::perms::none);
#endif
    const bool launched = frame->OpenSelection();
    const int before = feedback.shown;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Open File, Folder, Link, etc."));
#ifdef __WXMSW__
    const bool restored = wxRenameFile(moved, executable);
#else
    std::filesystem::permissions(binary, permissions);
    const bool restored = true;
#endif
    Require(restored && !launched && feedback.shown == before + 1,
            "failed executable startup is observable through both result and command feedback");
    Require(TestEditor::Text(frame) == target && editor->Send(Message::GetModify),
            "failed launch preserves dirty source bytes");
    TestEditor::RequireSelection(editor, 0, 10, "failed launch preserves source selection");
    std::puts("PASS: original no-op shapes and observable launch failure retain a usable source document");
}
void OpenChildDocument(EditorFrame* frame, const wxString& path, const std::optional<wxString>& caret) {
    long long line = 0, column = 0;
    if (caret) {
        const auto location = wxSplit(*caret, ',');
        Require(location.size() == 2 && location[0].ToLongLong(&line) && location[1].ToLongLong(&column),
                "child location uses real startup line,column syntax");
    }
    Require(frame->OpenRestartDocument(path, {}, {}, false, line, column).status == OpenStatus::Opened,
            "child reuses ordinary startup document loading");
}
class OpenTargetApp final : public wxApp {
    int result_ = 1;

public:
    bool OnInit() override {
        const bool standard = argc == 3 && wxString(argv[1]) == "--desktop-standards";
        const bool mail = argc == 3 && wxString(argv[1]) == "--desktop-mail-standards";
        if (standard || mail) wxSetEnv("WXNOTEPAD_OPEN_TARGET_ROOT", wxString(argv[2]));
        if (argc == 4 && wxString(argv[1]) == "--desktop-target")
            wxSetEnv("WXNOTEPAD_OPEN_TARGET_ROOT", wxString(argv[3]));
#ifdef __WXMSW__
        const auto log = Directory() / ("native-" + std::to_string(wxGetProcessId()));
        _wfreopen((log.wstring() + L".stdout.log").c_str(), L"wb", stdout);
        _wfreopen((log.wstring() + L".stderr.log").c_str(), L"wb", stderr);
#endif
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::printf("BEGIN process %lu argc=%d\n", wxGetProcessId(), argc);
#ifdef __WXGTK__
        if (argc == 2 && wxString(argv[1]) == "--file-manager") {
            RunOwnedFileManager(Directory());
            return false;
        }
#endif
        if (argc >= 3 && wxString(argv[1]) == "--desktop-target") {
            std::ofstream(Directory() / ("received-" + std::to_string(wxGetProcessId()) + ".txt"), std::ios::binary)
                << wxString(argv[2]).utf8_string() << '\n';
            std::ofstream(Directory() / "desktop-received.tmp", std::ios::binary)
                << wxString(argv[2]).utf8_string() << '\n';
            std::filesystem::rename(Directory() / "desktop-received.tmp", Directory() / "desktop-received");
            std::exit(0);
        }
#ifdef __WXMSW__
        if (argc == 1) {
            try {
                PublishedAcknowledgement();
                PublishedExplorerRequest();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                return false;
            }
        }
#endif
        auto* frame = new EditorFrame;
        const bool child = argc > 1 && wxString(argv[1]) == "--startup-file";
        if (child) {
            Require((argc == 3 || argc == 5) && (argc == 3 || wxString(argv[3]) == "--startup-caret"),
                    "child receives ordinary startup file and one-based caret arguments");
            OpenChildDocument(frame, argv[2], argc == 5 ? std::optional(wxString(argv[4])) : std::nullopt);
        }
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, child, standard, mail](bool ready) {
            try {
                Require(ready, "native frame must be presented");
                if (child) {
                    Require(TestEditor::Text(frame) == "first\nabcd中文\nlast\n",
                            "child independently loads exact disk bytes");
                    if (wxFileName(frame->DocumentPath()).GetFullName() == "target.cpp")
                        Require(frame->Editor()->Send(Message::GetLexer) == SCLEX_CPP,
                                "ordinary startup retains delivered filename syntax classification");
                    std::ofstream(Directory() / "child-received.tmp", std::ios::binary)
                        << wxFileName(frame->DocumentPath()).GetFullName().utf8_string() << '\n'
                        << frame->Editor()->Send(Message::GetCurrentPos) << '\n';
                    std::filesystem::rename(Directory() / "child-received.tmp", Directory() / "child-received");
#ifdef __WXMSW__
                } else if (mail) {
                    TargetFeedback feedback;
                    OwnedTargetAssociations::RequireMailHandler();
                    MailTargets(frame);
                    Require(feedback.shown == 0, "successful mail commands must not report launch failure");
                } else if (standard) {
                    TargetFeedback feedback;
                    OwnedTargetAssociations associations;
                    associations.RequireStandardHandlers();
                    DesktopTarget(frame, associations.Scheme(), associations.Extension(), true);
                    Require(feedback.shown == 0, "successful standard commands must not report launch failure");
                    Require(associations.Remove(), "fresh owned native associations must be removed");
#endif
                } else {
                    TargetFeedback feedback;
                    Anchor(frame);
                    FileTarget(frame);
                    FolderTarget(frame);
#ifdef __WXMSW__
                    {
                        OwnedTargetAssociations associations;
                        DesktopTarget(frame, associations.Scheme(), associations.Extension());
                        Require(associations.Remove(), "fresh owned native associations must be removed");
                    }
#else
                    DesktopTarget(frame, "wx242", ".wx242", true);
#endif
                    Require(feedback.shown == 0, "successful commands must not report launch failure");
                    NoopAndFailure(frame, feedback);
                }
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
};
} // namespace
wxIMPLEMENT_APP(OpenTargetApp);
