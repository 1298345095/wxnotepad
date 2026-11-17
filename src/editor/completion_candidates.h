#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// Original Notepad4 word limits leave room for a space, parentheses and NUL.
inline constexpr unsigned CompletionWordLimit = 124;
inline constexpr unsigned CompletionWordBufferSize = 128;

// A completion attempt's sorted, exact-duplicate-free candidates. The root is
// borrowed, must stay unchanged and must outlive this object; no NUL terminator
// or padding is needed. Root and candidate text contain no embedded NUL bytes.
// Candidate text is copied into owned storage before an add operation returns.
class CompletionCandidates final {
public:
    CompletionCandidates(std::string_view root, bool ignoreCase);
    CompletionCandidates(const CompletionCandidates&) = delete;
    CompletionCandidates& operator=(const CompletionCandidates&) = delete;

    std::string_view Root() const noexcept { return root_; }
    bool IgnoreCase() const noexcept { return ignoreCase_; }
    bool MatchesPrefix(std::string_view word) const noexcept;
    void AddKeywords(const char* list);
    // The caller supplies a prefix-matched document word, with
    // word.size() < CompletionWordBufferSize after the original suffix
    // processing. No NUL terminator is needed. Qualified subwords use the same
    // prefix and ordering rules.
    void AddDocumentWord(std::string_view word, bool includeSubwords);
    // Only an empty attempt can retry the suffix following a special prefix.
    // A populated set retains its root and the ordering used by its existing tree.
    bool RetrySubRoot() noexcept;
    unsigned Count() const noexcept { return count_; }
    bool HasCompletion() const noexcept;
    std::unique_ptr<char[]> Serialize() const;

private:
    struct Node;
    static Node* Skew(Node* node) noexcept;
    static Node* Split(Node* node) noexcept;
    static char* Word(Node* node) noexcept;
    static const char* Word(const Node* node) noexcept;
    uint32_t SortKey(std::string_view word) const noexcept;
    int CompareNode(const Node& node, std::string_view word, uint32_t sortKey) const noexcept;
    void AddBuffer(unsigned capacity);
    void AddWord(std::string_view word);
    void AddSubwords(std::string_view word);

    std::string_view root_;
    bool ignoreCase_;
    uint32_t rootSortKey_;
    Node* head_ = nullptr;
    unsigned count_ = 0;
    unsigned totalLength_ = 0;
    unsigned offset_ = 0;
    unsigned capacity_ = 0;
    std::byte* buffer_ = nullptr;
    std::vector<std::vector<std::byte>> buffers_;
    std::vector<Node*> insertionPath_;
};
