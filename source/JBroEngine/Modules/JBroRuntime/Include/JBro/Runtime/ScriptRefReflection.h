#pragma once

#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/NameTable.h>

#include <type_traits>

namespace JBro::Internal
{
    Bool ScriptRefToText(const InstanceRef& reference, ComponentTypeId typeId,
        char* buffer, std::size_t capacity, std::size_t& required) noexcept;
    Bool ScriptRefFromText(InstanceRef& reference, ComponentTypeId typeId,
        const char* text, std::size_t length) noexcept;
}

namespace JBro
{
    template<typename T>
    struct TypeDescriptorOf<Ref<T>>
    {
        static const TypeDescriptor& Get()
        {
            static_assert(std::is_base_of_v<GameScriptBase, T>,
                "only script refs have a script reference descriptor");
            static constexpr ComponentTypeId typeId = MakeStableTypeId(T::StaticTypeName());
            static const ValueCodec codec = [] {
                ValueCodec made;
                made.ToText = [](const void* value, char* buffer, std::size_t capacity,
                    std::size_t& required) noexcept -> Bool {
                    return Internal::ScriptRefToText(*static_cast<const Ref<T>*>(value),
                        typeId, buffer, capacity, required);
                };
                made.FromText = [](void* value, const char* text, std::size_t length) noexcept -> Bool {
                    return Internal::ScriptRefFromText(*static_cast<Ref<T>*>(value), typeId, text, length);
                };
                made.Equals = [](const void* left, const void* right) noexcept -> Bool {
                    return *static_cast<const Ref<T>*>(left) == *static_cast<const Ref<T>*>(right);
                };
                made.Assign = [](void* destination, const void* source) noexcept {
                    *static_cast<Ref<T>*>(destination) = *static_cast<const Ref<T>*>(source);
                };
                return made;
            }();
            static const RefTarget target = [] {
                RefTarget made;
                made.targetType = NameTable::Get().Intern(T::StaticTypeName());
                return made;
            }();
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.ScriptRef");
                made.size = static_cast<UInt32>(sizeof(Ref<T>));
                made.alignment = static_cast<UInt32>(alignof(Ref<T>));
                made.triviallyCopyable = true;
                made.refTarget = &target;
                made.codec = &codec;
                return made;
            }();
            return descriptor;
        }
    };
}
