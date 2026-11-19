#include <JBro/Editor/EditorPaths.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>

namespace JBro::EditorPaths
{
    namespace
    {
        // 마지막 구분자의 자리. 없으면 `String::npos` 다.
        std::size_t LastSeparator(const String& path)
        {
            return path.find_last_of("/\\");
        }
    }

    String FolderOf(const char* path)
    {
        const String text(path != nullptr ? path : "");
        const std::size_t slash = LastSeparator(text);
        return slash == String::npos ? String() : String(text.substr(0, slash).c_str());
    }

    const char* LeafOfPath(const char* path)
    {
        if (path == nullptr)
        {
            return nullptr;
        }
        const char* leaf = path;
        for (const char* at = path; *at != '\0'; ++at)
        {
            if (*at == '/' || *at == '\\')
            {
                leaf = at + 1;
            }
        }
        return leaf;
    }

    String JoinPath(const char* root, const char* relative)
    {
        String left(root != nullptr ? root : "");
        const String right(relative != nullptr ? relative : "");
        if (left.empty())
        {
            return right;
        }
        if (right.empty())
        {
            return left;
        }
        // 끝의 구분자를 하나로 맞춘다. 둘 다 있으면 `//` 가 되어 파일이 열리지 않는다.
        const char last = left.c_str()[left.size() - 1];
        if (last != '/' && last != '\\')
        {
            left += "/";
        }
        const char* start = right.c_str();
        if (*start == '/' || *start == '\\')
        {
            ++start;
        }
        left += start;
        return left;
    }
    String Normalize(const String& path)
    {
        String source = path;
        for (auto& ch : source)
        {
            if (ch == '\\')
            {
                ch = '/';
            }
        }
        String prefix;
        std::size_t at = 0;
        if (source.size() > 1 && source[1] == ':')
        {
            prefix = source.substr(0, 2);
            if (prefix[0] >= 'a' && prefix[0] <= 'z')
            {
                prefix[0] = static_cast<char>(prefix[0] - 'a' + 'A');
            }
            at = 2;
        }
        const Bool rooted = at < source.size() && source[at] == '/';
        if (rooted)
        {
            prefix += '/';
            ++at;
            if (prefix == "/" && at < source.size() && source[at] == '/')
            {
                prefix += '/';
                ++at;
            }
        }
        Array<String> parts;
        while (at < source.size())
        {
            const auto slash = source.find('/', at);
            const auto end = slash == String::npos ? source.size() : slash;
            const String part = source.substr(at, end - at);
            if (part == ".." && !parts.IsEmpty() && parts[parts.Size() - 1] != "..")
            {
                parts.RemoveAt(parts.Size() - 1);
            }
            else if (!part.empty() && part != "." && (part != ".." || !rooted))
            {
                parts.Add(part);
            }
            at = end + 1;
        }
        String result = prefix;
        for (const auto& part : parts)
        {
            if (!result.empty() && result.back() != '/' && result.back() != ':')
            {
                result += '/';
            }
            result += part;
        }
        return result;
    }
}
