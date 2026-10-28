#pragma once

namespace tsf {

// IME punctuation shortcuts produce Chinese punctuation in either input mode.
// Ordinary English symbols remain available without a phonetic composition.
constexpr bool blocks_idle_punctuation(bool english_mode, bool has_phonetic_input,
                                       bool ime_shortcut, bool printable_symbol) noexcept {
    return !has_phonetic_input && (ime_shortcut || (!english_mode && printable_symbol));
}

}  // namespace tsf
