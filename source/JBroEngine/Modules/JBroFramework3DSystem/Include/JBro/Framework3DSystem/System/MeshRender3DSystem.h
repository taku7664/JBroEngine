#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class MaterialLibrary;
    class MeshLibrary;
    class RenderWorld3D;
}

namespace JBro::System
{
    // 보이는 `MeshRenderer3D` 를 렌더 월드에 뜬다. 빈 `mesh` 핸들은 `meshId` 로 표에서 해석해 채운다.
    class MeshRender3DSystem final : public GameSystem
    {
    public:
        Int32 GetExecutionOrder() const override;
        void SetRenderWorld(RenderWorld3D* renderWorld);
        void SetMeshLibrary(MeshLibrary* library);
        // 머티리얼 에셋 → 렌더러 머티리얼(D-348). 없으면 메시는 빌트인 셰이더로 그린다.
        void SetMaterialLibrary(MaterialLibrary* library);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
        MeshLibrary* m_library = nullptr;
        MaterialLibrary* m_materialLibrary = nullptr;
    };
}
