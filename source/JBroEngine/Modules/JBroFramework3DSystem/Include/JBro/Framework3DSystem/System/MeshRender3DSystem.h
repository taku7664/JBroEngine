#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro
{
    class MeshLibrary;
    class RenderWorld3D;
}

namespace JBro::System
{
    // 보이는 `MeshRenderer3D` 를 렌더 월드에 뜬다. 빈 `mesh` 핸들은 `meshId` 로 표에서 해석해 채운다.
    class MeshRender3DSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;
        void SetRenderWorld(RenderWorld3D* renderWorld);
        void SetMeshLibrary(const MeshLibrary* library);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        RenderWorld3D* m_renderWorld = nullptr;
        const MeshLibrary* m_library = nullptr;
    };
}
