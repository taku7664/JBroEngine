#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 이름과 태그는 정수로 다닌다(D-51). 원문은 이 표 한 곳에만 있고,
    // 에디터·직렬화·로그가 되찾을 때만 문자열로 돌아간다.
    //
    // id 는 `MakeStableTypeId(text)` 라 텍스트만으로 구할 수 있다. 표를 거치지 않고
    // 비교할 수 있다는 뜻이고, 그래서 매 프레임 경로에서 문자열을 만들 일이 없다.
    using NameId = ComponentTypeId;
    inline constexpr NameId InvalidNameId = 0;

    // 텍스트 없이 id 만 구한다. 표를 건드리지 않는다.
    constexpr NameId MakeNameId(const char* text)
    {
        return text == nullptr ? InvalidNameId : MakeStableTypeId(text);
    }

    class NameTable final
    {
    public:
        NameTable() = default;
        NameTable(const NameTable&) = delete;
        NameTable& operator=(const NameTable&) = delete;

        // 이 모듈의 표. 호스트와 게임 DLL 은 Core 를 각각 정적 링크하므로 사본이 둘이다.
        static NameTable& Local();
        // 실제로 쓰는 표. 바인딩된 것이 있으면 그것, 없으면 Local() 이다.
        // 바인딩하지 않으면 호스트가 지은 이름을 DLL 이 되찾지 못한다(D-44 와 같은 함정).
        static NameTable& Get();
        // Main-thread only. 로드 시 1회만 부른다. nullptr 이면 Local() 로 되돌린다.
        static void Bind(NameTable* table);

        // 원문을 보관하고 id 를 돌려준다. 같은 텍스트는 언제나 같은 id 다.
        NameId Intern(const char* text);
        // 보관된 원문. 모르는 id 에는 빈 문자열을 돌려준다.
        const char* Resolve(NameId id) const;
        std::size_t GetCount() const;
        // 서로 다른 텍스트가 같은 id 로 접힌 횟수다. 0 이 아니면 이름 하나가 남의 원문을 되찾는다.
        std::size_t GetCollisionCount() const;
        void Clear();

    private:
        Table<NameId, String> m_texts;
        std::size_t           m_collisionCount = 0;
    };
}
