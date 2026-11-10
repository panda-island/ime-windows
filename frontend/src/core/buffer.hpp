#pragma once
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "bopomofo.hpp"
#include "tone_policy.hpp"
#include "engine/impl/engine.h"
using namespace std::literals;

namespace tsf {

class Buffer {
protected:
    std::vector<BopomofoPos> buffer;

public:
    std::u16string to_string() const {
        std::u16string res;
        for (const auto& item : buffer) {
            res += item.current();
        }
        return res;
    }
    void clear() {
        buffer.clear();
    }
    bool empty() const {
        return buffer.empty();
    }
};

class CompositionBuffer {
    std::vector<BopomofoPos> buffer;
    std::mutex mutex;
    int idx = -1;

    int candidate_target_index() const {
        if (buffer.empty()) return -1;

        const int size = static_cast<int>(buffer.size());
        const int right_of_caret = idx + 1;
        if (right_of_caret >= 0 && right_of_caret < size) {
            return right_of_caret;
        }

        if (idx >= 0 && idx < size) {
            return idx;
        }

        return -1;
    }

public:
    struct CandidateSpan {
        std::size_t start;
        std::size_t length;
        std::size_t offset;
        std::size_t text_length;
        std::vector<std::u32string> words;
    };

    std::optional<CandidateSpan> candidate_span(std::size_t length) const {
        if (length == 0 || length > buffer.size() || length > 8) return std::nullopt;
        const int target = candidate_target_index();
        if (target < 0) return std::nullopt;
        const auto start = idx + 1 == static_cast<int>(buffer.size())
            ? buffer.size() - length : static_cast<std::size_t>(target);
        if (start + length > buffer.size()) return std::nullopt;
        CandidateSpan span{start, length, 0, 0, {}};
        for (std::size_t i = 0; i < start; ++i) span.offset += buffer[i].current().size();
        std::vector<std::u16string> readings;
        std::u32string current;
        for (std::size_t i = start; i < start + length; ++i) {
            const auto& item = buffer[i];
            if (!item.is_compositable() || item.is_null()) return std::nullopt;
            readings.push_back(item.to_bopomofo_string());
            current += item.current32();
            span.text_length += item.current().size();
        }
        if (length == 1) {
            for (char32_t ch : buffer[start].selection_candidates()) span.words.emplace_back(1, ch);
        } else {
            const auto& words = HanziMapEngine::instance().lookup_phrases(readings);
            for (const auto& word : words) {
                if (word.size() != length) continue;
                bool valid = true;
                for (std::size_t i = 0; i < length; ++i) {
                    const auto* allowed = HanziMapEngine::instance().lookup_all(readings[i]);
                    valid &= allowed && std::find(allowed->begin(), allowed->end(), word[i]) != allowed->end();
                }
                if (valid) span.words.push_back(word);
            }
            if (!span.words.empty()) {
                std::erase(span.words, current);
                span.words.insert(span.words.begin(), current);
            }
        }
        if (span.words.empty()) return std::nullopt;
        return span;
    }

    struct CandidateChoice {
        CandidateSpan span;
        std::u32string word;
    };

    std::vector<CandidateChoice> candidate_choices() const {
        std::vector<CandidateChoice> choices;
        // Each choice carries its own length: no manual character-count setting.
        for (std::size_t length = std::min<std::size_t>(8, buffer.size()); length > 0; --length) {
            const auto span = candidate_span(length);
            if (!span) continue;
            for (const auto& word : span->words) choices.push_back({*span, word});
        }
        return choices;
    }

    std::vector<std::u16string> candidate_readings(const CandidateSpan& span) const {
        std::vector<std::u16string> readings;
        if (span.start + span.length > buffer.size()) return readings;
        for (std::size_t i = span.start; i < span.start + span.length; ++i) {
            readings.push_back(buffer[i].to_bopomofo_string());
        }
        return readings;
    }

    bool choose_phrase(const CandidateSpan& span, std::u32string_view word) {
        if (word.size() != span.length || span.start + span.length > buffer.size() ||
            std::find(span.words.begin(), span.words.end(), word) == span.words.end()) return false;
        for (std::size_t i = 0; i < span.length; ++i) {
            buffer[span.start + i].choose_candidate(word[i]);
            buffer[span.start + i].manually_selected = true;
        }
        idx = static_cast<int>(span.start + span.length) - 1;
        return true;
    }

    bool pre() {
        if (idx < 0) return false;
        idx--;
        return true;
    }
    bool next() {
        if (idx + 1 >= static_cast<int>(buffer.size())) return false;
        idx++;
        return true;
    }
    bool remove_last() {
        std::lock_guard lock(mutex);
        if (idx < 0 || idx >= static_cast<int>(buffer.size())) return false;

        bool removed = false;
        if (buffer[idx].is_compositable()) {
            buffer.erase(buffer.begin() + idx);
            idx--;
            removed = true;
        } else {
            removed = buffer[idx].pop_last_bopomofo();
            if (!removed || buffer[idx].is_null()) {
                buffer.erase(buffer.begin() + idx);
                idx--;
                removed = true;
            }
        }

        if (buffer.empty()) {
            idx = -1;
            return removed;
        }

        if (idx >= static_cast<int>(buffer.size())) {
            idx = static_cast<int>(buffer.size()) - 1;
        }

        for (int i = 0; i <= idx; i++) {
            buffer[i].predicted = false;
        }

        return removed;
    }
    void clear() {
        buffer.clear();
        idx = -1;
    }
    bool empty() const {
        return buffer.empty();
    }
    // Preserve converted characters while cancelling unfinished phonetic input.
    bool discard_unconverted() {
        std::lock_guard lock(mutex);
        bool removed = false;
        for (int i = static_cast<int>(buffer.size()) - 1; i >= 0; --i) {
            if (!buffer[i].is_compositable()) {
                buffer.erase(buffer.begin() + i);
                if (i <= idx) --idx;
                removed = true;
            }
        }
        if (removed) invalidate_all_predictions();
        return removed;
    }
    bool current_accepts_tone() const {
        if (idx < 0 || idx >= static_cast<int>(buffer.size())) return false;
        const auto& item = buffer[idx];
        return accepts_tone(item.initial, item.medial, item.final,
                            item.is_compositable(), item.is_invalid());
    }
    bool current_invalid() const {
        return idx >= 0 && idx < static_cast<int>(buffer.size()) && buffer[idx].is_invalid();
    }
    bool current_compositable() const {
        return idx >= 0 && idx < static_cast<int>(buffer.size()) && buffer[idx].is_compositable();
    }
    bool current_has_candidate_list() const {
        const int target = candidate_target_index();
        return target >= 0 && buffer[target].is_compositable() && buffer[target].get_candidates().size() > 1;
    }
    void invalidate_all_predictions() {
        for (int i = 0; i < static_cast<int>(buffer.size()); i++) {
            buffer[i].predicted = false;
        }
    }
    size_t caret_offset() const {
        size_t offset = 0;
        for (int i = 0; i <= idx && i < static_cast<int>(buffer.size()); i++) {
            offset += buffer[i].current().size();
        }
        return offset;
    }
    bool set_caret_offset(std::size_t offset) {
        if (offset == 0) {
            idx = -1;
            return true;
        }
        std::size_t boundary = 0;
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            boundary += buffer[i].current().size();
            if (boundary == offset) {
                idx = static_cast<int>(i);
                return true;
            }
            if (boundary > offset) break;
        }
        return false;
    }
    std::optional<std::pair<size_t, size_t>> current_invalid_span() const {
        if (!current_invalid()) {
            return std::nullopt;
        }

        size_t start = 0;
        for (int i = 0; i < idx; i++) {
            start += buffer[i].current().size();
        }
        return std::pair{start, buffer[idx].current().size()};
    }
    void add(char16_t ch) {
        std::lock_guard lock(mutex);
        if (Bopomofo::tone.contains(ch) && !current_accepts_tone()) return;
        if (current_invalid()) {
            // A failed lookup leaves a tone on the syllable. Remove that tone
            // so accept() can replace the matching phonetic component in place.
            buffer[idx].pop_last_bopomofo();
        }
        if (idx == -1 || !buffer[idx].accept(ch)) {
            idx++;
            // buffer.insert(buffer.begin() + idx, {}); 會炸 我操你媽的標準委員會
            buffer.insert(buffer.begin() + idx, BopomofoPos{});
            buffer[idx].accept(ch);
        } else {
            // current accept this ch
            // do nothing
        }
    }
    void add_chosen_candidate(char32_t ch) {
        std::lock_guard lock(mutex);
        if (idx >= 0 && idx < static_cast<int>(buffer.size()) && buffer[idx].is_invalid()) {
            buffer.erase(buffer.begin() + idx);
            idx--;
        }

        idx++;
        buffer.insert(buffer.begin() + idx, BopomofoPos{});
        buffer[idx].set_chosen_candidate(ch);

        for (int i = 0; i <= idx; i++) {
            buffer[i].predicted = false;
        }
        buffer[idx].predicted = true;
    }
    std::optional<std::vector<BopomofoPos>> prediction_snapshot() const {
        if (buffer.empty()) return std::nullopt;
        bool need_predict = false;
        for (const auto& item : buffer) {
            if (!item.is_predictable_by_engine()) {
                return std::nullopt;
            }
            need_predict |= !item.predicted;
        }
        if (!need_predict) return std::nullopt;
        return buffer;
    }
    bool matches_prediction(const std::vector<BopomofoPos>& snapshot) const {
        return buffer == snapshot;
    }
    bool apply_prediction(const std::vector<BopomofoPos>& snapshot,
                          std::vector<BopomofoPos> result) {
        if (result.size() != buffer.size() || !matches_prediction(snapshot)) return false;
        buffer = std::move(result);
        return true;
    }
    BopomofoPos& cur() {
        if (idx < 0 || idx >= static_cast<int>(buffer.size())) {
            throw std::runtime_error("out of range");
        }
        return buffer[idx];
    }
    BopomofoPos& candidate_target() {
        const int target = candidate_target_index();
        if (target < 0 || target >= static_cast<int>(buffer.size())) {
            throw std::runtime_error("candidate target out of range");
        }
        return buffer[target];
    }
    std::u16string to_string() const {
        std::u16string res;
        for (const auto& item : buffer) {
            res += item.current();
        }
        return res;
    }
    CommitSample commit_sample(std::u16string context) const {
        CommitSample sample;
        sample.context = std::move(context);
        sample.input.reserve(buffer.size());
        for (const auto& item : buffer) {
            const auto output = item.current();
            sample.answer += output;
            sample.input.push_back(CommitInputEntry{
                .reading = item.to_bopomofo_string(),
                .output = output,
                .manually_selected = item.was_manually_selected(),
            });
        }
        return sample;
    }
};

}  // namespace tsf
