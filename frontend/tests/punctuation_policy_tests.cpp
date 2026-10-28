#include "core/punctuation_policy.hpp"

#include <cstdio>

int main() {
    struct Case { bool english; bool phonetic; bool shortcut; bool symbol; bool blocked; };
    constexpr Case cases[] = {
        {false, false, true, false, true},  // Ctrl+comma, Chinese idle
        {true,  false, true, false, true},  // Ctrl+comma, English idle (regression)
        {false, true,  true, false, false}, // Ctrl+comma after Zhuyin
        {true,  true,  true, false, false},
        {false, false, false, true, true},  // Shift symbol, Chinese idle
        {true,  false, false, true, false}, // Normal English symbols
        {false, true,  false, true, false},
        {false, false, false, false, false}, // Zhuyin and application shortcuts
        {true,  false, false, false, false},
    };
    for (const auto& test : cases) {
        if (tsf::blocks_idle_punctuation(test.english, test.phonetic,
                                        test.shortcut, test.symbol) != test.blocked) {
            std::fputs("punctuation policy regression\n", stderr);
            return 1;
        }
    }
    return 0;
}
