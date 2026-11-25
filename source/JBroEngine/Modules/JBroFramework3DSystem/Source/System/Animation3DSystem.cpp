#include <JBro/Framework3DSystem/System/Animation3DSystem.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Core/Log.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace JBro::System
{
    namespace
    {
        // 같은 크기의 자세를 옮긴다 - 배열 대입은 다시 잡을 수 있어 프레임 경로에서는 칸마다 옮긴다.
        void CopyPose(const Array<BonePose>& from, Array<BonePose>& to)
        {
            const std::size_t count = from.Size() < to.Size() ? from.Size() : to.Size();
            for (std::size_t bone = 0; bone < count; ++bone)
            {
                to[bone] = from[bone];
            }
        }

        Bool SameHandle(AssetHandle left, AssetHandle right)
        {
            return left.index == right.index && left.generation == right.generation;
        }

        Bool SameText(const String& text, std::string_view name)
        {
            return std::string_view(text.data(), text.size()) == name;
        }

        // **이 애니메이터를 따르는 오브젝트**를 깊이 먼저 돈다(D-460): 애니메이터 오브젝트와 자식들이고, 제 `Animator3D` 를 든 자식 아래는
        // 다른 애니메이터의 것이라 내려가지 않는다(스킨 메시가 가장 가까운 조상의 애니메이터를 따르는 것과 같다, D-458). 할당하지 않는다.
        template <typename TVisit>
        void VisitFollowers(Canvas& canvas, Object::GameObject* root, TVisit&& visit)
        {
            Object::GameObject* stack[64];
            UInt32 depth = 0;
            if (root != nullptr)
            {
                stack[depth.Get()] = root;
                ++depth;
            }
            while (depth > 0)
            {
                --depth;
                Object::GameObject* object = stack[depth.Get()];
                if (object != root && canvas.FindComponentRaw<Component::Animator3D>(object) != nullptr)
                {
                    continue;
                }
                visit(*object);
                const Array<SafePtr<Object::GameObject>>& children = object->GetChildren();
                for (std::size_t at = children.Size(); at > 0; --at)
                {
                    Object::GameObject* child = children[at - 1].TryGet();
                    if (depth < 64 && child != nullptr)
                    {
                        stack[depth.Get()] = child;
                        ++depth;
                    }
                }
            }
        }

        // 소켓 목록에서 그 이름을 찾아 뼈 번호로 푼다. 이름이 같은 소켓이 여럿이면 처음 것이다.
        Bool FindNamedSocket(const Array<ModelSocket>& sockets, const ModelSkeleton& skeleton, std::string_view name,
            Animation3DSystem::ResolvedSocket& resolved)
        {
            for (std::size_t index = 0; index < sockets.Size(); ++index)
            {
                const ModelSocket& socket = sockets[index];
                if (socket.name.empty() || false == SameText(socket.name, name))
                {
                    continue;
                }
                const Int32 bone = FindBone(skeleton, std::string_view(socket.bone.data(), socket.bone.size()));
                if (bone < 0)
                {
                    Log::Write(LogLevel::Warning, "animation", "socket %s points at a bone the skeleton does not have: %s",
                        socket.name.c_str(), socket.bone.c_str());
                    return false;
                }
                resolved.bone = bone;
                resolved.position = socket.position;
                resolved.rotation = socket.rotation;
                resolved.scale = socket.scale;
                return true;
            }
            return false;
        }
    }

    Int32 Animation3DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Animation3DSystem::SetAssets(AssetSystem* assets)
    {
        m_assets = assets;
    }

    const Animation3DSystem::State* Animation3DSystem::FindState(InstanceId animator) const
    {
        const UInt32* index = m_stateOf.Find(animator);
        return index != nullptr ? &m_states[index->Get()] : nullptr;
    }

    Animation3DSystem::State* Animation3DSystem::FindState(InstanceId animator)
    {
        const UInt32* index = m_stateOf.Find(animator);
        return index != nullptr ? &m_states[index->Get()] : nullptr;
    }

    const Array<Matrix4x4>* Animation3DSystem::FindSkinMatrices(InstanceId animator) const
    {
        const State* state = FindState(animator);
        return state != nullptr && state->boneCount > 0 ? &state->skin : nullptr;
    }

    AssetHandle Animation3DSystem::FindSkeleton(Canvas& canvas, const Component::Animator3D& animator) const
    {
        if (animator.skeleton.generation != 0)
        {
            return animator.skeleton;
        }
        if (m_assets == nullptr)
        {
            return {};
        }
        // 이 오브젝트와 자식들의 스킨 메시가 잡은 뼈대다. 깊이 먼저, 처음 만난 것이다.
        Object::GameObject* owner = Internal::CanvasAccess::GetOwner(animator);
        Object::GameObject* stack[64];
        UInt32 depth = 0;
        if (owner != nullptr)
        {
            stack[depth.Get()] = owner;
            ++depth;
        }
        while (depth > 0)
        {
            --depth;
            Object::GameObject* object = stack[depth.Get()];
            if (const auto* renderer = canvas.FindComponentRaw<Component::MeshRenderer3D>(object))
            {
                const MeshData* mesh = renderer->mesh.generation != 0 ? m_assets->GetMesh(renderer->mesh) : nullptr;
                if (mesh != nullptr && mesh->skeletonAsset.generation != 0)
                {
                    return mesh->skeletonAsset;
                }
            }
            const Array<SafePtr<Object::GameObject>>& children = object->GetChildren();
            for (std::size_t at = children.Size(); at > 0; --at)
            {
                Object::GameObject* child = children[at - 1].TryGet();
                if (depth < 64 && child != nullptr)
                {
                    stack[depth.Get()] = child;
                    ++depth;
                }
            }
        }
        return {};
    }

    Bool Animation3DSystem::BindLayerClip(const State& state, AssetHandle clip, UInt32& generation, Array<Int32>& tracks)
    {
        const SkeletonData* skeleton = m_assets != nullptr ? m_assets->GetSkeleton(state.skeleton) : nullptr;
        const AnimationClipData* data = m_assets != nullptr && clip.generation != 0 ? m_assets->GetAnimationClip(clip) : nullptr;
        if (skeleton == nullptr || data == nullptr)
        {
            generation = 0;
            tracks.Clear();
            return false;
        }
        BindClipTracks(skeleton->skeleton, data->animation, tracks);
        generation = data->dataGeneration;
        return true;
    }

    Bool Animation3DSystem::Prepare(State& state, AssetHandle skeleton)
    {
        const SkeletonData* data = m_assets != nullptr && skeleton.generation != 0 ? m_assets->GetSkeleton(skeleton) : nullptr;
        if (data == nullptr || data->skeleton.bones.IsEmpty())
        {
            state.boneCount = 0;
            return false;
        }
        const std::size_t bones = data->skeleton.bones.Size();
        state.skeleton = skeleton;
        state.skeletonGeneration = data->dataGeneration;
        state.boneCount = static_cast<UInt32>(bones);
        state.rest.Resize(bones);
        state.pose.Resize(bones);
        state.sample.Resize(bones);
        state.blend.Resize(bones);
        state.model.Resize(bones);
        state.skin.Resize(bones);
        MakeRestPose(data->skeleton, state.rest);
        CopyPose(state.rest, state.pose);
        // 뼈대가 바뀌면 트랙과 마스크의 뼈 번호가 바뀐다 - 다시 묶는다.
        for (Layer& layer : state.layers)
        {
            BindLayerClip(state, layer.clip, layer.clipGeneration, layer.tracks);
            BindLayerClip(state, layer.fromClip, layer.fromGeneration, layer.fromTracks);
            if (layer.maskRoot >= 0 || false == layer.mask.IsEmpty())
            {
                BuildMaskWeights(data->skeleton, layer.maskRoot, layer.maskDepth, layer.mask);
            }
        }
        ComputeBoneMatrices(data->skeleton, state.pose, state.model);
        ComputeSkinMatrices(data->skeleton, state.model, state.skin);
        return true;
    }

    void Animation3DSystem::Evaluate(State& state, const Component::Animator3D& animator, Float deltaTime)
    {
        const SkeletonData* skeleton = m_assets->GetSkeleton(state.skeleton);
        if (skeleton == nullptr)
        {
            return;
        }
        const Float step = deltaTime * animator.speed;
        CopyPose(state.rest, state.pose);
        for (UInt32 index = 0; index < state.layers.Size(); ++index)
        {
            Layer& layer = state.layers[index.Get()];
            const AnimationClipData* clip = layer.clip.generation != 0 ? m_assets->GetAnimationClip(layer.clip) : nullptr;
            if (clip == nullptr || false == layer.playing)
            {
                continue;
            }
            // 클립이 다시 읽혔으면 다시 묶는다(드물다).
            if (clip->dataGeneration != layer.clipGeneration)
            {
                BindLayerClip(state, layer.clip, layer.clipGeneration, layer.tracks);
            }
            layer.time = layer.time + step * layer.speed;
            const Float time = WrapClipTime(layer.time, clip->animation.duration, layer.loop);
            // 덮어쓰기는 지금 자세에서 시작해 트랙이 닿는 뼈만 바꾼다 - 클립에 없는 뼈는 아래 레이어의 것이 남는다. 더하기는 쉬는 자세 기준의 차다.
            CopyPose(layer.mode == AnimatorLayerMode::Additive ? state.rest : state.pose, state.sample);
            SampleClip(clip->animation, layer.tracks, time, state.sample);
            if (layer.fade > 0.0f && layer.fadeElapsed < layer.fade)
            {
                const AnimationClipData* from = layer.fromClip.generation != 0 ? m_assets->GetAnimationClip(layer.fromClip) : nullptr;
                if (from != nullptr)
                {
                    layer.fromTime = layer.fromTime + step * layer.speed;
                    CopyPose(layer.mode == AnimatorLayerMode::Additive ? state.rest : state.pose, state.blend);
                    SampleClip(from->animation, layer.fromTracks, WrapClipTime(layer.fromTime, from->animation.duration, layer.fromLoop), state.blend);
                    // 앞 클립에서 새 클립으로 넘어가는 비율이다.
                    const Float t = layer.fadeElapsed / layer.fade;
                    const Array<Float> none;
                    BlendPose(state.blend, state.sample, t, none);
                    CopyPose(state.blend, state.sample);
                }
                layer.fadeElapsed = layer.fadeElapsed + step;
            }
            if (layer.mode == AnimatorLayerMode::Additive)
            {
                AddPose(state.pose, state.sample, state.rest, layer.weight, layer.mask);
            }
            else if (index == 0)
            {
                // 레이어 0 은 전신 바탕이다 - 가중치를 보지 않는다.
                CopyPose(state.sample, state.pose);
            }
            else
            {
                BlendPose(state.pose, state.sample, layer.weight, layer.mask);
            }
        }
        ComputeBoneMatrices(skeleton->skeleton, state.pose, state.model);
        ComputeSkinMatrices(skeleton->skeleton, state.model, state.skin);
    }

    void Animation3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        m_canvas = &canvas;
        if (m_assets == nullptr)
        {
            return;
        }
        ++m_stamp;
        canvas.ForEach<Component::Animator3D>([&](Component::Animator3D& animator)
        {
            if (false == animator.IsActiveComponent())
            {
                return;
            }
            const InstanceId instance = animator.GetInstanceId();
            State* state = FindState(instance);
            if (state == nullptr)
            {
                // 처음 만난 애니메이터다. 상태와 레이어 칸을 여기서 한 번 잡는다.
                m_stateOf.TryAdd(instance, static_cast<UInt32>(m_states.Size()));
                m_states.Add(State{});
                state = &m_states[m_states.Size() - 1];
                state->instance = instance;
                state->layers.Resize(MaxAnimatorLayers.Get());
            }
            state->stamp = m_stamp;
            const AssetHandle skeleton = FindSkeleton(canvas, animator);
            const SkeletonData* skeletonData = skeleton.generation != 0 ? m_assets->GetSkeleton(skeleton) : nullptr;
            if (skeletonData == nullptr)
            {
                return;
            }
            if (state->boneCount == 0 || false == SameHandle(state->skeleton, skeleton) || state->skeletonGeneration != skeletonData->dataGeneration)
            {
                if (false == Prepare(*state, skeleton))
                {
                    return;
                }
            }
            // 레이어 0 의 시작 클립이다(`animationClips[startClip]`). 처음 서면 재생한다.
            if (false == state->started)
            {
                state->started = true;
                const Int32 start = animator.startClip;
                if (animator.playOnStart && start >= 0 && static_cast<std::size_t>(start.Get()) < animator.animationClips.Size()
                    && animator.animationClips[static_cast<std::size_t>(start.Get())].generation != 0)
                {
                    Layer& base = state->layers[0];
                    base.clip = animator.animationClips[static_cast<std::size_t>(start.Get())];
                    base.clipIndex = start;
                    BindLayerClip(*state, base.clip, base.clipGeneration, base.tracks);
                    base.time = 0.0f;
                    base.loop = animator.loop;
                    base.playing = true;
                    base.fade = 0.0f;
                }
            }
            ApplyCommands(*state, animator);
            Evaluate(*state, animator, deltaTime);
            WriteBack(*state, animator);
        });
        // 사라진 애니메이터의 상태를 치운다(드물다). 뒤에서 앞으로 맨 끝 것과 바꿔 지운다.
        for (std::size_t index = m_states.Size(); index > 0; --index)
        {
            if (m_states[index - 1].stamp == m_stamp)
            {
                continue;
            }
            const std::size_t last = m_states.Size() - 1;
            m_stateOf.Remove(m_states[index - 1].instance);
            if (index - 1 != last)
            {
                m_states[index - 1] = std::move(m_states[last]);
                if (UInt32* moved = m_stateOf.Find(m_states[index - 1].instance))
                {
                    *moved = static_cast<UInt32>(index - 1);
                }
            }
            m_states.RemoveAt(last);
        }
    }

    void Animation3DSystem::ApplyCommands(State& state, Component::Animator3D& animator)
    {
        const UInt32 count = animator.m_commandCount;
        animator.m_commandCount = 0;
        for (UInt32 at = 0; at < count; ++at)
        {
            const Component::Animator3D::Command& command = animator.m_commands[at.Get()];
            if (command.layer >= state.layers.Size())
            {
                continue;
            }
            Layer& layer = state.layers[command.layer.Get()];
            const Bool known = command.clip >= 0 && static_cast<std::size_t>(command.clip.Get()) < animator.animationClips.Size();
            const AssetHandle clip = known ? animator.animationClips[static_cast<std::size_t>(command.clip.Get())] : AssetHandle{};
            switch (command.kind)
            {
            case Component::Animator3D::CommandKind::Play:
            case Component::Animator3D::CommandKind::CrossFade:
            {
                if (clip.generation == 0)
                {
                    break;
                }
                if (command.kind == Component::Animator3D::CommandKind::CrossFade && layer.playing && layer.clip.generation != 0 && command.value > 0.0f)
                {
                    layer.fromClip = layer.clip;
                    layer.fromGeneration = layer.clipGeneration;
                    std::swap(layer.fromTracks, layer.tracks);
                    layer.fromTime = layer.time;
                    layer.fromLoop = layer.loop;
                    layer.fade = command.value;
                    layer.fadeElapsed = 0.0f;
                }
                else
                {
                    layer.fade = 0.0f;
                    layer.fadeElapsed = 0.0f;
                }
                layer.clip = clip;
                layer.clipIndex = command.clip;
                BindLayerClip(state, clip, layer.clipGeneration, layer.tracks);
                layer.time = 0.0f;
                layer.loop = command.flag;
                layer.playing = true;
                break;
            }
            case Component::Animator3D::CommandKind::Stop:
                layer.playing = false;
                layer.fade = 0.0f;
                break;
            case Component::Animator3D::CommandKind::SetWeight:
                layer.weight = command.value < 0.0f ? Float(0.0f) : (command.value > 1.0f ? Float(1.0f) : command.value);
                break;
            case Component::Animator3D::CommandKind::SetAdditive:
                layer.mode = command.flag ? AnimatorLayerMode::Additive : AnimatorLayerMode::Override;
                break;
            case Component::Animator3D::CommandKind::SetMask:
                // 이름은 명령마다 한 번 번호로 푼다 - 프레임마다 견주지 않는다.
                SetLayerMask(state.instance, command.layer, std::string_view(command.bone, std::strlen(command.bone)),
                    static_cast<UInt32>(static_cast<std::uint32_t>(command.value.Get())));
                break;
            }
        }
    }

    void Animation3DSystem::WriteBack(const State& state, Component::Animator3D& animator) const
    {
        for (std::size_t at = 0; at < Component::Animator3D::LayerCount && at < state.layers.Size(); ++at)
        {
            const Layer& layer = state.layers[at];
            const AnimationClipData* clip = layer.clip.generation != 0 && m_assets != nullptr ? m_assets->GetAnimationClip(layer.clip) : nullptr;
            animator.m_layerTime[at] = clip != nullptr ? WrapClipTime(layer.time, clip->animation.duration, layer.loop) : Float(0.0f);
            // 한 번 재생은 끝을 지나면 서 있다 - 재생 중이 아니다.
            animator.m_layerPlaying[at] = layer.playing && clip != nullptr && (layer.loop || layer.time < clip->animation.duration);
            animator.m_layerClip[at] = layer.playing ? layer.clipIndex : Int32(-1);
        }
    }

    void Animation3DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        m_states.Clear();
        m_stateOf.Clear();
        m_canvas = nullptr;
    }

    UInt32 Animation3DSystem::GetSocketSourceRevision(Canvas& canvas, const Component::Animator3D& animator) const
    {
        const State* state = FindState(animator.GetInstanceId());
        if (state == nullptr || state->boneCount == 0 || m_assets == nullptr)
        {
            return 0;
        }
        // 정수 몇 개를 섞는다. 같은 판이 우연히 나올 수 있지만 판은 뼈대·메시 세대가 오를 때만 바뀌므로 겹칠 일이 드물다.
        UInt32 revision = state->skeleton.index * 2654435761u + state->skeleton.generation * 40503u + state->skeletonGeneration;
        UInt32 meshes = 0;
        VisitFollowers(canvas, Internal::CanvasAccess::GetOwner(animator), [&](Object::GameObject& object)
        {
            const auto* renderer = canvas.FindComponentRaw<Component::MeshRenderer3D>(&object);
            if (renderer == nullptr || renderer->mesh.generation == 0)
            {
                return;
            }
            const MeshData* mesh = m_assets->GetMesh(renderer->mesh);
            ++meshes;
            revision = revision * 31u + renderer->mesh.index * 7u + renderer->mesh.generation + (mesh != nullptr ? mesh->dataGeneration : UInt32(0));
        });
        return revision * 31u + meshes + 1u;
    }

    Bool Animation3DSystem::ResolveSocket(Canvas& canvas, const Component::Animator3D& animator, std::string_view name, ResolvedSocket& socket) const
    {
        const State* state = FindState(animator.GetInstanceId());
        const SkeletonData* skeleton = state != nullptr && state->boneCount > 0 && m_assets != nullptr ? m_assets->GetSkeleton(state->skeleton) : nullptr;
        if (skeleton == nullptr || name.empty())
        {
            return false;
        }
        socket = ResolvedSocket{};
        // 1. 뼈대 소켓 - 이 뼈대를 쓰는 모든 메시의 것이다.
        if (FindNamedSocket(skeleton->sockets, skeleton->skeleton, name, socket))
        {
            return true;
        }
        // 2. 이 애니메이터를 따르는 메시의 소켓.
        Bool found = false;
        VisitFollowers(canvas, Internal::CanvasAccess::GetOwner(animator), [&](Object::GameObject& object)
        {
            const auto* renderer = found ? nullptr : canvas.FindComponentRaw<Component::MeshRenderer3D>(&object);
            const MeshData* mesh = renderer != nullptr && renderer->mesh.generation != 0 ? m_assets->GetMesh(renderer->mesh) : nullptr;
            if (mesh != nullptr && FindNamedSocket(mesh->sockets, skeleton->skeleton, name, socket))
            {
                found = true;
            }
        });
        if (found)
        {
            return true;
        }
        // 3. 뼈 이름 - 소켓이 없어도 뼈를 그대로 고른다(오프셋 없음).
        socket = ResolvedSocket{};
        socket.bone = FindBone(skeleton->skeleton, name);
        return socket.bone >= 0;
    }

    Bool Animation3DSystem::ComputeSocketWorld(Canvas& canvas, const Component::Animator3D& animator, const ResolvedSocket& socket,
        Vector3& position, Quaternion& rotation, Vector3& scale) const
    {
        const State* state = FindState(animator.GetInstanceId());
        if (state == nullptr || state->boneCount == 0 || socket.bone < 0 || static_cast<std::size_t>(socket.bone.Get()) >= state->model.Size())
        {
            return false;
        }
        const auto* place = canvas.FindComponentRaw<Component::Transform3D>(Internal::CanvasAccess::GetOwner(animator));
        if (place == nullptr || false == place->worldValid)
        {
            return false;
        }
        Vector3 bonePosition;
        Quaternion boneRotation;
        Vector3 boneScale;
        DecomposeTransformMatrix3D(state->model[static_cast<std::size_t>(socket.bone.Get())], bonePosition, boneRotation, boneScale);
        Vector3 jointPosition;
        Quaternion jointRotation;
        Vector3 jointScale;
        ComposeTransform3D(place->worldPosition, place->worldRotation, place->worldScale, bonePosition, boneRotation, boneScale,
            jointPosition, jointRotation, jointScale);
        ComposeTransform3D(jointPosition, jointRotation, jointScale, socket.position, socket.rotation, socket.scale, position, rotation, scale);
        return true;
    }

    void Animation3DSystem::CollectSocketNames(Canvas& canvas, const Component::Animator3D& animator, Array<String>& names) const
    {
        names.Clear();
        const State* state = FindState(animator.GetInstanceId());
        const SkeletonData* skeleton = state != nullptr && state->boneCount > 0 && m_assets != nullptr ? m_assets->GetSkeleton(state->skeleton) : nullptr;
        if (skeleton == nullptr)
        {
            return;
        }
        const auto add = [&](const String& name) {
            if (name.empty())
            {
                return;
            }
            for (std::size_t index = 0; index < names.Size(); ++index)
            {
                if (names[index] == name)
                {
                    return;
                }
            }
            names.Add(name);
        };
        for (std::size_t index = 0; index < skeleton->sockets.Size(); ++index)
        {
            add(skeleton->sockets[index].name);
        }
        VisitFollowers(canvas, Internal::CanvasAccess::GetOwner(animator), [&](Object::GameObject& object)
        {
            const auto* renderer = canvas.FindComponentRaw<Component::MeshRenderer3D>(&object);
            const MeshData* mesh = renderer != nullptr && renderer->mesh.generation != 0 ? m_assets->GetMesh(renderer->mesh) : nullptr;
            for (std::size_t index = 0; mesh != nullptr && index < mesh->sockets.Size(); ++index)
            {
                add(mesh->sockets[index].name);
            }
        });
        for (std::size_t index = 0; index < skeleton->skeleton.bones.Size(); ++index)
        {
            add(skeleton->skeleton.bones[index].name);
        }
    }

    Int32 Animation3DSystem::FindSocket(const Component::Animator3D& animator, const char* name)
    {
        State* state = FindState(animator.GetInstanceId());
        if (state == nullptr || m_canvas == nullptr || name == nullptr || *name == '\0')
        {
            return -1;
        }
        const std::string_view wanted(name);
        for (std::size_t index = 0; index < state->scriptSockets.Size(); ++index)
        {
            if (SameText(state->scriptSockets[index].name, wanted))
            {
                return static_cast<Int32>(index);
            }
        }
        ScriptSocket entry;
        entry.revision = GetSocketSourceRevision(*m_canvas, animator);
        entry.found = ResolveSocket(*m_canvas, animator, wanted, entry.socket);
        // 자세가 아직 서지 않았으면(판 0) 번호를 주고 나중에 푼다. 섰는데 없는 이름이면 -1 이다.
        if (false == entry.found && entry.revision != 0)
        {
            return -1;
        }
        entry.name.assign(name);
        state->scriptSockets.Add(std::move(entry));
        return static_cast<Int32>(state->scriptSockets.Size() - 1);
    }

    Bool Animation3DSystem::GetSocketTransform(const Component::Animator3D& animator, Int32 socket, Vector3& position, Quaternion& rotation, Vector3& scale)
    {
        State* state = FindState(animator.GetInstanceId());
        if (state == nullptr || m_canvas == nullptr || socket < 0 || static_cast<std::size_t>(socket.Get()) >= state->scriptSockets.Size())
        {
            return false;
        }
        ScriptSocket& entry = state->scriptSockets[static_cast<std::size_t>(socket.Get())];
        const UInt32 revision = GetSocketSourceRevision(*m_canvas, animator);
        if (revision != entry.revision)
        {
            // 뼈대·소켓이 다시 읽혔다. 같은 이름으로 다시 푼다(드물다).
            entry.revision = revision;
            entry.found = ResolveSocket(*m_canvas, animator, std::string_view(entry.name.data(), entry.name.size()), entry.socket);
        }
        if (false == entry.found)
        {
            return false;
        }
        return ComputeSocketWorld(*m_canvas, animator, entry.socket, position, rotation, scale);
    }

    Bool Animation3DSystem::Play(InstanceId animator, UInt32 layer, AssetHandle clip, Bool loop)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size() || clip.generation == 0)
        {
            return false;
        }
        Layer& target = state->layers[layer.Get()];
        target.clip = clip;
        BindLayerClip(*state, clip, target.clipGeneration, target.tracks);
        target.time = 0.0f;
        target.loop = loop;
        target.playing = true;
        target.fade = 0.0f;
        target.fadeElapsed = 0.0f;
        return true;
    }

    Bool Animation3DSystem::SetLayerTime(InstanceId animator, UInt32 layer, Float time)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size() || false == std::isfinite(time))
        {
            return false;
        }
        Layer& target = state->layers[layer.Get()];
        target.time = time;
        target.fade = 0.0f;
        target.fadeElapsed = 0.0f;
        return true;
    }

    Bool Animation3DSystem::CrossFade(InstanceId animator, UInt32 layer, AssetHandle clip, Float seconds, Bool loop)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size() || clip.generation == 0)
        {
            return false;
        }
        Layer& target = state->layers[layer.Get()];
        if (false == target.playing || target.clip.generation == 0 || false == (seconds > 0.0f))
        {
            return Play(animator, layer, clip, loop);
        }
        // 지금 클립이 앞 클립이 된다. 트랙 배열은 맞바꿔 다시 잡지 않는다.
        target.fromClip = target.clip;
        target.fromGeneration = target.clipGeneration;
        std::swap(target.fromTracks, target.tracks);
        target.fromTime = target.time;
        target.fromLoop = target.loop;
        target.clip = clip;
        BindLayerClip(*state, clip, target.clipGeneration, target.tracks);
        target.time = 0.0f;
        target.loop = loop;
        target.fade = seconds;
        target.fadeElapsed = 0.0f;
        return true;
    }

    Bool Animation3DSystem::SetLayerWeight(InstanceId animator, UInt32 layer, Float weight)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size())
        {
            return false;
        }
        state->layers[layer.Get()].weight = weight < 0.0f ? Float(0.0f) : (weight > 1.0f ? Float(1.0f) : weight);
        return true;
    }

    Bool Animation3DSystem::SetLayerMode(InstanceId animator, UInt32 layer, AnimatorLayerMode mode)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size())
        {
            return false;
        }
        state->layers[layer.Get()].mode = mode;
        return true;
    }

    Bool Animation3DSystem::SetLayerMask(InstanceId animator, UInt32 layer, std::string_view rootBone, UInt32 blendDepth)
    {
        State* state = FindState(animator);
        if (state == nullptr || layer >= state->layers.Size())
        {
            return false;
        }
        Layer& target = state->layers[layer.Get()];
        if (rootBone.empty())
        {
            target.maskRoot = -1;
            target.mask.Clear();
            return true;
        }
        const SkeletonData* skeleton = m_assets != nullptr ? m_assets->GetSkeleton(state->skeleton) : nullptr;
        if (skeleton == nullptr)
        {
            return false;
        }
        const Int32 root = FindBone(skeleton->skeleton, rootBone);
        if (root < 0)
        {
            return false;
        }
        target.maskRoot = root;
        target.maskDepth = blendDepth;
        BuildMaskWeights(skeleton->skeleton, root, blendDepth, target.mask);
        return true;
    }
}
