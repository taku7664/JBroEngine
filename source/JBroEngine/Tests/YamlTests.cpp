#include <JBro/Core/Yaml.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    bool Parse(JBro::YamlDocument& document, const char* text)
    {
        JBro::YamlError error;
        const bool parsed = document.Parse(text, std::strlen(text), error);
        if (false == parsed)
        {
            std::cout << "  parse failed at line " << error.line
                << ": " << error.message.c_str() << '\n';
        }
        return parsed;
    }

    void TestAFlatMapReadsBack()
    {
        JBro::YamlDocument document;
        Check(Parse(document,
            "Version: 1\n"
            "# a comment is skipped\n"
            "Name: Test Actor\n"
            "Active: true\n"
            "Empty: \"\"\n"),
            "a flat map must parse");

        const std::uint32_t root = document.GetRoot();
        Check(document.GetKind(root) == JBro::YamlKind::Map, "the document is a map");
        Check(document.GetCount(root) == 4, "the comment must not become an entry");

        // 넣은 순서가 유지되어야 한다. 다시 쓸 때 줄 순서가 흔들리면 diff 가 무의미해진다.
        Check(std::strcmp(document.GetKey(root, 0), "Version") == 0, "keys keep their order");
        Check(std::strcmp(document.GetKey(root, 2), "Active") == 0, "keys keep their order");

        JBro::String name;
        Check(document.FindScalar(root, "Name", name), "a scalar must be findable");
        Check(name == "Test Actor", "a space inside an unquoted value must survive");

        bool active = false;
        Check(document.FindBool(root, "Active", active) && active, "a bool must read");

        JBro::String empty("not touched");
        Check(document.FindScalar(root, "Empty", empty), "an empty quoted value is still a value");
        Check(empty.empty(), "the quotes must come off");

        Check(document.Find(root, "Missing") == JBro::YamlDocument::InvalidNode,
            "a key that is not there must come back as nothing");
    }

    void TestASequenceOfMapsKeepsItsShape()
    {
        JBro::YamlDocument document;
        Check(Parse(document,
            "Objects:\n"
            "  - Name: First\n"
            "    ParentIndex: -1\n"
            "  - Name: Second\n"
            "    ParentIndex: 0\n"),
            "a sequence of maps must parse");

        const std::uint32_t objects = document.Find(document.GetRoot(), "Objects");
        Check(document.GetKind(objects) == JBro::YamlKind::Sequence, "Objects is a sequence");
        Check(document.GetCount(objects) == 2, "both entries must be there");

        const std::uint32_t second = document.GetElement(objects, 1);
        Check(document.GetKind(second) == JBro::YamlKind::Map, "each entry is a map");
        JBro::String name;
        Check(document.FindScalar(second, "Name", name) && name == "Second",
            "the key that shares the dash line belongs to that entry");

        std::int64_t parent = 99;
        Check(document.FindInt(second, "ParentIndex", parent) && parent == 0,
            "the keys after the dash line belong to the same entry");

        const std::uint32_t first = document.GetElement(objects, 0);
        Check(document.FindInt(first, "ParentIndex", parent) && parent == -1,
            "a negative number must read");
    }

    void TestTheTypeCanBeReadAfterTheFields()
    {
        // 이것이 문서 트리가 필요한 이유다. `Type:` 이 그 컴포넌트의 필드들보다 뒤에 온다.
        JBro::YamlDocument document;
        Check(Parse(document,
            "Components:\n"
            "  - Radius: 0.5\n"
            "    Segments: 64\n"
            "    Type: Circle2D\n"
            "    IsEnabled: true\n"),
            "a component entry must parse");

        const std::uint32_t component =
            document.GetElement(document.Find(document.GetRoot(), "Components"), 0);
        JBro::String type;
        Check(document.FindScalar(component, "Type", type) && type == "Circle2D",
            "the type must be readable even though it comes last");
        float radius = 0.0f;
        Check(document.FindFloat(component, "Radius", radius) && radius == 0.5f,
            "the fields before the type must still be there");
    }

    void TestEmptyContainersAreNotGuessed()
    {
        JBro::YamlDocument document;
        Check(Parse(document,
            "Components:\n"
            "  []\n"
            "Fields:\n"
            "  {}\n"
            "Inline: []\n"
            "AfterAll: 1\n"),
            "empty containers must parse");

        const std::uint32_t root = document.GetRoot();
        const std::uint32_t components = document.Find(root, "Components");
        Check(document.GetKind(components) == JBro::YamlKind::Sequence,
            "an empty sequence must still say it is a sequence");
        Check(document.GetCount(components) == 0, "and that it holds nothing");

        Check(document.GetKind(document.Find(root, "Fields")) == JBro::YamlKind::Map,
            "an empty map must not be mistaken for an empty sequence");
        Check(document.GetKind(document.Find(root, "Inline")) == JBro::YamlKind::Sequence,
            "the one line spelling must mean the same thing");

        // 빈 것 다음 줄이 다시 바깥으로 나오는지 본다. 깊이를 잘못 닫으면 여기서 드러난다.
        std::int64_t after = 0;
        Check(document.FindInt(root, "AfterAll", after) && after == 1,
            "the key after an empty container must belong to the outer map");
    }

    void TestNestingGoesDeepAndComesBack()
    {
        JBro::YamlDocument document;
        Check(Parse(document,
            "Objects:\n"
            "  - Name: A\n"
            "    Transform2D:\n"
            "      Position:\n"
            "        - 1.5\n"
            "        - -2.25\n"
            "      RotationRadians: 0\n"
            "    LayerIndex: 3\n"
            "Version: 7\n"),
            "four levels of nesting must parse");

        const std::uint32_t root = document.GetRoot();
        const std::uint32_t object = document.GetElement(document.Find(root, "Objects"), 0);
        const std::uint32_t transform = document.Find(object, "Transform2D");
        const std::uint32_t position = document.Find(transform, "Position");
        Check(document.GetCount(position) == 2, "the position must hold two numbers");
        Check(std::strcmp(document.GetText(document.GetElement(position, 1)), "-2.25") == 0,
            "a negative number in a sequence must not be read as a dash");

        // 깊은 곳에서 돌아 나오는 길이 맞는지 본다.
        float rotation = 1.0f;
        Check(document.FindFloat(transform, "RotationRadians", rotation) && rotation == 0.0f,
            "a key after a nested sequence must belong to the transform");
        std::int64_t layer = 0;
        Check(document.FindInt(object, "LayerIndex", layer) && layer == 3,
            "a key two levels back must belong to the object");
        std::int64_t version = 0;
        Check(document.FindInt(root, "Version", version) && version == 7,
            "a key all the way back must belong to the document");
    }

    void TestAColonInsideAValueIsNotAKey()
    {
        // 경로와 시각은 콜론을 품는다. 콜론만 보고 자르면 값이 반토막 난다.
        JBro::YamlDocument document;
        Check(Parse(document,
            "RootPath: C:/games/Test\n"
            "Text: a: b\n"
            "Ratio: 16:9\n"),
            "values with colons must parse");

        const std::uint32_t root = document.GetRoot();
        Check(document.GetCount(root) == 3, "each line is one key");

        JBro::String value;
        Check(document.FindScalar(root, "RootPath", value), "the path key must be found");
        Check(value == "C:/games/Test", "the drive letter must not be taken for a key");
        Check(document.FindScalar(root, "Ratio", value) && value == "16:9",
            "a colon with no space after it is part of the value");
        Check(document.FindScalar(root, "Text", value) && value == "a: b",
            "only the first colon that ends a key counts");

        // 키가 되려면 콜론 뒤에 공백이 오거나 줄이 끝나야 한다. 그냥 콜론이 있다고
        // 키로 삼으면 `16:9` 같은 줄이 조용히 키 하나가 된다.
        JBro::YamlError error;
        const char* bare = "Version: 1\n16:9\n";
        Check(false == document.Parse(bare, std::strlen(bare), error),
            "a colon with no space does not make a key");
        Check(error.line == 2, "the refusal must name the line");
    }

    void TestANumberIsNotADash()
    {
        // `- item` 은 항목이고 `-2.5` 는 아니다. 대시만 보고 자르면 음수가 항목이 된다.
        JBro::YamlDocument document;
        JBro::YamlError error;
        const char* bare = "Items:\n  -2.5\n";
        Check(false == document.Parse(bare, std::strlen(bare), error),
            "a line starting with a minus and no space is not a sequence entry");
        Check(error.line == 2, "the refusal must name the line");

        // 진짜 항목은 여전히 읽혀야 한다. 위 거절이 음수 자체를 막은 것이 아님을 본다.
        Check(Parse(document, "Items:\n  - -2.5\n"), "a negative entry must parse");
        const std::uint32_t items = document.Find(document.GetRoot(), "Items");
        Check(document.GetCount(items) == 1, "there is one entry");
        Check(std::strcmp(document.GetText(document.GetElement(items, 0)), "-2.5") == 0,
            "the entry keeps its sign");
    }

    void TestHalfANumberIsNotANumber()
    {
        JBro::YamlDocument document;
        Check(Parse(document,
            "Good: 12\n"
            "Trailing: 12abc\n"
            "Spaced: 1 2\n"
            "Words: none\n"),
            "the document itself is fine; the values are the question");

        const std::uint32_t root = document.GetRoot();
        float number = -1.0f;
        Check(document.FindFloat(root, "Good", number) && number == 12.0f, "a number reads");

        // 앞부분만 읽고 넘어가면 `12abc` 가 12 가 된다. 저장 파일이 조용히 달라진다.
        number = -1.0f;
        Check(false == document.FindFloat(root, "Trailing", number),
            "text after a number must fail rather than parse a prefix");
        Check(number == -1.0f, "a failed read must not touch the result");
        Check(false == document.FindFloat(root, "Spaced", number), "two numbers are not one");
        Check(false == document.FindFloat(root, "Words", number), "a word is not a number");

        std::int64_t whole = -1;
        Check(false == document.FindInt(root, "Trailing", whole),
            "the same must hold for whole numbers");
        Check(whole == -1, "a failed read must not touch the result");

        bool flag = true;
        Check(false == document.FindBool(root, "Words", flag), "only true and false are bools");
        Check(false == document.FindBool(root, "Good", flag), "a digit is not a bool");
        Check(flag, "a failed read must not touch the result");
    }

    void TestBadInputIsRefusedWithALineNumber()
    {
        JBro::YamlDocument document;
        JBro::YamlError error;

        const char* tabbed = "Version: 1\n\tName: x\n";
        Check(false == document.Parse(tabbed, std::strlen(tabbed), error),
            "a tab must not be taken for indentation");
        Check(error.line == 2, "the refusal must name the line");

        const char* unclosed = "Name: \"not closed\n";
        Check(false == document.Parse(unclosed, std::strlen(unclosed), error),
            "an unclosed quote must be refused rather than guessed");

        const char* bare = "Version: 1\nthis line has no colon\n";
        Check(false == document.Parse(bare, std::strlen(bare), error),
            "a line that is neither a key nor an entry must be refused");
        Check(error.line == 2, "the refusal must name the line");

        Check(false == document.Parse("", 0, error), "an empty document must be refused");
        Check(document.GetRoot() == JBro::YamlDocument::InvalidNode,
            "a refused parse must leave nothing behind");
    }

    void TestTheWriterProducesWhatTheReaderReads()
    {
        JBro::YamlWriter writer;
        writer.WriteInt("Version", 1);
        writer.BeginSequence("Objects");
        {
            writer.BeginMap(nullptr);
            writer.WriteString("Name", "First");
            writer.BeginMap("Transform2D");
            {
                writer.BeginSequence("Position");
                writer.WriteFloatItem(1.5f);
                writer.WriteFloatItem(-2.25f);
                writer.EndSequence();
                writer.WriteFloat("RotationRadians", 0.0f);
            }
            writer.EndMap();
            writer.BeginSequence("Components");
            writer.EndSequence();
            writer.WriteInt("ParentIndex", -1);
            writer.EndMap();
        }
        writer.EndSequence();
        writer.WriteString("Empty", "");

        JBro::YamlDocument document;
        JBro::YamlError error;
        if (false == document.Parse(writer.GetText().c_str(), writer.GetText().size(), error))
        {
            std::cout << "written text:\n" << writer.GetText().c_str() << '\n';
            std::cout << "  parse failed at line " << error.line
                << ": " << error.message.c_str() << '\n';
            Check(false, "what the writer produced must read back");
        }

        const std::uint32_t root = document.GetRoot();
        const std::uint32_t object = document.GetElement(document.Find(root, "Objects"), 0);
        JBro::String name;
        Check(document.FindScalar(object, "Name", name) && name == "First",
            "a value must survive the round trip");

        const std::uint32_t position =
            document.Find(document.Find(object, "Transform2D"), "Position");
        Check(document.GetCount(position) == 2, "a written sequence must read back");
        Check(std::strcmp(document.GetText(document.GetElement(position, 1)), "-2.25") == 0,
            "a float must come back as the same text");

        Check(document.GetKind(document.Find(object, "Components")) == JBro::YamlKind::Sequence,
            "an empty sequence must be written so it reads back as one");
        std::int64_t parent = 0;
        Check(document.FindInt(object, "ParentIndex", parent) && parent == -1,
            "the key after an empty sequence must stay on the object");

        JBro::String empty("not touched");
        Check(document.FindScalar(root, "Empty", empty) && empty.empty(),
            "an empty string must be written so it is still a value");
    }

    void TestTheWrittenShapeMatchesTheLegacyFile()
    {
        // 되읽히는 것만으로는 부족하다. 기존 엔진이 쓰는 모양과 같아야 형상 관리에서
        // diff 가 읽히고, 같은 씬을 두 엔진이 번갈아 저장해도 파일이 출렁이지 않는다.
        // 아래 모양은 NewScene.jcanvas 에서 그대로 옮긴 것이다.
        JBro::YamlWriter writer;
        writer.BeginSequence("Objects");
        writer.BeginMap(nullptr);
        writer.WriteString("Name", "GameObject");
        writer.BeginMap("Transform2D");
        writer.BeginSequence("Position");
        writer.WriteFloatItem(4.5f);
        writer.WriteFloatItem(4.0f);
        writer.EndSequence();
        writer.WriteFloat("RotationRadians", 0.0f);
        writer.EndMap();
        writer.BeginSequence("Components");
        writer.EndSequence();
        writer.WriteInt("ParentIndex", -1);
        writer.EndMap();
        writer.EndSequence();

        const char* const expected =
            "Objects:\n"
            "  - Name: GameObject\n"
            "    Transform2D:\n"
            "      Position:\n"
            "        - 4.5\n"
            "        - 4\n"
            "      RotationRadians: 0\n"
            "    Components:\n"
            "      []\n"
            "    ParentIndex: -1\n";
        if (writer.GetText() != expected)
        {
            std::cout << "written:\n" << writer.GetText().c_str()
                << "expected:\n" << expected;
            Check(false, "the written shape must match the file the old engine writes");
        }
    }

    void TestFloatsAreWrittenSoTheyReadBackTheSame()
    {
        // 로캘이 소수점을 쉼표로 바꾸면 파일이 갈린다. to_chars 는 그것을 타지 않는다.
        Check(JBro::FormatFloat(1.0f) == "1", "a whole number must not grow a tail");
        Check(JBro::FormatFloat(0.5f) == "0.5", "a half must be written plainly");
        Check(JBro::FormatFloat(-2.25f) == "-2.25", "a negative must keep its sign");

        const float awkward = 4.703572f;
        JBro::YamlDocument document;
        JBro::String text("Value: ");
        text.append(JBro::FormatFloat(awkward));
        text.append("\n");
        JBro::YamlError error;
        Check(document.Parse(text.c_str(), text.size(), error), "the written float must parse");
        float read = 0.0f;
        Check(document.FindFloat(document.GetRoot(), "Value", read), "it must read as a number");
        Check(read == awkward, "a float must come back bit for bit, or scenes drift every save");
    }

    // 손으로 옮겨 적은 표본은 진짜 파일이 아니다. `.jproject` 때 그 차이가 버그 둘을 잡았다.
    // 이 기계의 사용자 폴더 이름에 한글이 들어 있다. 환경 변수는 와이드로 받아 UTF-8 로 바꾼다 - 플랫폼의 경로는
    // UTF-8 이고(D-112), 좁은 `USERPROFILE` 은 ANSI 라 그대로 넘기면 없는 파일이 된다.
    bool UserProfileUtf8(JBro::String& out)
    {
        wchar_t* profile = nullptr;
        std::size_t length = 0;
        if (_wdupenv_s(&profile, &length, L"USERPROFILE") != 0 || profile == nullptr)
        {
            return false;
        }
        const std::u8string text = std::filesystem::path(profile).generic_u8string();
        std::free(profile);
        out = JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
        return true;
    }

    void TestARealLegacyCanvasParses()
    {
        JBro::String root;
        if (false == UserProfileUtf8(root))
        {
            std::cout << "  [skip] no USERPROFILE; legacy canvas not read" << std::endl;
            return;
        }
        root.append("/source/repos/JBroEngine/TestProject/Test/Contents/Assets/");

        const char* const scenes[] =
        {
            "NewScene.jcanvas",
            "LayerBlendTest.jcanvas",
            "Scenes/Tetris.jcanvas",
            "SplitScreenTest.jcanvas",
            "TransitionTest/CanvasA.jcanvas",
        };

        // 파일은 플랫폼이 읽는다(D-112). `YamlDocument` 에는 경로 API 가 없다.
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        std::size_t read = 0;
        for (const char* scene : scenes)
        {
            JBro::String path(root);
            path.append(scene);
            JBro::Array<std::byte> bytes;
            if (false == platform.ReadWholeFile(path.c_str(), bytes))
            {
                continue;
            }
            JBro::YamlDocument document;
            JBro::YamlError error;
            if (false == document.Parse(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), error))
            {
                std::cout << "  " << scene << " failed at line " << error.line
                    << ": " << error.message.c_str() << std::endl;
                Check(false, "a real legacy canvas must parse");
            }
            ++read;

            // 실제로 내용을 찾아본다. 파싱만 통과하고 구조가 무너진 경우를 거른다.
            const std::uint32_t document_root = document.GetRoot();
            std::int64_t version = 0;
            Check(document.FindInt(document_root, "Version", version) && version >= 1,
                "a real canvas must carry a version");

            const std::uint32_t objects = document.Find(document_root, "Objects");
            Check(objects != JBro::YamlDocument::InvalidNode,
                "a real canvas must carry its objects");
            Check(document.GetKind(objects) == JBro::YamlKind::Sequence,
                "the objects must come out as a sequence");

            for (std::size_t i = 0; i < document.GetCount(objects); ++i)
            {
                const std::uint32_t object = document.GetElement(objects, i);
                JBro::String name;
                Check(document.FindScalar(object, "Name", name),
                    "every object in a real canvas must have a name");
                std::int64_t parent = 0;
                Check(document.FindInt(object, "ParentIndex", parent),
                    "every object in a real canvas must say where it hangs");
                Check(parent == -1 || static_cast<std::size_t>(parent) < i,
                    "a parent must come before its child, so one pass can rebuild the tree");
                Check(document.Find(object, "Components") != JBro::YamlDocument::InvalidNode,
                    "every object must carry a component list, even an empty one");
            }
        }

        if (read == 0)
        {
            std::cout << "  [skip] no legacy canvas on this machine" << std::endl;
            return;
        }
        std::cout << "  read " << read << " real legacy canvas files" << std::endl;
    }
}

int RunYamlTests()
{
    TestAFlatMapReadsBack();
    TestASequenceOfMapsKeepsItsShape();
    TestTheTypeCanBeReadAfterTheFields();
    TestEmptyContainersAreNotGuessed();
    TestNestingGoesDeepAndComesBack();
    TestAColonInsideAValueIsNotAKey();
    TestANumberIsNotADash();
    TestHalfANumberIsNotANumber();
    TestBadInputIsRefusedWithALineNumber();
    TestTheWriterProducesWhatTheReaderReads();
    TestTheWrittenShapeMatchesTheLegacyFile();
    TestFloatsAreWrittenSoTheyReadBackTheSame();
    TestARealLegacyCanvasParses();
    std::cout << "Yaml tests passed.\n";
    return 0;
}
