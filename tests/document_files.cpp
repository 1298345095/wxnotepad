#include "document_files.h"
#include "document/document_file.h"
#include "file_helpers.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

size_t PendingFiles(const std::filesystem::path& destination) {
    const auto prefix = destination.filename().string() + ".wxnotepad-";
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(destination.parent_path())) {
        if (entry.path().filename().string().starts_with(prefix)) ++count;
    }
    return count;
}

void CancelPreparedSave(const std::filesystem::path& root) {
    const auto existing = root / "cancel-existing.txt";
    TestFiles::WriteBytes(existing, "original bytes");
    {
        DocumentFiles::SaveTransaction save;
        Require(save.Prepare(TestFiles::Filename(existing)).empty() &&
                    save.Prepare(TestFiles::Filename(existing)).empty(),
                "preparing a save is repeatable");
        Require(TestFiles::Bytes(existing) == "original bytes" && PendingFiles(existing) == 1,
                "preparation reserves one sibling and preserves the destination");
    }
    Require(TestFiles::Bytes(existing) == "original bytes" && PendingFiles(existing) == 0,
            "cancellation removes only the pending sibling");

    const auto unnamed = root / "cancel-new.txt";
    {
        DocumentFiles::SaveTransaction save;
        Require(save.Prepare(TestFiles::Filename(unnamed)).empty(), "a new destination can be prepared");
    }
    Require(!std::filesystem::exists(unnamed) && PendingFiles(unnamed) == 0,
            "a cancelled ordinary save leaves no new destination or pending sibling");
    std::puts("PASS: prepared saves preserve destinations and clean up on cancellation");
}

void CommitSavedBytes(const std::filesystem::path& root) {
    const auto destination = root / "commit.bin";
    TestFiles::WriteBytes(destination, "longer original contents");
    const std::string bytes("x\0y", 3);
    DocumentFiles::SaveTransaction save;
    Require(save.Commit(TestFiles::Filename(destination), bytes).empty() && TestFiles::Bytes(destination) == bytes &&
                PendingFiles(destination) == 0,
            "implicit preparation commits complete binary bytes without an old tail or pending sibling");
    std::puts("PASS: safe file commit replaces complete binary content");
}

void FailedReplacement(const std::filesystem::path& root) {
    const auto destination = root / "changed-destination";
    TestFiles::WriteBytes(destination, "old destination");
    {
        DocumentFiles::SaveTransaction save;
        Require(save.Prepare(TestFiles::Filename(destination)).empty(), "replacement fixture can reserve its sibling");
        Require(std::filesystem::remove(destination) && std::filesystem::create_directory(destination),
                "the destination changes after preparation");
        TestFiles::WriteBytes(destination / "sentinel", "retain this directory");
        Require(!save.Commit(TestFiles::Filename(destination), "new contents").empty(),
                "a replacement race reports a failed commit");
    }
    Require(TestFiles::Bytes(destination / "sentinel") == "retain this directory" && PendingFiles(destination) == 0,
            "a failed replacement retains the new destination and removes the pending sibling");
    std::puts("PASS: failed replacement retains the destination and cleans its sibling");
}

void PreservePreparedTimestamp(const std::filesystem::path& root) {
    const auto destination = root / "preserved-time.txt";
    TestFiles::WriteBytes(destination, "old contents");
    const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    std::filesystem::last_write_time(destination, old);
    const auto captured = std::filesystem::last_write_time(destination);
    DocumentFiles::SaveTransaction save(true);
    Require(save.Prepare(TestFiles::Filename(destination)).empty(), "original-time save prepares before editing");
    std::filesystem::last_write_time(destination, captured + std::chrono::hours(1));
    Require(save.Prepare(TestFiles::Filename(destination)).empty() &&
                save.Commit(TestFiles::Filename(destination), "new contents").empty(),
            "prepared original-time save commits");
    Require(std::filesystem::last_write_time(destination) == captured &&
                TestFiles::Bytes(destination) == "new contents",
            "repeated preparation retains the first captured timestamp through replacement");
    std::puts("PASS: original-time saves restore the timestamp captured at preparation");
}

void DestinationAfterDecision(const std::filesystem::path& root) {
    const auto original = root / "before-decision.txt";
    const auto changed = root / "after-decision.txt";
    TestFiles::WriteBytes(original, "original destination");
    TestFiles::WriteBytes(changed, "changed destination");
    auto destination = TestFiles::Filename(original);
    DocumentFiles::SaveTransaction save;
    Require(save.Prepare(destination).empty(), "save cleanup reserves a sibling before the decision");
    destination = TestFiles::Filename(changed);
    Require(save.Commit(destination, "decided contents").empty() &&
                TestFiles::Bytes(original) == "original destination" &&
                TestFiles::Bytes(changed) == "decided contents" && PendingFiles(original) == 0 &&
                PendingFiles(changed) == 0,
            "commit uses the destination supplied after a modal decision while retaining its prepared sibling");
    std::puts("PASS: save commit retains the destination read after a decision");
}

void PrepareCopyDestination(const std::filesystem::path& root) {
    const auto existing = root / "copy-existing.txt";
    const auto created = root / "copy-new.txt";
    TestFiles::WriteBytes(existing, "existing copy");
    Require(DocumentFiles::PrepareSaveCopy(TestFiles::Filename(existing)).empty() &&
                TestFiles::Bytes(existing) == "existing copy",
            "Save Copy preparation retains an existing destination's contents");
    Require(DocumentFiles::PrepareSaveCopy(TestFiles::Filename(created)).empty() && TestFiles::Bytes(created).empty(),
            "Save Copy preparation leaves a new empty destination before a later cancellation");
    Require(!DocumentFiles::PrepareSaveCopy(TestFiles::Filename(root)).empty(),
            "Save Copy preparation rejects a directory");
    std::puts("PASS: Save Copy preserves its early creation and cancellation behavior");
}
} // namespace

void CheckDocumentFileTransactions() {
    const TestFiles::TemporaryRoot root;
    CancelPreparedSave(root.Path());
    CommitSavedBytes(root.Path());
    FailedReplacement(root.Path());
    PreservePreparedTimestamp(root.Path());
    DestinationAfterDecision(root.Path());
    PrepareCopyDestination(root.Path());
}
