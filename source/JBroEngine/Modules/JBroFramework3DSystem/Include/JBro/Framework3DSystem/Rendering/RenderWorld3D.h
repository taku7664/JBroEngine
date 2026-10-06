#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Math3D.h>

#include <cstddef>

namespace JBro
{
    class GameObject;

    // 이번 프레임에 뜬 주 카메라다. 행렬이 아니라 값이다 - 행렬은 브리지가 만든다(§2.2).
    struct RenderCamera3D
    {
        GameObject* owner = nullptr;
        Vector3 position;
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
        Vector3 position;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
        AssetHandle mesh;
        AssetHandle material;
        Color tint{1.0f, 1.0f, 1.0f, 1.0f};
        // 레이어의 차례와 얹는 방식이다(D-280). 브리지가 레이어마다 뷰를 하나 열어 차례대로 그린다 - 포토샵의 레이어처럼 뒤 레이어가 늘 위다.
        std::uint16_t layerOrder = 0;
        LayerBlend layerBlend = LayerBlend::Normal;
        float layerOpacity = 1.0f;
        // 패럴랙스 계수다(D-285). 게임 화면에서 그 레이어 뷰의 카메라 위치가 이 배가 된다.
        float layerParallax = 1.0f;
    };

    // 3D 텍스트의 글자 하나다(D-222). 사각형은 **오브젝트 로컬 XY 평면의 유닛**이고(왼쪽 위와 크기, y 위쪽), 월드 자리·회전·크기는 오브젝트의
    // 것이다. 빌보드면 브리지가 회전을 뷰의 카메라 것으로 바꾼다. 뒤→앞 정렬도 뷰마다 브리지가 한다.
    struct WorldTextRenderItem
    {
        GameObject* owner = nullptr;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
        bool billboard = false;
        float left = 0.0f;
        float top = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        AssetHandle texture;
        float uvRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        Color tint{1.0f, 1.0f, 1.0f, 1.0f};
        bool linearFilter = true;
        bool sdf = false;
        std::uint16_t outlineEdge = 32768;
        std::uint8_t outlineColor[4] = {0, 0, 0, 0};
        // 레이어의 차례와 얹는 방식이다(D-280). 브리지가 레이어마다 뷰를 하나 열어 차례대로 그린다 - 포토샵의 레이어처럼 뒤 레이어가 늘 위다.
        std::uint16_t layerOrder = 0;
        LayerBlend layerBlend = LayerBlend::Normal;
        float layerOpacity = 1.0f;
        // 패럴랙스 계수다(D-285). 게임 화면에서 그 레이어 뷰의 카메라 위치가 이 배가 된다.
        float layerParallax = 1.0f;
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
        // 3D 텍스트의 글자다. 용량(`ReserveTexts`)을 넘으면 세고 버린다.
        bool ReserveTexts(std::size_t capacity);
        bool SubmitText(const WorldTextRenderItem& item);
        void EndFrame();

        const RenderCamera3D* GetCamera() const;
        std::size_t GetMeshCount() const;
        std::size_t GetMeshCapacity() const;
        std::size_t GetDroppedMeshCount() const;
        const MeshRenderItem& GetMesh(std::size_t index) const;
        std::size_t GetTextCount() const;
        std::size_t GetDroppedTextCount() const;
        const WorldTextRenderItem& GetText(std::size_t index) const;
        // 뷰마다 뒤→앞으로 늘어놓는 번호 배열이다. 브리지가 쓴다 - 용량은 `ReserveTexts` 가 잡아 매 프레임 할당하지 않는다.
        Array<std::uint32_t>& GetTextOrderScratch() const;
        // 이번 프레임에 그릴 것이 있는 레이어 차례를 모으는 배열이다(D-280). 아이템 수만큼 잡아 두어 매 프레임 할당하지 않는다.
        Array<std::uint16_t>& GetLayerOrderScratch() const;

    private:
        RenderCamera3D m_camera;
        bool m_hasCamera = false;
        Array<MeshRenderItem> m_meshes;
        std::size_t m_droppedMeshCount = 0;
        Array<WorldTextRenderItem> m_texts;
        std::size_t m_droppedTextCount = 0;
        mutable Array<std::uint32_t> m_textOrder;
        mutable Array<std::uint16_t> m_layerOrders;
    };
}
