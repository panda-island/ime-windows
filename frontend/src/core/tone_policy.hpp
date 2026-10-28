#pragma once

namespace tsf {

// A tone belongs to the pending syllable at the caret, never to an empty
// composition, punctuation, a converted character, or an invalid syllable.
constexpr bool accepts_tone(char16_t initial, char16_t medial, char16_t final,
                            bool compositable, bool invalid) noexcept {
    return (initial != 0 || medial != 0 || final != 0) && !compositable && !invalid;
}

}  // namespace tsf
