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
    }
}
}
int main() {
    const auto folder = std::filesystem::temp_directory_path() / ("llavon-language-tests-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    try {
        run_tests(folder);
        std::filesystem::remove_all(folder);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        std::filesystem::remove_all(folder);
        return 1;
    }
}
