#pragma once

#include <JBro/Runtime/Nullable.h>
#include <JBro/Reflection/Field.h>

namespace JBro
{
    template<typename T>
    struct TypeDescriptorOf<Nullable<T>>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] = {
                MakeFieldEntry<&Nullable<T>::present>(),
                MakeFieldEntry<&Nullable<T>::value>()
            };
            static const StaticPropertyTable<2> fields{entries};
            static const TypeDescriptor descriptor = [] {
                const TypeDescriptor& inner = TypeDescriptorOf<T>::Get();
                String name = "JBro.Nullable<";
                const char* innerName = NameTable::Get().Resolve(inner.typeName);
                name += innerName != nullptr ? innerName : "?";
                name += ">";
                return MakeStructTypeDescriptor<Nullable<T>>(name.c_str(), fields.Get());
            }();
            return descriptor;
        }
    };
}
