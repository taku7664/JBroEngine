#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/Graphics/Renderer.h>

#include <cstdint>

// 기즈모의 수학이다(framework3d-plan 4단계, D-109). ImGui 도 컴포넌트도 모른다 - 화면 좌표와 카메라
// 행렬만 받아 손잡이 모양·집기·끌기를 계산한다. 그래서 창 없이 시험된다.
//
// 3D(`Transform3D`)와 2D(`Transform2D`)가 같은 뼈대다. 2D 는 `planar` 로 Z 손잡이가 빠지고 회전이
// Z 하나만 남는 3D 다. 카메라는 뷰-투영 행렬 하나로 받으므로 직교(2D)든 원근(3D)이든 같은 길이다.
namespace JBro
{
    enum class GizmoMode : std::uint8_t
    {
        Translate,
        Rotate,
        Scale
    };

    // 손잡이를 **누구의 축**에 둘 것인가(D-171, 기존 기즈모의 `L`/`W` 단추).
    //
    // `Local` 은 오브젝트가 돈 만큼 손잡이도 돈다. `World` 는 화면의 축 그대로다 -
    // 45도 돌아간 것을 오른쪽으로 곧게 밀려면 이쪽이라야 한다.
    //
    // **크기는 늘 `Local` 이다.** 월드 축으로 늘린 결과는 로컬 스케일 세 값으로 적을 수 없다
    // (기울어진 변형이 된다). 유니티도 크기 기즈모에는 이 토글을 두지 않는다.
    enum class GizmoSpace : std::uint8_t
    {
        Local,
        World
    };

    // `Free` 는 가운데 손잡이다: 이동은 화면 평면 위로, 크기는 균등으로.
    enum class GizmoAxis : std::uint8_t
    {
        None,
        X,
        Y,
        Z,
        Free
    };

    // 화면과 월드를 잇는다. 뷰-투영과 그 역행렬, 그리고 NDC [-1,1] 이 앉는 화면 사각형(픽셀).
    struct GizmoCamera
    {
        Matrix4x4 viewProjection;
        Matrix4x4 inverseViewProjection;
        float left = 0.0f;
        float top = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    // 기즈모가 붙는 대상이다. 월드 위치·회전과 로컬 스케일 - 손잡이는 월드에 그리고, 스케일은 대상의 축을 따른다.
    struct GizmoSubject
    {
        Vec3 position;
        Quaternion rotation;
        Vec3 scale = {1.0f, 1.0f, 1.0f};
        bool planar = false;
    };

    // 끌기 하나의 시작 상태다. 매 프레임 마우스만 새로 받아 `UpdateDrag` 가 결과를 낸다 -
    // 누적하지 않으므로 프레임을 건너뛰어도 어긋나지 않는다.
    struct GizmoDrag
    {
        GizmoMode mode = GizmoMode::Translate;
        GizmoAxis axis = GizmoAxis::None;
        GizmoSubject start;
        Vec3 axisDirection;
        Vec3 planeNormal;
        Vec3 startHit;
        float startParameter = 0.0f;
        float startAngle = 0.0f;
        float angleSign = 1.0f;
        float startDistance = 0.0f;
        float centerX = 0.0f;
        float centerY = 0.0f;
    };

    // 화면에 그릴 손잡이 하나. 선분(축)·점(가운데)·고리(회전) 셋 중 하나다.
    struct GizmoHandleShape
    {
        static constexpr std::uint32_t RingPoints = 48;

        GizmoAxis axis = GizmoAxis::None;
        bool ring = false;
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float ringX[RingPoints] = {};
        float ringY[RingPoints] = {};
    };

    class GizmoModel
    {
    public:
        static constexpr std::uint32_t MaxHandles = 4;
        // 화면 픽셀 단위다. 카메라가 얼마나 멀든 손잡이는 같은 크기로 보인다.
        static constexpr float AxisLengthPixels = 70.0f;
        static constexpr float CenterRadiusPixels = 7.0f;
        static constexpr float RingRadiusPixels = 60.0f;
        static constexpr float PickDistancePixels = 8.0f;

        // 뷰·투영과 화면 사각형으로 카메라를 만든다. 역행렬이 없으면(특이 행렬) 거짓이다.
        static bool MakeCamera(const Matrix4x4& view, const Matrix4x4& projection,
            float left, float top, float width, float height, GizmoCamera& out);

        // 월드 점을 화면 픽셀로. 카메라 뒤에 있으면 거짓이다. `depth` 는 NDC z(0..1).
        static bool Project(const GizmoCamera& camera, const Vec3& world, float& x, float& y, float* depth = nullptr);
        // 화면 픽셀과 NDC 깊이를 월드 점으로.
        static bool Unproject(const GizmoCamera& camera, float x, float y, float ndcDepth, Vec3& world);
        // 화면 점을 지나는 월드 광선. 직교 카메라면 광선들이 평행하다.
        static bool MakeRay(const GizmoCamera& camera, float x, float y, Vec3& origin, Vec3& direction);

        // 대상의 로컬 축을 월드 단위 벡터로. `Free` 는 영벡터다.
        static Vec3 AxisDirection(const GizmoSubject& subject, GizmoAxis axis);

        // 그릴 손잡이들이다. 보이지 않는 것(카메라를 정면으로 가리키는 축)은 빠진다.
        static std::uint32_t BuildHandles(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
            GizmoHandleShape* out);
        // 마우스 아래의 손잡이. 없으면 `None`. 가운데 손잡이가 축보다 먼저다.
        static GizmoAxis Pick(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
            float mouseX, float mouseY);

        static bool BeginDrag(GizmoMode mode, GizmoAxis axis, const GizmoCamera& camera,
            const GizmoSubject& subject, float mouseX, float mouseY, GizmoDrag& drag);
        // 시작 상태와 지금 마우스로 새 대상을 낸다. 광선이 축과 평행해 풀 수 없으면 거짓이고 결과는 시작값이다.
        static bool UpdateDrag(const GizmoDrag& drag, const GizmoCamera& camera, float mouseX, float mouseY,
            GizmoSubject& result);

        // 4x4 역행렬. 특이 행렬이면 거짓이다. 테스트가 직접 쓴다.
        static bool Invert(const Matrix4x4& matrix, Matrix4x4& out);
    };
}
