#pragma once

#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

// 컴포넌트 밖에 사는 글자(D-200 (1), text-plan §3.1·§4.3).
//
// 컴포넌트 공개 필드에 `String` 을 두지 않는다(D-51) - 스크립트 DLL 이 대입하면 DLL 의 할당기로 잡은 메모리를 호스트가
// 푼다. 그래서 컴포넌트는 `TextId`(번호 + 세대, POD)만 들고 UTF-8 은 호스트의 `TextStore` 가 든다.
//
// `TextId` 는 **값처럼 행동한다.** 리플렉션 코덱이 파일과 인스펙터에는 글자로 적고(`AudioBusName` 과 같은 길), 복사
// (`Assign`)는 번호가 아니라 **글자를 옮긴다** - 받는 쪽이 칸이 있으면 그 칸에, 없으면 새 칸에. 그래서 복사·붙여넣기·
// 프리팹·되돌리기처럼 리플렉션으로 복사되는 경로에서 두 컴포넌트가 한 칸을 나눠 갖지 않는다. 칸은 그 컴포넌트가
// 떼일 때(`OnDetached`) 돌려준다. **C++ 복사는 번호를 복사한다** - 컴포넌트를 C++ 로 복사하는 길은 쓰지 않는다
// (엔진은 리플렉션으로만 복사한다, `TypeDescriptor::triviallyCopyable` 의 주석).
//
// 변경 감지는 칸의 판번호(`GetRevision`)다. 글자를 바꿀 때마다 오른다 - 시스템은 매 프레임 글자를 비교하지 않고
// 정수 둘을 비교한다(text-plan §1.2 의 4 번).
//
// **메인 스레드 전용이다.** 스크립트는 저장소를 직접 보지 않고 서비스(`Text2DService`)를 거친다 - 호스트의 저장소가 쓰인다.
namespace JBro
{
    struct TextId
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0; // 0 은 칸이 없다는 뜻이다(빈 글자)

        bool IsValid() const
        {
            return generation != 0;
        }
    };

    class TextStore final
    {
    public:
        static TextStore& Local();
        // 묶인 것이 있으면 그것, 없으면 이 모듈의 것이다(`ScriptRegistry` 와 같은 모양).
        static TextStore& Get();
        static void Bind(TextStore* store);

        // 새 칸을 만들어 글자를 넣는다.
        TextId Create(const char* utf8, std::size_t length);
        // 칸의 글자를 바꾼다. 칸이 없으면(무효·이미 돌려준 것) 거짓이다.
        bool Set(TextId id, const char* utf8, std::size_t length);
        // id 가 가리키는 칸이 살아 있으면 그 칸에, 아니면 새 칸에 쓰고 id 를 고친다. 코덱과 서비스가 쓰는 길이다.
        void Assign(TextId& id, const char* utf8, std::size_t length);
        void Destroy(TextId id);

        bool IsAlive(TextId id) const;
        // 칸의 글자다. 칸이 없으면 빈 글자다. 다음 쓰기까지 유효하다.
        ArrayView<const char> GetText(TextId id) const;
        // 칸의 판번호다. 쓸 때마다 오르고 1 부터다. 칸이 없으면 0 이다.
        std::uint32_t GetRevision(TextId id) const;
        std::uint32_t GetLiveCount() const;
        // 모든 칸을 버린다. 테스트와 프로젝트 닫기가 쓴다 - 컴포넌트가 들고 있는 번호는 그 뒤 무효가 된다.
        void Clear();

    private:
        struct Slot
        {
            String        text;
            std::uint32_t generation = 1;
            std::uint32_t revision = 0;
            bool          occupied = false;
        };

        const Slot* Find(TextId id) const;
        Slot* Find(TextId id);

        Array<Slot>          m_slots;
        Array<std::uint32_t> m_free;
        std::uint32_t        m_live = 0;
    };

    // 파일과 인스펙터에는 글자로, 복사는 글자째로(위 설명).
    const ValueCodec& GetTextIdCodec();

    template <>
    struct TypeDescriptorOf<TextId>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.TextId");
                made.size = static_cast<std::uint32_t>(sizeof(TextId));
                made.alignment = static_cast<std::uint32_t>(alignof(TextId));
                // 바이트를 옮기면 두 값이 한 칸을 나눠 갖는다. 복사는 코덱이 한다.
                made.triviallyCopyable = false;
                made.codec = &GetTextIdCodec();
                return made;
            }();
            return descriptor;
        }
    };
}
