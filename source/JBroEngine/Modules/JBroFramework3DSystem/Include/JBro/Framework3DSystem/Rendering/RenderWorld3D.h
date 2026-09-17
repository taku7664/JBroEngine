#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Math3D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Color.h>

#include <cstddef>

namespace JBro
{
    class GameObject;

    // 이번 프레임에 뜬 주 카메라다. 행렬이 아니라 값이다 - 행렬은 브리지가 만든다(§2.2).
    struct RenderCamera3D
    {
        GameObject* owner = nullptr;
        Vec3 position;
        Quaternion rotation;
        Component::CameraProjection3D projection = Component::CameraProjection3D::Perspective;
        float verticalFieldOfView = 60.0f;
        float orthographicSize = 10.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        Color clearColor{0.08f, 0.09f, 0.11f, 1.0f};
    };

    struct MeshRenderItem
    {
        GameObject* owner = nullptr;
        Vec3 position;
        Quaternion rotation;
        Vec3 scale{1.0f, 1.0f, 1.0f};
        AssetHandle mesh;
        AssetHandle material;
        Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    // 2D 의 `RenderWorld2D` 와 같은 자리다. 시스템이 채우고 브리지가 렌더러에 넘긴다.
    // 정렬은 아직 없다 - 불투명 메시만 있고 깊이 버퍼가 순서를 대신한다.
    class RenderWorld3D
    {
    public:
        bool ReserveMeshes(std::size_t capacity);
        void BeginFrame();
        void SetCamera(const RenderCamera3D& camera);
        bool SubmitMesh(const MeshRenderItem& item);
        void EndFrame();

        const RenderCamera3D* GetCamera() const;
        std::size_t GetMeshCount() const;
        std::size_t GetMeshCapacity() const;
        std::size_t GetDroppedMeshCount() const;
        const MeshRenderItem& GetMesh(std::size_t index) const;

    private:
        RenderCamera3D m_camera;
        bool m_hasCamera = false;
        Array<MeshRenderItem> m_meshes;
        std::size_t m_droppedMeshCount = 0;
    };
}
