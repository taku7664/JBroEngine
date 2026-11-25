#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Host/IFramework.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/IDebugDrawSystem.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/String.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class Canvas;
    class EditorApplication;
    class Framework3D;
    struct GizmoCamera;
    namespace Component
    {
        class Animator3D;
    }

    // **모델 에디터 한 장**이다(D-461, skeletal-plan 3 단계의 3a). 모델마다 하나씩 서는 비고유 패널이고, 뿌리에 메인 도크와 나란히 붙는
    // "모델 에디터" 도크에 속한다. 같은 모델을 다시 열면 그 패널을 앞으로 가져온다(`EditorApplication::OpenModelEditor`). 기존 엔진에는 같은 화면이 없다.
    //
    //   [뼈 표시] [격자] | [맞춤]
    //   ┌ 뼈대 ─────────┬─────────────────────────────┐
    //   │ ▾ Hip         │                             │
    //   │   ▸ Spine     │   미리보기(궤도 카메라)        │
    //   ├ 애니메이션 ───┤   뼈는 관절 점과 선으로 덧그린다 │
    //   │ Wave  1.00초  │                             │
    //   └───────────────┴─────────────────────────────┘
    //   [▶] 반복 [v] 속도 [1.0]  ───●────────  0.50 / 1.00
    //
    // **미리보기는 게임과 같은 길이다**(D-456 4): 3D 프레임워크의 미리보기 캔버스(`Framework3D::CreatePreview`)에 애니메이터 오브젝트 하나와 메시마다 자식 하나를
    // 세우고, 같은 `Animator3D`·스키닝·그림자 경로로 그린다. 열린 캔버스와 섞이지 않는다. 시간은 이 창이 준 만큼만 흐른다(에디터의 재생과 무관하다).
    // 3a 는 보기만 한다 - 클립 설정·소켓·마스크 편집과 창의 커맨드 스택은 3b~3d 다.
    class ModelEditorPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "ModelEditor";
        static constexpr const char* DockAreaName = "ModelEditor";

        const char* GetTitle() const override;
        // 탭에는 모델의 경로가 보인다.
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        // 미리보기와 그 텍스처를 놓는다.
        void OnDestroy() override;
        void OnDraw() override;

        // 모델이든 그 하위 에셋이든 받아 모델을 찾는다. 모델이 아니면 거짓이다.
        static Bool ResolveModel(const EditorApplication& editor, AssetId asset, AssetId& model);
        // 이 패널에 그 모델을 싣는다. 모델이 아니면 거짓이다. 만든 직후 한 번 부른다.
        Bool Show(AssetId model);
        // 하위 에셋을 고른다: 뼈대면 그 뼈대로 미리보기를 다시 세우고, 애니메이션이면 그 클립을 처음부터 튼다. 다른 것은 무시한다.
        void Focus(AssetId asset);
        AssetId GetModel() const { return m_model; }

        // ── 시험과 가이드가 보는 상태 ──
        UInt32 GetPreview() const { return m_preview; }
        UInt32 GetBoneCount() const { return static_cast<UInt32>(m_boneNames.Size()); }
        const String& GetBoneName(UInt32 bone) const { return m_boneNames[bone]; }
        Int32 GetSelectedBone() const { return m_selectedBone; }
        void SelectBone(Int32 bone);
        UInt32 GetClipCount() const { return static_cast<UInt32>(m_clipIds.Size()); }
        Int32 GetClip() const { return m_clip; }
        void SelectClip(Int32 clip);
        Bool IsPlaying() const { return m_playing; }
        void SetPlaying(Bool playing);
        Bool IsLooping() const { return m_loop; }
        void SetLooping(Bool loop) { m_loop = loop; }
        void SetSpeed(Float speed) { m_speed = speed; }
        // 고른 클립 안의 지금 시간이다(감은 값, 초). 자세가 서지 않았으면 0.
        Float GetTime() const;
        // 타임라인을 끈 것과 같다: 멈추고 그 시간으로 간다.
        void SetTime(Float seconds);
        // 마지막으로 그린 뼈의 화면 자리다(관절). 그리지 않았으면 거짓이다.
        Bool GetBoneScreenPosition(UInt32 bone, Float& x, Float& y) const;
        // 미리보기의 바라보는 점과 거리다(맞춤이 정한다).
        Vector3 GetCameraTarget() const { return Vector3{m_centerX, m_centerY, m_centerZ}; }
        Float GetCameraDistance() const { return m_distance; }

    private:
        void RefreshAssets();
        // 미리보기를 (다시) 세운다. 프레임워크가 없으면 거짓이다.
        Bool EnsurePreview();
        void DestroyPreview();
        Component::Animator3D* FindAnimator(Canvas& canvas) const;
        // 재생·끌기를 미리보기의 애니메이터에 건다. 미리보기 요청 전에 부른다.
        void DriveAnimation(Float deltaTime);
        // 뼈의 월드 자리를 지난 자세로 다시 잰다.
        void MeasureBones();
        // 메시(쉬는 자세)나 뼈로 모델을 감싸는 공을 잰다. 잴 것이 없으면 거짓이다.
        Bool MeasureBounds(Vector3& center, Float& radius) const;
        void FrameModel();
        Bool EnsureTexture(const Extent2D& extent);
        void ReleaseTexture();
        void BuildGrid();

        void DrawToolBar();
        void DrawSkeleton();
        void DrawBoneRow(UInt32 bone);
        void DrawClips();
        void DrawViewport(Float height);
        void HandleCamera(Bool pointerHere);
        Bool MakeCamera(GizmoCamera& camera) const;
        void DrawBones(Bool pointerHere);
        void DrawTimeline();

        EditorApplication* m_editor = nullptr;
        AssetId m_model;
        String m_title;
        UInt64 m_registryRevision = 0;
        // 모델의 하위 에셋이다. 레지스트리가 바뀔 때만 다시 모은다.
        Array<AssetId> m_meshIds;
        Array<String> m_meshNames;
        Array<AssetId> m_skeletonIds;
        Array<String> m_skeletonNames;
        Array<AssetId> m_clipIds;
        Array<String> m_clipNames;
        Int32 m_skeleton = -1;
        Int32 m_clip = -1;

        // 미리보기(프레임워크의 번호)와 그것을 세운 뼈대다. 뼈대를 바꾸면 다시 세운다.
        UInt32 m_preview = 0;
        Int32 m_previewSkeleton = -1;
        UInt32 m_previewRevision = 0;
        Int32 m_appliedClip = -1;
        TextureHandle m_texture;
        Extent2D m_textureExtent;

        // 뼈대의 뼈 이름·부모와 자식 목록(뼈대가 실린 뒤 한 번 짓는다), 그리고 그린 자리.
        Array<String> m_boneNames;
        Array<Int32> m_boneParents;
        Array<Int32> m_firstChild;
        Array<Int32> m_nextSibling;
        UInt32 m_bonesGeneration = 0;
        Array<Vector3> m_boneWorld;
        Array<Float> m_boneScreenX;
        Array<Float> m_boneScreenY;
        Array<Bool> m_boneOnScreen;
        Int32 m_selectedBone = -1;
        Int32 m_hoveredBone = -1;
        // 미리보기에서 고른 뼈까지 트리를 펼친다(한 번).
        Int32 m_revealBone = -1;

        Bool m_playing = true;
        Bool m_loop = true;
        Float m_speed = 1.0f;
        Float m_pendingTime = -1.0f;

        // 궤도 카메라(캔버스 뷰와 같은 조작, D-136·D-441).
        Float m_centerX = 0.0f;
        Float m_centerY = 0.0f;
        Float m_centerZ = 0.0f;
        Float m_distance = 6.0f;
        Float m_yawDegrees = 40.0f;
        Float m_pitchDegrees = -20.0f;
        Bool m_orbiting = false;
        Bool m_framed = false;
        Float m_gridStep = 1.0f;
        Array<DebugLine> m_gridLines;
        // 그린 그림의 자리다(패널 화면 좌표).
        Float m_viewLeft = 0.0f;
        Float m_viewTop = 0.0f;
        Float m_viewWidth = 0.0f;
        Float m_viewHeight = 0.0f;

        Bool m_showBones = true;
        Bool m_showGrid = true;
        Float m_sideWidth = 220.0f;
        Float m_treeHeight = 0.0f;
    };
}
