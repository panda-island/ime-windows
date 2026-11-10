#pragma once
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace llavon::service {

class SelectionMemory {
public:
    struct Match {
        std::size_t start;
        std::u32string word;
        std::int64_t revision;
    };

    explicit SelectionMemory(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;
        const auto utf8 = path.u8string();
        if (sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &db_,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            sqlite3_close(db_);
            db_ = nullptr;
            return;
        }
        sqlite3_busy_timeout(db_, 2000);
        if (sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS choices("
                "revision INTEGER PRIMARY KEY AUTOINCREMENT, reading BLOB UNIQUE NOT NULL, word BLOB NOT NULL)",
                nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    ~SelectionMemory() { if (db_) sqlite3_close(db_); }
    SelectionMemory(const SelectionMemory&) = delete;
    SelectionMemory& operator=(const SelectionMemory&) = delete;

    bool remember(std::u16string_view key, std::u32string_view word) {
        if (!db_ || key.empty() || key.size() > 128 || word.empty() || word.size() > 8) return false;
        std::lock_guard lock(mutex_);
        sqlite3_stmt* query = nullptr;
        if (sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO choices(reading,word) VALUES(?,?)",
                -1, &query, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_blob(query, 1, key.data(), static_cast<int>(key.size() * sizeof(char16_t)), SQLITE_TRANSIENT);
        sqlite3_bind_blob(query, 2, word.data(), static_cast<int>(word.size() * sizeof(char32_t)), SQLITE_TRANSIENT);
        const bool saved = sqlite3_step(query) == SQLITE_DONE;
        sqlite3_finalize(query);
        return saved;
    }

    std::vector<Match> recall(std::span<const std::u16string> readings) {
        std::vector<Match> matches;
        if (!db_) return matches;
        std::lock_guard lock(mutex_);
        sqlite3_stmt* query = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT word,revision FROM choices WHERE reading=?",
                -1, &query, nullptr) != SQLITE_OK) return matches;
        for (std::size_t start = 0; start < readings.size(); ++start) {
            std::u16string key;
            for (std::size_t length = 1; length <= 8 && start + length <= readings.size(); ++length) {
                const auto& reading = readings[start + length - 1];
                if (reading.empty()) break;
                if (!key.empty()) key += u'|';
                for (char16_t ch : reading) if (ch != u' ') key += ch;
                sqlite3_reset(query);
                sqlite3_bind_blob(query, 1, key.data(), static_cast<int>(key.size() * sizeof(char16_t)), SQLITE_TRANSIENT);
                if (sqlite3_step(query) == SQLITE_ROW) {
                    const auto bytes = sqlite3_column_bytes(query, 0);
                    if (bytes == static_cast<int>(length * sizeof(char32_t))) {
                        std::u32string word(length, 0);
                        std::memcpy(word.data(), sqlite3_column_blob(query, 0), static_cast<std::size_t>(bytes));
                        matches.push_back({start, std::move(word), sqlite3_column_int64(query, 1)});
                    }
                }
            }
        }
        sqlite3_finalize(query);
        std::sort(matches.begin(), matches.end(), [](const Match& left, const Match& right) {
            return left.revision > right.revision;
        });
        return matches;
    }

private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

}  // namespace llavon::service
