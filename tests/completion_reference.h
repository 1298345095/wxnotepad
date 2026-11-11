// Captured from original Notepad4 89b8b36 by issue-191/capture_completion.py.
#pragma once
#include <vector>
#include <string>
struct CompletionReference { const char* language; const char* before; const char* after; intptr_t caret; std::vector<std::string> items; };
inline const CompletionReference completionReference[] = {
    {"Plain Text", "alpha alpine al", "alpha alpine al", 15, {"alpha", "alpine"}},
    {"Plain Text", "alpha al", "alpha alpha", 11, {}},
    {"Plain Text", "alpha alpha", "alpha alpha", 11, {}},
    {"Plain Text", "alpha ", "alpha ", 6, {}},
    {"Plain Text", "Alpha alpha alpine al", "Alpha alpha alpine al", 21, {"alpha", "alpine"}},
    {"Plain Text", "12345 12399 123", "12345 12399 123", 15, {"12345", "12399"}},
    {"Plain Text", "0xABCD 0xABEF 0xAB", "0xABCD 0xABEF 0xAB", 18, {}},
    {"Plain Text", "中hello 中help 中he", "中hello 中help 中he", 22, {"hello", "help"}},
    {"Plain Text", "a::hello a::help a::he", "a::hello a::help a::he", 22, {"hello", "help"}},
    {"C/C++", "pri", "pri", 3, {"printf()", "priority_queue", "private"}},
    {"C/C++", "#inc", "#inc", 4, {"include", "include_next"}},
    {"Python", "@sta", "@staticmethod", 13, {}},
    {"HTML", "<scr", "<script", 7, {}},
    {"XML", "<roo", "<roo", 4, {}},
    {"Bash", "$HO", "$HO", 3, {"HOME", "HOST", "HOSTFILE", "HOSTNAME", "HOSTTYPE"}},
};
