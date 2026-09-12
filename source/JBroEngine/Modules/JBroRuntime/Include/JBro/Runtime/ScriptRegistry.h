#pragma once

#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace JBro
{
    // 스크립트 DLL 이 자기 타입을 호스트에 알리는 표다(H5, Open Decision 3).
    //
    // 기존 엔진은 `CreateScriptFunc` 가 캔버스를 받아 직접 컴포넌트를 붙였다. 여기서는
    // 그럴 수 없다 — `Canvas` 는 Tier E 이고 스크립트 DLL 의 include 경로에 없다(D-42).
    // 그래서 DLL 은 **저장을 하지 않는다**. "얼마나 크고, 어떻게 제자리 생성하고 부수는지"만
    // 알려 주고, 메모리는 호스트의 풀이 잡는다. 경계를 넘는 것은 POD 와 함수 포인터뿐이다.
    struct ScriptTypeInfo
    {
        NameId          name = InvalidNameId;
        ComponentTypeId typeId = InvalidComponentTypeId;
        std::uint32_t   size = 0;
        std::uint32_t   alignment = 0;
        // 호스트가 준 자리에 제자리 생성한다. storage 는 alignment 로 정렬돼 있고
        // 최소 size 바이트다. 실패하면 nullptr 을 돌려준다.
        GameScriptBase* (*Construct)(void* storage) noexcept = nullptr;
        // Construct 가 돌려준 것을 부순다. 메모리는 해제하지 않는다.
        void            (*Destruct)(GameScriptBase* script) noexcept = nullptr;
    };

    class ScriptRegistry final
    {
    public:
        ScriptRegistry() = default;
        ScriptRegistry(const ScriptRegistry&) = delete;
        ScriptRegistry& operator=(const ScriptRegistry&) = delete;

        // 이 모듈의 표. 호스트와 게임 DLL 은 Runtime 을 각각 정적 링크하므로 사본이 둘이다.
        static ScriptRegistry& Local();
        // 실제로 쓰는 표. 바인딩된 것이 있으면 그것, 없으면 Local() 이다.
        // 바인딩하지 않으면 DLL 이 자기 사본에 등록하고 호스트는 아무것도 못 본다
        // (D-44, D-51 과 같은 함정).
        static ScriptRegistry& Get();
        // Main-thread only. 로드 시 1회만 부른다. nullptr 이면 Local() 로 되돌린다.
        static void Bind(ScriptRegistry* registry);

        // 같은 이름이 이미 있으면 거절한다. 조용히 덮으면 어느 DLL 의 타입인지 알 수 없다.
        bool Register(const ScriptTypeInfo& info);
        // 모듈이 내려갈 때 그 모듈이 등록한 타입을 전부 지운다. 남겨 두면
        // 사라진 코드의 함수 포인터를 들고 있게 된다.
        void Clear();

        const ScriptTypeInfo* Find(NameId name) const;
        const ScriptTypeInfo* Find(const char* name) const;
        std::size_t GetCount() const;

    private:
        Table<NameId, ScriptTypeInfo> m_types;
    };

    // 타입 하나를 표의 항목으로 만든다. 생성·파괴 함수가 이 번역 단위에서 만들어지므로
    // 그 코드는 DLL 안에 있고, 호스트는 그 주소만 부른다.
    template<typename T>
    ScriptTypeInfo MakeScriptTypeInfo()
    {
        static_assert(std::is_base_of_v<GameScriptBase, T>,
            "a registered script must derive from GameScriptBase");
        static_assert(std::is_default_constructible_v<T>,
            "a script is constructed in place on a pool slot and needs a default constructor");

        ScriptTypeInfo info;
        info.name = MakeNameId(T::StaticTypeName());
        info.typeId = MakeStableTypeId(T::StaticTypeName());
        info.size = static_cast<std::uint32_t>(sizeof(T));
        info.alignment = static_cast<std::uint32_t>(alignof(T));
        // 생성이 던지면 경계를 넘기지 않고 실패로 바꾼다. 예외는 DLL 경계를 넘지 않는다.
        info.Construct = [](void* storage) noexcept -> GameScriptBase*
        {
            try
            {
                return static_cast<GameScriptBase*>(::new (storage) T());
            }
            catch (...)
            {
                return nullptr;
            }
        };
        info.Destruct = [](GameScriptBase* script) noexcept
        {
            static_cast<T*>(script)->~T();
        };
        return info;
    }

    // 이름을 표에 넣어 둔다. 호스트가 그 이름으로 스크립트를 붙일 수 있으려면
    // 원문도 필요하다.
    template<typename T>
    bool RegisterScriptType()
    {
        NameTable::Get().Intern(T::StaticTypeName());
        return ScriptRegistry::Get().Register(MakeScriptTypeInfo<T>());
    }
}
