#pragma once

#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <cstddef>

namespace JBro
{
    // 타입 이름으로 그 타입의 프로퍼티 표를 찾는다. 인스펙터와 직렬화는 손에 든 것이
    // 무슨 타입인지만 알고 정의는 못 보므로, 이름이 유일한 손잡이다.
    //
    // **표는 둘이다**(D-56 계획서 §8.4). 수명이 다르기 때문이다 —
    //
    // | 표 | 수명 | 채우는 쪽 |
    // |---|---|---|
    // | 빌트인 컴포넌트 | 엔진 | `JBRO_FIELD` 매크로 (C++) |
    // | 스크립트 | 로드된 DLL | 트랜스파일러가 생성한 코드 |
    //
    // 기존 엔진은 `EReflectTypeKind { Component, Script }` 로 구분해 한 표에 넣었다.
    // 그러면 DLL 이 내려갈 때마다 남길 것과 지울 것을 골라내야 한다. 여기서는 통째로 비운다.
    //
    // **스크립트 표의 항목은 DLL 이 내려가면 전부 죽는다.** `PropertyInfo::Address` 는
    // 사라질 코드 안의 함수 포인터이고 `edit->tooltip` 은 사라질 데이터 안의 문자열이다.
    // `ScriptRegistry::Clear` 가 같은 이유로 존재한다.
    class PropertyRegistry final
    {
    public:
        PropertyRegistry() = default;
        PropertyRegistry(const PropertyRegistry&) = delete;
        PropertyRegistry& operator=(const PropertyRegistry&) = delete;

        // 빌트인 표. 프로세스에 하나이고 비우지 않는다.
        static PropertyRegistry& Builtin();

        // 이 모듈의 스크립트 표. 호스트와 게임 DLL 은 Core 를 각각 정적 링크하므로 사본이 둘이다.
        static PropertyRegistry& ScriptLocal();
        // 실제로 쓰는 스크립트 표. 바인딩된 것이 있으면 그것, 없으면 ScriptLocal() 이다.
        // 바인딩하지 않으면 DLL 이 자기 사본에 등록하고 호스트는 아무것도 못 본다
        // (D-44, ScriptRegistry 와 같은 함정).
        static PropertyRegistry& Script();
        // Main-thread only. 로드 시 1회만 부른다. nullptr 이면 ScriptLocal() 로 되돌린다.
        static void BindScript(PropertyRegistry* registry);

        // 빌트인 표에 넣는다.
        static bool RegisterBuiltin(NameId typeName, const PropertyTable& table);
        // 스크립트 표에 넣는다. **빌트인이 이미 가진 이름이면 거절한다** —
        // 통과시키면 Lookup 이 조용히 빌트인 쪽을 주고, 스크립트 작성자는 자기 필드가
        // 왜 안 보이는지 알 길이 없다.
        static bool RegisterScript(NameId typeName, const PropertyTable& table);

        // 빌트인 먼저, 그다음 스크립트. 엔진 타입명을 스크립트가 덮지 못한다.
        static const PropertyTable* Lookup(NameId typeName);
        static const PropertyTable* Lookup(const char* typeName);

        // 같은 이름이 이미 있으면 거절한다. 조용히 덮으면 어느 쪽 표인지 알 수 없다.
        bool Register(NameId typeName, const PropertyTable& table);
        // 모듈이 내려갈 때 그 모듈이 등록한 것을 전부 지운다.
        void Clear();

        const PropertyTable* Find(NameId typeName) const;
        const PropertyTable* Find(const char* typeName) const;
        std::size_t GetCount() const;

    private:
        Table<NameId, PropertyTable> m_tables;
    };

    // 빌트인 컴포넌트 하나를 등록한다. 이름은 그 타입이 이미 들고 있는 것을 쓴다 —
    // 여기서 문자열을 다시 적으면 둘이 갈라질 수 있다.
    //
    // 원문도 표에 넣는다. 에디터가 id 에서 이름을 되찾아야 하기 때문이다.
    template <typename T>
    bool RegisterBuiltinProperties()
    {
        const NameId name = NameTable::Get().Intern(T::StaticTypeName());
        return PropertyRegistry::RegisterBuiltin(name, GetPropertyTable<T>());
    }

    // 스크립트 타입 하나를 등록한다. DLL 안에서 불려야 한다 —
    // 표가 가리키는 함수와 문자열이 전부 그 DLL 안에 있다.
    template <typename T>
    bool RegisterScriptProperties()
    {
        const NameId name = NameTable::Get().Intern(T::StaticTypeName());
        return PropertyRegistry::RegisterScript(name, GetPropertyTable<T>());
    }
}
