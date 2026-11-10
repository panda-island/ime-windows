#pragma once
#include "../../../shared/candidate_policy.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace llavon::service {

inline void apply_custom_name_candidate(std::vector<std::pair<char32_t, float>>& candidates,
                                        char32_t character) {
    if (linguistic::is_traditional_character(character)) {
        candidates = {{character, 1.0F}};
    } else {
        std::erase_if(candidates, [character](const auto& candidate) { return candidate.first == character; });
        candidates.insert(candidates.begin(), {character, 1.0F});
    }
}

// Apply the same script preference to both legacy and current pipe clients,
// after model output, custom names and remembered selections have been applied.
inline std::vector<char32_t> traditional_response_candidates(
    std::span<const std::pair<char32_t, float>> candidates) {
    std::vector<char32_t> result;
    result.reserve(candidates.size());
    for (const auto& [raw, probability] : candidates) {
        (void)probability;
        result.push_back(raw);
    }
    return linguistic::traditional_first_candidates(result);
}

}  // namespace llavon::service
