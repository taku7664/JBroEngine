#include <JBro/Canvas/PixelFields.h>

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // 타입 안의 `Float` 잎사귀를 모두 `factor` 배 한다. `Vector2` 는 x·y 로 내려간다.
        void ScaleFloats(const TypeDescriptor& type, void* address, Float factor)
        {
            if (&type == &TypeDescriptorOf<Float>::Get())
            {
                Float& value = *static_cast<Float*>(address);
                value = value * factor;
                return;
            }
            if (type.fields == nullptr)
            {
                return;
            }
            for (UInt32 index = 0; index < type.fields->count; ++index)
            {
                const PropertyInfo& property = type.fields->properties[index];
                void* inner = property.type != nullptr && property.Address != nullptr ? property.Address(address) : nullptr;
                if (inner != nullptr)
                {
                    ScaleFloats(*property.type, inner, factor);
                }
            }
        }
    }

    void ScalePixelFields(const PropertyTable& table, void* instance, Float factor)
    {
        if (instance == nullptr)
        {
            return;
        }
        for (UInt32 index = 0; index < table.count; ++index)
        {
            const PropertyInfo& property = table.properties[index];
            if (property.edit == nullptr || false == property.edit->pixels || property.type == nullptr || property.Address == nullptr)
            {
                continue;
            }
            void* address = property.Address(instance);
            if (address != nullptr)
            {
                ScaleFloats(*property.type, address, factor);
            }
        }
    }
}
