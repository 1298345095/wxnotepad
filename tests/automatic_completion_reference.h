// Frozen from pinned original Notepad4 89b8b36 native typing and key input.
// Reproduction: tests/reference_capture/capture_automatic_completion.py.
#pragma once
#include <string>
#include <utility>
#include <vector>
struct AutomaticReference {
    const char* scheme;
    const char* before;
    std::vector<std::pair<bool, int>> events;
    int fill;
    bool readOnly;
    bool latex;
    const char* popup;
    const char* after;
    int caret;
    bool active;
    std::vector<std::string> items;
    const char* undo;
    int undoCaret;
    int undoAnchor;
};
inline const std::vector<AutomaticReference> automaticReference = {
    {"Plain Text",
     "alpha alpine a",
     {{false, 108}},
     15,
     false,
     false,
     "",
     "alpha alpine al",
     15,
     true,
     {"alpha", "alpine"},
     "alpha alpine a",
     14,
     14},
    {"Plain Text",
     "alpha alpine al",
     {{false, 112}},
     15,
     false,
     false,
     "",
     "alpha alpine alp",
     16,
     true,
     {"alpha", "alpine"},
     "alpha alpine al",
     15,
     15},
    {"C/C++", "pr", {{false, 105}, {true, 13}}, 15, false, false, "", "printf()", 7, false, {}, "pri", 3, 0},
    {"Plain Text",
     "alpha alpine a",
     {{false, 108}, {true, 13}},
     14,
     false,
     false,
     "",
     "alpha alpine al\r\n",
     17,
     false,
     {},
     "alpha alpine a",
     14,
     14},
    {"Plain Text",
     "alpha alpine a",
     {{false, 108}, {true, 9}},
     13,
     false,
     false,
     "",
     "alpha alpine al\t",
     16,
     false,
     {},
     "alpha alpine al",
     15,
     15},
    {"Plain Text",
     "alpha alpine a",
     {{false, 108}, {false, 32}},
     15,
     false,
     false,
     "",
     "alpha alpine alpha ",
     19,
     false,
     {},
     "alpha alpine alpha",
     18,
     18},
    {"Plain Text",
     "alpha alpine al",
     {{false, 112}, {true, 8}},
     15,
     false,
     false,
     "",
     "alpha alpine al",
     15,
     true,
     {"alpha", "alpine"},
     "alpha alpine alp",
     16,
     16},
    {"C/C++",
     "obj->field obj-",
     {{false, 62}},
     15,
     false,
     false,
     "",
     "obj->field obj->",
     16,
     false,
     {},
     "obj->field obj-",
     15,
     15},
    {"Plain Text",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 ",
     {{false, 97}, {false, 98}},
     15,
     false,
     false,
     "",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 ab",
     402,
     true,
     {"ab00", "ab01", "ab02", "ab03", "ab04", "ab05", "ab06", "ab07", "ab08", "ab09",
      "ab10", "ab11", "ab12", "ab13", "ab14", "ab15", "ab16", "ab17", "ab18", "ab19"},
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 ",
     400,
     400},
    {"Plain Text",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 ",
     {{false, 97}, {false, 98}, {true, 8}},
     15,
     false,
     false,
     "",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 a",
     401,
     true,
     {"ab00", "ab01", "ab02", "ab03", "ab04", "ab05", "ab06", "ab07", "ab08", "ab09", "ab10", "ab11", "ab12", "ab13",
      "ab14", "ab15", "ab16", "ab17", "ab18", "ab19", "ac00", "ac01", "ac02", "ac03", "ac04", "ac05", "ac06", "ac07",
      "ac08", "ac09", "ac10", "ac11", "ac12", "ac13", "ac14", "ac15", "ac16", "ac17", "ac18", "ac19", "ac20", "ac21",
      "ac22", "ac23", "ac24", "ac25", "ac26", "ac27", "ac28", "ac29", "ac30", "ac31", "ac32", "ac33", "ac34", "ac35",
      "ac36", "ac37", "ac38", "ac39", "ac40", "ac41", "ac42", "ac43", "ac44", "ac45", "ac46", "ac47", "ac48", "ac49",
      "ac50", "ac51", "ac52", "ac53", "ac54", "ac55", "ac56", "ac57", "ac58", "ac59"},
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ac00 ac01 "
     "ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ac10 ac11 ac12 ac13 ac14 ac15 ac16 ac17 ac18 ac19 ac20 ac21 ac22 ac23 "
     "ac24 ac25 ac26 ac27 ac28 ac29 ac30 ac31 ac32 ac33 ac34 ac35 ac36 ac37 ac38 ac39 ac40 ac41 ac42 ac43 ac44 ac45 "
     "ac46 ac47 ac48 ac49 ac50 ac51 ac52 ac53 ac54 ac55 ac56 ac57 ac58 ac59 ab",
     402,
     402},
    {"Plain Text",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ab20 ab21 "
     "ab22 ab23 ab24 ab25 ab26 ab27 ab28 ab29 ab30 ab31 ab32 ab33 ab34 ab35 ab36 ab37 ab38 ab39 ab40 ab41 ab42 ab43 "
     "ab44 ab45 ab46 ab47 ab48 ab49 ab50 ab51 ab52 ab53 ab54 ab55 ab56 ab57 ab58 ab59 ab60 ab61 ab62 ab63 ab64 ab65 "
     "ab66 ab67 ab68 ab69 ac00 ac01 ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ",
     {{false, 97}, {false, 98}},
     15,
     false,
     false,
     "",
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ab20 ab21 "
     "ab22 ab23 ab24 ab25 ab26 ab27 ab28 ab29 ab30 ab31 ab32 ab33 ab34 ab35 ab36 ab37 ab38 ab39 ab40 ab41 ab42 ab43 "
     "ab44 ab45 ab46 ab47 ab48 ab49 ab50 ab51 ab52 ab53 ab54 ab55 ab56 ab57 ab58 ab59 ab60 ab61 ab62 ab63 ab64 ab65 "
     "ab66 ab67 ab68 ab69 ac00 ac01 ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ab",
     402,
     true,
     {"ab00", "ab01", "ab02", "ab03", "ab04", "ab05", "ab06", "ab07", "ab08", "ab09", "ab10", "ab11", "ab12", "ab13",
      "ab14", "ab15", "ab16", "ab17", "ab18", "ab19", "ab20", "ab21", "ab22", "ab23", "ab24", "ab25", "ab26", "ab27",
      "ab28", "ab29", "ab30", "ab31", "ab32", "ab33", "ab34", "ab35", "ab36", "ab37", "ab38", "ab39", "ab40", "ab41",
      "ab42", "ab43", "ab44", "ab45", "ab46", "ab47", "ab48", "ab49", "ab50", "ab51", "ab52", "ab53", "ab54", "ab55",
      "ab56", "ab57", "ab58", "ab59", "ab60", "ab61", "ab62", "ab63", "ab64", "ab65", "ab66", "ab67", "ab68", "ab69",
      "ac00", "ac01", "ac02", "ac03", "ac04", "ac05", "ac06", "ac07", "ac08", "ac09"},
     "ab00 ab01 ab02 ab03 ab04 ab05 ab06 ab07 ab08 ab09 ab10 ab11 ab12 ab13 ab14 ab15 ab16 ab17 ab18 ab19 ab20 ab21 "
     "ab22 ab23 ab24 ab25 ab26 ab27 ab28 ab29 ab30 ab31 ab32 ab33 ab34 ab35 ab36 ab37 ab38 ab39 ab40 ab41 ab42 ab43 "
     "ab44 ab45 ab46 ab47 ab48 ab49 ab50 ab51 ab52 ab53 ab54 ab55 ab56 ab57 ab58 ab59 ab60 ab61 ab62 ab63 ab64 ab65 "
     "ab66 ab67 ab68 ab69 ac00 ac01 ac02 ac03 ac04 ac05 ac06 ac07 ac08 ac09 ",
     400,
     400},
    {"Plain Text",
     "alpha alpine a",
     {{false, 108}},
     15,
     true,
     false,
     "",
     "alpha alpine a",
     14,
     true,
     {"alpha", "alpine"},
     "alpha alpine a",
     14,
     14},
    {"Plain Text", "\\i", {{false, 110}, {true, 9}}, 13, false, true, "", "\u2208", 3, false, {}, "\\in", 3, 3},
    {"C/C++", "ar", {{true, 13}}, 15, false, false, "array[]", "array[]", 6, false, {}, "ar", 2, 0},
    {"C/C++", "ar[]", {{true, 13}}, 15, false, false, "array[]", "array[]", 6, false, {}, "ar[]", 2, 0},
    {"C/C++", "ve", {{true, 13}}, 15, false, false, "vector<>", "vector<>", 7, false, {}, "ve", 2, 0},
    {"C/C++", "ve<>", {{true, 13}}, 15, false, false, "vector<>", "vector<>", 7, false, {}, "ve<>", 2, 0},
};
