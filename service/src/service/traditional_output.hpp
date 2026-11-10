#pragma once
#include "../../../shared/traditional_characters.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace llavon::service {

// The legacy pipe client has no script filter. Sanitize the final wire payload
// after model output, custom names and remembered selections have been applied.
inline std::vector<char32_t> traditional_response_candidates(
    std::span<const std::pair<char32_t, float>> candidates) {
    std::vector<char32_t> result;
    result.reserve(candidates.size());
    for (const auto& [raw, probability] : candidates) {
        (void)probability;
        const auto character = linguistic::to_traditional_character(raw);
        if (std::find(result.begin(), result.end(), character) == result.end()) {
            result.push_back(character);
        }
    }
    return result;
}

}  // namespace llavon::service
