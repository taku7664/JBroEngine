#include <JBro/Core/Yaml.h>

#include <charconv>
#include <cstdio>
#include <cstring>

namespace JBro
{
    namespace
    {
        bool Fail(YamlError& error, std::size_t line, const char* message)
        {
            error.line = line;
            error.message = message;
            return false;
        }

        // 줄 하나를 뜯어 본다. 들여쓰기 칸수와 내용의 시작·끝을 돌려준다.
        struct Line
        {
            std::size_t indent = 0;
            const char* begin = nullptr;
            const char* end = nullptr;
            bool        hasTab = false;
        };

        Line SplitLine(const char* begin, const char* end)
        {
            Line line;
            const char* cursor = begin;
            while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
            {
                if (*cursor == '\t')
                {
                    line.hasTab = true;
                }
                ++cursor;
                ++line.indent;
            }
            line.begin = cursor;
            // 뒤쪽 공백과 캐리지 리턴을 떼어 낸다.
            const char* tail = end;
            while (tail > cursor && (tail[-1] == ' ' || tail[-1] == '\t' || tail[-1] == '\r'))
            {
                --tail;
            }
            line.end = tail;
            return line;
        }

        bool IsBlankOrComment(const Line& line)
        {
            return line.begin == line.end || *line.begin == '#';
        }

        // 따옴표를 벗긴다. 짝이 맞지 않으면 false.
        bool Unquote(String& value)
        {
            if (value.size() < 2)
            {
                return true;
            }
            const char first = value[0];
            if (first != '"' && first != '\'')
            {
                return true;
            }
            if (value[value.size() - 1] != first)
            {
                return false;
            }
            value = value.substr(1, value.size() - 2);
            return true;
        }

        // "Key: value" 에서 키와 값을 가른다. 콜론 뒤에 공백이 있거나 줄이 끝나야 키다.
        // 값 안의 콜론(`C:/path`)을 키 구분자로 착각하지 않기 위해서다.
        bool SplitKey(const char* begin, const char* end, String& key, String& value, bool& hasValue)
        {
            for (const char* cursor = begin; cursor < end; ++cursor)
            {
                if (*cursor != ':')
                {
                    continue;
                }
                const bool endsLine = (cursor + 1 == end);
                if (false == endsLine && cursor[1] != ' ')
                {
                    continue;
                }
                key.assign(begin, static_cast<std::size_t>(cursor - begin));
                if (endsLine)
                {
                    value.clear();
                    hasValue = false;
                    return true;
                }
                const char* valueBegin = cursor + 2;
                while (valueBegin < end && *valueBegin == ' ')
                {
                    ++valueBegin;
                }
                value.assign(valueBegin, static_cast<std::size_t>(end - valueBegin));
                hasValue = false == value.empty();
                return true;
            }
            return false;
        }
    }

    std::uint32_t YamlDocument::AddNode(YamlKind kind)
    {
        Node node;
        node.kind = kind;
        m_nodes.Add(std::move(node));
        return static_cast<std::uint32_t>(m_nodes.Size() - 1);
    }

    bool YamlDocument::IsValid(std::uint32_t node) const
    {
        return node != InvalidNode && node < m_nodes.Size();
    }

    void YamlDocument::Clear()
    {
        m_nodes.Clear();
        m_root = InvalidNode;
    }

    bool YamlDocument::Parse(const char* text, std::size_t length, YamlError& error)
    {
        Clear();
        error.line = 0;
        error.message.clear();
        if (text == nullptr)
        {
            return Fail(error, 0, "there is nothing to read");
        }

        // 열려 있는 블록의 스택이다. 각 칸은 그 블록의 내용이 놓인 들여쓰기 깊이를 들고 있다.
        struct Open
        {
            std::size_t   indent = 0;
            std::uint32_t node = InvalidNode;
        };
        Array<Open> open;

        // 대시 항목이 맵을 열었을 때, 그 맵의 키들이 놓일 깊이를 아직 모르는 상태를 표시한다.
        std::size_t lineNumber = 0;
        std::size_t cursor = 0;

        while (cursor <= length)
        {
            std::size_t lineEnd = cursor;
            while (lineEnd < length && text[lineEnd] != '\n')
            {
                ++lineEnd;
            }
            const Line line = SplitLine(text + cursor, text + lineEnd);
            ++lineNumber;
            cursor = lineEnd + 1;

            if (IsBlankOrComment(line))
            {
                if (lineEnd >= length)
                {
                    break;
                }
                continue;
            }
            if (line.hasTab)
            {
                return Fail(error, lineNumber, "indentation uses a tab");
            }

            // 이 줄보다 깊은 블록을 닫는다.
            while (open.Size() > 0 && line.indent < open[open.Size() - 1].indent)
            {
                open.Resize(open.Size() - 1);
            }

            const bool isItem = (*line.begin == '-')
                && (line.begin + 1 == line.end || line.begin[1] == ' ');

            // 빈 컨테이너 표기. 부모가 방금 열어 둔 블록의 실제 종류를 여기서 정한다.
            const bool isEmptySequence = (line.end - line.begin == 2)
                && line.begin[0] == '[' && line.begin[1] == ']';
            const bool isEmptyMap = (line.end - line.begin == 2)
                && line.begin[0] == '{' && line.begin[1] == '}';

            if (open.Size() == 0)
            {
                // 첫 내용 줄이 문서의 뿌리를 정한다.
                const std::uint32_t root = AddNode(isItem ? YamlKind::Sequence : YamlKind::Map);
                m_root = root;
                Open first;
                first.indent = line.indent;
                first.node = root;
                open.Add(first);
            }

            // 값으로 받는다. 아래에서 AddNode 가 배열을 다시 잡으면 참조가 죽는다.
            const std::uint32_t blockNode = open[open.Size() - 1].node;
            const std::size_t   blockIndent = open[open.Size() - 1].indent;

            if (isEmptySequence || isEmptyMap)
            {
                // 부모가 이 줄 하나를 위해 열린 블록이어야 한다. 내용이 이미 있으면 틀린 파일이다.
                if (m_nodes[blockNode].children.Size() > 0)
                {
                    return Fail(error, lineNumber, "an empty container cannot follow other entries");
                }
                m_nodes[blockNode].kind = isEmptySequence ? YamlKind::Sequence : YamlKind::Map;
                open.Resize(open.Size() - 1);
                if (lineEnd >= length)
                {
                    break;
                }
                continue;
            }

            if (isItem)
            {
                if (m_nodes[blockNode].kind == YamlKind::Map && m_nodes[blockNode].children.Size() > 0)
                {
                    return Fail(error, lineNumber, "a sequence entry cannot sit inside a map");
                }
                m_nodes[blockNode].kind = YamlKind::Sequence;

                const char* itemBegin = line.begin + 1;
                while (itemBegin < line.end && *itemBegin == ' ')
                {
                    ++itemBegin;
                }
                if (itemBegin == line.end)
                {
                    // 대시만 있다. 아래 줄들이 이 항목의 내용이다.
                    const std::uint32_t child = AddNode(YamlKind::Map);
                    m_nodes[blockNode].children.Add(child);
                    Open inner;
                    // 내용은 이 대시보다 깊은 어느 깊이에도 올 수 있다. 다음 줄이 정한다.
                    inner.indent = line.indent + 1;
                    inner.node = child;
                    open.Add(inner);
                    if (lineEnd >= length)
                    {
                        break;
                    }
                    continue;
                }

                String key;
                String value;
                bool hasValue = false;
                if (SplitKey(itemBegin, line.end, key, value, hasValue))
                {
                    // `- Key: value` — 맵을 담은 항목이고 첫 키가 여기 있다.
                    const std::uint32_t child = AddNode(YamlKind::Map);
                    m_nodes[blockNode].children.Add(child);
                    Open inner;
                    inner.indent = line.indent + static_cast<std::size_t>(itemBegin - line.begin);
                    inner.node = child;
                    open.Add(inner);

                    if (false == Unquote(value))
                    {
                        return Fail(error, lineNumber, "a quoted value is not closed");
                    }
                    if (hasValue)
                    {
                        const std::uint32_t scalar = AddNode(YamlKind::Scalar);
                        m_nodes[scalar].text = std::move(value);
                        m_nodes[child].keys.Add(std::move(key));
                        m_nodes[child].children.Add(scalar);
                    }
                    else
                    {
                        const std::uint32_t nested = AddNode(YamlKind::Map);
                        m_nodes[child].keys.Add(std::move(key));
                        m_nodes[child].children.Add(nested);
                        Open deeper;
                        deeper.indent = inner.indent + 1;
                        deeper.node = nested;
                        open.Add(deeper);
                    }
                    if (lineEnd >= length)
                    {
                        break;
                    }
                    continue;
                }

                // `- value` — 스칼라 항목이다.
                String item(itemBegin, static_cast<std::size_t>(line.end - itemBegin));
                if (false == Unquote(item))
                {
                    return Fail(error, lineNumber, "a quoted value is not closed");
                }
                const std::uint32_t scalar = AddNode(YamlKind::Scalar);
                m_nodes[scalar].text = std::move(item);
                m_nodes[blockNode].children.Add(scalar);
                if (lineEnd >= length)
                {
                    break;
                }
                continue;
            }

            // 키가 있는 줄이다.
            String key;
            String value;
            bool hasValue = false;
            if (false == SplitKey(line.begin, line.end, key, value, hasValue))
            {
                return Fail(error, lineNumber, "this line is neither a key nor a sequence entry");
            }
            if (m_nodes[blockNode].kind == YamlKind::Sequence && m_nodes[blockNode].children.Size() > 0)
            {
                return Fail(error, lineNumber, "a key cannot sit inside a sequence");
            }
            m_nodes[blockNode].kind = YamlKind::Map;
            // 대시가 열어 둔 블록은 깊이를 아직 모른다. 첫 줄이 그것을 정한다.
            if (line.indent > blockIndent)
            {
                open[open.Size() - 1].indent = line.indent;
            }

            if (false == Unquote(value))
            {
                return Fail(error, lineNumber, "a quoted value is not closed");
            }

            const std::uint32_t owner = open[open.Size() - 1].node;
            if (hasValue)
            {
                // `Key: []` 와 `Key: {}` 는 한 줄로 끝나는 빈 컨테이너다.
                std::uint32_t child = InvalidNode;
                if (value == "[]")
                {
                    child = AddNode(YamlKind::Sequence);
                }
                else if (value == "{}")
                {
                    child = AddNode(YamlKind::Map);
                }
                else
                {
                    child = AddNode(YamlKind::Scalar);
                    m_nodes[child].text = std::move(value);
                }
                m_nodes[owner].keys.Add(std::move(key));
                m_nodes[owner].children.Add(child);
            }
            else
            {
                // 값이 없다. 아래 줄들이 이 키의 내용이고, 맵인지 시퀀스인지는 그때 정해진다.
                const std::uint32_t child = AddNode(YamlKind::Map);
                m_nodes[owner].keys.Add(std::move(key));
                m_nodes[owner].children.Add(child);
                Open inner;
                inner.indent = line.indent + 1;
                inner.node = child;
                open.Add(inner);
            }

            if (lineEnd >= length)
            {
                break;
            }
        }

        if (m_root == InvalidNode)
        {
            return Fail(error, 0, "the document has no content");
        }
        return true;
    }

    bool YamlDocument::Load(const char* path, YamlError& error)
    {
        Clear();
        if (path == nullptr || path[0] == '\0')
        {
            return Fail(error, 0, "no path was given");
        }
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
        {
            return Fail(error, 0, "cannot open the file");
        }
        String text;
        char buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            text.append(buffer, read);
        }
        std::fclose(file);
        return Parse(text.c_str(), text.size(), error);
    }

    std::uint32_t YamlDocument::GetRoot() const
    {
        return m_root;
    }

    YamlKind YamlDocument::GetKind(std::uint32_t node) const
    {
        return IsValid(node) ? m_nodes[node].kind : YamlKind::Scalar;
    }

    const char* YamlDocument::GetText(std::uint32_t node) const
    {
        if (false == IsValid(node) || m_nodes[node].kind != YamlKind::Scalar)
        {
            return "";
        }
        return m_nodes[node].text.c_str();
    }

    std::size_t YamlDocument::GetCount(std::uint32_t node) const
    {
        if (false == IsValid(node) || m_nodes[node].kind == YamlKind::Scalar)
        {
            return 0;
        }
        return m_nodes[node].children.Size();
    }

    std::uint32_t YamlDocument::GetElement(std::uint32_t node, std::size_t index) const
    {
        if (false == IsValid(node) || m_nodes[node].kind != YamlKind::Sequence)
        {
            return InvalidNode;
        }
        if (index >= m_nodes[node].children.Size())
        {
            return InvalidNode;
        }
        return m_nodes[node].children[index];
    }

    const char* YamlDocument::GetKey(std::uint32_t node, std::size_t index) const
    {
        if (false == IsValid(node) || m_nodes[node].kind != YamlKind::Map)
        {
            return "";
        }
        if (index >= m_nodes[node].keys.Size())
        {
            return "";
        }
        return m_nodes[node].keys[index].c_str();
    }

    std::uint32_t YamlDocument::GetValue(std::uint32_t node, std::size_t index) const
    {
        if (false == IsValid(node) || m_nodes[node].kind != YamlKind::Map)
        {
            return InvalidNode;
        }
        if (index >= m_nodes[node].children.Size())
        {
            return InvalidNode;
        }
        return m_nodes[node].children[index];
    }

    std::uint32_t YamlDocument::Find(std::uint32_t node, const char* key) const
    {
        if (false == IsValid(node) || m_nodes[node].kind != YamlKind::Map || key == nullptr)
        {
            return InvalidNode;
        }
        const Node& map = m_nodes[node];
        for (std::size_t i = 0; i < map.keys.Size(); ++i)
        {
            if (map.keys[i] == key)
            {
                return map.children[i];
            }
        }
        return InvalidNode;
    }

    bool YamlDocument::FindScalar(std::uint32_t node, const char* key, String& result) const
    {
        const std::uint32_t found = Find(node, key);
        if (false == IsValid(found) || m_nodes[found].kind != YamlKind::Scalar)
        {
            return false;
        }
        result = m_nodes[found].text;
        return true;
    }

    bool YamlDocument::FindBool(std::uint32_t node, const char* key, bool& result) const
    {
        String text;
        if (false == FindScalar(node, key, text))
        {
            return false;
        }
        if (text == "true")
        {
            result = true;
            return true;
        }
        if (text == "false")
        {
            result = false;
            return true;
        }
        return false;
    }

    bool YamlDocument::FindFloat(std::uint32_t node, const char* key, float& result) const
    {
        String text;
        if (false == FindScalar(node, key, text))
        {
            return false;
        }
        float parsed = 0.0f;
        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const std::from_chars_result read = std::from_chars(begin, end, parsed);
        if (read.ec != std::errc{} || read.ptr != end)
        {
            return false;
        }
        result = parsed;
        return true;
    }

    bool YamlDocument::FindInt(std::uint32_t node, const char* key, std::int64_t& result) const
    {
        String text;
        if (false == FindScalar(node, key, text))
        {
            return false;
        }
        std::int64_t parsed = 0;
        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const std::from_chars_result read = std::from_chars(begin, end, parsed);
        if (read.ec != std::errc{} || read.ptr != end)
        {
            return false;
        }
        result = parsed;
        return true;
    }

    std::size_t YamlDocument::GetNodeCount() const
    {
        return m_nodes.Size();
    }

    // -----------------------------------------------------------------------

    String FormatFloat(float value)
    {
        // 로캘을 타지 않고, 되읽으면 같은 값이 나오는 가장 짧은 표기다.
        char buffer[64];
        const std::to_chars_result written =
            std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (written.ec != std::errc{})
        {
            return String("0");
        }
        return String(buffer, static_cast<std::size_t>(written.ptr - buffer));
    }

    void YamlWriter::WriteIndent()
    {
        for (std::size_t i = 0; i < m_blocks.Size(); ++i)
        {
            m_text.append("  ", 2);
        }
    }

    void YamlWriter::WriteKeyLine(const char* key, const char* value)
    {
        if (m_blocks.Size() > 0)
        {
            m_blocks[m_blocks.Size() - 1].wrote = true;
        }
        if (m_pendingDash)
        {
            // 대시는 이 블록이 아니라 그 바깥 깊이에 놓인다.
            m_pendingDash = false;
            for (std::size_t i = 0; i + 1 < m_blocks.Size(); ++i)
            {
                m_text.append("  ", 2);
            }
            m_text.append("- ", 2);
        }
        else
        {
            WriteIndent();
        }
        if (key != nullptr)
        {
            m_text.append(key, std::strlen(key));
            m_text.append(":", 1);
            if (value != nullptr)
            {
                m_text.append(" ", 1);
            }
        }
        if (value != nullptr)
        {
            m_text.append(value, std::strlen(value));
        }
        m_text.append("\n", 1);
    }

    void YamlWriter::WriteString(const char* key, const char* value)
    {
        const char* text = value != nullptr ? value : "";
        if (text[0] == '\0')
        {
            // 빈 문자열은 따옴표로 적는다. 그러지 않으면 값이 없는 키와 구별되지 않는다.
            WriteKeyLine(key, "\"\"");
            return;
        }
        WriteKeyLine(key, text);
    }

    void YamlWriter::WriteBool(const char* key, bool value)
    {
        WriteKeyLine(key, value ? "true" : "false");
    }

    void YamlWriter::WriteFloat(const char* key, float value)
    {
        WriteKeyLine(key, FormatFloat(value).c_str());
    }

    void YamlWriter::WriteInt(const char* key, std::int64_t value)
    {
        char buffer[32];
        const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
        const std::size_t length = written.ec == std::errc{}
            ? static_cast<std::size_t>(written.ptr - buffer) : 1;
        if (written.ec != std::errc{})
        {
            buffer[0] = '0';
        }
        String text(buffer, length);
        WriteKeyLine(key, text.c_str());
    }

    void YamlWriter::WriteStringItem(const char* value)
    {
        if (m_blocks.Size() > 0)
        {
            m_blocks[m_blocks.Size() - 1].wrote = true;
        }
        WriteIndent();
        m_text.append("- ", 2);
        const char* text = value != nullptr ? value : "";
        if (text[0] == '\0')
        {
            m_text.append("\"\"", 2);
        }
        else
        {
            m_text.append(text, std::strlen(text));
        }
        m_text.append("\n", 1);
    }

    void YamlWriter::WriteFloatItem(float value)
    {
        WriteStringItem(FormatFloat(value).c_str());
    }

    void YamlWriter::WriteIntItem(std::int64_t value)
    {
        char buffer[32];
        const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (written.ec != std::errc{})
        {
            WriteStringItem("0");
            return;
        }
        String text(buffer, static_cast<std::size_t>(written.ptr - buffer));
        WriteStringItem(text.c_str());
    }

    void YamlWriter::BeginMap(const char* key)
    {
        if (key != nullptr)
        {
            WriteKeyLine(key, nullptr);
        }
        else
        {
            if (m_blocks.Size() > 0)
            {
                m_blocks[m_blocks.Size() - 1].wrote = true;
            }
            m_pendingDash = true;
        }
        Block block;
        block.isSequence = false;
        m_blocks.Add(block);
    }

    void YamlWriter::BeginSequence(const char* key)
    {
        if (key == nullptr)
        {
            // **시퀀스 항목 자리에 시퀀스를 연다.** 대시만 있는 줄을 적고 원소는 한 칸
            // 더 깊이 적는다 - 기존 엔진 `.jcanvas` 가 `Array<Vector2>` 를 이렇게 적고,
            // 파서는 대시만 있는 줄 아래를 그 항목의 내용으로 읽는다.
            // 처음에는 아무것도 적지 않고 돌아왔는데, 그러면 뒤따르는 `EndSequence` 가
            // 부모 블록을 대신 닫아 문서가 어긋났다.
            if (m_blocks.Size() > 0)
            {
                m_blocks[m_blocks.Size() - 1].wrote = true;
            }
            WriteIndent();
            m_text.append("-\n", 2);
        }
        else
        {
            WriteKeyLine(key, nullptr);
        }
        Block block;
        block.isSequence = true;
        m_blocks.Add(block);
    }

    void YamlWriter::EndMap()
    {
        const bool wrote = m_blocks.Size() > 0 && m_blocks[m_blocks.Size() - 1].wrote;
        if (m_blocks.Size() > 0)
        {
            m_blocks.Resize(m_blocks.Size() - 1);
        }
        if (false == wrote)
        {
            // 비어 있는 맵은 표기가 있어야 한다. 아무것도 안 적으면 다음 키가
            // 이 맵의 내용인지 형제인지 파일만 보고는 알 수 없다.
            m_blocks.Add(Block{});
            WriteIndent();
            m_blocks.Resize(m_blocks.Size() - 1);
            m_text.append("{}\n", 3);
        }
    }

    void YamlWriter::EndSequence()
    {
        const bool wrote = m_blocks.Size() > 0 && m_blocks[m_blocks.Size() - 1].wrote;
        if (m_blocks.Size() > 0)
        {
            m_blocks.Resize(m_blocks.Size() - 1);
        }
        if (false == wrote)
        {
            m_blocks.Add(Block{});
            WriteIndent();
            m_blocks.Resize(m_blocks.Size() - 1);
            m_text.append("[]\n", 3);
        }
    }

    const String& YamlWriter::GetText() const
    {
        return m_text;
    }

    bool YamlWriter::Save(const char* path) const
    {
        if (path == nullptr || path[0] == '\0')
        {
            return false;
        }
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr)
        {
            return false;
        }
        const std::size_t written = m_text.empty()
            ? 0 : std::fwrite(m_text.c_str(), 1, m_text.size(), file);
        std::fclose(file);
        return written == m_text.size();
    }
}
