#pragma once
#include "traditional_characters.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

namespace llavon::linguistic {

inline bool is_traditional_word(std::u32string_view word) {
    return std::all_of(word.begin(), word.end(), is_traditional_character);
}

// Keep every original character, preserving model/frequency order within each
// script group. Simplified-only readings remain usable when no alternative exists.
inline std::vector<char32_t> traditional_first_candidates(std::span<const char32_t> candidates) {
    std::vector<char32_t> result;
    result.reserve(candidates.size());
    for (const auto character : candidates) {
        if (std::find(result.begin(), result.end(), character) == result.end()) {
            result.push_back(character);
        }
    }
    std::stable_partition(result.begin(), result.end(), is_traditional_character);
    return result;
}

}  // namespace llavon::linguistic
