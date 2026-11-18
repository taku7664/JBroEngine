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
        // 라이브러리는 처음 만난 모델 메시를 올리므로 고칠 수 있는 것을 받는다(D-395).
        void SetMeshLibrary(MeshLibrary* library);
        // 재질을 푸는 곳이다(material-plan 3 단계). 없으면 모든 메시가 렌더러의 기본 재질이다.
        void SetMaterialLibrary(MaterialLibrary* library);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
        MeshLibrary* m_library = nullptr;
        MaterialLibrary* m_materials = nullptr;
    };
}
