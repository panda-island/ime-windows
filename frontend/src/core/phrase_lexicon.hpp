#pragma once
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tsf {

class PhraseLexicon {
public:
    static std::u16string key(std::span<const std::u16string> readings) {
        std::u16string result;
        for (const auto& reading : readings) {
            if (!result.empty()) result += u'|';
            for (char16_t ch : reading) {
                if (ch != u' ') result += ch;
            }
        }
        return result;
    }

    void add(std::u16string reading_key, std::u32string word) {
        entries_[std::move(reading_key)].push_back(std::move(word));
    }

    const std::vector<std::u32string>& lookup(std::span<const std::u16string> readings) const {
        static const std::vector<std::u32string> empty;
        const auto found = entries_.find(key(readings));
        return found == entries_.end() ? empty : found->second;
    }

private:
    std::unordered_map<std::u16string, std::vector<std::u32string>> entries_;
};

}  // namespace tsf
