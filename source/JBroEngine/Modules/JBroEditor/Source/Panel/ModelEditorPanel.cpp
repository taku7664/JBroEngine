#include "ModelEditorPanel.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/SetAssetFileCommand.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/Pane.h>
#include <JBro/Editor/Widget/Timeline.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Uuid.h>
#include <JBro/Types/ValueMath.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // 캔버스 뷰의 궤도 카메라와 같은 손맛이다(`CanvasViewPanel` 의 같은 이름 값).
        constexpr Float ZoomStep = 1.1f;
        constexpr Float MinPitchDegrees = -89.0f;
        constexpr Float MaxPitchDegrees = 89.0f;
        constexpr Float MinDistance = 0.05f;
        constexpr Float MaxDistance = 5000.0f;
        constexpr Float OrbitDegreesPerPixel = 0.4f;
        constexpr Float VerticalFieldOfView = 60.0f;
        // 관절 점의 반지름과 고르는 거리(픽셀).
        constexpr Float JointRadius = 3.5f;
        constexpr Float SelectedJointRadius = 5.5f;
        constexpr Float PickDistance = 8.0f;

        // 1·2·5 x 10^n 중 `value` 이상인 가장 작은 값이다(격자 칸).
        Float NiceStep(Float value)
        {
            if (false == std::isfinite(value) || value <= 0.0f)
            {
                return 1.0f;
            }
            const Float power = std::pow(10.0f, std::floor(std::log10(value)));
            for (const Float factor : {1.0f, 2.0f, 5.0f, 10.0f})
            {
                if (power * factor >= value)
                {
                    return power * factor;
                }
            }
            return power * 10.0f;
        }

        Bool SameIds(const Array<AssetId>& left, const Array<AssetId>& right)
        {
            if (left.Size() != right.Size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.Size(); ++index)
            {
                if (false == (left[index] == right[index]))
                {
                    return false;
                }
            }
            return true;
        }
    }

    namespace
    {
        ModelEditorPanel* PanelOf(const EditorActionContext& context)
        {
            return static_cast<ModelEditorPanel*>(context.panel);
        }

        Bool CanUndo(const EditorActionContext& context)
        {
            return context.panel != nullptr && PanelOf(context)->GetCommands().CanUndo();
        }

        Bool CanRedo(const EditorActionContext& context)
        {
            return context.panel != nullptr && PanelOf(context)->GetCommands().CanRedo();
        }

        const char* WhyNothing(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::ModelEditorNothingToUndo, "nothing to undo or redo in this window");
        }

        // 하위 키의 이름 쪽이다(`AnimationClip/Run` → `Run`).
        String KeyName(const String& key)
        {
            AssetType type = AssetType::Unknown;
            std::string_view name;
            return SplitSubAssetKey(key, type, name) ? String(name) : key;
        }

        Bool KeyTaken(const AssetMetaFile& meta, const String& key, std::size_t except)
        {
            for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
            {
                if (index != except && meta.subAssets[index].key == key)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void ModelEditorPanel::RegisterActions()
    {
        EditorActionRegistry& actions = EditorActionRegistry::Get();
        auto bind = [](ImGuiKey key, Bool control = false, Bool shift = false)
        {
            EditorShortcutBinding binding;
            binding.key = key;
            binding.control = control;
            binding.shift = shift;
            return binding;
        };
        {
            EditorActionInfo info;
            info.name = "model_editor.undo";
            info.labelKey = LocKeys::MenuUndo;
            info.fallbackLabel = "Undo";
            info.categoryKey = LocKeys::ModelEditorTitle;
            info.panelType = TypeName;
            info.primary = bind(ImGuiKey_Z, true);
            info.CanExecute = &CanUndo;
            info.WhyBlocked = &WhyNothing;
            info.Execute = [](EditorActionContext& context) -> Bool { return PanelOf(context)->Undo(); };
            actions.Register(info);
        }
        {
            EditorActionInfo info;
            info.name = "model_editor.redo";
            info.labelKey = LocKeys::MenuRedo;
            info.fallbackLabel = "Redo";
            info.categoryKey = LocKeys::ModelEditorTitle;
            info.panelType = TypeName;
            info.primary = bind(ImGuiKey_Y, true);
            info.secondary = bind(ImGuiKey_Z, true, true);
            info.CanExecute = &CanRedo;
            info.WhyBlocked = &WhyNothing;
            info.Execute = [](EditorActionContext& context) -> Bool { return PanelOf(context)->Redo(); };
            actions.Register(info);
        }
    }

    Bool ModelEditorPanel::Undo()
    {
        const Bool undone = m_commands.Undo();
        m_draftClip = -1;
        m_socketsStale = true;
        m_masksStale = true;
        return undone;
    }

    Bool ModelEditorPanel::Redo()
    {
        const Bool redone = m_commands.Redo();
        m_draftClip = -1;
        m_socketsStale = true;
        m_masksStale = true;
        return redone;
    }

    const char* ModelEditorPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* ModelEditorPanel::GetDisplayTitle() const
    {
        return m_title.empty() ? Loc::TextOr(LocKeys::ModelEditorTitle, "Model Editor") : m_title.c_str();
    }

    Bool ModelEditorPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void ModelEditorPanel::OnDestroy()
    {
        DestroyPreview();
        ReleaseTexture();
    }

    Bool ModelEditorPanel::ResolveModel(const EditorApplication& editor, AssetId asset, AssetId& model)
    {
        const AssetRegistry& registry = editor.GetAssetRegistry();
        const AssetRecord* record = registry.Find(asset);
        if (record == nullptr)
        {
            return false;
        }
        if (record->type == AssetType::Model)
        {
            model = record->id;
            return true;
        }
        const AssetRecord* owner = record->owner.IsNull() ? nullptr : registry.Find(record->owner);
        if (owner != nullptr && owner->type == AssetType::Model)
        {
            model = owner->id;
            return true;
        }
        return false;
    }

    Bool ModelEditorPanel::Show(AssetId model)
    {
        const AssetRecord* record = m_editor != nullptr ? m_editor->GetAssetRegistry().Find(model) : nullptr;
        if (record == nullptr || record->type != AssetType::Model)
        {
            return false;
        }
        m_model = model;
        m_title = record->relativePath;
        m_registryRevision = 0;
        RefreshAssets();
        return true;
    }

    void ModelEditorPanel::Focus(AssetId asset)
    {
        for (std::size_t index = 0; index < m_skeletonIds.Size(); ++index)
        {
            if (m_skeletonIds[index] == asset && m_skeleton != static_cast<Int32>(index))
            {
                // 뼈대가 바뀌면 애니메이터를 다시 세운다 - 뼈 번호가 다르다.
                m_skeleton = static_cast<Int32>(index);
                m_selectedBone = -1;
                DestroyPreview();
            }
        }
        for (std::size_t index = 0; index < m_clipIds.Size(); ++index)
        {
            if (m_clipIds[index] == asset)
            {
                SelectClip(static_cast<Int32>(index));
                m_focus = DetailFocus::Clip;
                m_pendingTime = 0.0f;
                m_playing = true;
            }
        }
    }

    void ModelEditorPanel::RefreshAssets()
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        if (m_registryRevision != 0 && m_registryRevision == registry.GetRevision())
        {
            return;
        }
        m_registryRevision = registry.GetRevision();
        Array<AssetId> owned;
        registry.CollectOwned(m_model, owned);
        Array<AssetId> meshes;
        Array<String> meshNames;
        Array<AssetId> skeletons;
        Array<String> skeletonNames;
        Array<AssetId> clips;
        Array<String> clipNames;
        for (const AssetId& id : owned)
        {
            const AssetRecord* record = registry.Find(id);
            if (record == nullptr)
            {
                continue;
            }
            switch (record->type)
            {
            case AssetType::Mesh:
                meshes.Add(id);
                meshNames.Add(record->name);
                break;
            case AssetType::Skeleton:
                skeletons.Add(id);
                skeletonNames.Add(record->name);
                break;
            case AssetType::AnimationClip:
                clips.Add(id);
                clipNames.Add(record->name);
                break;
            default:
                break;
            }
        }
        // **하위 에셋이 그대로면 미리보기를 두고 간다** - 레지스트리는 다른 파일 때문에도 바뀐다. 모델을 다시 읽어도 같은 아이디면 시스템이 세대로 다시 싣는다.
        const Bool changed = false == SameIds(meshes, m_meshIds) || false == SameIds(skeletons, m_skeletonIds) || false == SameIds(clips, m_clipIds);
        m_meshNames = std::move(meshNames);
        m_skeletonNames = std::move(skeletonNames);
        m_clipNames = std::move(clipNames);
        if (false == changed)
        {
            return;
        }
        m_meshIds = std::move(meshes);
        m_skeletonIds = std::move(skeletons);
        m_clipIds = std::move(clips);
        if (m_skeleton < 0 || m_skeleton >= static_cast<Int32>(m_skeletonIds.Size()))
        {
            m_skeleton = m_skeletonIds.IsEmpty() ? Int32(-1) : Int32(0);
        }
        if (m_clip >= static_cast<Int32>(m_clipIds.Size()) || (m_clip < 0 && false == m_clipIds.IsEmpty()))
        {
            m_clip = m_clipIds.IsEmpty() ? Int32(-1) : Int32(0);
        }
        // 막 나눈 클립이 목록에 섰으면 그것을 고른다.
        for (std::size_t index = 0; false == m_pendingClip.IsNull() && index < m_clipIds.Size(); ++index)
        {
            if (m_clipIds[index] == m_pendingClip)
            {
                m_clip = static_cast<Int32>(index);
                m_pendingClip = AssetId{};
            }
        }
        m_draftClip = -1;
        DestroyPreview();
    }

    Bool ModelEditorPanel::EnsurePreview()
    {
        Framework3D* framework = m_editor->GetFramework3D();
        if (framework == nullptr)
        {
            m_preview = 0;
            return false;
        }
        // 프로젝트를 다시 열면 프레임워크가 새로 서고 옛 미리보기는 사라진다. 번호는 겹치지 않으므로 찾지 못하면 다시 세운다.
        if (m_preview != 0 && framework->GetPreviewCanvas(m_preview) != nullptr && m_previewSkeleton == m_skeleton)
        {
            return true;
        }
        DestroyPreview();
        m_preview = framework->CreatePreview();
        Canvas* canvas = framework->GetPreviewCanvas(m_preview);
        if (canvas == nullptr)
        {
            m_preview = 0;
            return false;
        }
        // 애니메이터 오브젝트 하나와 메시마다 자식 하나다. 메시는 노드 자리 없이 원점에 선다 - 노드 트리째 놓기는 `.jobject` 계약 뒤다(skeletal-plan 3.3).
        Object::GameObject* root = canvas->CreateObject(m_title.c_str());
        canvas->AttachComponent<Component::Transform3D>(root);
        auto* animator = canvas->AttachComponent<Component::Animator3D>(root);
        if (m_skeleton >= 0)
        {
            animator->skeletonId = m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())];
        }
        for (const AssetId& clip : m_clipIds)
        {
            animator->animationClipIds.Add(clip);
        }
        animator->startClip = m_clip >= 0 ? m_clip : Int32(0);
        animator->playOnStart = m_clip >= 0;
        for (std::size_t index = 0; index < m_meshIds.Size(); ++index)
        {
            Object::GameObject* part = canvas->CreateObject(m_meshNames[index].c_str());
            canvas->AttachComponent<Component::Transform3D>(part);
            part->SetParent(root);
            auto* renderer = canvas->AttachComponent<Component::MeshRenderer3D>(part);
            renderer->meshId = m_meshIds[index];
        }
        framework->BindPreviewAssets(m_preview);
        m_previewSkeleton = m_skeleton;
        m_appliedClip = m_clip;
        m_bonesGeneration = 0;
        m_boneNames.Clear();
        m_socketsStale = true;
        m_masksStale = true;
        return true;
    }

    void ModelEditorPanel::DestroyPreview()
    {
        if (m_preview != 0 && m_editor != nullptr)
        {
            if (Framework3D* framework = m_editor->GetFramework3D())
            {
                framework->DestroyPreview(m_preview);
            }
        }
        m_preview = 0;
        m_previewSkeleton = -1;
        m_appliedClip = -1;
        m_appliedOverlayClip = -1;
        m_overlayMaskApplied = false;
        m_appliedOverlayMask.clear();
    }

    Component::Animator3D* ModelEditorPanel::FindAnimator(Canvas& canvas) const
    {
        Component::Animator3D* found = nullptr;
        canvas.ForEach<Component::Animator3D>([&found](Component::Animator3D& animator) {
            found = found == nullptr ? &animator : found;
        });
        return found;
    }

    void ModelEditorPanel::SelectBone(Int32 bone)
    {
        m_selectedBone = bone >= 0 && bone < static_cast<Int32>(m_boneNames.Size()) ? bone : Int32(-1);
    }

    void ModelEditorPanel::SelectClip(Int32 clip)
    {
        if (clip < 0 || clip >= static_cast<Int32>(m_clipIds.Size()) || clip == m_clip)
        {
            return;
        }
        m_clip = clip;
        m_playing = true;
    }

    void ModelEditorPanel::SetPlaying(Bool playing)
    {
        m_playing = playing;
        // 한 번 재생이 끝에 서 있으면 처음부터 다시 튼다.
        const AnimationClipData* data = GetClipData();
        if (playing && data != nullptr && false == data->loop && GetTime() >= data->animation.duration)
        {
            m_pendingTime = 0.0f;
        }
    }

    const AnimationClipData* ModelEditorPanel::GetClipData() const
    {
        Framework3D* framework = m_editor != nullptr ? m_editor->GetFramework3D() : nullptr;
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr;
        if (animator == nullptr || assets == nullptr || m_clip < 0 || m_clip >= static_cast<Int32>(animator->animationClips.Size()))
        {
            return nullptr;
        }
        return assets->GetAnimationClip(animator->animationClips[static_cast<std::size_t>(m_clip.Get())]);
    }

    Float ModelEditorPanel::GetTime() const
    {
        Framework3D* framework = m_editor != nullptr ? m_editor->GetFramework3D() : nullptr;
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const System::Animation3DSystem* animation = framework != nullptr ? framework->GetPreviewAnimation(m_preview) : nullptr;
        const System::Animation3DSystem::State* state = animator != nullptr && animation != nullptr ? animation->FindState(animator->GetInstanceId()) : nullptr;
        const AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr;
        if (state == nullptr || state->layers.IsEmpty() || assets == nullptr)
        {
            return 0.0f;
        }
        const System::Animation3DSystem::Layer& layer = state->layers[0];
        const AnimationClipData* data = assets->GetAnimationClip(layer.clip);
        if (data == nullptr)
        {
            return 0.0f;
        }
        return WrapClipTime(layer.time, data->animation.duration, data->loop);
    }

    void ModelEditorPanel::SetTime(Float seconds)
    {
        if (std::isfinite(seconds))
        {
            m_pendingTime = (std::max)(Float(0.0f), seconds);
            m_playing = false;
        }
    }

    void ModelEditorPanel::DriveAnimation(Float deltaTime)
    {
        Framework3D* framework = m_editor->GetFramework3D();
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        System::Animation3DSystem* animation = framework != nullptr ? framework->GetPreviewAnimation(m_preview) : nullptr;
        System::Animation3DSystem::State* state = animator != nullptr && animation != nullptr ? animation->FindState(animator->GetInstanceId()) : nullptr;
        const AssetSystem* assets = m_editor->GetAssetSystem();
        // 자세는 첫 갱신에 선다. 그 전에는 시작 클립이 `playOnStart` 로 걸린다.
        if (state == nullptr || state->layers.IsEmpty() || assets == nullptr || m_clip < 0
            || m_clip >= static_cast<Int32>(animator->animationClips.Size()))
        {
            return;
        }
        const InstanceId id = animator->GetInstanceId();
        const AssetHandle clip = animator->animationClips[static_cast<std::size_t>(m_clip.Get())];
        if (m_appliedClip != m_clip && animation->Play(id, 0, clip))
        {
            m_appliedClip = m_clip;
        }
        // **마스크 미리보기**(D-464): 고른 마스크로 레이어 1 에 겹칠 클립을 튼다. 마스크는 이름으로 건다 - 마스크를 고쳐 뼈대가 다시 읽히면 시스템이 이름으로 다시 짓는다.
        const Bool overlay = m_selectedMask >= 0 && m_selectedMask < static_cast<Int32>(m_maskDraft.Size()) && m_overlayClip >= 0
            && m_overlayClip < static_cast<Int32>(animator->animationClips.Size());
        if (overlay)
        {
            const String& mask = m_maskDraft[static_cast<std::size_t>(m_selectedMask.Get())].name;
            if (m_appliedOverlayClip != m_overlayClip && animation->Play(id, 1, animator->animationClips[static_cast<std::size_t>(m_overlayClip.Get())]))
            {
                m_appliedOverlayClip = m_overlayClip;
                // 바탕과 같은 시간에서 시작한다 - 타임라인 하나로 둘을 같이 본다.
                animation->SetLayerTime(id, 1, state->layers[0].time);
            }
            if (false == m_overlayMaskApplied || false == (m_appliedOverlayMask == mask))
            {
                animation->UseLayerMask(id, 1, std::string_view(mask.c_str(), mask.size()));
                m_appliedOverlayMask = mask;
                m_overlayMaskApplied = true;
            }
        }
        else if (m_appliedOverlayClip >= 0)
        {
            animator->Stop(1);
            m_appliedOverlayClip = -1;
            m_overlayMaskApplied = false;
            m_appliedOverlayMask.clear();
        }
        const System::Animation3DSystem::Layer& layer = state->layers[0];
        if (m_pendingTime >= 0.0f && animation->SetLayerTime(id, 0, m_pendingTime))
        {
            if (m_appliedOverlayClip >= 0)
            {
                animation->SetLayerTime(id, 1, m_pendingTime);
            }
            m_pendingTime = -1.0f;
        }
        const AnimationClipData* data = assets->GetAnimationClip(clip);
        if (false == m_playing || data == nullptr)
        {
            return;
        }
        // 반복은 클립이 정한다(D-462). 한 번 재생은 끝에 선다 - 재생 단추가 다시 처음부터 튼다.
        if (false == data->loop && layer.time >= data->animation.duration)
        {
            m_playing = false;
            return;
        }
        const Float step = deltaTime * m_speed;
        if (step > 0.0f)
        {
            framework->AdvancePreview(m_preview, step);
        }
    }

    void ModelEditorPanel::MeasureBones()
    {
        m_boneWorld.Clear();
        Framework3D* framework = m_editor->GetFramework3D();
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const System::Animation3DSystem* animation = framework != nullptr ? framework->GetPreviewAnimation(m_preview) : nullptr;
        const AssetSystem* assets = m_editor->GetAssetSystem();
        if (animator == nullptr || animation == nullptr || assets == nullptr)
        {
            return;
        }
        // 뼈 이름과 트리는 뼈대가 실리거나 다시 읽힐 때만 짓는다.
        const SkeletonData* skeleton = assets->GetSkeleton(animator->skeleton);
        if (skeleton == nullptr)
        {
            m_boneNames.Clear();
            m_bonesGeneration = 0;
            return;
        }
        if (m_bonesGeneration != skeleton->dataGeneration || m_boneNames.Size() != skeleton->skeleton.bones.Size())
        {
            m_bonesGeneration = skeleton->dataGeneration;
            const std::size_t count = skeleton->skeleton.bones.Size();
            m_boneNames.Clear();
            m_boneParents.Clear();
            m_firstChild.Clear();
            m_nextSibling.Clear();
            for (std::size_t index = 0; index < count; ++index)
            {
                m_boneNames.Add(skeleton->skeleton.bones[index].name);
                m_boneParents.Add(skeleton->skeleton.bones[index].parent);
                m_firstChild.Add(-1);
                m_nextSibling.Add(-1);
            }
            // 자식은 차례대로 잇는다 - 부모는 늘 앞이라 뒤에서부터 앞에 꽂으면 차례가 지켜진다.
            for (std::size_t index = count; index-- > 0;)
            {
                const Int32 parent = m_boneParents[index];
                if (parent >= 0 && parent < static_cast<Int32>(count))
                {
                    m_nextSibling[index] = m_firstChild[static_cast<std::size_t>(parent.Get())];
                    m_firstChild[static_cast<std::size_t>(parent.Get())] = static_cast<Int32>(index);
                }
            }
            if (m_selectedBone >= static_cast<Int32>(count))
            {
                m_selectedBone = -1;
            }
        }
        // 자리는 지난 갱신의 자세다 - 지금 붙인 그림을 그린 자세와 같다.
        const System::Animation3DSystem::State* state = animation->FindState(animator->GetInstanceId());
        if (state == nullptr || state->boneCount != m_boneNames.Size())
        {
            return;
        }
        for (std::size_t index = 0; index < m_boneNames.Size(); ++index)
        {
            System::Animation3DSystem::ResolvedSocket socket;
            socket.bone = static_cast<Int32>(index);
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
            if (false == animation->ComputeSocketWorld(*canvas, *animator, socket, position, rotation, scale))
            {
                m_boneWorld.Clear();
                return;
            }
            m_boneWorld.Add(position);
        }
    }

    Bool ModelEditorPanel::MeasureBounds(Vector3& center, Float& radius) const
    {
        Framework3D* framework = m_editor->GetFramework3D();
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const AssetSystem* assets = m_editor->GetAssetSystem();
        Vector3 low{1.0e30f, 1.0e30f, 1.0e30f};
        Vector3 high{-1.0e30f, -1.0e30f, -1.0e30f};
        Bool any = false;
        const auto add = [&](Float x, Float y, Float z) {
            low = Vector3{(std::min)(low.x, x), (std::min)(low.y, y), (std::min)(low.z, z)};
            high = Vector3{(std::max)(high.x, x), (std::max)(high.y, y), (std::max)(high.z, z)};
            any = true;
        };
        if (canvas != nullptr && assets != nullptr)
        {
            // 메시의 쉬는 자세다. 스킨 메시는 정점이 쉬는 자세의 모델 좌표에 있다(역바인드 x 쉬는 뼈 = 1).
            Bool waiting = false;
            canvas->ForEach<Component::MeshRenderer3D>([&](Component::MeshRenderer3D& renderer) {
                const MeshData* mesh = assets->GetMesh(renderer.mesh);
                if (mesh == nullptr)
                {
                    waiting = waiting || false == renderer.meshId.IsNull();
                    return;
                }
                for (const ModelVertex& vertex : mesh->vertices)
                {
                    add(vertex.position[0], vertex.position[1], vertex.position[2]);
                }
            });
            // 메시가 아직 읽히는 중이면 맞추지 않는다 - 뼈만으로 맞췄다가 메시가 서면 어긋난다.
            if (waiting)
            {
                return false;
            }
        }
        for (const Vector3& bone : m_boneWorld)
        {
            add(bone.x, bone.y, bone.z);
        }
        if (false == any)
        {
            return false;
        }
        center = Vector3{(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f, (low.z + high.z) * 0.5f};
        const Vector3 half{(high.x - low.x) * 0.5f, (high.y - low.y) * 0.5f, (high.z - low.z) * 0.5f};
        radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
        if (false == (radius > 0.001f))
        {
            radius = 0.5f;
        }
        return true;
    }

    void ModelEditorPanel::FrameModel()
    {
        Vector3 center;
        Float radius = 0.0f;
        if (false == MeasureBounds(center, radius))
        {
            return;
        }
        // 감싸는 공이 세로 시야에 들게 물러난다. 조금 여유를 둔다.
        constexpr Float Degrees = 3.14159265f / 180.0f;
        const Float halfView = VerticalFieldOfView * 0.5f * Degrees;
        m_centerX = center.x;
        m_centerY = center.y;
        m_centerZ = center.z;
        m_distance = std::clamp(Float(radius / std::sin(halfView) * 1.15f), MinDistance, MaxDistance);
        m_gridStep = NiceStep(radius * 0.25f);
        m_framed = true;
    }

    Bool ModelEditorPanel::EnsureTexture(const Extent2D& extent)
    {
        if (m_texture.IsValid() && m_textureExtent.width == extent.width && m_textureExtent.height == extent.height)
        {
            return true;
        }
        Renderer* renderer = m_editor->GetRenderer();
        IRHIDevice* device = renderer != nullptr ? renderer->GetDevice() : nullptr;
        if (device == nullptr)
        {
            return false;
        }
        TextureDesc desc;
        desc.extent = extent;
        desc.format = renderer->GetBackBufferFormat();
        desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        const TextureHandle created = device->CreateTexture(desc);
        if (false == created.IsValid())
        {
            return false;
        }
        // 새것을 만든 뒤에 옛것을 놓는다. 이번 프레임의 UI 가 옛것을 그리고 있을 수 있어 에디터가 다 쓴 뒤에 지운다.
        if (m_texture.IsValid())
        {
            m_editor->RetireUiTexture(m_texture);
        }
        m_texture = created;
        m_textureExtent = extent;
        return true;
    }

    void ModelEditorPanel::ReleaseTexture()
    {
        if (m_texture.IsValid() && m_editor != nullptr)
        {
            m_editor->RetireUiTexture(m_texture);
        }
        m_texture = {};
        m_textureExtent = {};
    }

    void ModelEditorPanel::BuildGrid()
    {
        // **바닥은 y=0 평면이다**(캔버스 뷰의 3D 격자와 같다, D-441). 엔진이 메시 뒤에서 깊이로 가려 그린다. 칸은 모델 크기로 정한다.
        m_gridLines.Clear();
        const Float step = m_gridStep;
        constexpr Int32 HalfLines = 10;
        constexpr Int32 Segments = 8;
        const Float half = step * static_cast<JBro::Float>(HalfLines);
        const Float baseX = std::floor(m_centerX / step) * step;
        const Float baseZ = std::floor(m_centerZ / step) * step;
        const std::uint8_t line[4] = {255, 255, 255, 22};
        const std::uint8_t axisX[4] = {220, 90, 90, 160};
        const std::uint8_t axisZ[4] = {90, 130, 220, 160};
        const auto addLine = [&](const Vector3& from, const Vector3& to, const std::uint8_t (&color)[4]) {
            for (Int32 index = 0; index < Segments; ++index)
            {
                const Float t0 = static_cast<JBro::Float>(index) / static_cast<JBro::Float>(Segments);
                const Float t1 = static_cast<JBro::Float>(index + 1) / static_cast<JBro::Float>(Segments);
                DebugLine piece;
                piece.from[0] = from.x + (to.x - from.x) * t0;
                piece.from[1] = from.y + (to.y - from.y) * t0;
                piece.from[2] = from.z + (to.z - from.z) * t0;
                piece.to[0] = from.x + (to.x - from.x) * t1;
                piece.to[1] = from.y + (to.y - from.y) * t1;
                piece.to[2] = from.z + (to.z - from.z) * t1;
                for (Int32 channel = 0; channel < 4; ++channel)
                {
                    piece.color[channel] = color[channel];
                }
                piece.thickness = 1.0f;
                m_gridLines.Add(piece);
            }
        };
        for (Int32 index = -HalfLines; index <= HalfLines; ++index)
        {
            const Float x = baseX + step * static_cast<JBro::Float>(index);
            const Float z = baseZ + step * static_cast<JBro::Float>(index);
            addLine(Vector3{x, 0.0f, baseZ - half}, Vector3{x, 0.0f, baseZ + half}, std::fabs(x) < step * 0.001f ? axisZ : line);
            addLine(Vector3{baseX - half, 0.0f, z}, Vector3{baseX + half, 0.0f, z}, std::fabs(z) < step * 0.001f ? axisX : line);
        }
    }

    void ModelEditorPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        if (m_editor->GetFramework3D() == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::ModelEditorNeeds3D, "the model editor works in 3D projects"));
            return;
        }
        RefreshAssets();
        if (m_editor->GetAssetRegistry().Find(m_model) == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::ModelEditorMissing, "the model is gone"));
            DestroyPreview();
            return;
        }
        if (false == EnsurePreview())
        {
            return;
        }
        const Float deltaTime = ImGui::GetIO().DeltaTime;
        DriveAnimation(deltaTime);
        MeasureBones();
        if (false == m_framed)
        {
            FrameModel();
        }

        // 목록과 초안은 그리기 전에 맞춘다 - 계층·디테일·미리보기가 같은 프레임의 것을 본다.
        SyncSockets();
        SyncMasks();
        SyncDraft();
        RebuildRowLabels();

        DrawToolBar();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        // 아래는 재생 줄 하나와 시간 축(눈금 줄·이벤트 트랙)이다.
        const Float timelineHeight = ImGui::GetFrameHeightWithSpacing() + Widget::Timeline::Height(1) + ImGui::GetStyle().ItemSpacing.y;
        const Float bodyHeight = (std::max)(Float(60.0f), Float(available.y - timelineHeight));
        constexpr Float Handle = 4.0f;
        constexpr Float MinSide = 150.0f;
        constexpr Float MinDetail = 200.0f;
        constexpr Float MinView = 200.0f;
        // 왼쪽 계층과 오른쪽 디테일은 사람이 끌어 바꾼다. 미리보기가 적어도 `MinView` 는 남게 자른다.
        const Float sideLimit = (std::max)(MinSide, Float(available.x - MinDetail - MinView - Handle * 2.0f));
        const Float sideWidth = JBro::Clamp(m_sideWidth, MinSide, sideLimit);
        const Float detailLimit = (std::max)(MinDetail, Float(available.x - sideWidth - MinView - Handle * 2.0f));
        const Float detailWidth = JBro::Clamp(m_detailWidth, MinDetail, detailLimit);
        // **본문은 높이가 정해진 칸 하나다** - 세로 분할 손잡이는 남은 높이를 다 잡으므로, 감싸지 않으면 아래 타임라인을 창 밖으로 밀어낸다(실제 화면에서 그랬다).
        if (false == ImGui::BeginChild("##model_body", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::EndChild();
            DrawTimeline();
            return;
        }
        if (ImGui::BeginChild("##model_outline", ImVec2(sideWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawOutline();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##model_split", true, Handle, &m_sideWidth, MinSide, sideLimit);
        ImGui::SameLine(0.0f, 0.0f);
        Float viewWidth = (std::max)(Float(1.0f), Float(ImGui::GetContentRegionAvail().x - detailWidth - Handle));
        // 미리보기 칸은 스크롤하지 않는다 - 휠은 줌이다.
        if (ImGui::BeginChild("##model_view", ImVec2(viewWidth, 0.0f), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            DrawViewport(ImGui::GetContentRegionAvail().y);
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        // 디테일은 오른쪽 칸이라 손잡이는 미리보기 폭을 끈다. 미리보기가 넓어진 만큼 디테일이 준다.
        const Float viewBefore = viewWidth;
        Widget::Splitter("##model_detail_split", true, Handle, &viewWidth, MinView, (std::max)(MinView, Float(viewBefore + detailWidth - MinDetail)));
        m_detailWidth = detailWidth + (viewBefore - viewWidth);
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginChild("##model_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawDetails();
        }
        ImGui::EndChild();
        ImGui::EndChild();
        DrawTimeline();
    }

    void ModelEditorPanel::DrawToolBar()
    {
        if (Widget::IconButton("##model_bones_toggle", Icons::GameObject)
                .Selected(m_showBones)
                .Tooltip(Loc::TextOr(LocKeys::ModelEditorShowBones, "Show Bones"))
                .Draw())
        {
            m_showBones = false == m_showBones;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::IconButton("##model_grid_toggle", Icons::Grid)
                .Selected(m_showGrid)
                .Tooltip(Loc::TextOr(LocKeys::CanvasViewGrid, "Grid"))
                .Draw())
        {
            m_showGrid = false == m_showGrid;
        }
        Widget::ToolBarSeparator();
        if (Widget::IconButton("##model_fit", Icons::Frame)
                .Tooltip(Loc::TextOr(LocKeys::ModelEditorFit, "Frame Model"))
                .Draw())
        {
            m_framed = false;
        }
        // 고른 소켓의 기즈모 모드다(D-463).
        Widget::ToolBarSeparator();
        Widget::GizmoModeBar(m_gizmoMode, Loc::TextOr(LocKeys::GizmoTranslate, "Move"), Loc::TextOr(LocKeys::GizmoRotate, "Rotate"),
            Loc::TextOr(LocKeys::GizmoScale, "Scale"));
    }

    void ModelEditorPanel::DrawOutline()
    {
        // ── 뼈대: 뼈 트리이고, 소켓은 제 뼈 아래 줄이다 ──
        const Bool skeletonOpen = Widget::CollapsingSection(Loc::TextOr(LocKeys::ModelEditorSkeleton, "Skeleton"), true, true);
        {
            Widget::TrailingButtons buttons;
            if (buttons.Icon("##add_socket", Icons::Plus, Loc::TextOr(LocKeys::ModelEditorAddSocketTooltip, "add a skeleton socket on the selected bone"),
                    m_skeleton < 0 || m_boneNames.IsEmpty()))
            {
                AddSocket();
            }
        }
        if (skeletonOpen)
        {
            if (m_skeleton < 0)
            {
                Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorNoSkeleton, "this model has no skeleton"));
            }
            else if (m_boneNames.IsEmpty())
            {
                Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorLoading, "reading the model"));
            }
            else
            {
                for (std::size_t index = 0; index < m_boneNames.Size(); ++index)
                {
                    if (m_boneParents[index] < 0)
                    {
                        DrawBoneRow(static_cast<UInt32>(index));
                    }
                }
                // 뼈대에 없는 뼈를 가리키는 소켓은 트리 끝에 둔다 - 고쳐 다른 뼈로 옮길 수 있어야 한다.
                for (std::size_t index = 0; index < m_socketBones.Size(); ++index)
                {
                    if (m_socketBones[index] < 0)
                    {
                        DrawSocketRow(static_cast<UInt32>(index));
                    }
                }
            }
            m_revealBone = -1;
        }

        // ── 뼈 마스크 ──
        const Bool masksOpen = Widget::CollapsingSection(Loc::TextOr(LocKeys::ModelEditorMasks, "Bone Masks"), true, true);
        {
            Widget::TrailingButtons buttons;
            if (buttons.Icon("##add_mask", Icons::Plus, Loc::TextOr(LocKeys::ModelEditorAddMaskTooltip, "create a mask that covers the selected bone and the bones below it"),
                    m_skeleton < 0 || m_boneNames.IsEmpty()))
            {
                AddMask();
            }
        }
        if (masksOpen)
        {
            if (m_maskDraft.IsEmpty())
            {
                Widget::HintText(Loc::TextOr(LocKeys::ModelEditorNoMasks, "no masks"));
            }
            for (std::size_t index = 0; index < m_maskLabels.Size() && index < m_maskDraft.Size(); ++index)
            {
                const Widget::IdScope scope(static_cast<Int32>(index));
                const Bool chosen = m_selectedMask == static_cast<Int32>(index);
                if (Widget::SelectableRow(m_maskLabels[index].c_str(), chosen))
                {
                    // 고른 마스크를 다시 누르면 놓는다 - 트리 줄 끝의 가중치와 관절 색이 걷힌다.
                    SelectMask(chosen ? Int32(-1) : static_cast<Int32>(index));
                    m_focus = chosen ? (m_selectedBone >= 0 ? DetailFocus::Bone : DetailFocus::None) : DetailFocus::Mask;
                }
            }
        }

        // ── 애니메이션 ──
        const Bool clipsOpen = Widget::CollapsingSection(Loc::TextOr(LocKeys::ModelEditorClips, "Animations"), true, true);
        {
            Widget::TrailingButtons buttons;
            if (buttons.Icon("##split_clip", Icons::Cut, Loc::TextOr(LocKeys::ModelEditorSplitTooltip, "make a new clip from the same source animation and range"), m_clip < 0))
            {
                SplitClip();
                m_focus = DetailFocus::Clip;
            }
        }
        if (clipsOpen)
        {
            if (m_clipIds.IsEmpty())
            {
                Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorNoClips, "this model has no animations"));
            }
            for (std::size_t index = 0; index < m_clipLabels.Size() && index < m_clipIds.Size(); ++index)
            {
                const Widget::IdScope scope(static_cast<Int32>(index));
                if (Widget::SelectableRow(m_clipLabels[index].c_str(), m_clip == static_cast<Int32>(index)))
                {
                    SelectClip(static_cast<Int32>(index));
                    m_focus = DetailFocus::Clip;
                }
            }
        }
    }

    void ModelEditorPanel::RebuildRowLabels()
    {
        // 줄의 글자(아이콘과 이름)는 목록이 바뀔 때만 짓는다 - 줄은 적기만 한다(§11.1).
        if (m_labelsBonesGeneration == m_bonesGeneration && m_labelsBoneCount == m_boneNames.Size() && m_labelsSocketsVersion == m_socketsVersion
            && m_labelsMasksVersion == m_masksVersion && m_labelsRegistryRevision == m_registryRevision && m_labelsClipCount == m_clipNames.Size())
        {
            return;
        }
        m_labelsBonesGeneration = m_bonesGeneration;
        m_labelsBoneCount = m_boneNames.Size();
        m_labelsSocketsVersion = m_socketsVersion;
        m_labelsMasksVersion = m_masksVersion;
        m_labelsRegistryRevision = m_registryRevision;
        m_labelsClipCount = m_clipNames.Size();
        const auto make = [](const char* icon, const char* name) {
            String label = icon;
            label += "  ";
            label += name;
            return label;
        };
        m_boneLabels.Clear();
        m_boneHasSockets.Clear();
        for (const String& name : m_boneNames)
        {
            m_boneLabels.Add(make(Icons::Bone, name.c_str()));
            m_boneHasSockets.Add(false);
        }
        m_socketLabels.Clear();
        m_socketBones.Clear();
        for (std::size_t index = 0; index < m_socketDraft.Size() && index < m_socketRows.Size(); ++index)
        {
            const ModelSocket& socket = m_socketDraft[index];
            String label = make(Icons::Socket, socket.name.empty() ? Loc::TextOr(LocKeys::ModelEditorUnnamedSocket, "unnamed socket") : socket.name.c_str());
            // 메시 소켓은 그 메시 이름을 붙인다 - 같은 이름이면 뼈대 소켓이 먼저 잡힌다.
            if (false == m_socketRows[index].skeleton)
            {
                label += "  [";
                label += m_socketRows[index].ownerName;
                label += "]";
            }
            m_socketLabels.Add(label);
            const Int32 bone = FindBone(socket.bone);
            m_socketBones.Add(bone);
            if (bone >= 0)
            {
                m_boneHasSockets[static_cast<std::size_t>(bone.Get())] = true;
            }
        }
        m_maskLabels.Clear();
        for (const ModelBoneMask& mask : m_maskDraft)
        {
            m_maskLabels.Add(make(Icons::BoneMask, mask.name.c_str()));
        }
        m_clipLabels.Clear();
        for (const String& name : m_clipNames)
        {
            m_clipLabels.Add(make(Icons::AnimationClip, name.c_str()));
        }
    }

    void ModelEditorPanel::PickBone(Int32 bone)
    {
        SelectBone(bone);
        // 마스크를 고친다면 뼈를 골라도 마스크 칸에 남는다 - 그 칸이 고른 뼈의 가중치를 고친다.
        if (m_focus != DetailFocus::Mask || m_selectedMask < 0)
        {
            m_focus = m_selectedBone >= 0 ? DetailFocus::Bone : DetailFocus::None;
        }
    }

    void ModelEditorPanel::DrawSocketRow(UInt32 index)
    {
        if (index.Get() >= m_socketLabels.Size())
        {
            return;
        }
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_selectedSocket == static_cast<Int32>(index) && m_focus == DetailFocus::Socket)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const Widget::IdScope kind("socket");
        const Widget::IdScope scope(static_cast<Int32>(index));
        Widget::Tree(m_socketLabels[index.Get()].c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            SelectSocket(static_cast<Int32>(index));
            m_focus = DetailFocus::Socket;
        }
    }

    void ModelEditorPanel::DrawDetails()
    {
        // 칸에 그리지 않는 것은 고치는 중일 수 없다 - 다음 맞추기가 막히지 않게 둔다.
        m_socketEditing = false;
        m_maskEditing = m_maskWeightDirty;
        m_draftEditing = m_draggingEvent >= 0;
        DetailFocus focus = m_focus;
        // 처음에는 고른 클립을 보인다.
        if (focus == DetailFocus::None && m_clip >= 0)
        {
            focus = DetailFocus::Clip;
        }
        if (focus == DetailFocus::Bone && m_selectedBone >= 0 && m_selectedBone < static_cast<Int32>(m_boneNames.Size()))
        {
            DrawBoneDetails();
        }
        else if (focus == DetailFocus::Socket && m_selectedSocket >= 0)
        {
            DrawSocketDetails();
        }
        else if (focus == DetailFocus::Mask && m_selectedMask >= 0)
        {
            DrawMaskDetails();
        }
        else if (focus == DetailFocus::Clip && m_clip >= 0)
        {
            DrawClipDetails();
        }
        else
        {
            Widget::EmptyState(Icons::FileModel, Loc::TextOr(LocKeys::ModelEditorNoSelection, "Nothing selected"));
        }
    }

    void ModelEditorPanel::DrawBoneDetails()
    {
        const std::size_t at = static_cast<std::size_t>(m_selectedBone.Get());
        Widget::DetailHeading(Icons::Bone, m_boneNames[at].c_str());
        const Int32 parent = m_boneParents[at];
        Widget::FormLayout layout("##model_bone");
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorParent, "Parent")), [&]() {
            Widget::Text(parent >= 0 ? m_boneNames[static_cast<std::size_t>(parent.Get())].c_str() : Loc::TextOr(LocKeys::InspectorNoSocket, "(none)"));
        });
        layout.Break([&]() {
            if (Widget::IconButton("##bone_add_socket", Icons::Plus)
                    .Caption(Loc::TextOr(LocKeys::ModelEditorAddSocket, "Add Socket"))
                    .Tooltip(Loc::TextOr(LocKeys::ModelEditorAddSocketTooltip, "add a skeleton socket on the selected bone"))
                    .Draw())
            {
                AddSocket();
            }
        });
    }

    Int32 ModelEditorPanel::FindBone(const String& name) const
    {
        for (std::size_t index = 0; index < m_boneNames.Size(); ++index)
        {
            if (m_boneNames[index] == name)
            {
                return static_cast<Int32>(index);
            }
        }
        return -1;
    }

    void ModelEditorPanel::SelectSocket(Int32 index)
    {
        m_selectedSocket = index >= 0 && index < static_cast<Int32>(m_socketDraft.Size()) ? index : Int32(-1);
        m_socketNameText = m_selectedSocket >= 0 ? m_socketDraft[static_cast<std::size_t>(m_selectedSocket.Get())].name : String();
    }

    void ModelEditorPanel::SyncSockets()
    {
        if (m_socketEditing || m_gizmo.dragging)
        {
            return;
        }
        Framework3D* framework = m_editor->GetFramework3D();
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SkeletonData* skeleton = animator != nullptr && assets != nullptr ? assets->GetSkeleton(animator->skeleton) : nullptr;
        const UInt32 generation = skeleton != nullptr ? skeleton->dataGeneration : UInt32(0);
        const UInt64 revision = m_editor->GetAssetRegistry().GetRevision();
        if (false == m_socketsStale && m_socketsRevision == revision && m_socketsGeneration == generation)
        {
            return;
        }
        m_socketsStale = false;
        m_socketsRevision = revision;
        m_socketsGeneration = generation;
        m_socketRows.Clear();
        m_socketDraft.Clear();
        ++m_socketsVersion;
        AssetSystem* writable = m_editor->GetAssetSystem();
        const AssetRecord* model = m_editor->GetAssetRegistry().Find(m_model);
        AssetMetaFile meta;
        AssetMetaError error;
        if (writable == nullptr || model == nullptr || false == LoadAssetMetaFile(m_editor->GetPlatform(), writable->GetMetaPath(*model).c_str(), meta, error))
        {
            SelectSocket(-1);
            return;
        }
        // 고른 뼈대의 소켓이 먼저, 그다음 메시마다의 소켓이다.
        const auto addRows = [&](AssetId owner, Bool isSkeleton, const String& ownerName) {
            for (const AssetSubEntry& entry : meta.subAssets)
            {
                if (false == (entry.id == owner))
                {
                    continue;
                }
                for (std::size_t index = 0; index < entry.sockets.Size(); ++index)
                {
                    SocketRow row;
                    row.owner = owner;
                    row.skeleton = isSkeleton;
                    row.index = static_cast<UInt32>(index);
                    row.ownerName = ownerName;
                    m_socketRows.Add(row);
                    m_socketDraft.Add(entry.sockets[index]);
                }
            }
        };
        if (m_skeleton >= 0)
        {
            addRows(m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())], true, m_skeletonNames[static_cast<std::size_t>(m_skeleton.Get())]);
        }
        for (std::size_t index = 0; index < m_meshIds.Size(); ++index)
        {
            addRows(m_meshIds[index], false, m_meshNames[index]);
        }
        Int32 keep = m_selectedSocket < static_cast<Int32>(m_socketDraft.Size()) ? m_selectedSocket : Int32(-1);
        if (m_hasPendingSocket)
        {
            for (std::size_t index = 0; index < m_socketRows.Size(); ++index)
            {
                if (m_socketRows[index].owner == m_pendingSocketOwner && m_socketRows[index].index == m_pendingSocketIndex)
                {
                    keep = static_cast<Int32>(index);
                    m_hasPendingSocket = false;
                    m_focus = DetailFocus::Socket;
                }
            }
        }
        SelectSocket(keep);
    }

    template <typename TChange>
    Bool ModelEditorPanel::EditSubEntry(const char* name, AssetId owner, TChange&& change)
    {
        AssetSystem* assets = m_editor->GetAssetSystem();
        const AssetRecord* model = m_editor->GetAssetRegistry().Find(m_model);
        AssetMetaFile before;
        AssetMetaError error;
        if (assets == nullptr || model == nullptr || false == LoadAssetMetaFile(m_editor->GetPlatform(), assets->GetMetaPath(*model).c_str(), before, error))
        {
            return false;
        }
        AssetMetaFile after = before;
        AssetSubEntry* entry = nullptr;
        for (AssetSubEntry& each : after.subAssets)
        {
            entry = each.id == owner ? &each : entry;
        }
        if (entry == nullptr || false == change(*entry))
        {
            return false;
        }
        AssetFileTarget target;
        if (false == m_editor->DescribeAssetMeta(owner, target))
        {
            return false;
        }
        // 소켓·마스크는 하위 목록을 바꾸지 않는다 - 레지스트리를 다시 훑지 않고 그 뼈대·메시만 다시 읽는다.
        target.afterWrite = nullptr;
        target.afterWriteUser = nullptr;
        target.name = name;
        if (false == m_commands.Execute(MakeOwnerPtr<SetAssetFileCommand>(target, FormatAssetMetaFile(before), FormatAssetMetaFile(after))))
        {
            return false;
        }
        m_socketsStale = true;
        m_masksStale = true;
        return true;
    }

    Bool ModelEditorPanel::AddSocket()
    {
        if (m_skeleton < 0 || m_boneNames.IsEmpty())
        {
            return false;
        }
        const AssetId owner = m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())];
        const String bone = m_boneNames[static_cast<std::size_t>(m_selectedBone >= 0 ? m_selectedBone.Get() : 0)];
        return EditSubEntry("Add Socket", owner, [&](AssetSubEntry& entry) {
            ModelSocket socket;
            socket.bone = bone;
            // 이름은 비지 않게, 겹치지 않게 짓는다 - 빈 이름의 소켓은 붙이기가 찾지 못한다.
            for (UInt32 suffix = 1; suffix < 1000 && socket.name.empty(); ++suffix)
            {
                String candidate = "Socket";
                if (suffix > 1)
                {
                    candidate += " ";
                    candidate += String(std::to_string(suffix.Get()).c_str());
                }
                Bool taken = false;
                for (const ModelSocket& each : m_socketDraft)
                {
                    taken = taken || each.name == candidate;
                }
                socket.name = taken ? String() : candidate;
            }
            m_pendingSocketOwner = owner;
            m_pendingSocketIndex = static_cast<UInt32>(entry.sockets.Size());
            m_hasPendingSocket = true;
            entry.sockets.Add(socket);
            return true;
        });
    }

    Bool ModelEditorPanel::SetSocket(UInt32 index, const ModelSocket& socket)
    {
        if (index.Get() >= m_socketRows.Size())
        {
            return false;
        }
        const SocketRow row = m_socketRows[index.Get()];
        ModelSocket value = socket;
        value.rotation = Normalize(value.rotation);
        m_socketDraft[index.Get()] = value;
        return EditSubEntry("Edit Socket", row.owner, [&](AssetSubEntry& entry) {
            if (row.index.Get() >= entry.sockets.Size())
            {
                return false;
            }
            entry.sockets[row.index.Get()] = value;
            return true;
        });
    }

    Bool ModelEditorPanel::RemoveSocket(UInt32 index)
    {
        if (index.Get() >= m_socketRows.Size())
        {
            return false;
        }
        const SocketRow row = m_socketRows[index.Get()];
        const Bool removed = EditSubEntry("Remove Socket", row.owner, [&](AssetSubEntry& entry) {
            if (row.index.Get() >= entry.sockets.Size())
            {
                return false;
            }
            entry.sockets.RemoveAt(row.index.Get());
            return true;
        });
        if (removed)
        {
            m_selectedSocket = -1;
        }
        return removed;
    }

    void ModelEditorPanel::DrawSocketDetails()
    {
        if (m_selectedSocket >= static_cast<Int32>(m_socketDraft.Size()))
        {
            return;
        }
        const std::size_t at = static_cast<std::size_t>(m_selectedSocket.Get());
        ModelSocket& draft = m_socketDraft[at];
        Bool editing = false;
        Bool commit = false;
        const SocketRow& row = m_socketRows[at];
        Widget::DetailHeading(Icons::Socket, draft.name.empty() ? Loc::TextOr(LocKeys::ModelEditorUnnamedSocket, "unnamed socket") : draft.name.c_str(),
            row.skeleton ? nullptr : row.ownerName.c_str());
        Bool remove = false;
        {
            Widget::TrailingButtons buttons;
            remove = buttons.Icon("##remove_socket", Icons::Delete, Loc::TextOr(LocKeys::ModelEditorRemoveSocket, "Remove Socket"));
        }
        Widget::FormLayout layout("##model_socket");
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipName, "Name")), [&]() {
            if (Widget::TextField("##socket_name", m_socketNameText).CommitOnFinish()())
            {
                draft.name = m_socketNameText;
                commit = true;
            }
            editing = editing || ImGui::IsItemActive();
        });
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorSocketBone, "Bone")), [&]() {
            m_boneNamePointers.Clear();
            for (const String& name : m_boneNames)
            {
                m_boneNamePointers.Add(name.c_str());
            }
            Int32 current = FindBone(draft.bone);
            if (Widget::FilterCombo("##socket_bone", ArrayView<const char* const>(m_boneNamePointers.Data(), m_boneNamePointers.Size()), current)
                    .EmptyText(draft.bone.empty() ? Loc::TextOr(LocKeys::InspectorNoSocket, "(none)") : draft.bone.c_str())
                    .Draw()
                && current >= 0 && false == (m_boneNames[static_cast<std::size_t>(current.Get())] == draft.bone))
            {
                draft.bone = m_boneNames[static_cast<std::size_t>(current.Get())];
                commit = true;
            }
        });
        // 위치·회전·크기는 끄는 동안 초안만 바꾸고 손을 떼면 커맨드 하나다. 회전은 쿼터니언 넷이다(인스펙터와 같다).
        // 묶음 칸은 칸을 여럿 쌓아 마지막 칸만 "손을 뗐다" 를 알린다 - 바뀐 값이 있고 잡은 칸이 없어지면 확정한다.
        const auto run = [&](const char* id, const char* label, const char* fallback, Float* values, Int32 count) {
            layout.Row(Widget::FieldLabel(Loc::TextOr(label, fallback)), [&]() {
                if (Widget::ScalarRunField(id, values, count, 0.01f, false, 0.0f, 0.0f))
                {
                    m_socketFieldDirty = true;
                }
            });
        };
        run("##socket_position", LocKeys::ModelEditorSocketPosition, "Position", &draft.position.x, 3);
        run("##socket_rotation", LocKeys::ModelEditorSocketRotation, "Rotation", &draft.rotation.x, 4);
        run("##socket_scale", LocKeys::ModelEditorSocketScale, "Scale", &draft.scale.x, 3);
        if (m_socketFieldDirty && false == ImGui::IsAnyItemActive())
        {
            m_socketFieldDirty = false;
            commit = true;
        }
        m_socketEditing = editing || m_socketFieldDirty;
        // 커맨드는 칸을 다 그린 뒤다 - 실행하면 목록이 다시 떠진다.
        if (remove)
        {
            RemoveSocket(static_cast<UInt32>(at));
        }
        else if (commit)
        {
            const ModelSocket value = draft;
            SetSocket(static_cast<UInt32>(at), value);
        }
    }

    Bool ModelEditorPanel::ComputeBoneWorld(Int32 bone, Vector3& position, Quaternion& rotation, Vector3& scale) const
    {
        Framework3D* framework = m_editor->GetFramework3D();
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const System::Animation3DSystem* animation = framework != nullptr ? framework->GetPreviewAnimation(m_preview) : nullptr;
        if (animator == nullptr || animation == nullptr || bone < 0)
        {
            return false;
        }
        System::Animation3DSystem::ResolvedSocket socket;
        socket.bone = bone;
        return animation->ComputeSocketWorld(*canvas, *animator, socket, position, rotation, scale);
    }

    Bool ModelEditorPanel::ComputeSocketWorld(UInt32 index, Vector3& position, Quaternion& rotation, Vector3& scale) const
    {
        if (index.Get() >= m_socketDraft.Size())
        {
            return false;
        }
        const ModelSocket& socket = m_socketDraft[index.Get()];
        Vector3 bonePosition;
        Quaternion boneRotation;
        Vector3 boneScale;
        if (false == ComputeBoneWorld(FindBone(socket.bone), bonePosition, boneRotation, boneScale))
        {
            return false;
        }
        ComposeTransform3D(bonePosition, boneRotation, boneScale, socket.position, socket.rotation, socket.scale, position, rotation, scale);
        return true;
    }

    Bool ModelEditorPanel::GetSocketScreenPosition(UInt32 index, Float& x, Float& y) const
    {
        const std::size_t at = index.Get();
        if (at >= m_socketOnScreen.Size() || false == m_socketOnScreen[at])
        {
            return false;
        }
        x = m_socketScreenX[at];
        y = m_socketScreenY[at];
        return true;
    }

    Bool ModelEditorPanel::DrawSocketOverlay(Bool pointerHere, const GizmoCamera& camera)
    {
        m_socketScreenX.Clear();
        m_socketScreenY.Clear();
        m_socketOnScreen.Clear();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        Int32 hovered = -1;
        Float nearest = PickDistance * PickDistance;
        for (std::size_t index = 0; index < m_socketDraft.Size(); ++index)
        {
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
            Float x = 0.0f;
            Float y = 0.0f;
            const Bool onScreen = ComputeSocketWorld(static_cast<UInt32>(index), position, rotation, scale) && GizmoModel::Project(camera, position, x, y);
            m_socketScreenX.Add(x);
            m_socketScreenY.Add(y);
            m_socketOnScreen.Add(onScreen);
            if (false == onScreen)
            {
                continue;
            }
            const Float dx = x - mouse.x;
            const Float dy = y - mouse.y;
            if (pointerHere && false == m_orbiting && dx * dx + dy * dy <= nearest)
            {
                nearest = dx * dx + dy * dy;
                hovered = static_cast<Int32>(index);
            }
        }
        // 고른 소켓의 기즈모가 먼저 마우스를 받는다. 끄는 동안 초안의 오프셋을 뼈 기준으로 되돌려 적는다.
        Bool grabbed = false;
        if (m_selectedSocket >= 0)
        {
            const UInt32 selected = static_cast<UInt32>(static_cast<std::uint32_t>(m_selectedSocket.Get()));
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
            Vector3 bonePosition;
            Quaternion boneRotation;
            Vector3 boneScale;
            ModelSocket& draft = m_socketDraft[selected.Get()];
            if (ComputeSocketWorld(selected, position, rotation, scale) && ComputeBoneWorld(FindBone(draft.bone), bonePosition, boneRotation, boneScale))
            {
                GizmoSubject subject;
                subject.position = position;
                subject.rotation = rotation;
                subject.scale = scale;
                const Widget::GizmoOutput output = Widget::Gizmo(m_gizmoMode, camera, subject, m_gizmo, pointerHere || m_gizmo.dragging);
                if (output.dragging)
                {
                    const Quaternion inverse = Conjugate(Normalize(boneRotation));
                    const auto divide = [](Float value, Float by) { return by != 0.0f ? Float(value / by) : value; };
                    const Vector3 local = Rotate(inverse, Vector3{output.subject.position.x - bonePosition.x,
                        output.subject.position.y - bonePosition.y, output.subject.position.z - bonePosition.z});
                    draft.position = Vector3{divide(local.x, boneScale.x), divide(local.y, boneScale.y), divide(local.z, boneScale.z)};
                    draft.rotation = Normalize(Multiply(inverse, output.subject.rotation));
                    draft.scale = Vector3{divide(output.subject.scale.x, boneScale.x), divide(output.subject.scale.y, boneScale.y),
                        divide(output.subject.scale.z, boneScale.z)};
                }
                if (output.dragEnded)
                {
                    const ModelSocket value = draft;
                    SetSocket(selected, value);
                }
                grabbed = output.hovered || output.dragging || output.dragEnded;
            }
        }
        // 소켓은 마름모다 - 관절(원)과 갈린다. 고른 소켓은 노랗다.
        for (std::size_t index = 0; index < m_socketDraft.Size(); ++index)
        {
            if (false == m_socketOnScreen[index])
            {
                continue;
            }
            const Bool selected = m_selectedSocket == static_cast<Int32>(index);
            const ImU32 color = selected ? IM_COL32(255, 214, 90, 255) : (hovered == static_cast<Int32>(index) ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 150, 70, 230));
            const Float half = selected ? Float(6.0f) : Float(4.5f);
            const Float x = m_socketScreenX[index];
            const Float y = m_socketScreenY[index];
            draw->AddQuadFilled(ImVec2(x, y - half), ImVec2(x + half, y), ImVec2(x, y + half), ImVec2(x - half, y), color);
            draw->AddQuad(ImVec2(x, y - half), ImVec2(x + half, y), ImVec2(x, y + half), ImVec2(x - half, y), IM_COL32(0, 0, 0, 170), 1.0f);
        }
        if (false == grabbed && hovered >= 0)
        {
            const ModelSocket& socket = m_socketDraft[static_cast<std::size_t>(hovered.Get())];
            Widget::ShowTooltip(socket.name.empty() ? Loc::TextOr(LocKeys::ModelEditorUnnamedSocket, "unnamed socket") : socket.name.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                SelectSocket(hovered);
                m_focus = DetailFocus::Socket;
            }
            grabbed = true;
        }
        return grabbed;
    }

    namespace
    {
        // 마스크 하나에서 그 뼈 이름의 줄이다. 없으면 null.
        ModelMaskBone* FindMaskBone(ModelBoneMask& mask, const String& bone)
        {
            for (ModelMaskBone& entry : mask.bones)
            {
                if (entry.bone == bone)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        // 그 뼈의 가중치를 적는다. 0 이하면 줄을 뺀다(적지 않은 뼈는 0 이다).
        void PutMaskWeight(ModelBoneMask& mask, const String& bone, Float weight)
        {
            const Float value = weight < 0.0f ? Float(0.0f) : (weight > 1.0f ? Float(1.0f) : weight);
            for (std::size_t index = 0; index < mask.bones.Size(); ++index)
            {
                if (mask.bones[index].bone == bone)
                {
                    if (value <= 0.0f)
                    {
                        mask.bones.RemoveAt(index);
                    }
                    else
                    {
                        mask.bones[index].weight = value;
                    }
                    return;
                }
            }
            if (value > 0.0f)
            {
                ModelMaskBone entry;
                entry.bone = bone;
                entry.weight = value;
                mask.bones.Add(entry);
            }
        }

        // 뿌리 뼈와 그 아래를 섞는 깊이로 채운다. 가지 밖의 줄은 그대로다(Unreal 의 Branch Filter 를 여럿 겹치는 것과 같다).
        void FillBranch(ModelBoneMask& mask, const ModelSkeleton& skeleton, Int32 root, UInt32 depth)
        {
            Array<Float> weights;
            BuildMaskWeights(skeleton, root, depth, weights);
            for (std::size_t bone = 0; bone < weights.Size() && bone < skeleton.bones.Size(); ++bone)
            {
                if (weights[bone] > 0.0f)
                {
                    PutMaskWeight(mask, skeleton.bones[bone].name, weights[bone]);
                }
            }
        }

        // 마스크 가중치의 색이다: 0 은 회색, 1 은 초록.
        ImU32 MaskColor(Float weight)
        {
            const Float t = weight < 0.0f ? Float(0.0f) : (weight > 1.0f ? Float(1.0f) : weight);
            const auto channel = [t](Float from, Float to) { return static_cast<int>(Float(from + (to - from) * t).Get()); };
            return IM_COL32(channel(110.0f, 80.0f), channel(110.0f, 225.0f), channel(120.0f, 120.0f), 235);
        }
    }

    const SkeletonData* ModelEditorPanel::GetSkeletonData() const
    {
        Framework3D* framework = m_editor != nullptr ? m_editor->GetFramework3D() : nullptr;
        Canvas* canvas = framework != nullptr ? framework->GetPreviewCanvas(m_preview) : nullptr;
        const Component::Animator3D* animator = canvas != nullptr ? FindAnimator(*canvas) : nullptr;
        const AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr;
        return animator != nullptr && assets != nullptr ? assets->GetSkeleton(animator->skeleton) : nullptr;
    }

    void ModelEditorPanel::SelectMask(Int32 index)
    {
        m_selectedMask = index >= 0 && index < static_cast<Int32>(m_maskDraft.Size()) ? index : Int32(-1);
        m_maskNameText = m_selectedMask >= 0 ? m_maskDraft[static_cast<std::size_t>(m_selectedMask.Get())].name : String();
        m_maskWeightDirty = false;
        RebuildMaskWeights();
    }

    void ModelEditorPanel::RebuildMaskWeights()
    {
        m_maskWeights.Resize(m_boneNames.Size());
        for (std::size_t bone = 0; bone < m_maskWeights.Size(); ++bone)
        {
            m_maskWeights[bone] = 0.0f;
        }
        if (m_selectedMask < 0 || m_selectedMask >= static_cast<Int32>(m_maskDraft.Size()))
        {
            return;
        }
        for (const ModelMaskBone& entry : m_maskDraft[static_cast<std::size_t>(m_selectedMask.Get())].bones)
        {
            const Int32 bone = FindBone(entry.bone);
            if (bone >= 0)
            {
                m_maskWeights[static_cast<std::size_t>(bone.Get())] = entry.weight;
            }
        }
    }

    void ModelEditorPanel::SyncMasks()
    {
        if (m_maskEditing)
        {
            return;
        }
        const SkeletonData* skeleton = GetSkeletonData();
        const UInt32 generation = skeleton != nullptr ? skeleton->dataGeneration : UInt32(0);
        const UInt64 revision = m_editor->GetAssetRegistry().GetRevision();
        if (false == m_masksStale && m_masksRevision == revision && m_masksGeneration == generation)
        {
            if (m_maskWeights.Size() != m_boneNames.Size())
            {
                RebuildMaskWeights();
            }
            return;
        }
        m_masksStale = false;
        m_masksRevision = revision;
        m_masksGeneration = generation;
        m_maskDraft.Clear();
        ++m_masksVersion;
        AssetSystem* assets = m_editor->GetAssetSystem();
        const AssetRecord* model = m_editor->GetAssetRegistry().Find(m_model);
        AssetMetaFile meta;
        AssetMetaError error;
        if (m_skeleton >= 0 && assets != nullptr && model != nullptr && LoadAssetMetaFile(m_editor->GetPlatform(), assets->GetMetaPath(*model).c_str(), meta, error))
        {
            const AssetId owner = m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())];
            for (const AssetSubEntry& entry : meta.subAssets)
            {
                if (entry.id == owner)
                {
                    m_maskDraft = entry.masks;
                }
            }
        }
        Int32 keep = m_selectedMask < static_cast<Int32>(m_maskDraft.Size()) ? m_selectedMask : Int32(-1);
        if (false == m_pendingMask.empty())
        {
            for (std::size_t index = 0; index < m_maskDraft.Size(); ++index)
            {
                if (m_maskDraft[index].name == m_pendingMask)
                {
                    keep = static_cast<Int32>(index);
                    m_pendingMask.clear();
                    m_focus = DetailFocus::Mask;
                }
            }
        }
        SelectMask(keep);
    }

    template <typename TChange>
    Bool ModelEditorPanel::EditSelectedMask(const char* name, TChange&& change)
    {
        if (m_skeleton < 0 || m_selectedMask < 0 || m_selectedMask >= static_cast<Int32>(m_maskDraft.Size()))
        {
            return false;
        }
        const std::size_t at = static_cast<std::size_t>(m_selectedMask.Get());
        const String target = m_maskDraft[at].name;
        ModelBoneMask edited = m_maskDraft[at];
        if (false == change(edited))
        {
            return false;
        }
        const Bool done = EditSubEntry(name, m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())], [&](AssetSubEntry& entry) {
            if (at >= entry.masks.Size() || false == (entry.masks[at].name == target))
            {
                return false;
            }
            entry.masks[at] = edited;
            return true;
        });
        if (done)
        {
            // 다시 읽히기 전에도 트리와 미리보기가 새 값을 보인다.
            m_maskDraft[at] = edited;
            m_maskNameText = edited.name;
            RebuildMaskWeights();
        }
        return done;
    }

    Bool ModelEditorPanel::AddMask()
    {
        const SkeletonData* skeleton = GetSkeletonData();
        if (m_skeleton < 0 || m_boneNames.IsEmpty() || skeleton == nullptr)
        {
            return false;
        }
        ModelBoneMask mask;
        // 이름은 비지 않게, 겹치지 않게 짓는다 - 레이어는 이름으로 고른다.
        for (UInt32 suffix = 1; suffix < 1000 && mask.name.empty(); ++suffix)
        {
            String candidate = "Mask";
            if (suffix > 1)
            {
                candidate += " ";
                candidate += String(std::to_string(suffix.Get()).c_str());
            }
            Bool taken = false;
            for (const ModelBoneMask& each : m_maskDraft)
            {
                taken = taken || each.name == candidate;
            }
            mask.name = taken ? String() : candidate;
        }
        FillBranch(mask, skeleton->skeleton, m_selectedBone >= 0 ? m_selectedBone : Int32(0), static_cast<UInt32>(m_maskDepth.Get()));
        const Bool added = EditSubEntry("Add Mask", m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())], [&](AssetSubEntry& entry) {
            entry.masks.Add(mask);
            return true;
        });
        if (added)
        {
            m_pendingMask = mask.name;
        }
        return added;
    }

    Bool ModelEditorPanel::RenameMask(UInt32 index, const char* name)
    {
        const String wanted = name != nullptr ? String(name) : String();
        if (m_skeleton < 0 || index.Get() >= m_maskDraft.Size() || wanted.empty())
        {
            return false;
        }
        for (std::size_t other = 0; other < m_maskDraft.Size(); ++other)
        {
            if (other != index.Get() && m_maskDraft[other].name == wanted)
            {
                return false;
            }
        }
        const String target = m_maskDraft[index.Get()].name;
        if (target == wanted)
        {
            return false;
        }
        const Bool renamed = EditSubEntry("Rename Mask", m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())], [&](AssetSubEntry& entry) {
            if (index.Get() >= entry.masks.Size() || false == (entry.masks[index.Get()].name == target))
            {
                return false;
            }
            entry.masks[index.Get()].name = wanted;
            return true;
        });
        if (renamed)
        {
            m_maskDraft[index.Get()].name = wanted;
            if (m_selectedMask == static_cast<Int32>(index))
            {
                m_maskNameText = wanted;
            }
        }
        return renamed;
    }

    Bool ModelEditorPanel::RemoveMask(UInt32 index)
    {
        if (m_skeleton < 0 || index.Get() >= m_maskDraft.Size())
        {
            return false;
        }
        const String target = m_maskDraft[index.Get()].name;
        const Bool removed = EditSubEntry("Remove Mask", m_skeletonIds[static_cast<std::size_t>(m_skeleton.Get())], [&](AssetSubEntry& entry) {
            if (index.Get() >= entry.masks.Size() || false == (entry.masks[index.Get()].name == target))
            {
                return false;
            }
            entry.masks.RemoveAt(index.Get());
            return true;
        });
        if (removed)
        {
            m_selectedMask = -1;
            m_maskWeights.Clear();
        }
        return removed;
    }

    Float ModelEditorPanel::GetMaskWeight(UInt32 bone) const
    {
        return bone.Get() < m_maskWeights.Size() && m_selectedMask >= 0 ? m_maskWeights[bone.Get()] : Float(0.0f);
    }

    Bool ModelEditorPanel::SetMaskWeight(UInt32 bone, Float weight)
    {
        if (bone.Get() >= m_boneNames.Size() || false == std::isfinite(weight))
        {
            return false;
        }
        const String name = m_boneNames[bone.Get()];
        return EditSelectedMask("Set Mask Weight", [&](ModelBoneMask& mask) {
            ModelMaskBone* entry = FindMaskBone(mask, name);
            const Float before = entry != nullptr ? entry->weight : Float(0.0f);
            PutMaskWeight(mask, name, weight);
            ModelMaskBone* after = FindMaskBone(mask, name);
            return false == (before == (after != nullptr ? after->weight : Float(0.0f)));
        });
    }

    Bool ModelEditorPanel::FillMaskBranch(UInt32 bone, UInt32 depth)
    {
        const SkeletonData* skeleton = GetSkeletonData();
        if (skeleton == nullptr || bone.Get() >= skeleton->skeleton.bones.Size())
        {
            return false;
        }
        return EditSelectedMask("Fill Mask Branch", [&](ModelBoneMask& mask) {
            FillBranch(mask, skeleton->skeleton, static_cast<Int32>(bone), depth);
            return true;
        });
    }

    Bool ModelEditorPanel::ClearMask()
    {
        return EditSelectedMask("Clear Mask", [](ModelBoneMask& mask) {
            if (mask.bones.IsEmpty())
            {
                return false;
            }
            mask.bones.Clear();
            return true;
        });
    }

    void ModelEditorPanel::SetOverlayClip(Int32 clip)
    {
        m_overlayClip = clip >= 0 && clip < static_cast<Int32>(m_clipIds.Size()) ? clip : Int32(-1);
    }

    void ModelEditorPanel::DrawMaskDetails()
    {
        if (m_selectedMask >= static_cast<Int32>(m_maskDraft.Size()))
        {
            return;
        }
        const std::size_t at = static_cast<std::size_t>(m_selectedMask.Get());
        const Bool hasBone = m_selectedBone >= 0 && m_selectedBone < static_cast<Int32>(m_boneNames.Size());
        Bool editing = false;
        Bool rename = false;
        Bool commitWeight = false;
        Bool fill = false;
        Bool clear = false;
        Widget::DetailHeading(Icons::BoneMask, m_maskDraft[at].name.c_str());
        Bool remove = false;
        {
            Widget::TrailingButtons buttons;
            remove = buttons.Icon("##remove_mask", Icons::Delete, Loc::TextOr(LocKeys::ModelEditorRemoveMask, "Remove Mask"));
        }
        Widget::FormLayout layout("##model_mask");
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipName, "Name")), [&]() {
            if (Widget::TextField("##mask_name", m_maskNameText).CommitOnFinish()())
            {
                rename = true;
            }
            editing = editing || ImGui::IsItemActive();
        });
        // 겹칠 클립은 미리보기에만 쓴다 - 메타에 적지 않는다.
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorOverlayClip, "Overlay Clip")), [&]() {
            m_clipNamePointers.Clear();
            m_clipNamePointers.Add(Loc::TextOr(LocKeys::InspectorNoSocket, "(none)"));
            for (const String& name : m_clipNames)
            {
                m_clipNamePointers.Add(name.c_str());
            }
            Int32 current = m_overlayClip + 1;
            if (Widget::FilterCombo("##mask_overlay", ArrayView<const char* const>(m_clipNamePointers.Data(), m_clipNamePointers.Size()), current)
                    .ShowFilter(false)
                    .Draw())
            {
                SetOverlayClip(current - 1);
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorOverlayClipTooltip, "in the preview, play this clip over the base clip through this mask"));
        });
        if (hasBone)
        {
            // 줄 이름이 고른 뼈의 이름이다 - 어느 뼈의 값인지가 칸 옆에 보인다. 끄는 동안은 초안만 바꾸고 손을 떼면 커맨드 하나다.
            const std::size_t bone = static_cast<std::size_t>(m_selectedBone.Get());
            layout.Row(Widget::FieldLabel(m_boneNames[bone].c_str()), [&]() {
                if (false == m_maskWeightDirty)
                {
                    m_maskWeightDraft = GetMaskWeight(static_cast<UInt32>(bone));
                }
                if (Widget::SliderField<Float>("##mask_weight", m_maskWeightDraft, 0.0f, 1.0f).Format("%.2f")())
                {
                    m_maskWeightDirty = true;
                    if (bone < m_maskWeights.Size())
                    {
                        m_maskWeights[bone] = m_maskWeightDraft;
                    }
                }
                editing = editing || ImGui::IsItemActive();
                if (m_maskWeightDirty && false == ImGui::IsItemActive())
                {
                    m_maskWeightDirty = false;
                    commitWeight = true;
                }
            });
        }
        else
        {
            layout.Break([&]() { Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorMaskPickBone, "select a bone to edit its weight")); });
        }
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorMaskDepth, "Blend Depth")), [&]() {
            Widget::DragField<Int32>("##mask_depth", m_maskDepth).Range(0, 32)();
        });
        layout.Break([&]() {
            if (Widget::IconButton("##mask_fill", Icons::Branch)
                    .Caption(Loc::TextOr(LocKeys::ModelEditorFillBranch, "Fill Branch"))
                    .Tooltip(Loc::TextOr(LocKeys::ModelEditorFillBranchTooltip, "set the weights of the selected bone and the bones below it"))
                    .Disabled(false == hasBone)
                    .Draw())
            {
                fill = true;
            }
            ImGui::SameLine(0.0f, 6.0f);
            if (Widget::IconButton("##mask_clear", Icons::Eraser).Caption(Loc::TextOr(LocKeys::ModelEditorClearMask, "Clear Weights")).Draw())
            {
                clear = true;
            }
        });
        m_maskEditing = editing || m_maskWeightDirty;
        // 커맨드는 칸을 다 그린 뒤다 - 실행하면 목록이 다시 떠진다.
        if (remove)
        {
            RemoveMask(static_cast<UInt32>(at));
        }
        else if (rename)
        {
            const String wanted = m_maskNameText;
            if (false == RenameMask(static_cast<UInt32>(at), wanted.c_str()))
            {
                m_maskNameText = m_maskDraft[at].name;
            }
        }
        else if (commitWeight && hasBone)
        {
            const Float value = m_maskWeightDraft;
            if (false == SetMaskWeight(static_cast<UInt32>(m_selectedBone), value))
            {
                RebuildMaskWeights();
            }
        }
        else if (fill && hasBone)
        {
            FillMaskBranch(static_cast<UInt32>(m_selectedBone), static_cast<UInt32>(m_maskDepth.Get()));
        }
        else if (clear)
        {
            ClearMask();
        }
    }

    void ModelEditorPanel::DrawBoneRow(UInt32 bone)
    {
        const std::size_t at = bone.Get();
        const Bool hasSockets = at < m_boneHasSockets.Size() && m_boneHasSockets[at];
        const Bool hasChildren = m_firstChild[at] >= 0 || hasSockets;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (false == hasChildren)
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        // 고른 줄은 디테일 칸이 보이는 것 하나다 - 마스크를 고치는 동안에는 가중치를 고칠 뼈다.
        if (m_selectedBone == static_cast<Int32>(bone) && (m_focus == DetailFocus::Bone || m_focus == DetailFocus::Mask))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        // 미리보기에서 고른 뼈까지 연다 - 그 뼈의 조상이면 이 줄을 펼친다.
        if (m_revealBone >= 0 && hasChildren)
        {
            for (Int32 walk = m_boneParents[static_cast<std::size_t>(m_revealBone.Get())]; walk >= 0; walk = m_boneParents[static_cast<std::size_t>(walk.Get())])
            {
                if (walk == static_cast<Int32>(bone))
                {
                    ImGui::SetNextItemOpen(true);
                    break;
                }
            }
        }
        const Widget::IdScope scope(static_cast<Int32>(bone));
        const Bool open = Widget::Tree(at < m_boneLabels.Size() ? m_boneLabels[at].c_str() : m_boneNames[at].c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && false == ImGui::IsItemToggledOpen())
        {
            PickBone(static_cast<Int32>(bone));
        }
        // 마스크를 골랐으면 줄 끝에 그 뼈의 가중치를 쓴다(D-464). 0 은 흐리다.
        if (m_selectedMask >= 0 && at < m_maskWeights.Size())
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(m_maskWeights[at].Get()));
            Widget::RowTrailingText(text, false == (m_maskWeights[at] > 0.0f));
        }
        if (m_revealBone == static_cast<Int32>(bone))
        {
            ImGui::SetScrollHereY();
        }
        if (false == open)
        {
            return;
        }
        for (Int32 child = m_firstChild[at]; child >= 0; child = m_nextSibling[static_cast<std::size_t>(child.Get())])
        {
            DrawBoneRow(static_cast<UInt32>(child));
        }
        // 이 뼈에 단 소켓은 자식 뼈 뒤의 잎 줄이다.
        for (std::size_t index = 0; hasSockets && index < m_socketBones.Size(); ++index)
        {
            if (m_socketBones[index] == static_cast<Int32>(bone))
            {
                DrawSocketRow(static_cast<UInt32>(index));
            }
        }
        Widget::TreePop();
    }

    Bool ModelEditorPanel::ReadClipEntry(AssetMetaFile& meta, std::size_t& entry)
    {
        AssetSystem* assets = m_editor->GetAssetSystem();
        const AssetRecord* model = m_editor->GetAssetRegistry().Find(m_model);
        if (assets == nullptr || model == nullptr || m_clip < 0 || m_clip >= static_cast<Int32>(m_clipIds.Size()))
        {
            return false;
        }
        // 디스크의 메타를 읽는다 - 커맨드가 되살릴 글자가 파일과 같아야 한다.
        AssetMetaError error;
        if (false == LoadAssetMetaFile(m_editor->GetPlatform(), assets->GetMetaPath(*model).c_str(), meta, error))
        {
            return false;
        }
        const AssetId clip = m_clipIds[static_cast<std::size_t>(m_clip.Get())];
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            if (meta.subAssets[index].id == clip)
            {
                entry = index;
                return true;
            }
        }
        return false;
    }

    template <typename TChange>
    Bool ModelEditorPanel::EditClip(const char* name, Bool structural, TChange&& change)
    {
        AssetMetaFile before;
        std::size_t at = 0;
        if (false == ReadClipEntry(before, at))
        {
            return false;
        }
        AssetMetaFile after = before;
        if (false == after.subAssets[at].hasClip)
        {
            after.subAssets[at].hasClip = true;
            after.subAssets[at].clip = ModelClipSettings{};
        }
        if (false == change(after, at))
        {
            return false;
        }
        AssetFileTarget target;
        if (false == m_editor->DescribeAssetMeta(m_clipIds[static_cast<std::size_t>(m_clip.Get())], target))
        {
            return false;
        }
        // 값만 바꾸면 하위 목록은 그대로다 - 레지스트리를 다시 훑지 않는다(모델을 다시 읽는다). 키를 바꾸면 훑는다.
        if (false == structural)
        {
            target.afterWrite = nullptr;
            target.afterWriteUser = nullptr;
        }
        target.name = name;
        if (false == m_commands.Execute(MakeOwnerPtr<SetAssetFileCommand>(target, FormatAssetMetaFile(before), FormatAssetMetaFile(after))))
        {
            return false;
        }
        // 다시 읽힌 클립이 서기 전에도 칸이 새 값을 보인다. 다시 읽히면 초안을 메타에서 다시 뜬다.
        if (at < after.subAssets.Size() && after.subAssets[at].id == m_clipIds[static_cast<std::size_t>(m_clip.Get())])
        {
            m_draft = after.subAssets[at].clip;
        }
        return true;
    }

    void ModelEditorPanel::SyncDraft()
    {
        if (m_draftEditing)
        {
            return;
        }
        const AnimationClipData* data = GetClipData();
        const UInt32 generation = data != nullptr ? data->dataGeneration : UInt32(0);
        const UInt64 revision = m_editor->GetAssetRegistry().GetRevision();
        if (m_draftClip == m_clip && m_draftGeneration == generation && m_draftRevision == revision)
        {
            return;
        }
        m_draftClip = m_clip;
        m_draftGeneration = generation;
        m_draftRevision = revision;
        m_draft = ModelClipSettings{};
        m_draftSplit = false;
        m_draftName.clear();
        AssetMetaFile meta;
        std::size_t at = 0;
        if (ReadClipEntry(meta, at))
        {
            const AssetSubEntry& entry = meta.subAssets[at];
            m_draft = entry.hasClip ? entry.clip : ModelClipSettings{};
            m_draftSplit = entry.hasClip && false == entry.clip.source.empty();
            m_draftName = KeyName(entry.key);
        }
        m_renameText = m_draftName;
        if (m_draftSplit)
        {
            m_clipCaption = Loc::TextOr(LocKeys::ModelEditorClipSource, "Source");
            m_clipCaption += " ";
            m_clipCaption += m_draft.source;
        }
        else
        {
            m_clipCaption = Loc::TextOr(LocKeys::ModelEditorImportedClip, "imported from the model file");
        }
        m_eventNames.Clear();
        for (const ModelClipEvent& event : m_draft.events)
        {
            m_eventNames.Add(event.name);
        }
        if (m_selectedEvent >= static_cast<Int32>(m_draft.events.Size()))
        {
            m_selectedEvent = -1;
        }
    }

    Bool ModelEditorPanel::SetClipRange(Float start, Float end)
    {
        const AnimationClipData* data = GetClipData();
        const Float source = data != nullptr ? data->sourceDuration : Float(0.0f);
        if (false == std::isfinite(start) || false == std::isfinite(end))
        {
            return false;
        }
        return EditClip("Set Clip Range", false, [&](AssetMetaFile& meta, std::size_t at) {
            ModelClipSettings& clip = meta.subAssets[at].clip;
            clip.start = (std::max)(Float(0.0f), source > 0.0f ? (std::min)(start, source) : start);
            // 원본 길이에 닿으면 "끝까지" 다 - 원본이 길어져도 따라간다.
            clip.end = source > 0.0f && end >= source ? Float(-1.0f) : (std::max)(clip.start, end);
            return true;
        });
    }

    Bool ModelEditorPanel::SetClipLoop(Bool loop)
    {
        return EditClip("Set Clip Loop", false, [&](AssetMetaFile& meta, std::size_t at) {
            meta.subAssets[at].clip.loop = loop;
            return true;
        });
    }

    Bool ModelEditorPanel::AddClipEvent(Float time, const char* name)
    {
        if (false == std::isfinite(time))
        {
            return false;
        }
        return EditClip("Add Animation Event", false, [&](AssetMetaFile& meta, std::size_t at) {
            ModelClipEvent event;
            event.name = name != nullptr ? name : "";
            event.time = (std::max)(Float(0.0f), time);
            meta.subAssets[at].clip.events.Add(event);
            m_selectedEvent = static_cast<Int32>(meta.subAssets[at].clip.events.Size() - 1);
            return true;
        });
    }

    Bool ModelEditorPanel::SetClipEvent(UInt32 index, const char* name, Float time)
    {
        if (false == std::isfinite(time))
        {
            return false;
        }
        return EditClip("Edit Animation Event", false, [&](AssetMetaFile& meta, std::size_t at) {
            Array<ModelClipEvent>& events = meta.subAssets[at].clip.events;
            if (index.Get() >= events.Size())
            {
                return false;
            }
            events[index.Get()].name = name != nullptr ? name : "";
            events[index.Get()].time = (std::max)(Float(0.0f), time);
            return true;
        });
    }

    Bool ModelEditorPanel::RemoveClipEvent(UInt32 index)
    {
        return EditClip("Remove Animation Event", false, [&](AssetMetaFile& meta, std::size_t at) {
            Array<ModelClipEvent>& events = meta.subAssets[at].clip.events;
            if (index.Get() >= events.Size())
            {
                return false;
            }
            events.RemoveAt(index.Get());
            m_selectedEvent = -1;
            return true;
        });
    }

    Bool ModelEditorPanel::SplitClip()
    {
        return EditClip("Split Animation Clip", true, [&](AssetMetaFile& meta, std::size_t at) {
            // 배열에 더하기 전에 값을 떠 둔다 - 더하면 자리가 옮겨질 수 있다.
            const AssetSubEntry from = meta.subAssets[at];
            const String source = from.clip.source.empty() ? KeyName(from.key) : from.clip.source;
            AssetSubEntry made;
            made.id = Uuid::Generate();
            made.hasClip = true;
            made.clip.source = source;
            made.clip.start = from.clip.start;
            made.clip.end = from.clip.end;
            made.clip.loop = from.clip.loop;
            for (UInt32 suffix = 2; suffix < 1000; ++suffix)
            {
                String key = "AnimationClip/";
                key += source;
                key += " ";
                key += String(std::to_string(suffix.Get()).c_str());
                if (false == KeyTaken(meta, key, meta.subAssets.Size()))
                {
                    made.key = key;
                    break;
                }
            }
            if (made.key.empty())
            {
                return false;
            }
            m_pendingClip = made.id;
            meta.subAssets.Add(std::move(made));
            return true;
        });
    }

    Bool ModelEditorPanel::RenameClip(const char* name)
    {
        String wanted = name != nullptr ? String(name) : String();
        while (false == wanted.empty() && (wanted.back() == ' ' || wanted.back() == '\t'))
        {
            wanted.pop_back();
        }
        while (false == wanted.empty() && (wanted.front() == ' ' || wanted.front() == '\t'))
        {
            wanted.erase(0, 1);
        }
        if (false == m_draftSplit || wanted.empty() || wanted == m_draftName || wanted.find('/') != String::npos)
        {
            return false;
        }
        return EditClip("Rename Animation Clip", true, [&](AssetMetaFile& meta, std::size_t at) {
            String key = "AnimationClip/";
            key += wanted;
            if (KeyTaken(meta, key, at))
            {
                return false;
            }
            meta.subAssets[at].key = key;
            return true;
        });
    }

    Bool ModelEditorPanel::DeleteClip()
    {
        if (false == m_draftSplit)
        {
            return false;
        }
        return EditClip("Delete Animation Clip", true, [&](AssetMetaFile& meta, std::size_t at) {
            meta.subAssets.RemoveAt(at);
            return true;
        });
    }

    void ModelEditorPanel::DrawClipDetails()
    {
        if (m_clip < 0)
        {
            return;
        }
        const AnimationClipData* data = GetClipData();
        const Float source = data != nullptr ? data->sourceDuration : Float(0.0f);
        Bool editing = false;
        // 제목 줄: 가져온 클립은 모델 파일의 것이라 이름을 바꾸거나 지울 수 없다. 나눈 클립은 원본을 적는다.
        Widget::DetailHeading(Icons::AnimationClip, m_clip < static_cast<Int32>(m_clipNames.Size()) ? m_clipNames[static_cast<std::size_t>(m_clip.Get())].c_str() : "",
            m_clipCaption.c_str());
        {
            Widget::TrailingButtons buttons;
            if (m_draftSplit && buttons.Icon("##delete_clip", Icons::Delete, Loc::TextOr(LocKeys::ModelEditorDeleteClip, "Delete Clip")))
            {
                DeleteClip();
                return;
            }
            if (buttons.Icon("##split_clip", Icons::Cut, Loc::TextOr(LocKeys::ModelEditorSplitTooltip, "make a new clip from the same source animation and range")))
            {
                SplitClip();
            }
        }
        // 폼 표는 이벤트 머리 전에 닫는다 - 열린 채로 머리를 그리면 머리가 표의 값 칸에 들어가 들여 써진다(화면에서 그랬다).
        {
            Widget::FormLayout layout("##model_clip_settings");
            if (m_draftSplit)
            {
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipName, "Name")), [&]() {
                    if (Widget::TextField("##clip_name", m_renameText).CommitOnFinish()())
                    {
                        if (false == RenameClip(m_renameText.c_str()))
                        {
                            m_renameText = m_draftName;
                        }
                    }
                    editing = editing || ImGui::IsItemActive();
                });
            }
            // 구간은 원본의 초다. 끄는 동안은 초안만 바꾸고 손을 떼면 커맨드 하나다.
            Float start = m_draft.start;
            Float end = m_draft.end < 0.0f ? source : m_draft.end;
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipStart, "Start")), [&]() {
                if (Widget::DragField("##clip_start", start).Range(0.0f, (std::max)(source, Float(0.0f))).Speed(0.01f).Format("%.2f").StepButtons(false)())
                {
                    m_draft.start = start;
                }
                editing = editing || ImGui::IsItemActive();
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    SetClipRange(m_draft.start, end);
                }
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipEnd, "End")), [&]() {
                if (Widget::DragField("##clip_end", end).Range(0.0f, (std::max)(source, Float(0.0f))).Speed(0.01f).Format("%.2f").StepButtons(false)())
                {
                    m_draft.end = end;
                }
                editing = editing || ImGui::IsItemActive();
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    SetClipRange(start, end);
                }
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorLoop, "Loop")), [&]() {
                Bool loop = m_draft.loop;
                if (Widget::Checkbox("##clip_loop", loop))
                {
                    SetClipLoop(loop);
                }
            });
        }
        // 이벤트는 접는 머리 아래 목록이다. 머리 끝의 + 는 지금 시간에 더한다(타임라인의 이벤트 줄을 두 번 눌러도 된다).
        ImGui::Spacing();
        const Bool eventsOpen = Widget::CollapsingSection(Loc::TextOr(LocKeys::ModelEditorEvents, "Events"), true, true);
        {
            Widget::TrailingButtons buttons;
            if (buttons.Icon("##add_event", Icons::Plus, Loc::TextOr(LocKeys::ModelEditorAddEvent, "Add Event")))
            {
                AddClipEvent(GetTime(), "");
            }
        }
        if (false == eventsOpen)
        {
            m_draftEditing = m_draggingEvent >= 0;
            return;
        }
        if (m_draft.events.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::ModelEditorNoEvents, "no events"));
        }
        Widget::FormLayout eventLayout("##model_clip_events");
        while (m_eventNames.Size() < m_draft.events.Size())
        {
            m_eventNames.Add(String());
        }
        // **커맨드는 줄을 다 그린 뒤에 실행한다.** 실행하면 초안을 메타의 값으로 새로 대입해 배열이 다시 잡힌다 - 도는 중에 하면 다음 줄이 지나간 원소를 읽는다.
        enum class EventEdit : std::uint8_t
        {
            None,
            Set,
            Remove
        };
        EventEdit pendingEdit = EventEdit::None;
        std::size_t pendingIndex = 0;
        for (std::size_t index = 0; index < m_draft.events.Size(); ++index)
        {
            // Id 는 칸 안에서 쌓는다 - `Break` 가 표를 닫을 때 바깥에 쌓인 Id 가 있으면 어긋난다.
            eventLayout.Break([&]() {
                const Widget::IdScope scope(static_cast<Int32>(index));
                const Float rowWidth = ImGui::GetContentRegionAvail().x;
                if (Widget::TextField("##event_name", m_eventNames[index]).Hint(Loc::TextOr(LocKeys::ModelEditorUnnamedEvent, "unnamed event")).Width((std::max)(Float(40.0f), Float(rowWidth - 110.0f))).CommitOnFinish()())
                {
                    m_draft.events[index].name = m_eventNames[index];
                    pendingEdit = EventEdit::Set;
                    pendingIndex = index;
                }
                editing = editing || ImGui::IsItemActive();
                if (ImGui::IsItemActivated())
                {
                    m_selectedEvent = static_cast<Int32>(index);
                }
                ImGui::SameLine(0.0f, 4.0f);
                Float time = m_draft.events[index].time;
                if (Widget::DragField("##event_time", time).Range(0.0f, data != nullptr ? data->animation.duration : Float(1000.0f))
                        .Speed(0.01f).Format("%.2f").StepButtons(false).Width(70.0f)())
                {
                    m_draft.events[index].time = time;
                }
                editing = editing || ImGui::IsItemActive();
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    pendingEdit = EventEdit::Set;
                    pendingIndex = index;
                }
                ImGui::SameLine(0.0f, 4.0f);
                if (Widget::TextButton(Icons::Delete))
                {
                    pendingEdit = EventEdit::Remove;
                    pendingIndex = index;
                }
                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorRemoveEvent, "Remove Event"));
            });
        }
        if (pendingEdit == EventEdit::Set && pendingIndex < m_draft.events.Size())
        {
            const ModelClipEvent edited = m_draft.events[pendingIndex];
            SetClipEvent(static_cast<UInt32>(pendingIndex), edited.name.c_str(), edited.time);
        }
        else if (pendingEdit == EventEdit::Remove)
        {
            RemoveClipEvent(static_cast<UInt32>(pendingIndex));
        }
        m_draftEditing = editing || m_draggingEvent >= 0;
    }

    Bool ModelEditorPanel::MakeCamera(GizmoCamera& camera) const
    {
        // **그린 카메라를 그대로 쓴다**(D-140 과 같은 까닭) - 여기서 궤도 행렬을 한 번 더 세우면 둘로 갈린다. 한 프레임 전의 것이다.
        Framework3D* framework = m_editor->GetFramework3D();
        CameraParams drawn;
        if (framework == nullptr || false == framework->GetPreviewCamera(m_preview, drawn) || m_textureExtent.width == 0 || m_textureExtent.height == 0)
        {
            return false;
        }
        return GizmoModel::MakeCamera(drawn.view, drawn.projection, m_viewLeft, m_viewTop,
            static_cast<JBro::Float>(m_textureExtent.width), static_cast<JBro::Float>(m_textureExtent.height), camera);
    }

    void ModelEditorPanel::DrawViewport(Float height)
    {
        const ImVec2 available(ImGui::GetContentRegionAvail().x, height);
        if (available.x <= 1.0f || available.y <= 1.0f)
        {
            return;
        }
        // 텍스처는 64 의 배수로 올려 잡는다 - 창을 조금씩 끄는 동안 다시 만들지 않는다(캔버스 뷰와 같다).
        constexpr UInt32 Step = 64;
        const UInt32 wantedWidth = static_cast<JBro::UInt32>(available.x);
        const UInt32 wantedHeight = static_cast<JBro::UInt32>(available.y);
        const Extent2D rounded{((wantedWidth + Step - 1) / Step) * Step, ((wantedHeight + Step - 1) / Step) * Step};
        Framework3D* framework = m_editor->GetFramework3D();
        CameraParams drawn;
        const Bool rendered = m_texture.IsValid() && framework != nullptr && framework->GetPreviewCamera(m_preview, drawn);
        const TextureHandle shown = m_texture;
        const Extent2D shownExtent = m_textureExtent;
        if (EnsureTexture(rounded))
        {
            PreviewViewDesc request;
            request.preview = m_preview;
            EditorViewDesc& view = request.view;
            view.target = m_texture;
            view.extent = m_textureExtent;
            view.centerX = m_centerX;
            view.centerY = m_centerY;
            view.centerZ = m_centerZ;
            view.distance = m_distance;
            view.yawDegrees = m_yawDegrees;
            view.pitchDegrees = m_pitchDegrees;
            view.verticalFieldOfView = VerticalFieldOfView;
            view.clearColor[0] = EditorTheme::ViewportBackground.x;
            view.clearColor[1] = EditorTheme::ViewportBackground.y;
            view.clearColor[2] = EditorTheme::ViewportBackground.z;
            view.clearColor[3] = 1.0f;
            if (m_showGrid)
            {
                BuildGrid();
                view.lines = m_gridLines.Data();
                view.lineCount = static_cast<JBro::UInt32>(m_gridLines.Size());
            }
            m_editor->RequestPreviewView(request);
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        m_viewLeft = origin.x;
        m_viewTop = origin.y;
        m_viewWidth = available.x;
        m_viewHeight = available.y;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // 이번 프레임에 그릴 그림은 지난 프레임에 그린 것이다. 텍스처를 막 바꿨으면 아직 그린 적이 없어 바탕만 칠한다.
        if (rendered && shown == m_texture && shownExtent.width != 0 && shownExtent.height != 0)
        {
            const Float u = available.x / static_cast<JBro::Float>(shownExtent.width);
            const Float v = available.y / static_cast<JBro::Float>(shownExtent.height);
            draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(shown)), origin,
                ImVec2(origin.x + available.x, origin.y + available.y), ImVec2(0.0f, 0.0f), ImVec2(u, v));
        }
        else
        {
            draw->AddRectFilled(origin, ImVec2(origin.x + available.x, origin.y + available.y), ImGui::GetColorU32(EditorTheme::ViewportBackground));
        }
        // 기즈모 손잡이가 이 자리 위에서 마우스를 받도록 겹침을 허락한다(캔버스 뷰와 같다).
        ImGui::SetNextItemAllowOverlap();
        Widget::HitArea("##model_preview", available,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const Bool pointerHere = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        HandleCamera(pointerHere);
        draw->PushClipRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), true);
        GizmoCamera socketCamera;
        const Bool socketGrabbed = MakeCamera(socketCamera) && DrawSocketOverlay(pointerHere, socketCamera);
        DrawBones(pointerHere && false == socketGrabbed);
        draw->PopClipRect();
    }

    void ModelEditorPanel::HandleCamera(Bool pointerHere)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (m_orbiting)
        {
            if (false == ImGui::IsMouseDown(ImGuiMouseButton_Right) && false == ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                m_orbiting = false;
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                // 가운데 단추는 바라보는 점을 옮긴다 - 그 깊이에서 한 픽셀이 담는 월드 길이만큼 카메라의 오른쪽·위 축으로 민다(캔버스 뷰와 같다, D-441).
                Framework3D* framework = m_editor->GetFramework3D();
                CameraParams drawn;
                if (framework != nullptr && framework->GetPreviewCamera(m_preview, drawn) && m_textureExtent.height != 0
                    && drawn.projection.values[5] != 0.0f)
                {
                    const Float perPixel = 2.0f * m_distance / (drawn.projection.values[5] * static_cast<JBro::Float>(m_textureExtent.height));
                    const Vector3 right{drawn.view.values[0], drawn.view.values[1], drawn.view.values[2]};
                    const Vector3 up{drawn.view.values[4], drawn.view.values[5], drawn.view.values[6]};
                    m_centerX += right.x * -io.MouseDelta.x * perPixel + up.x * io.MouseDelta.y * perPixel;
                    m_centerY += right.y * -io.MouseDelta.x * perPixel + up.y * io.MouseDelta.y * perPixel;
                    m_centerZ += right.z * -io.MouseDelta.x * perPixel + up.z * io.MouseDelta.y * perPixel;
                }
            }
            else
            {
                // 오른쪽 단추는 돈다.
                m_yawDegrees -= io.MouseDelta.x * OrbitDegreesPerPixel;
                m_pitchDegrees = std::clamp(Float(m_pitchDegrees - io.MouseDelta.y * OrbitDegreesPerPixel), MinPitchDegrees, MaxPitchDegrees);
            }
        }
        else if (pointerHere && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            m_orbiting = true;
        }
        if (pointerHere && io.MouseWheel != 0.0f)
        {
            // 줌은 가까이 가는 것이다. 바라보는 점은 그대로다.
            m_distance = std::clamp(Float(m_distance * std::pow(ZoomStep, -io.MouseWheel)), MinDistance, MaxDistance);
        }
    }

    Bool ModelEditorPanel::GetBoneScreenPosition(UInt32 bone, Float& x, Float& y) const
    {
        const std::size_t at = bone.Get();
        if (at >= m_boneOnScreen.Size() || false == m_boneOnScreen[at])
        {
            return false;
        }
        x = m_boneScreenX[at];
        y = m_boneScreenY[at];
        return true;
    }

    void ModelEditorPanel::DrawBones(Bool pointerHere)
    {
        m_boneScreenX.Clear();
        m_boneScreenY.Clear();
        m_boneOnScreen.Clear();
        m_hoveredBone = -1;
        GizmoCamera camera;
        if (m_boneWorld.Size() != m_boneNames.Size() || m_boneWorld.IsEmpty() || false == MakeCamera(camera))
        {
            return;
        }
        for (const Vector3& world : m_boneWorld)
        {
            Float x = 0.0f;
            Float y = 0.0f;
            const Bool onScreen = GizmoModel::Project(camera, world, x, y);
            m_boneScreenX.Add(x);
            m_boneScreenY.Add(y);
            m_boneOnScreen.Add(onScreen);
        }
        if (false == m_showBones)
        {
            return;
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        Float nearest = PickDistance * PickDistance;
        if (pointerHere && false == m_orbiting)
        {
            for (std::size_t index = 0; index < m_boneWorld.Size(); ++index)
            {
                if (false == m_boneOnScreen[index])
                {
                    continue;
                }
                const Float dx = m_boneScreenX[index] - mouse.x;
                const Float dy = m_boneScreenY[index] - mouse.y;
                const Float distance = dx * dx + dy * dy;
                if (distance <= nearest)
                {
                    nearest = distance;
                    m_hoveredBone = static_cast<Int32>(index);
                }
            }
        }
        // **뼈는 그림 위에 덧그린다** - 메시 안에 묻힌 관절도 보여야 고를 수 있다. 부모 관절에서 자식 관절로 선, 관절마다 점이다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 boneColor = IM_COL32(150, 200, 255, 200);
        const ImU32 selectedColor = IM_COL32(255, 210, 80, 255);
        const ImU32 hoveredColor = IM_COL32(255, 255, 255, 255);
        // 마스크를 골랐으면 뼈마다 가중치 색이다(D-464) - 0 은 회색, 1 은 초록.
        const Bool masked = m_selectedMask >= 0 && m_maskWeights.Size() == m_boneWorld.Size();
        for (std::size_t index = 0; index < m_boneWorld.Size(); ++index)
        {
            const Int32 parent = m_boneParents[index];
            if (parent < 0 || false == m_boneOnScreen[index] || false == m_boneOnScreen[static_cast<std::size_t>(parent.Get())])
            {
                continue;
            }
            const std::size_t from = static_cast<std::size_t>(parent.Get());
            const Bool selected = m_selectedBone == static_cast<Int32>(index);
            draw->AddLine(ImVec2(m_boneScreenX[from], m_boneScreenY[from]), ImVec2(m_boneScreenX[index], m_boneScreenY[index]),
                selected ? selectedColor : (masked ? MaskColor(m_maskWeights[index]) : boneColor), selected ? 2.5f : 1.5f);
        }
        for (std::size_t index = 0; index < m_boneWorld.Size(); ++index)
        {
            if (false == m_boneOnScreen[index])
            {
                continue;
            }
            const Bool selected = m_selectedBone == static_cast<Int32>(index);
            const Bool hovered = m_hoveredBone == static_cast<Int32>(index);
            const ImVec2 at(m_boneScreenX[index], m_boneScreenY[index]);
            draw->AddCircleFilled(at, selected ? SelectedJointRadius : JointRadius,
                selected ? selectedColor : (hovered ? hoveredColor : (masked ? MaskColor(m_maskWeights[index]) : boneColor)));
            draw->AddCircle(at, selected ? SelectedJointRadius : JointRadius, IM_COL32(0, 0, 0, 160), 0, 1.0f);
        }
        if (m_hoveredBone >= 0)
        {
            Widget::ShowTooltip(m_boneNames[static_cast<std::size_t>(m_hoveredBone.Get())].c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                PickBone(m_hoveredBone);
                m_revealBone = m_hoveredBone;
            }
        }
    }

    void ModelEditorPanel::DrawTimeline()
    {
        const AnimationClipData* data = GetClipData();
        const Float duration = data != nullptr ? data->animation.duration : Float(0.0f);
        const Bool ready = data != nullptr && duration > 0.0f;
        const Widget::DisableScope disabled(false == ready);
        // 재생 줄: 재생·멈춤, 지금 시간, 오른쪽 끝의 속도. 반복은 클립 설정이라 디테일 칸에 있다.
        if (Widget::IconButton("##model_play", m_playing ? Icons::Pause : Icons::Play)
                .Tooltip(m_playing ? Loc::TextOr(LocKeys::ModelEditorPause, "Pause") : Loc::TextOr(LocKeys::ModelEditorPlay, "Play"))
                .Draw())
        {
            SetPlaying(false == m_playing);
        }
        ImGui::SameLine(0.0f, 10.0f);
        Float time = ready ? GetTime() : Float(0.0f);
        Widget::TextF(Loc::TextOr(LocKeys::ModelEditorTime, "%.2f / %.2f s"), static_cast<double>(time.Get()), static_cast<double>(duration.Get()));
        constexpr Float SpeedWidth = 64.0f;
        ImGui::SameLine(0.0f, 0.0f);
        const Float speedLeft = ImGui::GetContentRegionAvail().x - SpeedWidth - ImGui::GetTextLineHeight() - ImGui::GetStyle().ItemSpacing.x;
        if (speedLeft > 0.0f)
        {
            ImGui::SameLine(0.0f, speedLeft.Get());
        }
        Widget::InlineIcon(Icons::Speed);
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorSpeed, "Speed"));
        Widget::DragField("##model_speed", m_speed).Range(0.1f, 3.0f).Speed(0.01f).Format("x%.2f").StepButtons(false).Width(SpeedWidth)();
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorSpeed, "Speed"));

        // 시간 축: 눈금 줄을 누르거나 끌면 그 시간으로 가고, 이벤트 줄의 표식은 끌어 옮긴다(손을 뗄 때 커맨드 하나).
        Widget::Timeline timeline("##model_timeline", ready ? duration : Float(1.0f));
        if (timeline.Ruler(time) && ready)
        {
            SetTime(time);
        }
        m_rulerTop = timeline.GetLastRowTop();
        m_rulerHeight = timeline.GetLastRowHeight();
        m_eventTimes.Clear();
        m_eventNamePointers.Clear();
        for (const ModelClipEvent& event : m_draft.events)
        {
            m_eventTimes.Add(ready ? event.time : Float(0.0f));
            m_eventNamePointers.Add(event.name.empty() ? Loc::TextOr(LocKeys::ModelEditorUnnamedEvent, "unnamed event") : event.name.c_str());
        }
        if (false == ready)
        {
            m_eventTimes.Clear();
        }
        const Widget::TimelineMarkerEdit edit = timeline.MarkerTrack("##model_events", Icons::Event, Loc::TextOr(LocKeys::ModelEditorEvents, "Events"),
            ArrayView<const Float>(m_eventTimes.Data(), m_eventTimes.Size()), ArrayView<const char* const>(m_eventNamePointers.Data(), m_eventTimes.Size()),
            m_selectedEvent, Loc::TextOr(LocKeys::ModelEditorStripTooltip, "double-click to add an event here; drag a marker to move it"));
        m_stripLeft = timeline.GetTrackLeft();
        m_stripTop = timeline.GetLastRowTop();
        m_stripWidth = timeline.GetTrackWidth();
        m_stripHeight = timeline.GetLastRowHeight();
        m_eventScreenX.Clear();
        for (const Float& each : m_eventTimes)
        {
            m_eventScreenX.Add(timeline.XAt(each));
        }
        timeline.End(ready ? GetTime() : Float(0.0f));
        if (false == ready)
        {
            return;
        }
        const std::size_t count = m_draft.events.Size();
        if (edit.pressed >= 0 && static_cast<std::size_t>(edit.pressed.Get()) < count)
        {
            m_selectedEvent = edit.pressed;
            m_focus = DetailFocus::Clip;
        }
        if (edit.dragging >= 0 && static_cast<std::size_t>(edit.dragging.Get()) < count)
        {
            // 끄는 동안은 초안만 옮긴다.
            m_draft.events[static_cast<std::size_t>(edit.dragging.Get())].time = edit.dragTime;
            m_draggingEvent = edit.dragging;
        }
        if (edit.released >= 0)
        {
            m_draggingEvent = -1;
            // 누르기만 하고 옮기지 않았으면 값이 같아 커맨드가 서지 않는다(`SetAssetFileCommand` 가 같은 글을 거절한다).
            if (static_cast<std::size_t>(edit.released.Get()) < count)
            {
                const ModelClipEvent moved = m_draft.events[static_cast<std::size_t>(edit.released.Get())];
                SetClipEvent(static_cast<UInt32>(edit.released), moved.name.c_str(), moved.time);
            }
        }
        else if (edit.add)
        {
            AddClipEvent(edit.addTime, "");
        }
    }

    Bool ModelEditorPanel::GetEventMarkerPosition(UInt32 index, Float& x, Float& y) const
    {
        if (index.Get() >= m_eventScreenX.Size() || m_stripHeight <= 0.0f)
        {
            return false;
        }
        x = m_eventScreenX[index.Get()];
        y = m_stripTop + m_stripHeight * 0.5f;
        return true;
    }

    Bool ModelEditorPanel::GetRulerRow(Float& left, Float& top, Float& width, Float& height) const
    {
        left = m_stripLeft;
        top = m_rulerTop;
        width = m_stripWidth;
        height = m_rulerHeight;
        return m_rulerHeight > 0.0f;
    }

    Bool ModelEditorPanel::GetEventStripRow(Float& left, Float& top, Float& width, Float& height) const
    {
        left = m_stripLeft;
        top = m_stripTop;
        width = m_stripWidth;
        height = m_stripHeight;
        return m_stripHeight > 0.0f;
    }

}
