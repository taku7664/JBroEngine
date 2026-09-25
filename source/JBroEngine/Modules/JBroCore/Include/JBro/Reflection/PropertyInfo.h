#pragma once

#include <JBro/Reflection/TypeDescriptor.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 인스펙터만 읽는다. 런타임(직렬화)은 이것을 보지 않으므로 게임 빌드에서는
    // PropertyInfo::edit 를 nullptr 로 두어 한글 툴팁 문자열까지 통째로 뺄 수 있다.
    //
    // 문자열은 이 표를 등록한 모듈 안에 산다. 스크립트 DLL 이 등록한 것이라면
    // 그 DLL 이 내려갈 때 죽는다 — 호스트가 오래 들고 있어야 하면 NameTable 에 인턴한다.
    struct PropertyEditInfo
    {
        const char* displayName = nullptr;
        const char* tooltip     = nullptr;
        const char* category    = nullptr;
        bool        hasRange = false;
        float       rangeMin = 0.0f;
        float       rangeMax = 0.0f;
        bool        editable = true;
    };

    // 필드 하나의 명세다(D-56 계획서 §8.3).
    //
    // **접근 방법은 하나뿐이다.** 기존 엔진은 Offset(offsetof)과 GetFieldPtr(함수 포인터)를
    // 나란히 두고 주석에 "둘 중 하나만 사용" 이라고 적었다 — 어느 쪽이 유효한지 구조가 말해
    // 주지 않아 소비자마다 알아야 했다. 접근자 하나면 private 멤버도, 가상 함수를 가진
    // 파생 클래스도, 생성된 코드도 전부 같은 방식이다.
    //
    // 타입·크기는 여기 적지 않는다. type 이 가리키는 곳에만 있다.
    struct PropertyInfo
    {
        NameId name = InvalidNameId;
        const TypeDescriptor* type = nullptr;

        // 소유 객체의 주소를 받아 그 필드의 주소를 돌려준다.
        void*       (*Address)(void* owner) noexcept = nullptr;
        const void* (*ConstAddress)(const void* owner) noexcept = nullptr;

        // false 면 인스펙터에는 나오되 저장 파일에는 쓰지 않는다(런타임 전용 값).
        bool serialize = true;
        const PropertyEditInfo* edit = nullptr;
    };

    // 타입 하나가 가진 프로퍼티 목록이다. 배열은 이 표를 등록한 모듈 안에 있다.
    struct PropertyTable
    {
        const PropertyInfo* properties = nullptr;
        std::uint32_t       count = 0;
    };

    // 이 구조체들은 호스트와 게임 DLL 사이를 넘는다. POD 이고 함수 포인터만 담는다.
    // 크기가 바뀌면 양쪽이 다른 레이아웃을 읽게 되므로 여기서 멈춘다 —
    // ScriptModuleLoadContext 가 같은 이유로 같은 단언을 갖고 있다.
    // 아래 숫자는 짐작이 아니라 MSVC 14.51 x64 에서 재어 넣은 값이다.
    // 64 비트(포인터 8 바이트)에서만 잰다 - 웹(wasm32)은 포인터가 4 바이트이고 스크립트 DLL 경계가 없다(D-206).
    static_assert(sizeof(void*) != 8 || sizeof(PropertyEditInfo) == 40, "PropertyEditInfo crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(PropertyInfo)     == 48, "PropertyInfo crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(PropertyTable)    == 16, "PropertyTable crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(ValueCodec)       == 32, "ValueCodec crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(TypeDescriptor)   == 96, "TypeDescriptor crosses the script DLL boundary");
    // 48 에서 56 으로: 옮기기(`Move`)를 더했다(D-89).
    static_assert(sizeof(void*) != 8 || sizeof(ArrayOps)         == 56, "ArrayOps crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(TableOps)         == 112, "TableOps crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(EnumNames)        == 32, "EnumNames crosses the script DLL boundary");
    static_assert(sizeof(void*) != 8 || sizeof(RefTarget)        == 16, "RefTarget crosses the script DLL boundary");
}
