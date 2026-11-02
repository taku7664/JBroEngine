#include <JBro/Runtime/MaterialService.h>

#include <JBro/Runtime/IMaterialSystem.h>
#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        System::IMaterialSystem* Materials()
        {
            return GetSystemContext().Material;
        }

        Bool Set(MaterialParameter parameter, const Float* values, UInt32 count, Bool integer)
        {
            System::IMaterialSystem* system = Materials();
            return system != nullptr && parameter.IsValid() && system->SetValues(parameter.value, values, count, integer);
        }
    }

    MaterialParameter MaterialService::FindParameter(Uuid material, const char* parameter) const
    {
        System::IMaterialSystem* system = Materials();
        if (system == nullptr || material.IsNull() || parameter == nullptr)
        {
            return {};
        }
        return {system->FindParameter(material, parameter)};
    }

    MaterialParameter MaterialService::FindParameter(const Internal::ComponentHandle& renderer, Uuid material, const char* parameter) const
    {
        System::IMaterialSystem* system = Materials();
        const InstanceId owner = Internal::ComponentHandleAccess::GetReference(renderer).ComponentId;
        if (system == nullptr || owner == InvalidInstanceId || material.IsNull() || parameter == nullptr)
        {
            return {};
        }
        return {system->FindObjectParameter(owner, material, parameter)};
    }

    Bool MaterialService::SetFloat(MaterialParameter parameter, Float value) const
    {
        return Set(parameter, &value, 1, false);
    }

    Bool MaterialService::SetInt(MaterialParameter parameter, Int32 value) const
    {
        const Float values[1] = {static_cast<JBro::Float>(value.Get())};
        return Set(parameter, values, 1, true);
    }

    Bool MaterialService::SetBool(MaterialParameter parameter, Bool value) const
    {
        const Float values[1] = {value ? 1.0f : 0.0f};
        return Set(parameter, values, 1, true);
    }

    Bool MaterialService::SetVector2(MaterialParameter parameter, Vector2 value) const
    {
        const Float values[2] = {value.x, value.y};
        return Set(parameter, values, 2, false);
    }

    Bool MaterialService::SetColor(MaterialParameter parameter, Color value) const
    {
        const Float values[4] = {value.R, value.G, value.B, value.A};
        return Set(parameter, values, 4, false);
    }

    Bool MaterialService::SetVector4(MaterialParameter parameter, Float x, Float y, Float z, Float w) const
    {
        const Float values[4] = {x, y, z, w};
        return Set(parameter, values, 4, false);
    }

    void MaterialService::Clear(MaterialParameter parameter) const
    {
        System::IMaterialSystem* system = Materials();
        if (system != nullptr && parameter.IsValid())
        {
            system->Clear(parameter.value);
        }
    }

    void MaterialService::ClearAll() const
    {
        if (System::IMaterialSystem* system = Materials())
        {
            system->ClearAll();
        }
    }
}
