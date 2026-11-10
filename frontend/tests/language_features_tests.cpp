#include "core/buffer.hpp"
#include "../../service/src/service/selection_memory.hpp"
#include "../../service/src/service/traditional_output.hpp"
#include "../../service/src/service/custom_name_matcher.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void run_tests(const std::filesystem::path& folder) {
    std::ofstream(folder / "bopomofo_char.json") << R"({"ㄋㄧˇ":["你","妳","拟"],"ㄏㄠˇ":["好","郝"],"ㄍㄨㄛˊ":["國","国"],"ㄅㄨˋ":["不","部"],"ㄕㄥˋ":["圣","聖","勝"],"ㄊㄧˇ":["体","體"],"ㄅㄧㄢˋ":["变"]})";
    std::ofstream(folder / "phrases.tsv") << "ㄋㄧˇ|ㄏㄠˇ\t你好\t妳好\n"
        "ㄕㄥˋ|ㄊㄧˇ\t圣体\t聖體\nㄕㄥˋ|ㄊㄧˇ|ㄅㄨˋ\t圣体不\n";
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
        require(tsf::is_traditional_character(position.current32()) &&
                std::find(position.get_candidates().begin(), position.get_candidates().end(), entry.simplified) !=
                    position.get_candidates().end(), "local simplified alternative was lost or preferred");
        position.set_candaiates({entry.simplified, entry.traditional});
        require(position.current32() == entry.traditional && position.get_candidates().back() == entry.simplified,
                "model ranking preferred simplified output or removed it");
        position.set_candaiates({entry.simplified});
        require(tsf::is_traditional_character(position.current32()),
                "model token coverage hid the local traditional alternative");
        position.choose_candidate(entry.simplified);
        require(position.current32() == entry.simplified, "manual simplified choice was changed");
        require(tsf::is_traditional_character(position.selection_candidates().front()) &&
                position.selection_candidates().back() == entry.simplified,
                "manual selection changed the script preference of the candidate list");
        position.set_chosen_candidate(entry.simplified);
        require(position.current32() == entry.simplified, "explicit fixed character was converted");
        std::u16string expected;
        utf8::append16(entry.simplified, expected);
        require(position.current() == expected, "final UTF-16 output changed the selected character");
    }
    const std::vector<std::pair<char32_t, float>> legacy_candidates{
        {U'圣', 0.9F}, {U'聖', 0.8F}, {U'体', 0.7F}, {U'體', 0.6F}, {U'勝', 0.5F}, {U'圣', 0.4F}};
    require(llavon::service::traditional_response_candidates(legacy_candidates) ==
            std::vector<char32_t>{U'聖', U'體', U'勝', U'圣', U'体'},
            "legacy pipe response removed simplified candidates or did not prefer traditional output");
    llavon::service::CustomNameMatcher simplified_name({
        llavon::service::CustomNameSetting{U"圣体", {u"ㄕㄥˋ", u"ㄊㄧˇ"}}});
    std::vector<llavon::ime::core::PaddingEntry> name_padding{
        {.chosen = false, .chosen_char = 0, .bopomofo = u"ㄕㄥˋ"},
        {.chosen = false, .chosen_char = 0, .bopomofo = u"ㄊㄧˇ"}};
    const auto name_matches = simplified_name.apply(name_padding);
    require(name_matches[0] == U'圣' && name_matches[1] == U'体' &&
            !name_padding[0].chosen && !name_padding[1].chosen,
            "simplified custom name prevented traditional model alternatives");
    std::vector<std::pair<char32_t, float>> name_candidates{{U'聖', 0.9F}};
    llavon::service::apply_custom_name_candidate(name_candidates, *name_matches[0]);
    require(llavon::service::traditional_response_candidates(name_candidates) ==
            std::vector<char32_t>{U'聖', U'圣'}, "simplified custom name became the automatic first choice");
    tsf::CompositionBuffer fixed_output;
    fixed_output.add_chosen_candidate(U'圣');
    fixed_output.add_chosen_candidate(U'体');
    require(fixed_output.to_string() == u"圣体" && fixed_output.commit_sample({}).answer == u"圣体",
            "composition or commit changed the explicit character choice");
    require(tsf::HanziMapEngine::instance().lookup_all(u"ㄍㄨㄛˊ")->size() == 2,
            "local simplified alternative was removed");
    tsf::CompositionBuffer simplified_only;
    for (const auto ch : std::u16string(u"ㄅㄧㄢˋ")) simplified_only.add(ch);
    require(simplified_only.to_string() == u"变" && !simplified_only.current_invalid(),
            "simplified-only reading became untypeable or was silently converted");
    tsf::BopomofoPos predicted;
    predicted.set_chosen_candidate(U'國');
    predicted.set_candaiates({U'国'});
    predicted.choose_candidate(U'国');
    require(predicted.current32() == U'国', "manual simplified selection was not preserved");
    tsf::BopomofoPos narrowed;
    for (char16_t ch : std::u16string(u"ㄋㄧˇ")) narrowed.accept(ch);
    narrowed.set_candaiates({U'你', U'拟'});
    const auto alternatives = narrowed.selection_candidates();
    require(std::find(alternatives.begin(), alternatives.end(), U'妳') != alternatives.end(),
            "model token coverage removed a local manual alternative");
    require(alternatives == std::vector<char32_t>{U'你', U'妳', U'拟'},
            "simplified model alternative was removed or placed ahead of traditional alternatives");
    tsf::CompositionBuffer mixed_phrases;
    for (const auto ch : std::u16string(u"ㄕㄥˋㄊㄧˇㄅㄨˋ")) mixed_phrases.add(ch);
    require(mixed_phrases.set_caret_offset(0), "mixed phrase caret positioning failed");
    const auto mixed_choices = mixed_phrases.candidate_choices();
    require(std::is_partitioned(mixed_choices.begin(), mixed_choices.end(), [](const auto& choice) {
                return tsf::is_traditional_word(choice.word);
            }), "a longer simplified phrase preceded traditional candidates");
    const auto simple_phrase = std::find_if(mixed_choices.begin(), mixed_choices.end(), [](const auto& choice) {
        return choice.word == U"圣体";
    });
    require(simple_phrase != mixed_choices.end() && mixed_phrases.choose_phrase(simple_phrase->span, simple_phrase->word),
            "simplified phrase could not be manually selected");
    require(mixed_phrases.to_string() == u"圣体不" && mixed_phrases.caret_offset() == 2,
            "simplified phrase selection changed its spelling or adjacent text");
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
        require(memory.remember(u"ㄊㄧˇ", U"體") &&
                memory.remember(tsf::PhraseLexicon::key(reported), U"圣体"), "mixed-script preferences were not saved");
        const auto mixed_memory = memory.recall(reported);
        require(mixed_memory.size() == 2 && mixed_memory.front().word == U"體" &&
                mixed_memory.back().word == U"圣体", "newer simplified memory displaced a traditional preference");
        const std::vector<std::pair<char32_t, float>> remembered_candidates{
            {mixed_memory.back().word.front(), 1.0F}, {U'聖', 0.5F}};
        require(llavon::service::traditional_response_candidates(remembered_candidates) ==
                std::vector<char32_t>{U'聖', U'圣'}, "remembered simplified choice became the default");
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
    require(recovered.size() == 2 && recovered.front().word == U"體" && recovered.back().word == U"圣体",
            "old simplified memory was changed or prioritized ahead of traditional memory");
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
    std::ifstream original(folder / "bopomofo_char.json");
    const auto source = rfl::json::read<std::unordered_map<std::string, std::vector<std::string>>>(original).value();
    std::size_t restored = 0;
    for (const auto& [reading, characters] : source) {
        const auto* candidates = tsf::HanziMapEngine::instance().lookup_all(utf8::utf8to16(reading));
        require(candidates && !candidates->empty(), "an original reading disappeared from the packaged table");
        require(std::is_partitioned(candidates->begin(), candidates->end(), tsf::is_traditional_character),
                "packaged character candidates did not prefer traditional characters");
        for (const auto& text : characters) {
            const auto character = utf8::utf8to32(text).front();
            require(std::find(candidates->begin(), candidates->end(), character) != candidates->end(),
                    "original dictionary character is missing from candidates");
            if (!tsf::is_traditional_character(character)) ++restored;
        }
    }
    require(restored > 0, "real dictionary did not restore simplified alternatives");
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
