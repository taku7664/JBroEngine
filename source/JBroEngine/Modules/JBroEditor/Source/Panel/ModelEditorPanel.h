#pragma once

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Widget/Gizmo.h>
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
    struct AnimationClipData;
    struct SkeletonData;
    class Framework3D;
    struct GizmoCamera;
    namespace Component
    {
        class Animator3D;
    }

    // **모델 에디터 한 장**이다(D-461, skeletal-plan 3 단계의 3a). 모델마다 하나씩 서는 비고유 패널이고, 뿌리에 메인 도크와 나란히 붙는
    // "모델 에디터" 도크에 속한다. 같은 모델을 다시 열면 그 패널을 앞으로 가져온다(`EditorApplication::OpenModelEditor`). 기존 엔진에는 같은 화면이 없다.
    //
    //   [뼈 표시] [격자] | [맞춤] | [옮기기] [돌리기] [크기]
    //   ┌ 계층 ───────────┬──────────────────────┬ 디테일 ─────────┐
    //   │ ▾ 뼈대        [+] │                      │ ◆ Socket   [삭제] │
    //   │   ▾ Hip           │   미리보기(궤도 카메라) │ 이름  [Socket   ] │
    //   │     ▸ Spine       │   관절 점·뼈 선·소켓    │ 뼈    [Spine   ▾] │
    //   │       ◆ Socket    │                      │ 위치  [0  0  0  ] │
    //   │ ▾ 뼈 마스크   [+] │                      │                  │
    //   │ ▾ 애니메이션  [✂] │                      │                  │
    //   └─────────────────┴──────────────────────┴──────────────────┘
    //   [▶] 0.50 / 1.00초                                      ⏲ [x1.00]
    //            0 ──── 0.25 ──── 0.5 ──── 0.75 ──── 1   ← 눈금 줄(누르거나 끌기)
    //   ⚑ 이벤트       ◆           ◆                     ← 이벤트 트랙
    //
    // **다시 짠 배치**(D-465): 계층·미리보기·디테일 셋과 아래 시간 축이다. 계층은 접는 머리 셋이고 머리 끝의 아이콘 단추가 더하기다. 소켓은 제 뼈 아래 잎 줄이다.
    // 디테일은 마지막으로 고른 것 하나의 폼이다 - 목록과 폼이 한 칸에서 서로를 밀어내지 않는다. 마스크를 고르면 뼈를 골라도 마스크 폼에 남아 그 뼈의 가중치를 고친다.
    //
    // **미리보기는 게임과 같은 길이다**(D-456 4): 3D 프레임워크의 미리보기 캔버스(`Framework3D::CreatePreview`)에 애니메이터 오브젝트 하나와 메시마다 자식 하나를
    // 세우고, 같은 `Animator3D`·스키닝·그림자 경로로 그린다. 열린 캔버스와 섞이지 않는다. 시간은 이 창이 준 만큼만 흐른다(에디터의 재생과 무관하다).
    // **3b 클립 설정**(D-462): 고른 클립의 구간(원본의 초)·반복·이벤트를 디테일 칸에서 고치고, 원본에서 구간을 떼어 새 클립을 만든다(나눈 클립은 이름을 바꾸고 지울 수 있다).
    // 값은 모델 `.jmeta` 의 그 하위 항목 `Clip` 블록이고 **편집은 이 창의 커맨드다**(창마다 되돌리기 스택, D-456 4) - Ctrl+Z 는 이 창에 포커스가 있으면 이 창의 것을 되돌린다.
    // 끄는 칸(구간·이벤트 시간)은 창 안 초안을 고치다 손을 뗄 때 메타 커맨드 하나로 남긴다 - 끄는 프레임마다 모델 메타를 쓰면 다시 읽기가 매 프레임 돈다.
    // 이벤트는 시간 축의 이벤트 트랙 표식이다: 두 번 누르면 그 시간에 더하고, 표식을 끌어 옮긴다.
    // **3c 소켓**(D-463): 소켓은 뼈대 트리에서 제 뼈 아래 줄이다(고른 뼈대의 소켓과 메시마다의 소켓). 고른 소켓은 이름·뼈·위치·회전·크기를 고치고, 미리보기에서 마름모로 보이며
    // 고른 소켓에 기즈모가 서서 끌어 옮긴다(손을 뗄 때 메타 커맨드 하나, 창의 스택). 소켓 추가는 고른 뼈에 뼈대 소켓을 단다.
    // **3d 뼈 마스크**(D-464): 계층의 `뼈 마스크` 머리 아래가 고른 뼈대의 마스크 목록이다(뼈대 메타의 `Masks`). 마스크를 고르면 뼈대 트리 줄 끝과 미리보기의 관절이 뼈마다의 가중치를 보이고,
    // 고른 뼈의 가중치를 고치거나 고른 뼈의 가지를 섞는 깊이로 채운다. 겹칠 클립을 고르면 미리보기의 레이어 1 에 그 클립을 그 마스크로 겹쳐 튼다(바탕 클립은 레이어 0).
    class ModelEditorPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "ModelEditor";
        static constexpr const char* DockAreaName = "ModelEditor";

        // 이 창의 행동(실행 취소·다시 실행)을 행동 표에 올린다. 포커스가 있을 때 캔버스의 같은 조합을 가린다.
        static void RegisterActions();

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
        // 고른 클립의 반복이다(클립 설정, D-462).
        Bool IsLooping() const { return m_draft.loop; }
        void SetSpeed(Float speed) { m_speed = speed; }

        // ── 클립 설정(D-462). 고른 클립에 건다. 하나가 메타 커맨드 하나다. ──
        EditorCommandManager& GetCommands() { return m_commands; }
        Bool Undo();
        Bool Redo();
        // 원본의 초다. `end` 가 원본 길이 이상이면 끝까지다.
        Bool SetClipRange(Float start, Float end);
        Bool SetClipLoop(Bool loop);
        // 클립 안의 시간에 이벤트를 더한다.
        Bool AddClipEvent(Float time, const char* name);
        Bool SetClipEvent(UInt32 index, const char* name, Float time);
        Bool RemoveClipEvent(UInt32 index);
        // 고른 클립의 원본과 구간으로 새 클립(새 하위 에셋)을 만든다. 레지스트리를 다시 읽은 뒤 그 클립을 고른다.
        Bool SplitClip();
        // 나눈 클립만 이름을 바꾸고 지운다 - 가져온 클립은 모델 파일의 것이다.
        Bool RenameClip(const char* name);
        Bool DeleteClip();
        Bool IsSplitClip() const { return m_draftSplit; }
        const ModelClipSettings& GetClipDraft() const { return m_draft; }
        // ── 소켓(D-463). 목록은 고른 뼈대의 소켓 다음 메시마다의 소켓이다. 하나가 메타 커맨드 하나다. ──
        UInt32 GetSocketCount() const { return static_cast<UInt32>(m_socketDraft.Size()); }
        const ModelSocket& GetSocket(UInt32 index) const { return m_socketDraft[index.Get()]; }
        Bool IsSkeletonSocket(UInt32 index) const { return m_socketRows[index.Get()].skeleton; }
        Int32 GetSelectedSocket() const { return m_selectedSocket; }
        void SelectSocket(Int32 index);
        // 고른 뼈(없으면 첫 뼈)에 뼈대 소켓을 단다. 다시 읽힌 뒤 그것을 고른다.
        Bool AddSocket();
        Bool SetSocket(UInt32 index, const ModelSocket& socket);
        Bool RemoveSocket(UInt32 index);
        // 소켓의 미리보기 월드 자리(초안 기준)와 마지막으로 그린 화면 자리다.
        Bool ComputeSocketWorld(UInt32 index, Vector3& position, Quaternion& rotation, Vector3& scale) const;
        Bool GetSocketScreenPosition(UInt32 index, Float& x, Float& y) const;
        void SetGizmoMode(GizmoMode mode) { m_gizmoMode = mode; }
        // ── 뼈 마스크(D-464). 고른 뼈대 메타의 `Masks` 다. 하나가 메타 커맨드 하나다. ──
        UInt32 GetMaskCount() const { return static_cast<UInt32>(m_maskDraft.Size()); }
        const ModelBoneMask& GetMask(UInt32 index) const { return m_maskDraft[index.Get()]; }
        Int32 GetSelectedMask() const { return m_selectedMask; }
        void SelectMask(Int32 index);
        // 고른 뼈(없으면 첫 뼈)와 그 아래를 담은(섞는 깊이만큼 서서히) 마스크를 겹치지 않는 이름으로 만든다. 다시 읽힌 뒤 그것을 고른다.
        Bool AddMask();
        // 빈 이름이나 다른 마스크와 같은 이름은 거짓이다.
        Bool RenameMask(UInt32 index, const char* name);
        Bool RemoveMask(UInt32 index);
        // 고른 마스크에서 그 뼈의 가중치다(적지 않은 뼈는 0). 고른 마스크가 없으면 0.
        Float GetMaskWeight(UInt32 bone) const;
        // 고른 마스크의 그 뼈 가중치를 0..1 로 놓는다. 0 이면 목록에서 뺀다(적지 않은 뼈는 0 이다).
        Bool SetMaskWeight(UInt32 bone, Float weight);
        // 고른 마스크에서 그 뼈와 그 아래 뼈를 섞는 깊이에 맞춰 채운다(가지 밖은 그대로).
        Bool FillMaskBranch(UInt32 bone, UInt32 depth);
        // 고른 마스크의 가중치를 모두 지운다(모두 0).
        Bool ClearMask();
        Int32 GetMaskDepth() const { return m_maskDepth; }
        void SetMaskDepth(Int32 depth) { m_maskDepth = depth < 0 ? Int32(0) : depth; }
        // 미리보기에서 고른 마스크로 레이어 1 에 겹쳐 트는 클립이다(-1 이면 겹치지 않는다). 마스크를 고르지 않으면 겹치지 않는다.
        Int32 GetOverlayClip() const { return m_overlayClip; }
        void SetOverlayClip(Int32 clip);

        // 이벤트 표식의 화면 자리다(마지막으로 그린 것). 그리지 않았으면 거짓이다.
        Bool GetEventMarkerPosition(UInt32 index, Float& x, Float& y) const;
        Bool GetEventStripRow(Float& left, Float& top, Float& width, Float& height) const;
        // 시간 축의 눈금 줄이다(시간 칸은 이벤트 줄과 같다). 누르면 그 시간으로 간다.
        Bool GetRulerRow(Float& left, Float& top, Float& width, Float& height) const;
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
        // 왼쪽 계층 칸: 뼈대(뼈 트리와 뼈마다의 소켓)·뼈 마스크·애니메이션. 머리 끝의 단추가 더하기다.
        void DrawOutline();
        void DrawBoneRow(UInt32 bone);
        void DrawSocketRow(UInt32 index);
        // 줄의 글자(아이콘과 이름)를 목록이 바뀔 때만 다시 짓는다.
        void RebuildRowLabels();
        // 트리·미리보기에서 뼈를 고른다. 마스크를 고치는 중이면 디테일은 마스크에 남는다.
        void PickBone(Int32 bone);
        // 오른쪽 디테일 칸: 마지막으로 고른 것 하나(뼈·소켓·마스크·클립)의 폼이다.
        void DrawDetails();
        void DrawBoneDetails();
        void DrawSocketDetails();
        void DrawMaskDetails();
        void DrawClipDetails();
        void DrawViewport(Float height);
        void HandleCamera(Bool pointerHere);
        Bool MakeCamera(GizmoCamera& camera) const;
        void DrawBones(Bool pointerHere);
        // 아래 재생 줄과 시간 축(눈금 줄·이벤트 트랙, `Widget::Timeline`)이다.
        void DrawTimeline();
        // 고른 클립의 메타를 읽고 그 항목 번호를 찾는다.
        Bool ReadClipEntry(AssetMetaFile& meta, std::size_t& entry);
        // 고른 클립의 메타 항목을 고치는 커맨드 하나다. `structural` 이면(키를 더하거나 바꾸거나 뺀다) 쓴 뒤 레지스트리를 다시 훑는다.
        template <typename TChange>
        Bool EditClip(const char* name, Bool structural, TChange&& change);
        // 초안을 메타의 값으로 다시 읽는다(고른 클립·레지스트리·클립 자료가 바뀌었고 칸을 고치는 중이 아닐 때).
        void SyncDraft();
        // 지금 고른 클립의 자료(자른 뒤)다. 아직 실리지 않았으면 null.
        const AnimationClipData* GetClipData() const;
        // 소켓 목록을 메타에서 다시 뜬다(커맨드·되돌리기·레지스트리·뼈대가 바뀌었고 고치는 중이 아닐 때).
        void SyncSockets();
        // 소켓 마름모와 고른 소켓의 기즈모. 무엇을 잡았으면 참이다(관절 고르기가 양보한다).
        Bool DrawSocketOverlay(Bool pointerHere, const GizmoCamera& camera);
        // 그 뼈의 미리보기 월드 자리(오프셋 없음)다.
        Bool ComputeBoneWorld(Int32 bone, Vector3& position, Quaternion& rotation, Vector3& scale) const;
        Int32 FindBone(const String& name) const;
        // 그 하위 항목(뼈대·메시)을 고치는 메타 커맨드 하나다(소켓·마스크). 하위 목록은 바꾸지 않는다.
        template <typename TChange>
        Bool EditSubEntry(const char* name, AssetId owner, TChange&& change);
        // 마스크 목록을 메타에서 다시 뜬다(커맨드·되돌리기·레지스트리·뼈대가 바뀌었고 고치는 중이 아닐 때).
        void SyncMasks();
        // 미리보기 애니메이터의 뼈대 자료다. 아직 실리지 않았으면 null.
        const SkeletonData* GetSkeletonData() const;
        // 고른 마스크의 뼈마다 가중치(뼈 번호)를 초안에서 다시 짓는다.
        void RebuildMaskWeights();
        // 고른 마스크를 고치는 메타 커맨드 하나다. `change` 는 그 마스크를 고친다.
        template <typename TChange>
        Bool EditSelectedMask(const char* name, TChange&& change);

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
        Float m_speed = 1.0f;

        // 이 창의 되돌리기 스택이다(D-456 4).
        EditorCommandManager m_commands;
        // 고른 클립의 설정 초안이다. 칸이 이것을 고치고 손을 떼면 커맨드로 남긴다.
        ModelClipSettings m_draft;
        Int32 m_draftClip = -1;
        UInt32 m_draftGeneration = 0;
        UInt64 m_draftRevision = 0;
        Bool m_draftSplit = false;
        Bool m_draftEditing = false;
        String m_draftName;
        String m_renameText;
        Array<String> m_eventNames;
        // 나눈 뒤 레지스트리가 다시 읽히면 고를 클립이다.
        AssetId m_pendingClip;
        Int32 m_selectedEvent = -1;
        Int32 m_draggingEvent = -1;
        Array<Float> m_eventScreenX;
        // 소켓 목록의 줄마다 주인 항목과 그 안의 번호다.
        struct SocketRow
        {
            AssetId owner;
            Bool skeleton = false;
            UInt32 index = 0;
            String ownerName;
        };
        Array<SocketRow> m_socketRows;
        Array<ModelSocket> m_socketDraft;
        Bool m_socketsStale = true;
        UInt64 m_socketsRevision = 0;
        UInt32 m_socketsGeneration = 0;
        Int32 m_selectedSocket = -1;
        Bool m_socketEditing = false;
        Bool m_socketFieldDirty = false;
        AssetId m_pendingSocketOwner;
        UInt32 m_pendingSocketIndex = 0;
        Bool m_hasPendingSocket = false;
        String m_socketNameText;
        Array<const char*> m_boneNamePointers;
        Array<Float> m_socketScreenX;
        Array<Float> m_socketScreenY;
        Array<Bool> m_socketOnScreen;
        // 고른 뼈대의 마스크 초안과, 고른 마스크의 뼈마다 가중치(뼈 번호, 다시 지을 때만 짓는다)다.
        Array<ModelBoneMask> m_maskDraft;
        Array<Float> m_maskWeights;
        Bool m_masksStale = true;
        UInt64 m_masksRevision = 0;
        UInt32 m_masksGeneration = 0;
        Int32 m_selectedMask = -1;
        // 막 더한 마스크의 이름이다. 다시 읽힌 뒤 그것을 고른다.
        String m_pendingMask;
        Bool m_maskEditing = false;
        Bool m_maskWeightDirty = false;
        // 고른 뼈의 가중치 칸을 끄는 동안의 값이다(손을 떼면 커맨드 하나).
        Float m_maskWeightDraft = 0.0f;
        String m_maskNameText;
        Int32 m_maskDepth = 0;
        Int32 m_overlayClip = -1;
        Int32 m_appliedOverlayClip = -1;
        String m_appliedOverlayMask;
        Bool m_overlayMaskApplied = false;
        Array<const char*> m_clipNamePointers;
        Widget::GizmoState m_gizmo;
        GizmoMode m_gizmoMode = GizmoMode::Translate;
        Float m_stripLeft = 0.0f;
        Float m_stripTop = 0.0f;
        Float m_stripWidth = 0.0f;
        Float m_stripHeight = 0.0f;
        Float m_rulerTop = 0.0f;
        Float m_rulerHeight = 0.0f;
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
        Float m_sideWidth = 200.0f;
        Float m_detailWidth = 260.0f;
        // 디테일 칸이 보이는 것이다. 마지막으로 고른 것이고, 뼈를 골라도 마스크를 고치는 중이면 마스크에 남는다.
        enum class DetailFocus : std::uint8_t
        {
            None,
            Bone,
            Socket,
            Mask,
            Clip
        };
        DetailFocus m_focus = DetailFocus::None;
        // 줄의 글자(아이콘과 이름)와 그것을 지은 판이다. 목록이 바뀔 때만 다시 짓는다.
        Array<String> m_boneLabels;
        Array<Bool> m_boneHasSockets;
        Array<String> m_socketLabels;
        // 소켓마다 그것이 달린 뼈 번호다(뼈대에 없으면 -1).
        Array<Int32> m_socketBones;
        Array<String> m_maskLabels;
        Array<String> m_clipLabels;
        UInt32 m_socketsVersion = 0;
        UInt32 m_masksVersion = 0;
        UInt32 m_labelsBonesGeneration = 0;
        std::size_t m_labelsBoneCount = 0;
        UInt32 m_labelsSocketsVersion = 0;
        UInt32 m_labelsMasksVersion = 0;
        UInt64 m_labelsRegistryRevision = 0;
        std::size_t m_labelsClipCount = 0;
        // 클립 제목 줄의 설명(가져온 클립인지, 나눈 클립의 원본)이다. 초안을 뜰 때 짓는다.
        String m_clipCaption;
        // 시간 축에 넘기는 이벤트 시간과 이름이다(프레임마다 다시 채우되 칸은 그대로 쓴다).
        Array<Float> m_eventTimes;
        Array<const char*> m_eventNamePointers;
    };
}
