#pragma once

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/NameTable.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace JBro
{
    // ---------------------------------------------------------------------
    // 이름은 손으로 쓰지 않는다
    // ---------------------------------------------------------------------
    //
    // 멤버 포인터를 템플릿 인자로 받으면 컴파일러 서명 문자열에 그 이름이 들어 있다.
    // 거기서 잘라 쓴다 — 문자열 리터럴을 따로 적지 않으므로 **필드 이름을 바꿨는데
    // 등록된 이름은 옛것인** 경로가 아예 없다.
    //
    // ⚠ 컴파일러 서명 형식에 기대는 코드다. 툴체인을 올리면 깨질 수 있으므로
    // 테스트가 static_assert 로 실제 이름 하나를 붙잡아 둔다.
    // MSVC 14.51 x64 실측: `... __cdecl JBro::Detail::FieldSignature<&Game::Player::Speed>(void)`
    namespace Detail
    {
        template <auto MemberPointer>
        constexpr std::string_view FieldSignature()
        {
            return __FUNCSIG__;
        }

        // 마지막 `>(` 가 템플릿 인자의 끝이고, 그 앞의 마지막 `::` 가 이름의 시작이다.
        constexpr std::string_view DeriveFieldName(std::string_view signature)
        {
            const std::size_t close = signature.rfind(">(");
            if (close == std::string_view::npos)
            {
                return {};
            }
            const std::size_t scope = signature.rfind("::", close);
            if (scope == std::string_view::npos)
            {
                return {};
            }
            return signature.substr(scope + 2, close - scope - 2);
        }

        // NameTable::Intern 은 널 종료 문자열을 받는데 서명에서 잘라낸 조각은 그렇지 않다.
        // 컴파일 타임에 한 번 복사해 둔다.
        template <std::size_t Length>
        struct FieldNameStorage
        {
            char data[Length + 1] {};

            constexpr explicit FieldNameStorage(std::string_view text)
            {
                for (std::size_t i = 0; i < Length; ++i)
                {
                    data[i] = text[i];
                }
            }
        };

        template <auto MemberPointer>
        struct FieldName
        {
            static constexpr std::string_view View = DeriveFieldName(FieldSignature<MemberPointer>());
            static_assert(View.empty() == false,
                "the compiler signature did not contain a member name - the toolchain format changed");

            static constexpr FieldNameStorage<View.size()> Storage { View };

            static constexpr const char* Text()
            {
                return Storage.data;
            }
        };

        // 멤버 포인터에서 소유 타입과 필드 타입을 되찾는다.
        template <auto MemberPointer>
        struct MemberPointerTraits;

        template <typename Owner, typename Field, Field Owner::* MemberPointer>
        struct MemberPointerTraits<MemberPointer>
        {
            using OwnerType = Owner;
            using FieldType = Field;
        };
    }

    // ---------------------------------------------------------------------
    // 어트리뷰트
    // ---------------------------------------------------------------------
    //
    // 전부 constexpr 값이고 `|` 로 겹친다. **오타는 컴파일 에러다** — 기존 엔진은
    // 알 수 없는 어트리뷰트를 로그 경고로 넘겨서, 오타 난 Range 하나가 조용히 사라졌다.
    struct FieldAttributes
    {
        const char* displayName = nullptr;
        const char* tooltip     = nullptr;
        const char* category    = nullptr;
        bool        hasRange    = false;
        float       rangeMin    = 0.0f;
        float       rangeMax    = 0.0f;
        bool        editable    = true;
        bool        serialize   = true;

        // 편집 메타데이터가 하나라도 있는지. 없으면 PropertyInfo::edit 를 nullptr 로 두어
        // 게임 빌드가 이 필드의 표시 이름·툴팁을 통째로 건너뛴다.
        constexpr bool HasEditInfo() const
        {
            return displayName != nullptr
                || tooltip     != nullptr
                || category    != nullptr
                || hasRange
                || editable == false;
        }
    };

    // 오른쪽이 이긴다. 같은 어트리뷰트를 두 번 쓰면 나중 것이 남는다.
    // editable 과 serialize 는 끄는 쪽만 있으므로 둘 중 하나라도 껐으면 꺼진다.
    constexpr FieldAttributes operator|(const FieldAttributes& left, const FieldAttributes& right)
    {
        FieldAttributes merged;
        merged.displayName = right.displayName != nullptr ? right.displayName : left.displayName;
        merged.tooltip     = right.tooltip     != nullptr ? right.tooltip     : left.tooltip;
        merged.category    = right.category    != nullptr ? right.category    : left.category;
        merged.hasRange    = left.hasRange || right.hasRange;
        merged.rangeMin    = right.hasRange ? right.rangeMin : left.rangeMin;
        merged.rangeMax    = right.hasRange ? right.rangeMax : left.rangeMax;
        merged.editable    = left.editable  && right.editable;
        merged.serialize   = left.serialize && right.serialize;
        return merged;
    }

    // JBRO_FIELD 안에서만 수식 없이 보인다(매크로가 함수 본문에 using 지시를 넣는다).
    // JBro 직속에 Name 이나 Range 같은 흔한 이름을 두면 프렐류드의 `using namespace JBro;`
    // 가 사용자 코드와 충돌한다.
    namespace Attribute
    {
        constexpr FieldAttributes Name(const char* text)
        {
            FieldAttributes attributes;
            attributes.displayName = text;
            return attributes;
        }

        constexpr FieldAttributes Tooltip(const char* text)
        {
            FieldAttributes attributes;
            attributes.tooltip = text;
            return attributes;
        }

        constexpr FieldAttributes Category(const char* text)
        {
            FieldAttributes attributes;
            attributes.category = text;
            return attributes;
        }

        constexpr FieldAttributes Range(float minimum, float maximum)
        {
            FieldAttributes attributes;
            attributes.hasRange = true;
            attributes.rangeMin = minimum;
            attributes.rangeMax = maximum;
            return attributes;
        }

        // 인스펙터에 보이되 고칠 수 없다.
        constexpr FieldAttributes ReadOnly()
        {
            FieldAttributes attributes;
            attributes.editable = false;
            return attributes;
        }

        // 인스펙터에는 보이되 저장 파일에는 쓰지 않는다(런타임 전용 값).
        constexpr FieldAttributes NoSerialize()
        {
            FieldAttributes attributes;
            attributes.serialize = false;
            return attributes;
        }
    }

    // ---------------------------------------------------------------------
    // 필드 하나가 컴파일 타임에 남기는 것
    // ---------------------------------------------------------------------
    //
    // PropertyInfo 와 따로인 이유: PropertyInfo::name 은 NameId 라 NameTable 을
    // 거쳐야 하고 그것은 실행 시간 일이다. 여기까지가 컴파일 타임이고,
    // GetPropertyTable 이 실행 시간에 옮겨 담는다.
    struct FieldEntry
    {
        const char* name = nullptr;
        const TypeDescriptor& (*GetType)() = nullptr;
        void*       (*Address)(void* owner) noexcept = nullptr;
        const void* (*ConstAddress)(const void* owner) noexcept = nullptr;
        FieldAttributes attributes;
    };

    template <auto MemberPointer>
    constexpr FieldEntry MakeFieldEntry(const FieldAttributes& attributes = {})
    {
        using Traits = Detail::MemberPointerTraits<MemberPointer>;
        using Owner  = typename Traits::OwnerType;
        using Field  = typename Traits::FieldType;

        FieldEntry entry;
        entry.name = Detail::FieldName<MemberPointer>::Text();
        entry.GetType = &TypeDescriptorOf<Field>::Get;
        // 오프셋이 아니라 접근자다. 가상 함수를 가진 파생 클래스도, private 멤버도 같은 길이다.
        entry.Address = [](void* owner) noexcept -> void*
        {
            return &(static_cast<Owner*>(owner)->*MemberPointer);
        };
        entry.ConstAddress = [](const void* owner) noexcept -> const void*
        {
            return &(static_cast<const Owner*>(owner)->*MemberPointer);
        };
        entry.attributes = attributes;
        return entry;
    }

    // ---------------------------------------------------------------------
    // 개수는 스스로 센다 — END 매크로가 없다
    // ---------------------------------------------------------------------
    template <std::size_t Index>
    struct FieldIndex {};

    namespace Detail
    {
        template <typename T, std::size_t Index>
        concept HasFieldAt = requires { T::JBroFieldAt(FieldIndex<Index>{}); };

        template <typename T, std::size_t Index = 0>
        constexpr std::size_t CountFields()
        {
            if constexpr (HasFieldAt<T, Index>)
            {
                return CountFields<T, Index + 1>();
            }
            else
            {
                return Index;
            }
        }

        // 인덱스는 __COUNTER__ 로 매긴다. 클래스 본문 안에서 누가 __COUNTER__ 를 한 번 더
        // 쓰면 번호에 구멍이 나고, 세는 쪽은 거기서 멈춘다 — **뒤의 필드가 조용히 사라진다.**
        // 그래서 멈춘 자리 뒤로 여덟 칸을 더 확인하고, 뭔가 있으면 시끄럽게 실패한다.
        template <typename T, std::size_t Start, std::size_t Probe = 1>
        constexpr bool HasGapAfter()
        {
            if constexpr (Probe > 8)
            {
                return false;
            }
            else if constexpr (HasFieldAt<T, Start + Probe>)
            {
                return true;
            }
            else
            {
                return HasGapAfter<T, Start, Probe + 1>();
            }
        }

        template <typename T, std::size_t Index>
        void FillField(PropertyInfo& info, PropertyEditInfo& edit)
        {
            constexpr FieldEntry entry = T::JBroFieldAt(FieldIndex<Index>{});

            info.name         = NameTable::Get().Intern(entry.name);
            info.type         = &entry.GetType();
            info.Address      = entry.Address;
            info.ConstAddress = entry.ConstAddress;
            info.serialize    = entry.attributes.serialize;

            if constexpr (entry.attributes.HasEditInfo())
            {
                edit.displayName = entry.attributes.displayName;
                edit.tooltip     = entry.attributes.tooltip;
                edit.category    = entry.attributes.category;
                edit.hasRange    = entry.attributes.hasRange;
                edit.rangeMin    = entry.attributes.rangeMin;
                edit.rangeMax    = entry.attributes.rangeMax;
                edit.editable    = entry.attributes.editable;
                info.edit        = &edit;
            }
        }

        template <typename T, std::size_t... Indices>
        void FillFields(PropertyInfo* properties, PropertyEditInfo* edits, std::index_sequence<Indices...>)
        {
            (FillField<T, Indices>(properties[Indices], edits[Indices]), ...);
        }
    }

    // 타입 하나의 프로퍼티 표. 처음 물어볼 때 한 번 만들고 그 뒤로 같은 것을 준다 —
    // PropertyInfo::type 처럼 이 표의 주소를 들고 다니는 곳이 있기 때문이다.
    //
    // **처음 부르기 전에 NameTable 이 바인딩돼 있어야 한다.** 스크립트 DLL 이 호스트의 표를
    // 받기 전에 이것을 부르면 이름이 DLL 쪽 사본에 들어가고, 호스트가 되찾지 못한다(D-44).
    template <typename T>
    const PropertyTable& GetPropertyTable()
    {
        constexpr std::size_t count = Detail::CountFields<T>();
        static_assert(false == Detail::HasGapAfter<T, count>(),
            "a gap in the field index means something else consumed __COUNTER__ inside the class body");

        static PropertyInfo     properties[count > 0 ? count : 1] {};
        static PropertyEditInfo edits     [count > 0 ? count : 1] {};
        static const PropertyTable table = []() -> PropertyTable
        {
            Detail::FillFields<T>(properties, edits, std::make_index_sequence<count>{});
            PropertyTable built;
            built.properties = count > 0 ? properties : nullptr;
            built.count = static_cast<std::uint32_t>(count);
            return built;
        }();
        return table;
    }
}

// 클래스 본문을 연다. 여기서 매긴 번호가 아래 JBRO_FIELD 들의 기준점이다.
// 이 매크로 다음 줄부터는 private 이다 — 매크로가 private: 으로 끝나기 때문이다.
#define JBRO_REFLECT_BODY(Type)                                                      \
    public:                                                                          \
        using JBroSelf = Type;                                                       \
        static constexpr std::size_t JBroFieldBase = __COUNTER__;                    \
    private:

// 필드를 선언하면서 동시에 등록한다. 기본값은 매크로 밖에 쓴다:
//
//     JBRO_FIELD(int, FieldRows, Range(4, 40) | Category("Field")) = 20;
//     JBRO_FIELD(float, Elapsed, NoSerialize()) = 0.0f;
//     JBRO_FIELD(float, Speed) = 1.0f;
//
// **이 매크로가 선언한 필드는 public 이다.** 등록 함수가 밖에서 불려야 하기 때문이고,
// 인스펙터에 나오는 값이 클래스 밖에서 안 보이는 것도 앞뒤가 맞지 않는다.
//
// 어트리뷰트 이름은 함수 본문 안의 using 지시로만 보인다. JBro 직속을 어지럽히지 않는다.
#define JBRO_FIELD(FieldType, FieldName, ...)                                        \
    public:                                                                          \
        static constexpr auto JBroFieldAt(                                           \
            ::JBro::FieldIndex<__COUNTER__ - JBroFieldBase - 1>)                     \
        {                                                                            \
            using namespace ::JBro::Attribute;                                       \
            return ::JBro::MakeFieldEntry<&JBroSelf::FieldName>(__VA_ARGS__);        \
        }                                                                            \
        FieldType FieldName
