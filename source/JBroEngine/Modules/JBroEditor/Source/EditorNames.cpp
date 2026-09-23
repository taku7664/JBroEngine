#include <JBro/Editor/EditorNames.h>

#include <JBro/Editor/Localization.h>

#include <cstdio>
#include <cstring>

namespace JBro::EditorNames
{
    const char* DisplayTypeName(const char* typeName)
    {
        if (typeName == nullptr)
        {
            return nullptr;
        }
        const char* lastColon = std::strrchr(typeName, ':');
        if (lastColon != nullptr && *(lastColon + 1) != '\0')
        {
            return lastColon + 1;
        }
        return typeName;
    }

    const char* ComponentCategoryLabel(const char* category)
    {
        if (category == nullptr || category[0] == '\0')
        {
            return "";
        }
        // 키를 그 자리에서 짓는다. `Loc::TextOr` 는 키를 읽기만 하고, 돌려주는 것은
        // 표가 들고 있는 글자이거나 넘긴 갈래 이름이므로 이 버퍼보다 오래 산다.
        char key[64] = {};
        const int written = std::snprintf(key, sizeof(key), "component_category.%s", category);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(key))
        {
            return category;
        }
        return Loc::TextOr(key, category);
    }
}
