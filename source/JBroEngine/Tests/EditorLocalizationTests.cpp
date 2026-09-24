#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 화면에 나오는 글자를 키로 다루는 표다(ProjectRule §11.2).
//
// 여기서 재는 것은 **못 찾았을 때 무엇이 나오는가** 다. 번역이 빠지는 것은 늘
// 일어나는 일이고, 그때 화면이 어떻게 되는지가 이 표의 값어치를 정한다.

namespace
{
    // 파일은 플랫폼이 읽는다(D-112). 테스트마다 하나를 만들지 않고 한 번 초기화해 나눠 쓴다.
    JBro::WindowsPlatform& Platform()
    {
        static JBro::WindowsPlatform platform;
        static bool initialized = false;
        if (false == initialized)
        {
            JBro::JMemoryContext memory;
            initialized = platform.Initialize(memory);
        }
        return platform;
    }

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    std::filesystem::path MakeProbeDirectory()
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / "JBroLocalizationProbe";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        return directory;
    }

    void WriteFile(const std::filesystem::path& path, const char* text)
    {
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    void TestTheTableFindsAndFallsBack()
    {
        const std::filesystem::path directory = MakeProbeDirectory();
        WriteFile(directory / "ko-KR.yaml",
            "Locale: ko-KR\n"
            "Entries:\n"
            "  probe.both: 둘 다 있음\n"
            "  probe.korean_only: 한국어만\n");
        WriteFile(directory / "en-US.yaml",
            "Locale: en-US\n"
            "Entries:\n"
            "  probe.both: in both\n"
            "  probe.english_only: english only\n");

        JBro::LocalizationTable& table = JBro::LocalizationTable::Get();
        Check(table.Load(Platform(), directory.string().c_str(), "ko-KR", "en-US"),
            "both locale files must load");
        Check(table.GetLocale() == JBro::String("ko-KR"), "and the locale must be set");
        Check(table.GetCount() == 2, "with the entries the file had");

        Check(std::strcmp(JBro::Loc::Text("probe.both"), "둘 다 있음") == 0,
            "a key in the current locale comes back in that locale");
        Check(std::strcmp(JBro::Loc::Text("probe.korean_only"), "한국어만") == 0,
            "and so does one only that locale has");

        // **폴백이 있으면 키가 아니라 다른 언어를 보여 준다.** 화면에
        // `probe.english_only` 가 나오는 것보다 영어가 나오는 편이 낫다.
        Check(std::strcmp(JBro::Loc::Text("probe.english_only"), "english only") == 0,
            "a key missing from the current locale falls back to the other one");

        // **아무 데도 없으면 키가 나온다.** 빈 자리로 두면 빠진 줄을 모른다.
        Check(std::strcmp(JBro::Loc::Text("probe.nowhere"), "probe.nowhere") == 0,
            "a key nobody has shows itself, so the gap is visible");
        // 원문을 주면 그것이 나온다. 코드에 있는 영어가 이 자리다.
        Check(std::strcmp(JBro::Loc::TextOr("probe.nowhere", "fallback text"),
                "fallback text") == 0,
            "and TextOr shows the text the caller carried");
        Check(std::strcmp(JBro::Loc::TextOr("probe.both", "fallback text"),
                "둘 다 있음") == 0,
            "but a key that exists wins over the caller's text");

        Check(std::strcmp(JBro::Loc::Text(nullptr), "") == 0, "nothing is not a key");
        Check(std::strcmp(JBro::Loc::TextOr(nullptr, "given"), "given") == 0,
            "and then the caller's text stands");

        std::filesystem::remove_all(directory);
    }

    // **읽지 못하면 있던 표를 지우지 않는다.** 지우면 이미 그려지던 화면이
    // 갑자기 키로 바뀐다 - 파일 하나 잘못 건드린 대가치고 너무 크다.
    void TestAFailedLoadLeavesTheOldTableStanding()
    {
        const std::filesystem::path directory = MakeProbeDirectory();
        WriteFile(directory / "ko-KR.yaml",
            "Locale: ko-KR\n"
            "Entries:\n"
            "  probe.kept: 남아 있어야 한다\n");

        JBro::LocalizationTable& table = JBro::LocalizationTable::Get();
        Check(table.Load(Platform(), directory.string().c_str(), "ko-KR", "ko-KR"),
            "the locale must load");
        Check(std::strcmp(JBro::Loc::Text("probe.kept"), "남아 있어야 한다") == 0,
            "and be readable");

        Check(false == table.Load(Platform(), directory.string().c_str(), "nobody", "nobody"),
            "a locale with no file must report failure");
        Check(std::strcmp(JBro::Loc::Text("probe.kept"), "남아 있어야 한다") == 0,
            "and must leave what was already loaded alone");

        table.Clear();
        Check(table.GetCount() == 0, "clearing empties it");
        Check(std::strcmp(JBro::Loc::Text("probe.kept"), "probe.kept") == 0,
            "and then the key shows itself again");

        std::filesystem::remove_all(directory);
    }

    // 에디터가 쓰는 로케일 파일이 **두 벌 다 같은 키를 담고 있는가.**
    // 한쪽에만 있으면 그 언어에서 조용히 다른 언어가 나온다.
    void TestTheShippedLocalesAgree()
    {
        JBro::LocalizationTable& table = JBro::LocalizationTable::Get();
        if (false == table.Load(Platform(), "Localization", "ko-KR", "en-US"))
        {
            std::cout << "  [skip] no Localization directory beside the test"
                << std::endl;
            return;
        }
        const std::size_t korean = table.GetCount();
        Check(korean > 0, "the shipped Korean locale must have entries");

        Check(table.Load(Platform(), "Localization", "en-US", "ko-KR"),
            "and the English one must load too");
        Check(table.GetCount() == korean,
            "both shipped locales must carry the same number of keys");

        // 키 상수가 실제로 표에 있는지 몇 개만 짚어 본다. 전부 세려면 키 목록을
        // 데이터로 들고 있어야 하는데, 그러면 목록이 둘이 된다.
        Check(table.Find(JBro::LocKeys::PanelInspector) != nullptr,
            "a key the code names must exist in the file");
        Check(table.Find(JBro::LocKeys::InspectorAddComponent) != nullptr,
            "and so must the rest of the ones this test names");
        Check(table.Find(JBro::LocKeys::MenuUndo) != nullptr, "including the menu");
        Check(table.Find(JBro::LocKeys::StatsDropped) != nullptr, "and the stats");

        table.Clear();
    }

    // printf 지정자에서 인자를 읽는 방법만 남긴다 - 길이 수식어와 변환 문자다.
    // 폭과 정밀도(`%.2f` 대 `%.1f`)는 번역이 달리해도 안전하므로 뺀다.
    std::string ConversionsOf(const char* text)
    {
        std::string conversions;
        for (const char* at = text; *at != '\0'; ++at)
        {
            if (*at != '%')
            {
                continue;
            }
            ++at;
            if (*at == '%')
            {
                continue;
            }
            while (*at != '\0' && std::strchr("diouxXeEfFgGaAcspn", *at) == nullptr)
            {
                if (std::strchr("hljztL", *at) != nullptr)
                {
                    conversions += *at;
                }
                ++at;
            }
            if (*at == '\0')
            {
                conversions += '?';
                break;
            }
            conversions += *at;
            conversions += ' ';
        }
        return conversions;
    }

    bool ReadEntries(const char* path, JBro::YamlDocument& document, std::uint32_t& entries)
    {
        std::ifstream file(path, std::ios::binary);
        if (false == file.is_open())
        {
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        JBro::YamlError error;
        Check(document.Parse(text.c_str(), text.size(), error),
            "a shipped locale file must parse");
        entries = document.Find(document.GetRoot(), "Entries");
        Check(entries != 0 && document.GetKind(entries) == JBro::YamlKind::Map,
            "and carry its entries as a map");
        return true;
    }

    // **번역이 printf 지정자를 바꾸면 안 된다.** 인자는 코드가 넘기므로, 한 언어가
    // `%llu` 를 `%u` 로 적으면 그 언어에서만 틀린 크기로 읽는다 - 컴파일러는 형식이
    // 데이터에 있어 보지 못하고, 영어로 도는 테스트도 보지 못한다.
    void TestTheShippedLocalesAgreeOnFormats()
    {
        JBro::YamlDocument korean;
        JBro::YamlDocument english;
        std::uint32_t koreanEntries = 0;
        std::uint32_t englishEntries = 0;
        if (false == ReadEntries("Localization/ko-KR.yaml", korean, koreanEntries)
            || false == ReadEntries("Localization/en-US.yaml", english, englishEntries))
        {
            std::cout << "  [skip] no Localization directory beside the test" << std::endl;
            return;
        }

        std::size_t formats = 0;
        const std::size_t count = korean.GetCount(koreanEntries);
        for (std::size_t index = 0; index < count; ++index)
        {
            const char* key = korean.GetKey(koreanEntries, index);
            const std::uint32_t other = english.Find(englishEntries, key);
            Check(other != 0, "every Korean key must have an English entry");
            const char* koreanText = korean.GetText(korean.GetValue(koreanEntries, index));
            const char* englishText = english.GetText(other);
            Check(koreanText != nullptr && englishText != nullptr,
                "and both must be text");
            const std::string conversions = ConversionsOf(englishText);
            if (conversions != ConversionsOf(koreanText))
            {
                std::cout << "  " << key << ": [" << conversions << "] vs ["
                    << ConversionsOf(koreanText) << "]" << std::endl;
                Check(false, "a translation must keep the printf conversions of its key");
            }
            if (false == conversions.empty())
            {
                ++formats;
            }
        }
        // 형식 문자열이 하나도 없으면 이 검사는 아무것도 재지 않은 것이다.
        Check(formats > 0, "the shipped locales must contain format strings to compare");
    }

    // 한 줄에서 `inline constexpr const char* Name = "key";` 의 둘을 꺼낸다.
    bool ParseKeyLine(const std::string& line, std::string& name, std::string& key)
    {
        const std::size_t star = line.find("const char* ");
        const std::size_t equals = line.find(" = \"", star);
        if (star == std::string::npos || equals == std::string::npos)
        {
            return false;
        }
        const std::size_t close = line.find('"', equals + 4);
        if (close == std::string::npos)
        {
            return false;
        }
        name = line.substr(star + 12, equals - (star + 12));
        key = line.substr(equals + 4, close - (equals + 4));
        return name.empty() == false && key.empty() == false;
    }

    // **키 목록과 로케일 파일과 코드가 셋 다 같은 것을 말하는지 본다**(D-195).
    //
    // 위의 `TestTheShippedLocalesAgree` 는 키 상수 넷만 짚어 본다 - "전부 세려면 목록이
    // 둘이 된다" 는 이유였다. 목록을 또 적는 대신 **헤더를 읽으면** 그 문제가 없다.
    // 이 검사가 막는 것은 셋이다. 로케일 파일에 없는 키를 코드가 부르면 화면에 키가
    // 그대로 나오고(폴백), 코드가 아무 데서도 부르지 않는 키는 번역해야 할 목록을
    // 부풀리며, 헤더에 없는 키가 파일에 있으면 아무도 그것이 죽은 줄 모른다.
    void TestEveryKeyIsDeclaredTranslatedAndUsed()
    {
        namespace fs = std::filesystem;
        const fs::path header(
            "Modules/JBroEditor/Include/JBro/Editor/LocalizationKeys.h");
        std::error_code ignored;
        if (false == fs::is_regular_file(header, ignored))
        {
            std::cout << "  [skip] the editor sources are not beside the test" << std::endl;
            return;
        }
        std::vector<std::pair<std::string, std::string>> declared;
        {
            std::ifstream in(header, std::ios::binary);
            std::string line;
            while (std::getline(in, line))
            {
                std::string name;
                std::string key;
                if (ParseKeyLine(line, name, key))
                {
                    declared.emplace_back(name, key);
                }
            }
        }
        Check(declared.size() > 200, "the key header must actually have been read");

        // ① 선언한 키는 두 로케일 모두에 있어야 한다.
        JBro::YamlDocument korean;
        JBro::YamlDocument english;
        std::uint32_t koreanEntries = 0;
        std::uint32_t englishEntries = 0;
        if (false == ReadEntries("Localization/ko-KR.yaml", korean, koreanEntries)
            || false == ReadEntries("Localization/en-US.yaml", english, englishEntries))
        {
            std::cout << "  [skip] no Localization directory beside the test" << std::endl;
            return;
        }
        for (const auto& [name, key] : declared)
        {
            if (korean.Find(koreanEntries, key.c_str()) == 0
                || english.Find(englishEntries, key.c_str()) == 0)
            {
                std::cout << "  " << name << " (" << key << ")" << std::endl;
                Check(false, "every declared key must be translated in both locales");
            }
        }

        // ② 로케일 파일의 키는 헤더가 선언한 것이어야 한다.
        const std::size_t count = korean.GetCount(koreanEntries);
        for (std::size_t index = 0; index < count; ++index)
        {
            const char* key = korean.GetKey(koreanEntries, index);
            bool found = false;
            for (const auto& [name, declaredKey] : declared)
            {
                found = found || declaredKey == key;
            }
            if (false == found)
            {
                std::cout << "  " << key << std::endl;
                Check(false, "a translated key that the header does not declare is dead");
            }
        }

        // ③ 선언한 상수는 어딘가에서 불려야 한다. **갈래 이름만 예외다** -
        // `EditorNames::ComponentCategoryLabel` 이 `component_category.<갈래>` 를
        // 그 자리에서 지으므로 상수 이름으로는 코드에 나타나지 않는다.
        std::string sources;
        for (const char* folder : {"Modules/JBroEditor", "Modules/JBroEditorHost"})
        {
            for (const fs::directory_entry& entry :
                fs::recursive_directory_iterator(folder, ignored))
            {
                const std::string extension = entry.path().extension().string();
                if (extension != ".cpp" && extension != ".h")
                {
                    continue;
                }
                if (entry.path().filename() == "LocalizationKeys.h")
                {
                    continue;
                }
                std::ifstream in(entry.path(), std::ios::binary);
                sources.append((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
            }
        }
        Check(sources.size() > 100000, "the editor sources must actually have been read");
        for (const auto& [name, key] : declared)
        {
            if (key.rfind("component_category.", 0) == 0)
            {
                continue;
            }
            if (sources.find(name) == std::string::npos)
            {
                std::cout << "  " << name << " (" << key << ")" << std::endl;
                Check(false, "a key nobody names is a translation nobody needs");
            }
        }
    }
}

int RunEditorLocalizationTests()
{
    TestTheTableFindsAndFallsBack();
    TestAFailedLoadLeavesTheOldTableStanding();
    TestTheShippedLocalesAgree();
    TestTheShippedLocalesAgreeOnFormats();
    TestEveryKeyIsDeclaredTranslatedAndUsed();
    std::cout << "Editor localization tests passed.\n";
    return 0;
}
