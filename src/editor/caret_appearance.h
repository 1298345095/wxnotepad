#pragma once

// Original Styles.h::CaretStyle and Notepad4.cpp's decimal-packed preferences.
struct CaretAppearance {
    int lineWidth = 1; // Zero selects the insertion block.
    bool overstrikeBlock = false;
    bool blockAfterSelection = false;
    int blinkPeriod = -1; // System default; zero disables blinking.
};
