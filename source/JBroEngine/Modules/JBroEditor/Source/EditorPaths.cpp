#include <JBro/Editor/EditorPaths.h>

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
}
