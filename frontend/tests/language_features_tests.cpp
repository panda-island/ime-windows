#include "core/buffer.hpp"
#include "../../service/src/service/selection_memory.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void run_tests(const std::filesystem::path& folder) {
    std::ofstream(folder / "bopomofo_char.json") << R"({"ㄋㄧˇ":["你","妳","拟"],"ㄏㄠˇ":["好","郝"],"ㄍㄨㄛˊ":["國","国"],"ㄅㄨˋ":["不","部"]})";
    std::ofstream(folder / "phrases.tsv") << "ㄋㄧˇ|ㄏㄠˇ\t你好\t妳好\n";
    SetEnvironmentVariableW(L"LLAVON_IME_TABLES_DIR", folder.wstring().c_str());
    require(!tsf::is_traditional_character(U'国'), "simplified-only character allowed");
    require(tsf::is_traditional_character(U'國'), "traditional character excluded");
    require(tsf::is_traditional_character(U'后'), "shared traditional character excluded");
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
    }
}

void dictionary_tests(const std::filesystem::path& folder, const char* mapping, const char* phrases) {
    std::filesystem::copy_file(mapping, folder / "bopomofo_char.json");
    std::filesystem::copy_file(phrases, folder / "phrases.tsv");
    SetEnvironmentVariableW(L"LLAVON_IME_TABLES_DIR", folder.wstring().c_str());
    struct Case { std::u16string reading; std::u32string word; };
    const Case cases[] = {{u"ㄊㄞˊㄨㄢ ", U"臺灣"}, {u"ㄕㄨ ㄖㄨˋㄈㄚˇ", U"輸入法"},
                          {u"ㄖㄣˊㄍㄨㄥ ㄓˋㄏㄨㄟˋ", U"人工智慧"}};
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
