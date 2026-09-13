#pragma once

#include <JBro/Types/NameTable.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // 값 하나를 글자로 바꾸고 되돌리는 방법이다(D-56 계획서 §8.5).
    //
    // 여기 있는 것은 **잎사귀 값**뿐이다. 배열·표는 직렬화기가 ArrayOps/TableOps 로 걸어
    // 내려가면서 원소마다 이 코덱을 부른다 — 컨테이너를 코덱이 알 필요가 없다.
    //
    // YAML 도 모른다. 코덱은 알맹이만 내놓고 따옴표·들여쓰기·줄바꿈은 직렬화기가 처리한다.
    // 기존 엔진에서 프로젝트 파서가 여러 줄 스칼라에서 두 번 죽었다 — YAML 을 아는 곳이
    // 여러 군데면 그런 것이 여기저기서 터진다. 한 곳만 알게 둔다.
    struct ValueCodec
    {
        // 글자로 쓴다. 버퍼가 모자라면 false 를 돌려주고 required 에 필요한 크기를 적는다.
        // 경계를 넘으므로 호출자가 버퍼를 소유한다 — 문자열을 돌려주지 않는다.
        bool (*ToText)(
            const void* value,
            char* buffer,
            std::size_t capacity,
            std::size_t& required) noexcept = nullptr;

        // 글자에서 읽는다. 못 읽으면 value 를 건드리지 않고 false 다.
        // 예외는 DLL 경계를 넘지 않는다.
        bool (*FromText)(void* value, const char* text, std::size_t length) noexcept = nullptr;

        // 같은 값인지 본다. 인스펙터의 "기본값으로 되돌리기" 와 undo 의 변경 판정이 쓴다.
        bool (*Equals)(const void* left, const void* right) noexcept = nullptr;

        // 복사한다. **memcpy 로 대신하지 않는다** — String 처럼 내용이 밖에 있는 타입은
        // 얕은 복사가 되어 먼저 죽는 쪽이 남은 쪽을 망가뜨린다.
        // 단순한 타입이면 내부에서 memcpy 하면 되고, 부르는 쪽은 고민할 것이 없다.
        void (*Assign)(void* destination, const void* source) noexcept = nullptr;
    };

    // enum 값의 이름표. 인스펙터 드롭다운과 이름으로 하는 직렬화가 쓴다.
    // 이름 배열은 이 표를 등록한 모듈 안에 산다 — 그 모듈이 내려가면 같이 죽는다.
    struct EnumNames
    {
        const char* const* names = nullptr;
        std::uint32_t      count = 0;
        // 현재 값 → 이름 배열 인덱스. 맞는 것이 없으면 -1.
        std::int32_t (*ToIndex)(const void* value) noexcept = nullptr;
        void         (*FromIndex)(void* value, std::int32_t index) noexcept = nullptr;
    };

    // Ref<T> 가 무엇을 가리키는지. 에디터의 드롭 대상 필터와 참조 수집이 쓴다.
    struct RefTarget
    {
        // 가리키는 대상 타입의 이름. 오브젝트·컴포넌트·스크립트·에셋 무엇이든 이름으로 건다.
        NameId targetType = InvalidNameId;
        // 에셋을 가리킬 때만 유효하다. 0 이면 제한 없음.
        std::uint32_t expectedAssetKind = 0;
    };

    // 배열의 타입소거 조작. 저장소 레이아웃을 직렬화기가 직접 캐스팅하지 않게 한다.
    struct ArrayOps
    {
        std::size_t (*GetSize)(const void* array) noexcept = nullptr;
        void*       (*GetElement)(void* array, std::size_t index) noexcept = nullptr;
        const void* (*GetConstElement)(const void* array, std::size_t index) noexcept = nullptr;
        bool        (*AddDefault)(void* array) noexcept = nullptr;
        bool        (*RemoveAt)(void* array, std::size_t index) noexcept = nullptr;
        void        (*Clear)(void* array) noexcept = nullptr;
    };

    // 표의 타입소거 조작. 배열과 달리 **인덱스로 원소를 지목할 수 없다** — open addressing 이라
    // 슬롯이 조밀하지 않고, 삽입 한 번에 리해시가 나면 기존 슬롯 번호가 전부 무효가 된다.
    // 그래서 순회는 불투명한 커서로 하고 수정은 **키**로 지목한다. (기존 엔진이 얻은 교훈)
    struct TableOps
    {
        static constexpr std::size_t InvalidSlot = static_cast<std::size_t>(-1);

        std::size_t (*GetSize)(const void* table) noexcept = nullptr;

        // 커서 계약: BeginSlot 이 첫 유효 슬롯을, NextSlot 이 그 다음을 준다.
        // 더 없으면 둘 다 InvalidSlot 이다. 순회 도중 삽입·삭제하면 커서는 무효다.
        std::size_t (*BeginSlot)(const void* table) noexcept = nullptr;
        std::size_t (*NextSlot)(const void* table, std::size_t slot) noexcept = nullptr;
        const void* (*GetKeyAt)(const void* table, std::size_t slot) noexcept = nullptr;
        void*       (*GetValueAt)(void* table, std::size_t slot) noexcept = nullptr;

        // 키는 Key 타입 객체의 주소다. 이미 있는 키면 InsertDefault 는 아무것도 하지 않는다.
        bool  (*ContainsKey)(const void* table, const void* key) noexcept = nullptr;
        bool  (*InsertDefault)(void* table, const void* key) noexcept = nullptr;
        bool  (*RemoveKey)(void* table, const void* key) noexcept = nullptr;
        // 슬롯 커서는 삽입 한 번에 무효가 되므로, 방금 넣은 자리를 다시 잡으려면 이쪽을 쓴다.
        void* (*FindValue)(void* table, const void* key) noexcept = nullptr;

        // 기본 생성한 키·값 하나를 만들고 없앤다. 위 함수들이 객체 주소를 받는데 호출부는
        // 타입이 소거돼 있어 스택에 만들 수 없다 — 역직렬화가 글자에서 키를 복원할 때 쓴다.
        // **할당이 일어난다.** 직렬화·에디터 전용이고 프레임 루프에서 쓰지 않는다.
        void* (*CreateKey)() noexcept = nullptr;
        void  (*DestroyKey)(void* key) noexcept = nullptr;
        void* (*CreateValue)() noexcept = nullptr;
        void  (*DestroyValue)(void* value) noexcept = nullptr;

        void  (*Clear)(void* table) noexcept = nullptr;
    };

    // 타입 하나의 실행 시간 명세다. 크기·정렬·복사 방식·컨테이너 조작이 전부 여기 있고,
    // PropertyInfo 는 이것을 가리키기만 한다 — **같은 사실을 두 군데 적지 않는다.**
    //
    // 닫힌 타입 enum 을 두지 않는 것이 요점이다. "배열인가?" 는 arrayOps 의 존재가 답하고,
    // "무슨 타입인가?" 는 typeName 이 답한다. 새 타입을 더해도 코어를 건드리지 않는다.
    // 기존 엔진은 18값 enum 을 두었고, 그 switch 가 직렬화기 한 파일에만 여섯 벌 있었다.
    struct TypeDescriptor
    {
        NameId        typeName  = InvalidNameId;   // "float", "JBro.Vec2", "Ref<Sprite>"
        std::uint32_t size      = 0;
        std::uint32_t alignment = 0;
        // 참고용이다. 복사는 언제나 codec->Assign 을 거친다.
        bool          triviallyCopyable = false;

        // 구조는 ops 의 존재로 드러난다. 별도 Kind 축을 두지 않는다.
        const ArrayOps* arrayOps = nullptr;   // != nullptr 이면 element 가 유효
        const TableOps* tableOps = nullptr;   // != nullptr 이면 key·value 가 유효
        const TypeDescriptor* element = nullptr;
        const TypeDescriptor* key     = nullptr;
        const TypeDescriptor* value   = nullptr;

        // 타입에 붙는 사실이다. 프로퍼티가 아니라 타입의 성질이라 여기 둔다.
        const EnumNames* enumNames = nullptr;   // enum 일 때만
        const RefTarget* refTarget = nullptr;   // Ref<T> 일 때만

        // 잎사귀 값의 글자 변환. 컨테이너 타입에는 없을 수 있다.
        const ValueCodec* codec = nullptr;
    };
}
