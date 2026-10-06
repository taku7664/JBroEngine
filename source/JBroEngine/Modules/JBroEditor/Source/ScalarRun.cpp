#include <JBro/Editor/ScalarRun.h>

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Types/NameTable.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    namespace
    {
        Bool IsFloat(const TypeDescriptor& type)
        {
            const char* name = NameTable::Get().Resolve(type.typeName);
            return name != nullptr && std::strcmp(name, "float") == 0;
        }

        // `address` 가 비어 있으면 타입만 본다. 셈은 같고 주소만 모으지 않는다.
        Bool CollectInto(const TypeDescriptor& type, void* address, ScalarRun& run)
        {
            if (type.fields == nullptr)
            {
                return false;
            }
            for (UInt32 index = 0; index < type.fields->count; ++index)
            {
                const PropertyInfo& property = type.fields->properties[index];
                if (property.type == nullptr || property.Address == nullptr)
                {
                    return false;
                }
                void* field = nullptr;
                if (address != nullptr)
                {
                    field = property.Address(address);
                    if (field == nullptr)
                    {
                        return false;
                    }
                }
                if (property.type->fields != nullptr)
                {
                    // 한 단계 더 내려간다. `Rect` 는 `Vector2` 두 개이고, 기존 엔진은
                    // 그것도 한 줄에 그렸다.
                    // 안쪽에서 돌아온 뒤에도 둘 이상이어야 한다 - 인스펙터에 있던 때의
                    // 셈을 그대로 옮긴다.
                    if (false == CollectInto(*property.type, field, run) || run.count < 2)
                    {
                        return false;
                    }
                    continue;
                }
                if (false == IsFloat(*property.type))
                {
                    return false;
                }
                if (run.count >= ScalarRun::MaxCount)
                {
                    // 다섯 개부터는 한 줄에 넣어 봐야 읽을 수 없다. 타고 내려간다.
                    return false;
                }
                run.values[run.count] = static_cast<Float*>(field);
                ++run.count;
            }
            return true;
        }
    }

    Bool CollectScalarRun(const TypeDescriptor& type, void* address, ScalarRun& run)
    {
        run = ScalarRun{};
        return CollectInto(type, address, run) && run.count >= 2;
    }

    Bool IsScalarRunType(const TypeDescriptor& type)
    {
        ScalarRun run;
        return CollectInto(type, nullptr, run) && run.count >= 2;
    }

    Bool CollectNumbers(const TypeDescriptor& type, void* address, ScalarRun& run)
    {
        if (CollectScalarRun(type, address, run))
        {
            return true;
        }
        run = ScalarRun{};
        if (false == IsFloat(type))
        {
            return false;
        }
        run.values[0] = static_cast<Float*>(address);
        run.count = 1;
        return true;
    }
}
