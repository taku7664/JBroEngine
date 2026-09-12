#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Component
{
    class MeshRenderer3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::MeshRenderer3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        AssetHandle mesh;
        AssetHandle material;
    };
}
