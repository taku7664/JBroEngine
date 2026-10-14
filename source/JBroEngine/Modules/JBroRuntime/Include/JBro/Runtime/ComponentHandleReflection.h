#pragma once

#include <JBro/Core/StableTypeId.h>
#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro::Internal
{
    // **컴포넌트 핸들 필드의 글자다**(D-302). 오브젝트 참조 필드(`Handle::GameObject`, D-233)와 같은 두 문맥을 쓴다.
    //   캔버스 파일 안: `3:0` - 파일의 오브젝트 번호와 그 오브젝트에 붙은 같은 타입 중 몇 번째인지. 컴포넌트 번호는 실행마다 다르다.
    //   그 밖(되돌리기·클립보드·핫 리로드): `@123:456` - 이번 실행의 오브젝트 번호와 컴포넌트 번호.
    // 빈 글자는 빈 참조다. 파일에서 읽을 때 그 자리에 그 타입이 없으면 빈 참조로 읽는다(오브젝트 참조가 사라진 오브젝트를 비우는 것과 같다).
    // `@` 글자가 다른 타입의 컴포넌트를 가리키면 읽지 않는다 - 핸들의 멤버가 그 자리를 자기 데이터 클래스로 보기 때문이다.
    // 파일 글자를 읽는 순간 그 오브젝트에 컴포넌트가 붙어 있어야 한다. 캔버스 파일은 모두 붙인 뒤에 필드를 읽는다.
    Bool ComponentHandleToText(const ComponentHandle& handle, ComponentTypeId typeId,
        char* buffer, std::size_t capacity, std::size_t& required) noexcept;
    Bool ComponentHandleFromText(ComponentHandle& handle, ComponentTypeId typeId,
        const char* text, std::size_t length) noexcept;

    // 모든 컴포넌트 핸들 필드가 같은 타입 이름(`JBro.ComponentHandle`)이다. 무엇을 가리키는지는 `refTarget` 이 컴포넌트의 타입 이름으로 말한다 -
    // 에디터는 그것으로 고를 수 있는 컴포넌트를 거른다.
    template<typename THandle>
    const TypeDescriptor& GetComponentHandleTypeDescriptor()
    {
        static_assert(std::is_base_of_v<ComponentHandle, THandle> && sizeof(THandle) == sizeof(ComponentHandle),
            "only a builtin component handle is a component handle field");
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(THandle::StaticTypeName());
        static const ValueCodec codec = [] {
            ValueCodec made;
            made.ToText = [](const void* value, char* buffer, std::size_t capacity,
                std::size_t& required) noexcept -> Bool {
                return ComponentHandleToText(*static_cast<const THandle*>(value), TypeId, buffer, capacity, required);
            };
            made.FromText = [](void* value, const char* text, std::size_t length) noexcept -> Bool {
                return ComponentHandleFromText(*static_cast<THandle*>(value), TypeId, text, length);
            };
            made.Equals = [](const void* left, const void* right) noexcept -> Bool {
                return *static_cast<const THandle*>(left) == *static_cast<const THandle*>(right);
            };
            made.Assign = [](void* destination, const void* source) noexcept {
                *static_cast<THandle*>(destination) = *static_cast<const THandle*>(source);
            };
            return made;
        }();
        static const RefTarget target = [] {
            RefTarget made;
            made.targetType = NameTable::Get().Intern(THandle::StaticTypeName());
            return made;
        }();
        static const TypeDescriptor descriptor = [] {
            TypeDescriptor made;
            made.typeName = NameTable::Get().Intern("JBro.ComponentHandle");
            made.size = static_cast<JBro::UInt32>(sizeof(THandle));
            made.alignment = static_cast<JBro::UInt32>(alignof(THandle));
            made.triviallyCopyable = true;
            made.refTarget = &target;
            made.codec = &codec;
            return made;
        }();
        return descriptor;
    }
}

// 핸들 헤더 끝에서 한 번 부른다. 전역 범위에서 쓴다.
#define JBRO_DEFINE_COMPONENT_HANDLE_TYPE(HandleType)                                   \
    template <>                                                                         \
    struct JBro::TypeDescriptorOf<HandleType>                                           \
    {                                                                                   \
        static const JBro::TypeDescriptor& Get()                                        \
        {                                                                               \
            return JBro::Internal::GetComponentHandleTypeDescriptor<HandleType>();      \
        }                                                                               \
    }
