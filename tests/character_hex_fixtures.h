// Captured through original Notepad4 CustomCaseMapping (2139), UTF-8 buffer.
// Original revision: 89b8b36e0ee2c23f73be922315f17261ccfbbe1a.
#pragma once
#include <array>
#include <string_view>
struct CharacterHexFixture {
    const char* name;
    int command;
    std::string_view before;
    std::string_view after;
};
inline constexpr std::array<CharacterHexFixture, 18> CharacterHexFixtures{{
    {"ascii", 40202, std::string_view{"\x{41}", 1}, std::string_view{"\x{5c}\x{78}\x{34}\x{31}", 4}},
    {"latin-chinese-eol", 40202, std::string_view{"\x{41}\x{c3}\x{a9}\x{e4}\x{b8}\x{ad}\x{0d}\x{0a}", 8},
     std::string_view{"\x{5c}\x{78}\x{34}\x{31}\x{5c}\x{78}\x{45}\x{39}\x{5c}\x{75}\x{34}\x{45}\x{32}\x{44}\x{5c}\x{78}"
                      "\x{30}\x{44}\x{5c}\x{78}\x{30}\x{41}",
                      22}},
    {"supplementary-isolated", 40202, std::string_view{"\x{f0}\x{9f}\x{98}\x{80}", 4},
     std::string_view{"\x{5c}\x{75}\x{44}\x{38}\x{33}\x{44}\x{5c}\x{75}\x{44}\x{45}\x{30}\x{30}\x{20}\x{55}\x{2b}\x{30}"
                      "\x{31}\x{46}\x{36}\x{30}\x{30}",
                      21}},
    {"supplementary-mixed", 40202, std::string_view{"\x{41}\x{f0}\x{9f}\x{98}\x{80}", 5},
     std::string_view{
         "\x{5c}\x{78}\x{34}\x{31}\x{5c}\x{75}\x{44}\x{38}\x{33}\x{44}\x{5c}\x{75}\x{44}\x{45}\x{30}\x{30}", 16}},
    {"nul-encode", 40202, std::string_view{"\x{41}\x{00}\x{42}", 3},
     std::string_view{"\x{5c}\x{78}\x{34}\x{31}\x{5c}\x{78}\x{30}\x{30}\x{5c}\x{78}\x{34}\x{32}", 12}},
    {"malformed-utf8", 40202, std::string_view{"\x{ff}\x{c0}\x{af}\x{ed}\x{a0}\x{80}", 6},
     std::string_view{"\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}"
                      "\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}"
                      "\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}",
                      30}},
    {"utf8-surrogate", 40202, std::string_view{"\x{ed}\x{a0}\x{80}", 3},
     std::string_view{"\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}", 12}},
    {"utf8-overlong3", 40202, std::string_view{"\x{e0}\x{80}\x{80}", 3},
     std::string_view{"\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}", 12}},
    {"utf8-overlong4", 40202, std::string_view{"\x{f0}\x{80}\x{80}\x{80}", 4},
     std::string_view{
         "\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}",
         18}},
    {"utf8-large", 40202, std::string_view{"\x{f4}\x{90}\x{80}\x{80}", 4},
     std::string_view{
         "\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}",
         18}},
    {"utf8-truncated", 40202, std::string_view{"\x{e1}\x{80}", 2},
     std::string_view{"\x{5c}\x{75}\x{46}\x{46}\x{46}\x{44}", 6}},
    {"forms", 40203,
     std::string_view{"\x{5c}\x{78}\x{34}\x{31}\x{5c}\x{78}\x{30}\x{30}\x{45}\x{39}\x{5c}\x{75}\x{34}\x{45}\x{32}\x{44}"
                      "\x{5c}\x{55}\x{30}\x{30}\x{30}\x{31}\x{46}\x{36}"
                      "\x{30}\x{30}\x{20}\x{55}\x{2b}\x{30}\x{31}\x{46}\x{36}\x{30}\x{30}",
                      35},
     std::string_view{"\x{41}\x{c3}\x{a9}\x{e4}\x{b8}\x{ad}\x{f0}\x{9f}\x{98}\x{80}\x{20}\x{f0}\x{9f}\x{98}\x{80}",
                      15}},
    {"odd", 40203,
     std::string_view{"\x{5c}\x{78}\x{34}\x{20}\x{5c}\x{78}\x{34}\x{31}\x{34}\x{20}\x{5c}\x{78}\x{34}\x{31}\x{34}\x{32}"
                      "\x{46}\x{20}\x{5c}\x{75}\x{30}\x{34}\x{31}\x{20}\x{5c}\x{75}\x{30}"
                      "\x{30}\x{34}\x{31}\x{34}\x{20}\x{5c}\x{55}\x{30}\x{30}\x{30}\x{31}\x{46}\x{36}\x{30}\x{20}\x{5c}"
                      "\x{55}\x{30}\x{30}\x{30}\x{31}\x{46}\x{36}\x{30}\x{30}\x{46}\x{20}"
                      "\x{55}\x{2b}\x{31}\x{32}\x{33}\x{20}\x{55}\x{2b}\x{31}\x{46}\x{36}\x{30}\x{30}\x{20}\x{55}\x{2b}"
                      "\x{30}\x{31}\x{46}\x{36}\x{30}\x{30}\x{46}",
                      77},
     std::string_view{"\x{5c}\x{78}\x{34}\x{20}\x{41}\x{34}\x{20}\x{e4}\x{85}\x{82}\x{46}\x{20}\x{5c}\x{75}\x{30}\x{34}"
                      "\x{31}\x{20}\x{41}\x{34}\x{20}\x{5c}\x{55}\x{30}\x{30}\x{30}\x{31}"
                      "\x{46}\x{36}\x{30}\x{20}\x{f0}\x{9f}\x{98}\x{80}\x{46}\x{20}\x{55}\x{2b}\x{31}\x{32}\x{33}\x{20}"
                      "\x{e1}\x{bd}\x{a0}\x{30}\x{20}\x{f0}\x{9f}\x{98}\x{80}\x{46}",
                      53}},
    {"bounds", 40203,
     std::string_view{"\x{5c}\x{55}\x{30}\x{30}\x{31}\x{30}\x{46}\x{46}\x{46}\x{46}\x{20}\x{5c}\x{55}\x{30}\x{30}\x{31}"
                      "\x{31}\x{30}\x{30}\x{30}\x{30}\x{20}\x{5c}\x{55}\x{46}\x{46}\x{46}"
                      "\x{46}\x{46}\x{46}\x{46}\x{46}\x{20}\x{55}\x{2b}\x{31}\x{31}\x{30}\x{30}\x{30}\x{30}\x{20}\x{55}"
                      "\x{2b}\x{31}\x{30}\x{46}\x{46}\x{46}\x{46}",
                      50},
     std::string_view{
         "\x{f4}\x{8f}\x{bf}\x{bf}\x{20}\x{5c}\x{55}\x{30}\x{30}\x{31}\x{31}\x{30}\x{30}\x{30}\x{30}\x{20}\x{5c}\x{55}"
         "\x{46}\x{46}\x{46}\x{46}\x{46}\x{46}"
         "\x{46}\x{46}\x{20}\x{55}\x{2b}\x{31}\x{31}\x{30}\x{30}\x{30}\x{30}\x{20}\x{f4}\x{8f}\x{bf}\x{bf}",
         40}},
    {"surrogate", 40203,
     std::string_view{"\x{5c}\x{75}\x{44}\x{38}\x{30}\x{30}\x{20}\x{5c}\x{75}\x{44}\x{43}\x{30}\x{30}\x{20}\x{5c}\x{75}"
                      "\x{44}\x{38}\x{33}\x{44}\x{5c}\x{75}\x{44}\x{45}"
                      "\x{30}\x{30}\x{20}\x{5c}\x{78}\x{44}\x{38}\x{30}\x{30}",
                      33},
     std::string_view{
         "\x{ef}\x{bf}\x{bd}\x{20}\x{ef}\x{bf}\x{bd}\x{20}\x{f0}\x{9f}\x{98}\x{80}\x{20}\x{ef}\x{bf}\x{bd}", 16}},
    {"nul-decode", 40203,
     std::string_view{"\x{41}\x{5c}\x{78}\x{30}\x{30}\x{42}\x{5c}\x{75}\x{30}\x{30}\x{30}\x{30}\x{43}\x{5c}\x{55}\x{30}"
                      "\x{30}\x{30}\x{30}\x{30}\x{30}\x{30}\x{30}\x{44}",
                      24},
     std::string_view{"\x{41}\x{00}\x{42}\x{00}\x{43}\x{00}\x{44}", 7}},
    {"literal", 40203,
     std::string_view{"\x{68}\x{65}\x{6c}\x{6c}\x{6f}\x{20}\x{5c}\x{58}\x{34}\x{31}\x{20}\x{75}\x{2b}\x{30}\x{30}\x{34}"
                      "\x{31}\x{20}\x{5c}\x{71}\x{34}\x{31}\x{20}\x{5c}"
                      "\x{78}\x{20}\x{5c}\x{75}\x{20}\x{5c}\x{55}\x{20}\x{55}\x{2b}\x{20}\x{5c}\x{78}\x{47}\x{47}\x{20}"
                      "\x{5c}\x{75}\x{31}\x{32}\x{5a}\x{5a}",
                      46},
     std::string_view{"\x{68}\x{65}\x{6c}\x{6c}\x{6f}\x{20}\x{5c}\x{58}\x{34}\x{31}\x{20}\x{75}\x{2b}\x{30}\x{30}\x{34}"
                      "\x{31}\x{20}\x{5c}\x{71}\x{34}\x{31}\x{20}\x{5c}"
                      "\x{78}\x{20}\x{5c}\x{75}\x{20}\x{5c}\x{55}\x{20}\x{55}\x{2b}\x{20}\x{5c}\x{78}\x{47}\x{47}\x{20}"
                      "\x{5c}\x{75}\x{31}\x{32}\x{5a}\x{5a}",
                      46}},
    {"actual-nul", 40203, std::string_view{"\x{41}\x{00}\x{5c}\x{78}\x{34}\x{32}", 6},
     std::string_view{"\x{41}\x{00}\x{42}", 3}},
}};
