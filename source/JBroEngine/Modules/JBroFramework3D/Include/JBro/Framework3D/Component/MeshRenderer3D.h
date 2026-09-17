#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework3D/Math3DReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
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

        JBRO_REFLECT_BODY(MeshRenderer3D)

        // `SpriteRenderer2D` 와 같은 분리다. `AssetId` 가 저장되는 쪽이고
        // `AssetHandle` 은 이번 실행에서의 자리라 저장하면 뜻이 없다.
        // 빈 핸들은 `MeshRender3DSystem` 이 `MeshLibrary` 에서 매 프레임 해석해 채운다
        // (framework3d-plan §2.3). `AssetSystem` 이 실제로 로드하게 되면 그쪽이 채운다.
        JBRO_FIELD(AssetId,     meshId);
        JBRO_FIELD(AssetHandle, mesh,       NoSerialize() | ReadOnly() | Tooltip("meshId 에서 해석된 값"));
        JBRO_FIELD(AssetId,     materialId);
        JBRO_FIELD(AssetHandle, material,   NoSerialize() | ReadOnly() | Tooltip("materialId 에서 해석된 값"));
        JBRO_FIELD(Color,       tint) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(bool,        visible) = true;
    };
}
