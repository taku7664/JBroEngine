#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 엔진 데이터 파일(`.jproject`, `.jcanvas`)이 쓰는 YAML 부분집합을 읽고 쓴다.
    //
    // `.jproject` 는 키를 아는 채로 곧장 구조체에 담을 수 있었지만 `.jcanvas` 는 그럴 수 없다.
    // **컴포넌트의 `Type:` 이 그 컴포넌트의 필드들보다 뒤에 나온다** — 무슨 타입인지 알기 전에
    // 필드를 이미 지나친 뒤다. 그래서 한 번 문서로 읽어 두고 그 위에서 본다.
    //
    // 읽는 부분집합은 다음이 전부다. 모르는 것을 만나면 추측하지 않고 줄 번호와 함께 실패한다.
    //
    //   Key: value          스칼라
    //   Key:                중첩 블록(맵이든 시퀀스든, 들여쓰기가 깊어지면 시작)
    //     Inner: value
    //   Key:                시퀀스
    //     - item
    //   Key: []             빈 시퀀스 — 다음 줄에 `[]` 만 오는 형태도 같다
    //   Key: {}             빈 맵    — 다음 줄에 `{}` 만 오는 형태도 같다
    //   - Key: value        맵을 담은 시퀀스 항목(첫 키가 대시와 같은 줄에 온다)
    //   -                   시퀀스를 담은 시퀀스 항목(대시만 있고 아래가 깊어진다)
    //     - item
    //   # 주석               줄 전체 주석
    //
    // 앵커, 플로우 맵·시퀀스(빈 것 제외), 여러 줄 스칼라, 탭 들여쓰기는 읽지 않는다.
    // 들여쓰기는 공백이며 깊이는 일정할 필요가 없되 같은 블록 안에서는 같아야 한다.
    enum class YamlKind : std::uint8_t
    {
        Scalar,
        Sequence,
        Map
    };

    struct YamlError
    {
        // 0 이면 파일 자체를 열지 못했거나 내용이 비었다는 뜻이다.
        std::size_t line = 0;
        String      message;
    };

    class YamlDocument final
    {
    public:
        static constexpr std::uint32_t InvalidNode = static_cast<std::uint32_t>(-1);

        YamlDocument() = default;
        YamlDocument(const YamlDocument&) = delete;
        YamlDocument& operator=(const YamlDocument&) = delete;

        // 실패하면 문서는 비워지고 error 가 채워진다. 반쯤 읽힌 문서를 남기지 않는다.
        bool Parse(const char* text, std::size_t length, YamlError& error);
        bool Load(const char* path, YamlError& error);
        void Clear();

        // 빈 문서면 InvalidNode 다.
        std::uint32_t GetRoot() const;
        YamlKind      GetKind(std::uint32_t node) const;

        // 스칼라의 원문이다. 따옴표는 이미 벗겨져 있다. 스칼라가 아니면 빈 문자열.
        const char* GetText(std::uint32_t node) const;

        // 시퀀스의 항목 수이거나 맵의 키 수다. 스칼라면 0.
        std::size_t GetCount(std::uint32_t node) const;

        // 시퀀스의 index 번째. 범위를 벗어나면 InvalidNode.
        std::uint32_t GetElement(std::uint32_t node, std::size_t index) const;

        // 맵의 index 번째 키와 값. **넣은 순서가 유지된다** — 파일을 다시 쓸 때
        // 줄 순서가 흔들리면 형상 관리에서 diff 가 무의미해진다.
        const char*   GetKey(std::uint32_t node, std::size_t index) const;
        std::uint32_t GetValue(std::uint32_t node, std::size_t index) const;

        // 맵에서 키로 찾는다. 없으면 InvalidNode.
        std::uint32_t Find(std::uint32_t node, const char* key) const;

        // 자주 쓰는 조합이다. 없거나 스칼라가 아니거나 값이 읽히지 않으면 false 이고
        // result 는 손대지 않는다.
        bool FindScalar(std::uint32_t node, const char* key, String& result) const;
        bool FindBool  (std::uint32_t node, const char* key, bool& result) const;
        bool FindFloat (std::uint32_t node, const char* key, float& result) const;
        bool FindInt   (std::uint32_t node, const char* key, std::int64_t& result) const;

        std::size_t GetNodeCount() const;

    private:
        struct Node
        {
            YamlKind                  kind = YamlKind::Scalar;
            String                    text;
            Array<String>             keys;      // Map 일 때만
            Array<std::uint32_t>      children;  // Sequence 의 항목 또는 Map 의 값
        };

        std::uint32_t AddNode(YamlKind kind);
        bool          IsValid(std::uint32_t node) const;

        Array<Node>   m_nodes;
        std::uint32_t m_root = InvalidNode;
    };

    // 위 부분집합으로 다시 쓴다. 들여쓰기는 두 칸이며 기존 엔진이 쓰는 모양과 같다.
    //
    // 트리를 만든 뒤 뱉지 않고 곧장 흘려 쓴다 — 쓰는 쪽은 구조를 이미 알고 있고,
    // 중간 트리를 두면 같은 구조를 두 번 적게 된다.
    class YamlWriter final
    {
    public:
        YamlWriter() = default;
        YamlWriter(const YamlWriter&) = delete;
        YamlWriter& operator=(const YamlWriter&) = delete;

        // 맵 항목
        void WriteString(const char* key, const char* value);
        void WriteBool  (const char* key, bool value);
        void WriteFloat (const char* key, float value);
        void WriteInt   (const char* key, std::int64_t value);

        // 시퀀스 항목(대시로 시작하는 줄)
        void WriteStringItem(const char* value);
        void WriteFloatItem (float value);
        void WriteIntItem   (std::int64_t value);

        // key 가 nullptr 이면 시퀀스 항목 자리에 여는 것이다(`- ` 다음에 온다).
        void BeginMap(const char* key);
        void EndMap();
        void BeginSequence(const char* key);
        void EndSequence();

        const String& GetText() const;
        bool Save(const char* path) const;

    private:
        void WriteIndent();
        void WriteKeyLine(const char* key, const char* value);

        String m_text;
        // 열려 있는 블록마다 한 칸. 그 블록이 시퀀스인지, 항목이 하나라도 있었는지 기억한다 —
        // 비어 있는 채로 닫히면 `[]` 나 `{}` 를 적어야 한다.
        struct Block
        {
            bool isSequence = false;
            bool wrote = false;
        };
        Array<Block> m_blocks;
        // 다음 줄을 `- ` 로 열어야 하는지. BeginMap(nullptr) 이 세운다.
        bool m_pendingDash = false;
    };

    // 부동소수를 파일에 적을 모양으로 바꾼다. 기존 엔진처럼 `1` 은 `1` 로, 나머지는
    // 되읽어서 같은 값이 나오는 가장 짧은 표기로 적는다. 로캘을 타지 않는다.
    String FormatFloat(float value);
}
