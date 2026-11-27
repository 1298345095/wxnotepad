// Adapted from Notepad4 89b8b36 EditAutoC.cpp: WordList.
// Copyright and BSD 3-Clause license: licenses/Notepad4.txt.
#include "editor/completion_candidates.h"
#include "editor/ascii_character.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace {
constexpr unsigned SortKeyLength = 4;
constexpr unsigned InitialBufferSize = 4096;
constexpr size_t InitialTreePathCapacity = 32;

constexpr bool IsKeywordSeparator(unsigned char ch) noexcept {
    return ch <= ' ' || ch == '(' || ch == ')' || ch == ',' || ch == '.' || ch == ';' || ch == '^';
}

constexpr bool IsSpecialStart(char ch) noexcept {
    return ch == ':' || ch == '.' || ch == '#' || ch == '@' || ch == '<' || ch == '\\' || ch == '/' || ch == '-' ||
           ch == '>' || ch == '$' || ch == '%';
}
} // namespace

struct CompletionCandidates::Node {
    std::array<Node*, 2> link;
    uint32_t sortKey;
    unsigned length;
    unsigned level;
};

CompletionCandidates::CompletionCandidates(std::string_view root, bool ignoreCase)
    : root_(root), ignoreCase_(ignoreCase), rootSortKey_(SortKey(root)) {
    insertionPath_.reserve(InitialTreePathCapacity);
    AddBuffer(InitialBufferSize);
}

char* CompletionCandidates::Word(Node* node) noexcept {
    return reinterpret_cast<char*>(node) + sizeof(Node);
}

const char* CompletionCandidates::Word(const Node* node) noexcept {
    return reinterpret_cast<const char*>(node) + sizeof(Node);
}

uint32_t CompletionCandidates::SortKey(std::string_view word) const noexcept {
    uint32_t key = 0;
    // Read only the supplied bytes. The original cached key had the same
    // big-endian value but required four readable bytes even for a short root.
    for (unsigned index = 0; index < SortKeyLength; ++index) {
        key <<= 8;
        if (index < word.size()) {
            const auto ch = static_cast<unsigned char>(word[index]);
            key |= ignoreCase_ ? Ascii::Lower(ch) : ch;
        }
    }
    return key;
}

bool CompletionCandidates::MatchesPrefix(std::string_view word) const noexcept {
    if (word.size() < root_.size()) return false;
    if (root_.size() <= SortKeyLength) return rootSortKey_ == SortKey(word.substr(0, root_.size()));
    for (size_t index = 0; index < root_.size(); ++index) {
        const auto left = static_cast<unsigned char>(root_[index]);
        const auto right = static_cast<unsigned char>(word[index]);
        if ((ignoreCase_ ? Ascii::Lower(left) : left) != (ignoreCase_ ? Ascii::Lower(right) : right)) return false;
    }
    return true;
}

CompletionCandidates::Node* CompletionCandidates::Skew(Node* node) noexcept {
    if (node->link[0] && node->level == node->link[0]->level) {
        Node* save = node->link[0];
        node->link[0] = save->link[1];
        save->link[1] = node;
        return save;
    }
    return node;
}

CompletionCandidates::Node* CompletionCandidates::Split(Node* node) noexcept {
    if (node->link[1] && node->link[1]->link[1] && node->level == node->link[1]->link[1]->level) {
        Node* save = node->link[1];
        node->link[1] = save->link[0];
        save->link[0] = node;
        ++save->level;
        return save;
    }
    return node;
}

void CompletionCandidates::AddBuffer(unsigned capacity) {
    buffers_.emplace_back(capacity);
    capacity_ = capacity;
    offset_ = 0;
    buffer_ = buffers_.back().data();
}

int CompletionCandidates::CompareNode(const Node& node, std::string_view word, uint32_t sortKey) const noexcept {
    int difference = static_cast<int>(node.sortKey - sortKey);
    if (difference == 0 && (word.size() > SortKeyLength || node.length > SortKeyLength || ignoreCase_))
        difference = std::string_view(Word(&node), node.length).compare(word);
    return difference;
}

void CompletionCandidates::AddWord(std::string_view word) {
    Node* root = head_;
    // Long roots already share their first four bytes, so preserve the original
    // full-word comparison instead of mixing cached keys after a prefix retry.
    const uint32_t sortKey = root_.size() > SortKeyLength ? 0 : SortKey(word);
    if (root == nullptr) {
        auto* node = std::construct_at(reinterpret_cast<Node*>(buffer_ + offset_));
        std::memcpy(Word(node), word.data(), word.size());
        node->sortKey = sortKey;
        node->length = static_cast<unsigned>(word.size());
        node->level = 1;
        root = node;
    } else {
        Node* iter = root;
        auto& path = insertionPath_;
        path.clear();
        int direction;
        for (;;) {
            path.push_back(iter);
            direction = CompareNode(*iter, word, sortKey);
            if (direction == 0) return;
            direction = direction < 0;
            if (iter->link[direction] == nullptr) break;
            iter = iter->link[direction];
        }

        if (capacity_ < offset_ + word.size() + 1 + sizeof(Node)) AddBuffer(capacity_ << 1);
        auto* node = std::construct_at(reinterpret_cast<Node*>(buffer_ + offset_));
        std::memcpy(Word(node), word.data(), word.size());
        node->sortKey = sortKey;
        node->length = static_cast<unsigned>(word.size());
        node->level = 1;
        iter->link[direction] = node;

        for (auto top = path.size(); top != 0;) {
            --top;
            if (top != 0) direction = path[top - 1]->link[1] == path[top];
            path[top] = Split(Skew(path[top]));
            if (top != 0)
                path[top - 1]->link[direction] = path[top];
            else
                root = path[top];
        }
    }

    head_ = root;
    ++count_;
    totalLength_ += static_cast<unsigned>(word.size()) + 1;
    const auto nodeSize = word.size() + 1 + sizeof(Node);
    offset_ += static_cast<unsigned>((nodeSize + alignof(Node) - 1) & ~(alignof(Node) - 1));
}

void CompletionCandidates::AddKeywords(const char* list) {
    if (!list || !*list) return;
    std::array<char, CompletionWordBufferSize> word;
    unsigned length = 0;
    bool matched = false;
    while (true) {
        unsigned char ch;
        const char* next = list;
        do {
            ch = static_cast<unsigned char>(*next++);
        } while (!IsKeywordSeparator(ch));

        unsigned segmentLength = static_cast<unsigned>(next - list - 1);
        // Prefix markers may use the suffix reserve; restore the word limit
        // before appending the next segment or its function parentheses.
        length = std::min(length, CompletionWordLimit);
        segmentLength = std::min(CompletionWordLimit - length, segmentLength);
        std::memcpy(word.data() + length, list, segmentLength);
        length += segmentLength;
        list = next;
        if (length >= root_.size()) {
            if (ch == '(') {
                word[length++] = '(';
                word[length++] = ')';
            }
            word[length] = '\0';
            if (matched || MatchesPrefix({word.data(), length})) {
                AddWord({word.data(), length});
                matched = ch == '.';
            }
        }
        if (ch == '\0') break;
        if (ch == '^')
            word[length++] = ' ';
        else if (!matched && ch != '.')
            length = 0;
        else
            word[length++] = '.';
    }
}

void CompletionCandidates::AddSubwords(std::string_view word) {
    std::array<unsigned, 8> starts;
    unsigned count = 0;
    for (size_t index = 0; index + 1 < word.size(); ++index) {
        const char ch = word[index];
        if (ch != '.' && ch != '-' && ch != ':') continue;
        if (index >= root_.size()) {
            AddWord(word.substr(0, index));
            for (unsigned part = 0; part < count; ++part) {
                const auto length = index - starts[part];
                if (length >= root_.size()) AddWord(word.substr(starts[part], length));
            }
        }
        if (ch != '.' && (word[index + 1] == '>' || word[index + 1] == ':')) ++index;
        const auto suffix = word.substr(index + 1);
        if (MatchesPrefix(suffix)) {
            AddWord(suffix);
            if (count < starts.size()) starts[count++] = static_cast<unsigned>(index + 1);
        }
    }
}

void CompletionCandidates::AddDocumentWord(std::string_view word, bool includeSubwords) {
    AddWord(word);
    if (includeSubwords) AddSubwords(word);
}

bool CompletionCandidates::RetrySubRoot() noexcept {
    if (count_ != 0 || root_.empty()) return false;
    auto start = root_.find_first_of(":.#@<\\/->$%");
    if (start == std::string_view::npos) return false;
    while (start < root_.size() && IsSpecialStart(root_[start]))
        ++start;
    if (start == root_.size()) return false;
    root_.remove_prefix(start);
    rootSortKey_ = SortKey(root_);
    return true;
}

bool CompletionCandidates::HasCompletion() const noexcept {
    return count_ > 0 && !(count_ == 1 && totalLength_ == root_.size() + 1);
}

std::unique_ptr<char[]> CompletionCandidates::Serialize() const {
    Node* root = head_;
    std::vector<Node*> path;
    path.reserve(InitialTreePathCapacity);
    auto list = std::make_unique<char[]>(totalLength_ + 1);
    char* output = list.get();
    while (root || !path.empty()) {
        if (root) {
            path.push_back(root);
            root = root->link[0];
        } else {
            root = path.back();
            path.pop_back();
            std::memcpy(output, Word(root), root->length);
            output += root->length;
            *output++ = '\n';
            root = root->link[1];
        }
    }
    if (output != list.get()) *(--output) = '\0';
    return list;
}
