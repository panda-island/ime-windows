#include "core/buffer.hpp"
#include "../../service/src/service/selection_memory.hpp"
#include "../../service/src/service/traditional_output.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void run_tests(const std::filesystem::path& folder) {
    std::ofstream(folder / "bopomofo_char.json") << R"({"ㄋㄧˇ":["你","妳","拟"],"ㄏㄠˇ":["好","郝"],"ㄍㄨㄛˊ":["國","国"],"ㄅㄨˋ":["不","部"],"ㄕㄥˋ":["圣","聖","勝"],"ㄊㄧˇ":["体","體"]})";
    std::ofstream(folder / "phrases.tsv") << "ㄋㄧˇ|ㄏㄠˇ\t你好\t妳好\n";
    SetEnvironmentVariableW(L"LLAVON_IME_TABLES_DIR", folder.wstring().c_str());
    require(!tsf::is_traditional_character(U'国'), "simplified-only character allowed");
    require(tsf::is_traditional_character(U'國'), "traditional character excluded");
    require(tsf::is_traditional_character(U'后'), "shared traditional character excluded");
    require(std::is_sorted(tsf::simplified_only.begin(), tsf::simplified_only.end()),
            "generated simplified-character lookup is not sorted");
    for (const auto character : tsf::simplified_only) {
        const auto normalized = tsf::to_traditional_character(character);
        require(tsf::is_traditional_character(normalized) &&
                tsf::to_traditional_character(normalized) == normalized,
                "conversion produced a simplified character or was not idempotent");
    }
    struct ScriptCase { char32_t simplified; char32_t traditional; std::u16string reading; };
    const ScriptCase script_cases[] = {{U'圣', U'聖', u"ㄕㄥˋ"}, {U'体', U'體', u"ㄊㄧˇ"}};
    for (const auto& entry : script_cases) {
        require(!tsf::is_traditional_character(entry.simplified) &&
                tsf::to_traditional_character(entry.simplified) == entry.traditional,
                "reported simplified character was not converted");
        tsf::BopomofoPos position;
        for (const auto ch : entry.reading) position.accept(ch);
        require(std::ranges::all_of(position.get_candidates(), tsf::is_traditional_character),
                "reported simplified character leaked through the local table");
        position.set_candaiates({entry.simplified, entry.traditional});
        require(position.current32() == entry.traditional && position.get_candidates().size() == 1,
                "model output was not normalized and deduplicated");
        position.choose_candidate(entry.simplified);
        require(position.current32() == entry.traditional, "chosen model output was simplified");
        position.set_chosen_candidate(entry.simplified);
        require(position.current32() == entry.traditional, "fixed character bypassed normalization");
        // Simulate a stale engine or imported candidate bypassing setters.
        position.candidates = {entry.simplified};
        require(position.current32() == entry.traditional, "final UTF-32 output was simplified");
        std::u16string expected;
        utf8::append16(entry.traditional, expected);
        require(position.current() == expected, "final UTF-16 output was simplified");
        require(position.selection_candidates() == std::vector<char32_t>{entry.traditional},
                "selection list leaked an unnormalized fixed character");
    }
    const std::vector<std::pair<char32_t, float>> legacy_candidates{
        {U'圣', 0.9F}, {U'聖', 0.8F}, {U'体', 0.7F}, {U'體', 0.6F}, {U'勝', 0.5F}};
    require(llavon::service::traditional_response_candidates(legacy_candidates) ==
            std::vector<char32_t>{U'聖', U'體', U'勝'},
            "legacy pipe response leaked simplified output or changed ranking");
    tsf::CompositionBuffer fixed_output;
    fixed_output.add_chosen_candidate(U'圣');
    fixed_output.add_chosen_candidate(U'体');
    require(fixed_output.to_string() == u"聖體" && fixed_output.commit_sample({}).answer == u"聖體",
            "composition or committed output was simplified");
    require(tsf::HanziMapEngine::instance().lookup_all(u"ㄍㄨㄛˊ")->size() == 1, "local candidates were not filtered");
    tsf::BopomofoPos predicted;
    predicted.set_chosen_candidate(U'國');
    predicted.set_candaiates({U'国'});
    predicted.choose_candidate(U'国');
    require(predicted.current32() == U'國', "model prediction bypassed traditional filtering");
    tsf::BopomofoPos narrowed;
    for (char16_t ch : std::u16string(u"ㄋㄧˇ")) narrowed.accept(ch);
    narrowed.set_candaiates({U'你', U'拟'});
    const auto alternatives = narrowed.selection_candidates();
    require(std::find(alternatives.begin(), alternatives.end(), U'妳') != alternatives.end(),
            "model token coverage removed a local manual alternative");
    require(std::find(alternatives.begin(), alternatives.end(), U'拟') == alternatives.end(),
            "simplified model alternative appeared in the selection list");
    tsf::CompositionBuffer buffer;
    for (char16_t ch : std::u16string(u"ㄋㄧˇㄏㄠˇㄅㄨˋ")) buffer.add(ch);
    buffer.pre();
    buffer.pre();
    buffer.pre(); // caret before all three syllables
    const auto choices = buffer.candidate_choices();
    const auto word = std::find_if(choices.begin(), choices.end(), [](const auto& entry) {return entry.word == U"妳好";});
    require(word != choices.end(), "phrase at the insertion caret is missing");
    require(word->span.start == 0 && word->span.length == 2, "incorrect forward phrase range");
    require(buffer.choose_phrase(word->span, word->word), "phrase selection failed");
    require(buffer.to_string() == u"妳好不", "phrase selection changed adjacent text");
    require(buffer.caret_offset() == 2, "caret did not advance past the whole phrase");
    const auto after = buffer.candidate_choices();
    require(!after.empty() && after.front().span.start == 2, "next selection does not start at the caret");
    buffer.clear();
    for (char16_t ch : std::u16string(u"ㄅㄨˋㄋㄧˇㄏㄠˇㄅㄨˋ")) buffer.add(ch);
    require(buffer.set_caret_offset(1), "host caret position was not accepted");
    const auto middle = buffer.candidate_span(2);
    require(middle && middle->start == 1 && middle->offset == 1 && middle->text_length == 2,
            "middle caret did not select the following word");
    require(buffer.choose_phrase(*middle, U"妳好"), "middle phrase selection failed");
    require(buffer.to_string() == u"不妳好不" && buffer.caret_offset() == 3,
            "middle phrase selection changed surrounding text or caret");
    const auto sample = buffer.commit_sample({});
    require(!sample.input[0].manually_selected && sample.input[1].manually_selected &&
            sample.input[2].manually_selected && !sample.input[3].manually_selected,
            "manual phrase boundaries were not preserved");
    require(!buffer.set_caret_offset(99) && buffer.caret_offset() == 3,
            "invalid host caret changed the insertion position");
    buffer.clear();
    for (char16_t ch : std::u16string(u"ㄋㄧˇㄏㄠˇ")) buffer.add(ch);
    const auto tail = buffer.candidate_span(2);
    require(tail && tail->start == 0, "tail caret did not offer the preceding word");
    buffer.clear();
    buffer.add_chosen_candidate(U'\U00020000');
    buffer.add_chosen_candidate(U'，');
    require(!buffer.set_caret_offset(1), "host caret split a surrogate pair");
    require(buffer.set_caret_offset(2) && buffer.caret_offset() == 2,
            "supplementary character caret used the wrong UTF-16 offset");
    buffer.clear();
    for (char16_t ch : std::u16string(u"ㄋㄧˇ")) buffer.add(ch);
    buffer.add_chosen_candidate(U'，');
    for (char16_t ch : std::u16string(u"ㄏㄠˇ")) buffer.add(ch);
    require(buffer.set_caret_offset(0) && !buffer.candidate_span(3),
            "phrase selection crossed punctuation");
    const std::vector<std::u16string> readings{u"ㄋㄧˇ", u"ㄏㄠˇ"};
    const auto database = folder / "selection-memory.sqlite3";
    {
        llavon::service::SelectionMemory memory(database);
        require(memory.remember(tsf::PhraseLexicon::key(readings), U"妳好"), "selection was not saved");
    }
    {
        llavon::service::SelectionMemory memory(database);
        const auto recalled = memory.recall(readings);
        require(recalled.size() == 1 && recalled.front().word == U"妳好", "selection did not survive restart");
        require(memory.remember(tsf::PhraseLexicon::key(readings), U"你好"), "replacement preference was not saved");
        const auto replaced = memory.recall(readings);
        require(replaced.size() == 1 && replaced.front().word == U"你好", "latest choice was not preferred");
        require(memory.remember(u"ㄏㄠˇ", U"郝"), "overlapping single choice was not saved");
        const auto overlap = memory.recall(readings);
        require(overlap.size() == 2 && overlap.front().word == U"郝" && overlap.front().start == 1,
                "newest overlapping preference did not take precedence");
        const std::vector<std::u16string> separated{u"ㄋㄧˇ", u"", u"ㄏㄠˇ"};
        const auto boundary = memory.recall(separated);
        require(boundary.size() == 1 && boundary.front().start == 2,
                "remembered phrase crossed a fixed-text boundary");
        const std::vector<std::u16string> first_tone{u"ㄅㄚ "};
        require(memory.remember(tsf::PhraseLexicon::key(first_tone), U"巴") &&
                memory.recall(first_tone).size() == 1, "first-tone preference was not normalized");
        const std::vector<std::u16string> reported{u"ㄕㄥˋ", u"ㄊㄧˇ"};
        require(memory.remember(tsf::PhraseLexicon::key(reported), U"圣体") &&
                memory.recall(reported).front().word == U"聖體",
                "new selection memory retained simplified text");
    }
    // Seed an old database directly, before the normalization path existed.
    sqlite3* legacy = nullptr;
    const auto filename = database.u8string();
    require(sqlite3_open(reinterpret_cast<const char*>(filename.c_str()), &legacy) == SQLITE_OK,
            "could not open legacy selection memory");
    sqlite3_stmt* insert = nullptr;
    require(sqlite3_prepare_v2(legacy, "INSERT OR REPLACE INTO choices(reading,word) VALUES(?,?)",
                -1, &insert, nullptr) == SQLITE_OK, "legacy selection insert failed");
    const std::u16string legacy_key = u"ㄕㄥˋ|ㄊㄧˇ";
    const std::u32string legacy_word = U"圣体";
    sqlite3_bind_blob(insert, 1, legacy_key.data(), static_cast<int>(legacy_key.size() * sizeof(char16_t)), SQLITE_TRANSIENT);
    sqlite3_bind_blob(insert, 2, legacy_word.data(), static_cast<int>(legacy_word.size() * sizeof(char32_t)), SQLITE_TRANSIENT);
    const auto saved = sqlite3_step(insert);
    sqlite3_finalize(insert);
    sqlite3_close(legacy);
    require(saved == SQLITE_DONE, "legacy selection could not be seeded");
    llavon::service::SelectionMemory upgraded(database);
    const std::vector<std::u16string> reported{u"ㄕㄥˋ", u"ㄊㄧˇ"};
    const auto recovered = upgraded.recall(reported);
    require(recovered.size() == 1 && recovered.front().word == U"聖體",
            "old persisted simplified selection bypassed conversion");
}

void dictionary_tests(const std::filesystem::path& folder, const char* mapping, const char* phrases) {
    std::filesystem::copy_file(mapping, folder / "bopomofo_char.json");
    std::filesystem::copy_file(phrases, folder / "phrases.tsv");
    SetEnvironmentVariableW(L"LLAVON_IME_TABLES_DIR", folder.wstring().c_str());
    struct Case { std::u16string reading; std::u32string word; };
    const Case cases[] = {{u"ㄊㄞˊㄨㄢ ", U"臺灣"}, {u"ㄕㄨ ㄖㄨˋㄈㄚˇ", U"輸入法"},
                          {u"ㄖㄣˊㄍㄨㄥ ㄓˋㄏㄨㄟˋ", U"人工智慧"}, {u"ㄕㄥˋㄊㄧˇ", U"聖體"}};
    for (const auto& entry : cases) {
        tsf::CompositionBuffer buffer;
        for (const auto ch : entry.reading) buffer.add(ch);
        require(buffer.set_caret_offset(0), "dictionary caret positioning failed");
        const auto choices = buffer.candidate_choices();
        const auto found = std::find_if(choices.begin(), choices.end(), [&entry](const auto& choice) {
            return choice.word == entry.word;
        });
        require(found != choices.end(), "packaged dictionary word is missing from actual candidates");
        require(buffer.choose_phrase(found->span, found->word), "packaged dictionary word could not be selected");
    }
    for (const auto ch : tsf::HanziMapEngine::instance().get_word_set()) {
        require(tsf::is_traditional_character(ch), "packaged character table leaked simplified output");
    }
}
}
int main(int argc, char** argv) {
    const auto folder = std::filesystem::temp_directory_path() / ("llavon-language-tests-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    try {
        if (argc == 3) dictionary_tests(folder, argv[1], argv[2]);
        else run_tests(folder);
        std::filesystem::remove_all(folder);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        std::filesystem::remove_all(folder);
        return 1;
    }
}
