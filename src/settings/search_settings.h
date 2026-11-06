#pragma once

class SearchSession;
struct OriginalSettings;

void RestoreSearchSettings(SearchSession& search, const OriginalSettings& settings);
void CaptureSearchSettings(const SearchSession& search, OriginalSettings& settings);
