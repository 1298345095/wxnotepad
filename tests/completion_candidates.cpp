#include "editor/completion_candidates.h"
#include "completion_allocation_fault.h"
#include <array>
#include <cstdio>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void RequireCandidates(const CompletionCandidates& candidates, std::string_view expected, unsigned count) {
    Require(candidates.Count() == count, "candidate count matches the visible list");
    Require(std::string_view(candidates.Serialize().get()) == expected,
            "every serialized candidate retains its original order");
}

std::string NumberedWord(unsigned index) {
    const auto digits = std::to_string(index);
    return "a" + std::string(8 - digits.size(), '0') + digits;
}

void PrefixAndOrdering() {
    // These roots and words have exactly their stated byte lengths, with no
    // terminator or four-byte padding for the cached sort key to read.
    const std::array<char, 1> root{'a'};
    const std::array<char, 2> shortWord{'a', 'b'};
    CompletionCandidates candidates({root.data(), root.size()}, false);
    Require(!candidates.HasCompletion(), "an empty set has no completion");
    Require(candidates.MatchesPrefix({shortWord.data(), shortWord.size()}), "short unpadded prefixes match");
    Require(!candidates.MatchesPrefix("A"), "case-sensitive prefix matching preserves ASCII case");
    candidates.AddDocumentWord({root.data(), root.size()}, false);
    Require(!candidates.HasCompletion(), "a sole exact root does not show completion");
    candidates.AddDocumentWord({shortWord.data(), shortWord.size()}, false);
    candidates.AddDocumentWord({shortWord.data(), shortWord.size()}, false);
    Require(candidates.HasCompletion(), "a distinct longer candidate permits completion");
    RequireCandidates(candidates, "a\nab", 2);

    CompletionCandidates folded("a", true);
    folded.AddKeywords("apple APPLE Apple apricot APPLE");
    RequireCandidates(folded, "APPLE\nApple\napple\napricot", 4);
    Require(folded.MatchesPrefix("APPLE") && folded.IgnoreCase(), "folding applies to ASCII prefix matching");

    CompletionCandidates longRoot("apple", true);
    longRoot.AddKeywords("appleZ AppleA APPLEb appleA");
    RequireCandidates(longRoot, "APPLEb\nAppleA\nappleA\nappleZ", 4);
    Require(!longRoot.MatchesPrefix("app"), "a shorter word cannot match a long root");

    const std::array<char, 3> unicodeRoot{static_cast<char>(0xe4), static_cast<char>(0xb8), static_cast<char>(0xad)};
    CompletionCandidates unicode({unicodeRoot.data(), unicodeRoot.size()}, true);
    const std::string first = std::string(unicodeRoot.data(), unicodeRoot.size()) + 'a';
    const std::string second = std::string(unicodeRoot.data(), unicodeRoot.size()) + 'b';
    unicode.AddDocumentWord(second, false);
    unicode.AddDocumentWord(first, false);
    RequireCandidates(unicode, first + '\n' + second, 2);
}

void KeywordsAndSubwords() {
    CompletionCandidates keywords("foo", false);
    keywords.AddKeywords(nullptr);
    keywords.AddKeywords("");
    keywords.AddKeywords("foo bar foo() foo.bar foo^baz");
    RequireCandidates(keywords, "foo\nfoo baz\nfoo()\nfoo.bar", 4);

    CompletionCandidates longest("x", false);
    const std::string limit(CompletionWordLimit, 'x');
    longest.AddKeywords((limit + "^y(z").c_str());
    RequireCandidates(longest, limit + '\n' + limit + "()", 2);

    CompletionCandidates subwords("b", false);
    subwords.AddDocumentWord("bugprone-branch-clone", true);
    RequireCandidates(subwords, "branch\nbranch-clone\nbugprone\nbugprone-branch\nbugprone-branch-clone", 5);

    CompletionCandidates qualified("a", false);
    qualified.AddDocumentWord("alpha::apple->apricot", true);
    RequireCandidates(qualified, "alpha\nalpha::apple\nalpha::apple->apricot\napple\napple->apricot\napricot", 6);
}

void PrefixRetry() {
    CompletionCandidates candidates("qualifier::a", false);
    candidates.AddKeywords("aa ab");
    RequireCandidates(candidates, "", 0);
    Require(candidates.RetrySubRoot() && candidates.Root() == "a", "an empty long root retries its short suffix");
    candidates.AddKeywords("ab aa");
    RequireCandidates(candidates, "aa\nab", 2);
    Require(!candidates.RetrySubRoot() && candidates.Root() == "a", "a populated tree retains its ordering root");

    CompletionCandidates populated("a::b", false);
    populated.AddDocumentWord("a::beta", false);
    Require(!populated.RetrySubRoot() && populated.Root() == "a::b", "prefix retry cannot alter a populated tree");
    RequireCandidates(populated, "a::beta", 1);

    CompletionCandidates nested("alpha::beta.gamma", false);
    Require(nested.RetrySubRoot() && nested.Root() == "beta.gamma", "the first retry retains the remaining qualifier");
    Require(nested.RetrySubRoot() && nested.Root() == "gamma", "a still-empty attempt can retry the next subroot");
    Require(!nested.RetrySubRoot(), "an unqualified root has no retry");
    CompletionCandidates trailing("alpha::", false);
    Require(!trailing.RetrySubRoot() && trailing.Root() == "alpha::", "a trailing qualifier retains the original root");
}

void ConstructorFailures() {
    unsigned failures = 0;
    bool completed = false;
    for (size_t ordinal = 0; ordinal < 16; ++ordinal) {
        bool caught = false;
        bool injected = false;
        {
            const CompletionAllocationFault::ScopedFailure failure(0, ordinal);
            try {
                const CompletionCandidates candidates("a", false);
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            injected = failure.Injected();
        }
        Require(caught == injected, "constructor failures retain the allocation error");
        if (!injected) {
            completed = true;
            break;
        }
        ++failures;
    }
    Require(completed && failures >= 2, "constructor probes cover path and candidate storage before success");
    CompletionCandidates retry("a", false);
    retry.AddKeywords("alpha");
    RequireCandidates(retry, "alpha", 1);
}

void GrowthFailure() {
    CompletionCandidates candidates("a", false);
    candidates.AddDocumentWord(NumberedWord(0), false);
    std::string expected = NumberedWord(0);
    bool tested = false;
    for (unsigned index = 1; index < 256; ++index) {
        const auto word = NumberedWord(index);
        bool caught = false;
        bool injected = false;
        {
            const CompletionAllocationFault::ScopedFailure failure;
            try {
                candidates.AddDocumentWord(word, false);
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            injected = failure.Injected();
        }
        Require(caught == injected, "candidate insertion preserves its allocation failure");
        if (caught) {
            RequireCandidates(candidates, expected, index);
            candidates.AddDocumentWord(word, false);
            expected += '\n' + word;
            RequireCandidates(candidates, expected, index + 1);
            tested = true;
            break;
        }
        expected += '\n' + word;
    }
    Require(tested, "candidate growth reaches its allocation failure and permits retry");
}

void SerializationFailures() {
    CompletionCandidates candidates("a", false);
    candidates.AddKeywords("alpha apricot apple");
    constexpr std::string_view expected = "alpha\napple\napricot";
    unsigned failures = 0;
    bool completed = false;
    for (size_t ordinal = 0; ordinal < 16; ++ordinal) {
        bool caught = false;
        bool injected = false;
        std::unique_ptr<char[]> output;
        {
            const CompletionAllocationFault::ScopedFailure failure(0, ordinal);
            try {
                output = candidates.Serialize();
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            injected = failure.Injected();
        }
        Require(caught == injected, "serialization retains its allocation failure");
        RequireCandidates(candidates, expected, 3);
        if (!injected) {
            Require(std::string_view(output.get()) == expected,
                    "successful serialization retains every candidate after failures");
            completed = true;
            break;
        }
        ++failures;
    }
    Require(completed && failures >= 2, "serialization probes cover traversal and output storage before success");
}

void LargeCandidateSet() {
    constexpr unsigned count = 200000;
    CompletionCandidates candidates("a", false);
    std::string expected;
    expected.reserve(count * 10);
    for (unsigned index = 0; index < count; ++index) {
        const auto word = NumberedWord(index);
        candidates.AddDocumentWord(word, false);
        if (index) expected += '\n';
        expected += word;
    }
    RequireCandidates(candidates, expected, count);
}
} // namespace

int main() try {
    PrefixAndOrdering();
    KeywordsAndSubwords();
    PrefixRetry();
    ConstructorFailures();
    GrowthFailure();
    SerializationFailures();
    LargeCandidateSet();
    std::puts("PASS: completion candidates preserve ordering, prefix retries and allocation-failure recovery");
    return 0;
} catch (const std::exception& failure) {
    std::fprintf(stderr, "FAIL: %s\n", failure.what());
    return 1;
}
