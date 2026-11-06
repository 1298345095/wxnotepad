#include "settings/search_settings.h"
#include "settings/original_settings.h"
#include "search/search_session.h"
#include <utility>

namespace {
constexpr int MatchCase = 4;
constexpr int WholeWord = 2;
constexpr int Regex = 0x20;
constexpr int PosixRegex = 0x40;
constexpr int Cxx11Regex = 0x80;
constexpr int TransformBackslashes = 8 << 10;
constexpr int LastReplaceEmpty = 32 << 10;
constexpr int NoWrap = 4 << 4;
constexpr int BookmarkMatches = 8;
} // namespace

void RestoreSearchSettings(SearchSession& search, const OriginalSettings& settings) {
    SearchRequest request;
    const int flags = settings.findReplaceFlag;
    request.options.matchCase = (flags & MatchCase) != 0;
    request.options.wholeWord = (flags & WholeWord) != 0;
    request.options.regularExpression = (flags & Regex) != 0;
    request.options.transformBackslashes = (flags & TransformBackslashes) != 0;
    request.wrapAround = (settings.findReplaceOption & NoWrap) == 0;
    request.bookmarkMatchedLines = (settings.findReplaceOption & BookmarkMatches) != 0;
    search.Configure(std::move(request));
    search.RestoreHistory(settings.recentFind, settings.recentReplace, (flags & LastReplaceEmpty) != 0);
}

void CaptureSearchSettings(const SearchSession& search, OriginalSettings& settings) {
    const auto& request = search.Request();
    // Preserve bits owned by other original options. The original regex-engine
    // preference remains in FindReplaceOption even though the port uses its core matcher.
    auto& flags = settings.findReplaceFlag;
    flags &= ~(MatchCase | WholeWord | Regex | PosixRegex | Cxx11Regex | TransformBackslashes | LastReplaceEmpty);
    flags |= (request.options.matchCase ? MatchCase : 0) | (request.options.wholeWord ? WholeWord : 0) |
             (request.options.regularExpression ? (Regex | ((settings.findReplaceOption & 1) ? Cxx11Regex : PosixRegex))
                                                : 0) |
             (request.options.transformBackslashes ? TransformBackslashes : 0) |
             (search.LastReplaceEmpty() ? LastReplaceEmpty : 0);
    settings.findReplaceOption = (settings.findReplaceOption & ~(NoWrap | BookmarkMatches)) |
                                 (request.wrapAround ? 0 : NoWrap) |
                                 (request.bookmarkMatchedLines ? BookmarkMatches : 0);
    settings.recentFind = search.RecentFind();
    settings.recentReplace = search.RecentReplace();
}
