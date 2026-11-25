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
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Framework3DSystem/Framework3D.h>
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
        return undone;
    }

    Bool ModelEditorPanel::Redo()
    {
        const Bool redone = m_commands.Redo();
        m_draftClip = -1;
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
        const System::Animation3DSystem::Layer& layer = state->layers[0];
        if (m_pendingTime >= 0.0f && animation->SetLayerTime(id, 0, m_pendingTime))
        {
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

        DrawToolBar();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        // 타임라인 한 줄과 그 아래 이벤트 줄(12 픽셀)이다.
        const Float timelineHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 14.0f;
        const Float bodyHeight = (std::max)(Float(60.0f), Float(available.y - timelineHeight));
        const Float sideWidth = JBro::Clamp(m_sideWidth, 140.0f, (std::max)(140.0f, available.x - 200.0f));
        // **본문은 높이가 정해진 칸 하나다** - 세로 분할 손잡이는 남은 높이를 다 잡으므로, 감싸지 않으면 아래 타임라인을 창 밖으로 밀어낸다(실제 화면에서 그랬다).
        if (false == ImGui::BeginChild("##model_body", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::EndChild();
            DrawTimeline();
            return;
        }
        if (ImGui::BeginChild("##model_side", ImVec2(sideWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            const Float sideHeight = ImGui::GetContentRegionAvail().y;
            // 처음에는 클립 설정이 다 보이게 뼈대 트리를 작게 둔다. 사람이 끌어 바꾼다.
            if (m_treeHeight <= 0.0f)
            {
                m_treeHeight = sideHeight * 0.35f;
            }
            const Float treeLimit = (std::max)(Float(60.0f), Float(sideHeight - 60.0f));
            const Float treeHeight = JBro::Clamp(m_treeHeight, Float(60.0f), treeLimit);
            if (ImGui::BeginChild("##model_bones", ImVec2(0.0f, treeHeight)))
            {
                DrawSkeleton();
            }
            ImGui::EndChild();
            Widget::Splitter("##model_side_split", false, 4.0f, &m_treeHeight, 60.0f, treeLimit);
            if (ImGui::BeginChild("##model_clips", ImVec2(0.0f, 0.0f)))
            {
                DrawClips();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##model_split", true, 4.0f, &m_sideWidth, 140.0f, (std::max)(160.0f, available.x - 200.0f));
        ImGui::SameLine(0.0f, 0.0f);
        // 미리보기 칸은 스크롤하지 않는다 - 휠은 줌이다.
        if (ImGui::BeginChild("##model_view", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            DrawViewport(ImGui::GetContentRegionAvail().y);
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
    }

    void ModelEditorPanel::DrawSkeleton()
    {
        Widget::HintText(Loc::TextOr(LocKeys::ModelEditorSkeleton, "Skeleton"));
        if (m_skeleton < 0)
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorNoSkeleton, "this model has no skeleton"));
            return;
        }
        if (m_boneNames.IsEmpty())
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorLoading, "reading the model"));
            return;
        }
        for (std::size_t index = 0; index < m_boneNames.Size(); ++index)
        {
            if (m_boneParents[index] < 0)
            {
                DrawBoneRow(static_cast<UInt32>(index));
            }
        }
        m_revealBone = -1;
    }

    void ModelEditorPanel::DrawBoneRow(UInt32 bone)
    {
        const std::size_t at = bone.Get();
        const Bool hasChildren = m_firstChild[at] >= 0;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (false == hasChildren)
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (m_selectedBone == static_cast<Int32>(bone))
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
        const Bool open = Widget::Tree(m_boneNames[at].c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && false == ImGui::IsItemToggledOpen())
        {
            SelectBone(static_cast<Int32>(bone));
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
        Widget::TreePop();
    }

    void ModelEditorPanel::DrawClips()
    {
        Widget::HintText(Loc::TextOr(LocKeys::ModelEditorClips, "Animations"));
        if (m_clipIds.IsEmpty())
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorNoClips, "this model has no animations"));
            return;
        }
        for (std::size_t index = 0; index < m_clipIds.Size(); ++index)
        {
            const Widget::IdScope scope(static_cast<Int32>(index));
            if (Widget::SelectableRow(m_clipNames[index].c_str(), m_clip == static_cast<Int32>(index)))
            {
                SelectClip(static_cast<Int32>(index));
            }
        }
        ImGui::Separator();
        DrawClipSettings();
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

    void ModelEditorPanel::DrawClipSettings()
    {
        if (m_clip < 0)
        {
            return;
        }
        SyncDraft();
        const AnimationClipData* data = GetClipData();
        const Float source = data != nullptr ? data->sourceDuration : Float(0.0f);
        Bool editing = false;
        if (Widget::Button(Loc::TextOr(LocKeys::ModelEditorSplit, "Split")))
        {
            SplitClip();
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorSplitTooltip, "make a new clip from the same source animation and range"));
        if (m_draftSplit)
        {
            ImGui::SameLine(0.0f, 6.0f);
            if (Widget::Button(Loc::TextOr(LocKeys::ModelEditorDeleteClip, "Delete Clip")))
            {
                DeleteClip();
                return;
            }
        }
        // 안내는 폼 밖에 둔다 - 폼의 한 칸에 두면 그 칸의 폭으로 접혀 낱말마다 줄이 바뀐다.
        if (false == m_draftSplit)
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorImportedClip, "imported from the model file"));
        }
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
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ModelEditorClipSource, "Source")), [&]() {
                Widget::Text(m_draft.source.c_str());
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
        layout.Break([&]() {
            Widget::HintText(Loc::TextOr(LocKeys::ModelEditorEvents, "Events"));
            ImGui::SameLine(0.0f, 8.0f);
            if (Widget::Button(Loc::TextOr(LocKeys::ModelEditorAddEvent, "Add Event")))
            {
                AddClipEvent(GetTime(), "");
            }
        });
        if (m_draft.events.IsEmpty())
        {
            layout.Break([&]() {
                Widget::WrappedHintText(Loc::TextOr(LocKeys::ModelEditorNoEvents, "no events - double-click the strip under the timeline to add one"));
            });
        }
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
            layout.Break([&]() {
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
        Widget::HitArea("##model_preview", available,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const Bool pointerHere = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        HandleCamera(pointerHere);
        draw->PushClipRect(origin, ImVec2(origin.x + available.x, origin.y + available.y), true);
        DrawBones(pointerHere);
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
                selected ? selectedColor : boneColor, selected ? 2.5f : 1.5f);
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
            draw->AddCircleFilled(at, selected ? SelectedJointRadius : JointRadius, selected ? selectedColor : (hovered ? hoveredColor : boneColor));
            draw->AddCircle(at, selected ? SelectedJointRadius : JointRadius, IM_COL32(0, 0, 0, 160), 0, 1.0f);
        }
        if (m_hoveredBone >= 0)
        {
            Widget::ShowTooltip(m_boneNames[static_cast<std::size_t>(m_hoveredBone.Get())].c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                SelectBone(m_hoveredBone);
                m_revealBone = m_hoveredBone;
            }
        }
    }

    void ModelEditorPanel::DrawTimeline()
    {
        const AnimationClipData* data = GetClipData();
        const Float duration = data != nullptr ? data->animation.duration : Float(0.0f);
        const Widget::DisableScope disabled(data == nullptr || false == (duration > 0.0f));
        if (Widget::IconButton("##model_play", m_playing ? Icons::Pause : Icons::Play)
                .Tooltip(m_playing ? Loc::TextOr(LocKeys::ModelEditorPause, "Pause") : Loc::TextOr(LocKeys::ModelEditorPlay, "Play"))
                .Draw())
        {
            SetPlaying(false == m_playing);
        }
        ImGui::SameLine(0.0f, 12.0f);
        // 반복은 클립 설정이다(D-462) - 누르면 메타 커맨드다.
        Bool loop = m_draft.loop;
        if (Widget::Checkbox("##model_loop", loop))
        {
            SetClipLoop(loop);
        }
        ImGui::SameLine(0.0f, 4.0f);
        Widget::Text(Loc::TextOr(LocKeys::ModelEditorLoop, "Loop"));
        ImGui::SameLine(0.0f, 12.0f);
        Widget::Text(Loc::TextOr(LocKeys::ModelEditorSpeed, "Speed"));
        ImGui::SameLine(0.0f, 4.0f);
        Widget::SliderField("##model_speed", m_speed, 0.1f, 3.0f).Format("%.2f").Width(90.0f)();
        ImGui::SameLine(0.0f, 12.0f);
        Float time = GetTime();
        char label[64] = {};
        std::snprintf(label, sizeof(label), Loc::TextOr(LocKeys::ModelEditorTime, "%.2f / %.2f s"), static_cast<double>(time.Get()), static_cast<double>(duration.Get()));
        const Float labelWidth = ImGui::CalcTextSize(label).x;
        const Float sliderWidth = (std::max)(Float(80.0f), Float(ImGui::GetContentRegionAvail().x - labelWidth - 12.0f));
        if (Widget::SliderField("##model_time", time, 0.0f, (std::max)(duration, Float(0.001f))).Format("%.2f").Width(sliderWidth)())
        {
            SetTime(time);
        }
        const ImVec2 sliderMin = ImGui::GetItemRectMin();
        const ImVec2 sliderMax = ImGui::GetItemRectMax();
        ImGui::SameLine(0.0f, 8.0f);
        Widget::HintText(label);
        // 이벤트 줄은 시간 막대 바로 아래, 같은 폭이다 - 표식이 막대의 그 시간 아래에 선다.
        if (data != nullptr && duration > 0.0f)
        {
            ImGui::SetCursorScreenPos(ImVec2(sliderMin.x, sliderMax.y + 2.0f));
            DrawEventStrip(sliderMin.x, sliderMax.x - sliderMin.x, duration);
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

    Bool ModelEditorPanel::GetEventStripRow(Float& left, Float& top, Float& width, Float& height) const
    {
        left = m_stripLeft;
        top = m_stripTop;
        width = m_stripWidth;
        height = m_stripHeight;
        return m_stripHeight > 0.0f;
    }

    void ModelEditorPanel::DrawEventStrip(Float left, Float width, Float duration)
    {
        // 막대의 손잡이가 움직이는 폭과 맞춘다(ImGui 슬라이더: 양쪽 2 픽셀 + 손잡이 절반).
        const Float grab = ImGui::GetStyle().GrabMinSize;
        const Float usableLeft = left + 2.0f + grab * 0.5f;
        const Float usableWidth = (std::max)(Float(1.0f), Float(width - 4.0f - grab));
        const Float height = 12.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        m_stripLeft = left;
        m_stripTop = origin.y;
        m_stripWidth = width;
        m_stripHeight = height;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(255, 255, 255, 14), 2.0f);
        const auto timeAt = [&](Float x) {
            const Float t = (x - usableLeft) / usableWidth * duration;
            return JBro::Clamp(t, Float(0.0f), duration);
        };
        // 빈 자리를 두 번 누르면 그 시간에 이벤트를 더한다. 표식이 위에 서도록 겹침을 허락한다.
        ImGui::SetNextItemAllowOverlap();
        Widget::HitArea("##event_strip", ImVec2(width, height));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            AddClipEvent(timeAt(ImGui::GetIO().MousePos.x), "");
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::ModelEditorStripTooltip, "double-click to add an event here; drag a marker to move it"));
        m_eventScreenX.Clear();
        Bool dragging = false;
        // 놓은 표식의 커맨드는 반복 뒤에 실행한다(실행이 초안 배열을 다시 잡는다).
        Int32 released = -1;
        for (std::size_t index = 0; index < m_draft.events.Size(); ++index)
        {
            ModelClipEvent& event = m_draft.events[index];
            const Float x = usableLeft + JBro::Clamp(event.time / duration, Float(0.0f), Float(1.0f)) * usableWidth;
            m_eventScreenX.Add(x);
            const Widget::IdScope scope(static_cast<Int32>(index));
            ImGui::SetCursorScreenPos(ImVec2(x - 5.0f, origin.y));
            Widget::HitArea("##event_marker", ImVec2(10.0f, height));
            if (ImGui::IsItemActivated())
            {
                m_selectedEvent = static_cast<Int32>(index);
            }
            if (ImGui::IsItemActive())
            {
                // 끄는 동안은 초안만 옮긴다. 손을 떼면 커맨드 하나다.
                event.time = timeAt(ImGui::GetIO().MousePos.x);
                m_draggingEvent = static_cast<Int32>(index);
                dragging = true;
            }
            if (ImGui::IsItemDeactivated() && m_draggingEvent == static_cast<Int32>(index))
            {
                m_draggingEvent = -1;
                released = static_cast<Int32>(index);
            }
            if (ImGui::IsItemHovered())
            {
                Widget::ShowTooltip(event.name.empty() ? Loc::TextOr(LocKeys::ModelEditorUnnamedEvent, "unnamed event") : event.name.c_str());
            }
            const Bool selected = m_selectedEvent == static_cast<Int32>(index);
            const ImU32 color = selected ? IM_COL32(255, 210, 80, 255) : IM_COL32(150, 200, 255, 220);
            draw->AddTriangleFilled(ImVec2(x - 4.0f, origin.y + 1.0f), ImVec2(x + 4.0f, origin.y + 1.0f), ImVec2(x, origin.y + height - 1.0f), color);
        }
        if (false == dragging && m_draggingEvent >= 0 && false == ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_draggingEvent = -1;
        }
        if (released >= 0 && static_cast<std::size_t>(released.Get()) < m_draft.events.Size())
        {
            const ModelClipEvent moved = m_draft.events[static_cast<std::size_t>(released.Get())];
            SetClipEvent(static_cast<UInt32>(released), moved.name.c_str(), moved.time);
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

}
