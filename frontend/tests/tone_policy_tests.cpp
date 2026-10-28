#include "core/tone_policy.hpp"

#include <cstdio>

int main() {
    struct Case { char16_t initial; char16_t medial; char16_t final; bool converted; bool invalid; bool allowed; };
    constexpr Case cases[] = {
        {0, 0, 0, false, false, false},           // standalone tone
        {u'ㄅ', 0, 0, false, false, true},          // consonant-only syllable
        {0, u'ㄧ', 0, false, false, true},          // medial-only syllable
        {0, 0, u'ㄚ', false, false, true},          // vowel-only syllable
        {u'ㄅ', u'ㄧ', u'ㄢ', false, false, true},
        {0, 0, 0, true, false, false},             // punctuation / fixed text
        {u'ㄅ', 0, 0, true, false, false},          // previous converted syllable
        {u'ㄅ', 0, 0, false, true, false},          // invalid syllable
    };
    for (const auto& test : cases) {
        if (tsf::accepts_tone(test.initial, test.medial, test.final,
                              test.converted, test.invalid) != test.allowed) {
            std::fputs("tone policy regression\n", stderr);
            return 1;
        }
    }
}
