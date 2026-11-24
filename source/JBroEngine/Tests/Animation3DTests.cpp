#include "TestCases.h"
#include "TestModels.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/ModelImport.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/BoneAttachment3D.h>
#include <JBro/Framework3D/Handle/BoneAttachment3D.h>
#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/Service/Animation3DService.h>
#include <JBro/Framework3D/Handle/Animator3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
#include <JBro/Framework3DSystem/System/BoneAttachment3DSystem.h>
#include <JBro/Framework3DSystem/System/Transform3DSystem.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **뼈대 애니메이션의 자세 셈과 애니메이터 시스템**(D-457, skeletal-plan §3.3). 시험 모델은 `TestModels::SkinnedGltf` 다:
// `Armature`(z 5) > `Hip` > `Spine`(y 1), 클립 `Wave` 는 Spine 을 Z 둘레로 1 초에 90 도 돌리고 Hip 을 0.5 초에 y 2 로 옮긴다(STEP).
namespace
{
    namespace fs = std::filesystem;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float a, JBro::Float b, JBro::Float tolerance = 0.001f)
    {
        return std::fabs((a - b).Get()) <= tolerance.Get();
    }

    JBro::ModelData DecodeWalker()
    {
        const std::string gltf = TestModels::SkinnedGltf();
        JBro::ModelData model;
        JBro::String error;
        Check(JBro::DecodeModel({reinterpret_cast<const std::byte*>(gltf.data()), static_cast<JBro::UInt32>(gltf.size())}, JBro::ModelFormat::Gltf, {}, {},
                  model, error),
            "the skinned test model decodes");
        return model;
    }

    // 행렬로 점을 옮긴다(`values[row * 4 + column]`, 열 벡터).
    JBro::Vector3 Apply(const JBro::Matrix4x4& matrix, JBro::Vector3 point)
    {
        const JBro::Float* m = matrix.values;
        return JBro::Vector3{m[0] * point.x + m[1] * point.y + m[2] * point.z + m[3], m[4] * point.x + m[5] * point.y + m[6] * point.z + m[7],
            m[8] * point.x + m[9] * point.y + m[10] * point.z + m[11]};
    }

    JBro::Bool NearPoint(JBro::Vector3 a, JBro::Vector3 b)
    {
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
    }

    // **쉬는 자세의 스키닝 행렬은 단위 행렬이다** - 역바인드가 쉬는 자세의 뼈 행렬의 역이기 때문이다(시험 모델은 바인드 = 쉬는 자세).
    // 클립을 1 초에 뽑으면 Spine 이 90 도 돌고 Hip 이 y 2 에 선다. 위 정점은 Spine 을 따라 돈다.
    void TestThePoseSamplesTheClipAndSkins()
    {
        const JBro::ModelData model = DecodeWalker();
        const JBro::ModelSkeleton& skeleton = model.skeletons[0];
        const JBro::ModelAnimation& clip = model.animations[0];
        JBro::Array<JBro::BonePose> pose;
        pose.Resize(skeleton.bones.Size());
        JBro::Array<JBro::Matrix4x4> bones;
        bones.Resize(skeleton.bones.Size());
        JBro::Array<JBro::Matrix4x4> skin;
        skin.Resize(skeleton.bones.Size());
        JBro::MakeRestPose(skeleton, pose);
        JBro::ComputeBoneMatrices(skeleton, pose, bones);
        JBro::ComputeSkinMatrices(skeleton, bones, skin);
        Check(NearPoint(Apply(bones[1], {0.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 5.0f}), "at rest Spine sits at y 1 under the Armature's z 5");
        JBro::Bool identity = true;
        for (std::size_t bone = 0; bone < skin.Size(); ++bone)
        {
            for (JBro::UInt32 at = 0; at < 16; ++at)
            {
                identity = identity && Near(skin[bone].values[at.Get()], (at.Get() % 5 == 0) ? 1.0f : 0.0f);
            }
        }
        Check(identity, "at rest every skinning matrix is the identity");

        JBro::Array<JBro::Int32> tracks;
        JBro::BindClipTracks(skeleton, clip, tracks);
        Check(tracks.Size() == 2 && tracks[0] == 1 && tracks[1] == 0, "the tracks find their bones by name");
        JBro::SampleClip(clip, tracks, 0.5f, pose);
        Check(Near(pose[1].rotation.z, 0.38268f) && Near(pose[1].rotation.w, 0.92388f), "half way the rotation is 45 degrees (shortest-path nlerp)");
        JBro::MakeRestPose(skeleton, pose);
        JBro::SampleClip(clip, tracks, 0.4f, pose);
        Check(Near(pose[0].position.y, 0.0f), "a STEP track holds its earlier key before the next time");
        JBro::SampleClip(clip, tracks, 1.0f, pose);
        Check(Near(pose[0].position.y, 2.0f) && Near(pose[1].rotation.z, 0.70711f), "at the end Hip is lifted and Spine turned 90 degrees");
        JBro::ComputeBoneMatrices(skeleton, pose, bones);
        JBro::ComputeSkinMatrices(skeleton, bones, skin);
        Check(NearPoint(Apply(skin[1], {0.0f, 1.0f, 0.0f}), {0.0f, 3.0f, 0.0f}) && NearPoint(Apply(skin[1], {1.0f, 1.0f, 0.0f}), {0.0f, 4.0f, 0.0f}),
            "a vertex on Spine turns with it about its joint and rises with Hip");
        Check(NearPoint(Apply(skin[0], {1.0f, 0.0f, 0.0f}), {1.0f, 2.0f, 0.0f}), "a vertex on Hip only rises");
    }

    // **시간 맞추기·마스크·섞기·더하기.**
    void TestMasksBlendAndAddLayers()
    {
        Check(Near(JBro::WrapClipTime(2.5f, 1.0f, true), 0.5f) && Near(JBro::WrapClipTime(-0.25f, 1.0f, true), 0.75f), "a looping clip wraps both ways");
        Check(Near(JBro::WrapClipTime(2.5f, 1.0f, false), 1.0f) && Near(JBro::WrapClipTime(-1.0f, 1.0f, false), 0.0f), "a one-shot clip stops at its ends");
        Check(Near(JBro::WrapClipTime(3.0f, 0.0f, true), 0.0f), "a clip of no length stays at 0");

        const JBro::ModelData model = DecodeWalker();
        const JBro::ModelSkeleton& skeleton = model.skeletons[0];
        JBro::Array<JBro::Float> mask;
        JBro::BuildMaskWeights(skeleton, 1, 0, mask);
        Check(mask.Size() == 2 && Near(mask[0], 0.0f) && Near(mask[1], 1.0f), "a mask from Spine takes Spine and leaves Hip");
        JBro::BuildMaskWeights(skeleton, 0, 1, mask);
        Check(Near(mask[0], 0.5f) && Near(mask[1], 1.0f), "a blend depth of 1 starts the root at half and reaches full one bone down");
        JBro::BuildMaskWeights(skeleton, -1, 0, mask);
        Check(Near(mask[0], 1.0f) && Near(mask[1], 1.0f), "no root bone masks nothing");

        JBro::Array<JBro::BonePose> base;
        base.Resize(2);
        JBro::Array<JBro::BonePose> layer;
        layer.Resize(2);
        JBro::MakeRestPose(skeleton, base);
        JBro::MakeRestPose(skeleton, layer);
        layer[0].position = {0.0f, 4.0f, 0.0f};
        layer[1].position = {0.0f, 3.0f, 0.0f};
        JBro::BuildMaskWeights(skeleton, 1, 0, mask);
        JBro::BlendPose(base, layer, 0.5f, mask);
        Check(Near(base[0].position.y, 0.0f) && Near(base[1].position.y, 2.0f), "an override layer moves only the masked bones by its weight");

        JBro::Array<JBro::BonePose> reference;
        reference.Resize(2);
        JBro::MakeRestPose(skeleton, reference);
        JBro::Array<JBro::BonePose> added;
        added.Resize(2);
        JBro::MakeRestPose(skeleton, added);
        JBro::MakeRestPose(skeleton, base);
        base[1].rotation = JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, JBro::Radian(1.5707963f));
        added[1].rotation = JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f));
        added[0].position = {0.0f, 1.0f, 0.0f};
        const JBro::Array<JBro::Float> none;
        JBro::AddPose(base, added, reference, 1.0f, none);
        const JBro::Quaternion expected = JBro::Multiply(JBro::FromAxisAngle({0.0f, 0.0f, 1.0f}, JBro::Radian(1.5707963f)),
            JBro::FromAxisAngle({1.0f, 0.0f, 0.0f}, JBro::Radian(1.5707963f)));
        Check(Near(base[0].position.y, 1.0f) && Near(base[1].rotation.x, expected.x) && Near(base[1].rotation.y, expected.y) && Near(base[1].rotation.z, expected.z),
            "an additive layer adds its offset from the reference on top of what is there");
    }

    // **애니메이터 시스템**: 뼈대가 비면 자식 스킨 메시가 잡은 뼈대를 쓰고, `playOnStart` 클립을 재생하며, 시간이 흐른 만큼 자세가 선다.
    // 스크립트 조작(크로스페이드·레이어 가중치·마스크)이 다음 프레임의 자세에 들어간다. 애니메이터가 사라지면 상태도 치운다.
    void TestTheSystemAnimatesAnAnimator()
    {
        const fs::path root = fs::temp_directory_path() / L"JBroAnimationProbe·애니";
        fs::remove_all(root);
        fs::create_directories(root / "Art");
        {
            const std::string gltf = TestModels::SkinnedGltf();
            std::ofstream out(root / "Art" / "walker.gltf", std::ios::binary);
            out.write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
        }
        JBro::JMemoryContext memory;
        JBro::WindowsPlatform platform;
        Check(platform.Initialize(memory), "the platform initializes");
        const std::u8string rootText = root.generic_u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, rootPath.c_str(), options, report), "the folder scans");
        JBro::AssetSystem assets;
        Check(assets.Initialize(memory), "the asset system initializes");
        assets.Bind(platform, registry, rootPath.c_str());
        const JBro::AssetRecord* model = registry.FindByPath("Art/walker.gltf");
        JBro::Array<JBro::AssetId> owned;
        registry.CollectOwned(model->id, owned);
        JBro::AssetId meshId;
        JBro::AssetId clipId;
        for (const JBro::AssetId& id : owned)
        {
            meshId = registry.Find(id)->type == JBro::AssetType::Mesh ? id : meshId;
            clipId = registry.Find(id)->type == JBro::AssetType::AnimationClip ? id : clipId;
        }

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Object::GameObject* character = canvas.CreateObject("character");
        JBro::Object::GameObject* body = canvas.CreateObject("body");
        canvas.AttachComponent<JBro::Component::Transform3D>(character);
        canvas.AttachComponent<JBro::Component::Transform3D>(body);
        body->SetParent(character);
        auto* animator = canvas.AttachComponent<JBro::Component::Animator3D>(character);
        auto* renderer = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(body);
        // 해석 패스가 하는 일을 손으로 한다.
        renderer->mesh = assets.Load(meshId);
        animator->animationClips.Add(assets.Load(clipId));
        animator->startClip = 0;
        // 한 번 재생이다 - 반복이면 정확히 1 초에 0 으로 감긴다(감기는 `WrapClipTime` 시험이 본다).
        animator->loop = false;
        const JBro::MeshData* mesh = assets.GetMesh(renderer->mesh);
        Check(mesh != nullptr && mesh->skeletonAsset.generation != 0, "a skinned mesh holds its skeleton");

        JBro::System::Animation3DSystem system;
        system.SetAssets(&assets);
        system.Initialize(canvas);
        system.Update(canvas, 0.5f);
        const JBro::System::Animation3DSystem::State* state = system.FindState(animator->GetInstanceId());
        Check(state != nullptr && state->boneCount == 2, "an animator without a skeleton takes the one its child's skinned mesh holds");
        Check(Near(state->pose[1].rotation.z, 0.38268f), "the start clip plays: half a second in, Spine is at 45 degrees");
        system.Update(canvas, 0.5f);
        Check(Near(state->pose[1].rotation.z, 0.70711f) && Near(state->pose[0].position.y, 2.0f), "a second later it is at the end");
        const JBro::Array<JBro::Matrix4x4>* skin = system.FindSkinMatrices(animator->GetInstanceId());
        Check(skin != nullptr && skin->Size() == 2 && NearPoint(Apply((*skin)[1], {1.0f, 1.0f, 0.0f}), {0.0f, 4.0f, 0.0f}),
            "the skinning matrices follow the pose");
        system.Update(canvas, 0.25f);
        Check(Near(state->pose[1].rotation.z, 0.70711f), "a one-shot clip holds its last pose past the end");

        // 위 레이어가 Spine 만 덮어쓴다: 같은 클립을 0 에서 시작해 마스크를 Spine 으로.
        const JBro::InstanceId instance = animator->GetInstanceId();
        Check(system.Play(instance, 1, animator->animationClips[0], false), "a script plays a clip on layer 1");
        Check(system.SetLayerMask(instance, 1, "Spine", 0), "and masks it to Spine");
        Check(false == system.SetLayerMask(instance, 1, "Tail", 0), "a bone the skeleton does not have is refused");
        Check(system.SetLayerWeight(instance, 1, 1.0f), "at full weight");
        system.Update(canvas, 0.0f);
        Check(Near(state->pose[1].rotation.z, 0.0f) && Near(state->pose[0].position.y, 2.0f),
            "layer 1 at time 0 puts Spine back at rest and leaves Hip to layer 0 (held at its end, lifted)");
        system.SetLayerWeight(instance, 1, 0.0f);
        Check(system.CrossFade(instance, 0, animator->animationClips[0], 1.0f, true), "a cross-fade starts");
        system.Update(canvas, 0.5f);
        Check(state->pose[1].rotation.z > 0.0f, "half way through the fade the pose mixes the old and the new time");

        // **스크립트의 명령**(D-459): 컴포넌트에 쌓고 다음 갱신이 처리하며, 레이어 상태를 컴포넌트에 돌려 적는다.
        Check(animator->Stop(1) && animator->Play(2, 0, false) && animator->SetLayerWeight(2, 1.0f) && animator->SetLayerMask(2, "Spine", 0),
            "a script queues commands on the component");
        Check(false == animator->Play(4, 0, false), "a layer past the last is refused");
        Check(false == animator->SetLayerMask(2, std::string(80, 'x'), 0), "a bone name too long for the command is refused");
        system.Update(canvas, 0.25f);
        Check(animator->IsPlaying(2) && animator->GetLayerClip(2) == 0 && Near(animator->GetLayerTime(2), 0.25f) && false == animator->IsPlaying(1),
            "the next update plays layer 2 from the clip list, stops layer 1, and writes the layer state back");
        Check(Near(state->pose[0].position.y, 2.0f) && state->pose[1].rotation.z > 0.1f && state->pose[1].rotation.z < 0.2f,
            "layer 2 masked to Spine sets Spine a quarter of the way while Hip stays with layer 0");
        Check(animator->Play(2, 7, false), "a clip number the list does not have is queued");
        system.Update(canvas, 0.0f);
        Check(animator->GetLayerClip(2) == 0, "and ignored by the system, which keeps what it was playing");
        system.Update(canvas, 1.0f);
        Check(false == animator->IsPlaying(2) && Near(animator->GetLayerTime(2), 1.0f), "a one-shot layer past its end holds there and is no longer playing");

        // **스크립트의 핸들**(D-459): 같은 명령을 핸들로 건다. 속도는 모든 레이어에 곱한다.
        JBro::Handle::Animator3D handle = character->GetScriptHandle().GetComponent<JBro::Handle::Animator3D>();
        Check(handle.IsValid() && handle.GetLayerClip(2) == 0 && false == handle.IsPlaying(2), "the handle reads the layer state the system wrote back");
        handle.SetSpeed(2.0f);
        Check(handle.Play(3, 0, false) && handle.SetLayerMask(3, "Spine") && handle.SetLayerMask(3, nullptr) && handle.CrossFade(3, 0, 0.5f, true)
                && handle.SetLayerWeight(3, 0.5f) && handle.SetLayerAdditive(3, false),
            "the handle queues commands, and a null bone name clears the mask");
        system.Update(canvas, 0.25f);
        Check(handle.IsPlaying(3) && Near(handle.GetLayerTime(3), 0.5f) && handle.GetSpeed() == 2.0f, "the next update plays layer 3 at twice the speed");
        Check(handle.Stop(3), "the handle stops a layer");
        system.Update(canvas, 0.0f);
        Check(false == handle.IsPlaying(3) && handle.GetLayerClip(3) == -1, "and the stopped layer has no clip");

        canvas.DestroyObject(character);
        canvas.FlushPendingDestroy();
        system.Update(canvas, 0.0f);
        Check(system.FindState(instance) == nullptr, "a destroyed animator's state is cleared");
        system.Shutdown(canvas);
        assets.Shutdown();
        platform.Shutdown();
        fs::remove_all(root);
    }
    // **뼈에 붙이기와 소켓**(D-460). 시험 모델(`TestModels::SkinnedGltf`): `Armature`(z 5) > `Hip` > `Spine`(y 1). 모델 메타의 뼈대 항목에 소켓 `Tip`(Spine 에서 y 0.5)과
    // 없는 뼈를 가리키는 `Broken`, 메시 항목에 `Grip`(Hip 에서 x 1)을 적는다. 캐릭터는 x 10 에 서고 애니메이터를 든다.
    void TestABoneAttachmentFollowsItsSocket()
    {
        const fs::path root = fs::temp_directory_path() / L"JBroSocketProbe·소켓";
        fs::remove_all(root);
        fs::create_directories(root / "Art");
        {
            const std::string gltf = TestModels::SkinnedGltf();
            std::ofstream out(root / "Art" / "walker.gltf", std::ios::binary);
            out.write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
        }
        JBro::JMemoryContext memory;
        JBro::WindowsPlatform platform;
        Check(platform.Initialize(memory), "the platform initializes");
        const std::u8string rootText = root.generic_u8string();
        const JBro::String rootPath(reinterpret_cast<const char*>(rootText.data()), rootText.size());
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, rootPath.c_str(), options, report), "the folder scans");
        const JBro::AssetRecord* model = registry.FindByPath("Art/walker.gltf");
        Check(model != nullptr, "the model is registered");
        JBro::Array<JBro::AssetId> owned;
        registry.CollectOwned(model->id, owned);
        JBro::AssetId meshId;
        JBro::AssetId skeletonId;
        JBro::AssetId clipId;
        for (const JBro::AssetId& id : owned)
        {
            meshId = registry.Find(id)->type == JBro::AssetType::Mesh ? id : meshId;
            skeletonId = registry.Find(id)->type == JBro::AssetType::Skeleton ? id : skeletonId;
            clipId = registry.Find(id)->type == JBro::AssetType::AnimationClip ? id : clipId;
        }

        // ── 메타의 소켓 ──────────────────────────────────────────────
        const fs::path metaPath = root / "Art" / "walker.gltf.jmeta";
        const std::u8string metaText = metaPath.generic_u8string();
        const JBro::String metaUtf8(reinterpret_cast<const char*>(metaText.data()), metaText.size());
        JBro::AssetMetaFile meta;
        JBro::AssetMetaError metaError;
        Check(JBro::LoadAssetMetaFile(platform, metaUtf8.c_str(), meta, metaError), "the model meta reads");
        const auto socket = [](const char* name, const char* bone, JBro::Vector3 position) {
            JBro::ModelSocket made;
            made.name = name;
            made.bone = bone;
            made.position = position;
            return made;
        };
        JBro::AssetSubEntry* skeletonEntry = nullptr;
        JBro::AssetSubEntry* meshEntry = nullptr;
        JBro::AssetSubEntry* clipEntry = nullptr;
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            skeletonEntry = meta.subAssets[index].id == skeletonId ? &meta.subAssets[index] : skeletonEntry;
            meshEntry = meta.subAssets[index].id == meshId ? &meta.subAssets[index] : meshEntry;
            clipEntry = meta.subAssets[index].id == clipId ? &meta.subAssets[index] : clipEntry;
        }
        Check(skeletonEntry != nullptr && meshEntry != nullptr && clipEntry != nullptr, "the meta lists the skeleton, the mesh and the clip");
        skeletonEntry->sockets.Add(socket("Tip", "Spine", {0.0f, 0.5f, 0.0f}));
        skeletonEntry->sockets.Add(socket("Broken", "NoSuchBone", {0.0f, 0.0f, 0.0f}));
        meshEntry->sockets.Add(socket("Grip", "Hip", {1.0f, 0.0f, 0.0f}));
        {
            JBro::AssetMetaFile wrong = meta;
            for (std::size_t index = 0; index < wrong.subAssets.Size(); ++index)
            {
                if (wrong.subAssets[index].id == clipId)
                {
                    wrong.subAssets[index].sockets.Add(socket("Nope", "Hip", {}));
                }
            }
            JBro::String ignored;
            Check(false == JBro::FormatAssetMetaFile(wrong, ignored), "a clip entry may not carry sockets - the writer refuses");
        }
        const JBro::String written = JBro::FormatAssetMetaFile(meta);
        Check(written.find("Sockets:") != JBro::String::npos && written.find("bone: Spine") != JBro::String::npos, "the sockets are written under their entries");
        {
            JBro::AssetMetaFile reread;
            Check(JBro::ParseAssetMetaFile(written.c_str(), written.size(), reread, metaError), "and read back");
            const JBro::AssetSubEntry* entry = nullptr;
            for (std::size_t index = 0; index < reread.subAssets.Size(); ++index)
            {
                entry = reread.subAssets[index].id == skeletonId ? &reread.subAssets[index] : entry;
            }
            Check(entry != nullptr && entry->sockets.Size() == 2 && entry->sockets[0].name == "Tip" && entry->sockets[0].bone == "Spine"
                    && Near(entry->sockets[0].position.y, 0.5f),
                "with their names, bones and offsets");
            // 다시 가져와도 같은 키의 소켓은 산다.
            JBro::Bool changed = true;
            JBro::String refreshError;
            Check(JBro::RefreshModelSubAssets(platform, rootPath.c_str(), "Art/walker.gltf", reread, changed, refreshError) && false == changed,
                "refreshing the sub-asset list keeps the sockets and changes nothing");
            JBro::String clipSockets = written;
            const std::size_t clipKey = clipSockets.find("Key: AnimationClip/");
            const std::size_t clipLineEnd = clipKey != JBro::String::npos ? clipSockets.find('\n', clipSockets.find("Id:", clipKey)) : JBro::String::npos;
            Check(clipLineEnd != JBro::String::npos, "the test finds the clip entry in the text");
            clipSockets.insert(clipLineEnd + 1, "    Sockets:\n      - name: Nope\n        bone: Hip\n");
            Check(false == JBro::ParseAssetMetaFile(clipSockets.c_str(), clipSockets.size(), reread, metaError), "a meta with sockets on a clip entry fails to read");
        }
        Check(JBro::SaveAssetMetaFile(platform, metaUtf8.c_str(), meta), "the meta with sockets saves");

        JBro::AssetSystem assets;
        Check(assets.Initialize(memory), "the asset system initializes");
        assets.Bind(platform, registry, rootPath.c_str());
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Object::GameObject* character = canvas.CreateObject("character");
        JBro::Object::GameObject* body = canvas.CreateObject("body");
        JBro::Object::GameObject* sword = canvas.CreateObject("sword");
        JBro::Object::GameObject* gem = canvas.CreateObject("gem");
        canvas.AttachComponent<JBro::Component::Transform3D>(character)->position = {10.0f, 0.0f, 0.0f};
        canvas.AttachComponent<JBro::Component::Transform3D>(body);
        canvas.AttachComponent<JBro::Component::Transform3D>(sword);
        canvas.AttachComponent<JBro::Component::Transform3D>(gem)->position = {0.0f, 0.0f, 1.0f};
        body->SetParent(character);
        sword->SetParent(character);
        gem->SetParent(sword);
        auto* animator = canvas.AttachComponent<JBro::Component::Animator3D>(character);
        auto* renderer = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(body);
        auto* attachment = canvas.AttachComponent<JBro::Component::BoneAttachment3D>(sword);
        Check(animator != nullptr && renderer != nullptr && attachment != nullptr, "the components attach");
        renderer->mesh = assets.Load(meshId);
        animator->playOnStart = false;
        const JBro::MeshData* mesh = assets.GetMesh(renderer->mesh);
        const JBro::SkeletonData* skeleton = mesh != nullptr ? assets.GetSkeleton(mesh->skeletonAsset) : nullptr;
        Check(skeleton != nullptr && skeleton->sockets.Size() == 2 && mesh->sockets.Size() == 1 && mesh->sockets[0].name == "Grip",
            "loading reads the skeleton's and the mesh's sockets from the model meta");
        JBro::TextStore::Get().Assign(attachment->socket, "Tip", 3);

        JBro::System::Transform3DSystem transforms;
        JBro::System::Animation3DSystem animation;
        JBro::System::BoneAttachment3DSystem attachments;
        animation.SetAssets(&assets);
        attachments.SetAnimation(&animation);
        const auto frame = [&](JBro::Float deltaTime) {
            transforms.Update(canvas, deltaTime);
            animation.Update(canvas, deltaTime);
            attachments.Update(canvas, deltaTime);
        };
        const auto at = [&](JBro::Object::GameObject* object) {
            return canvas.FindComponentRaw<JBro::Component::Transform3D>(object)->worldPosition;
        };
        const auto near3 = [](JBro::Vector3 value, JBro::Float x, JBro::Float y, JBro::Float z) {
            return Near(value.x, x) && Near(value.y, y) && Near(value.z, z);
        };
        frame(0.0f);
        Check(near3(at(sword), 10.0f, 1.5f, 5.0f), "a skeleton socket puts the object at the bone plus the socket offset, under the animator's place");
        Check(near3(at(gem), 10.0f, 1.5f, 6.0f), "and its children follow");
        const JBro::System::BoneAttachment3DSystem::State* state = attachments.FindState(attachment->GetInstanceId());
        const JBro::UInt32 revision = state != nullptr ? state->nameRevision : JBro::UInt32(0);
        frame(0.0f);
        Check(state != nullptr && state->found && state->nameRevision == revision, "an unchanged name is not looked up again");

        // 제 로컬 값은 소켓에서의 덧 오프셋이다.
        canvas.FindComponentRaw<JBro::Component::Transform3D>(sword)->position = {0.0f, 0.0f, 2.0f};
        frame(0.0f);
        Check(near3(at(sword), 10.0f, 1.5f, 7.0f), "the object's own position is an offset from the socket");
        canvas.FindComponentRaw<JBro::Component::Transform3D>(sword)->position = {};

        JBro::TextStore::Get().Assign(attachment->socket, "Grip", 4);
        frame(0.0f);
        Check(near3(at(sword), 11.0f, 0.0f, 5.0f), "a mesh socket of a mesh that follows the animator is found by its name");
        JBro::TextStore::Get().Assign(attachment->socket, "Spine", 5);
        frame(0.0f);
        Check(near3(at(sword), 10.0f, 1.0f, 5.0f), "a bare bone name works with no offset");
        JBro::TextStore::Get().Assign(attachment->socket, "Broken", 6);
        frame(0.0f);
        Check(false == attachments.FindState(attachment->GetInstanceId())->found && near3(at(sword), 10.0f, 0.0f, 0.0f),
            "a socket on a bone the skeleton lacks is not found and the object keeps its plain transform");

        // 스크립트 핸들이 이름을 바꾼다.
        JBro::Handle::BoneAttachment3D attachmentHandle = sword->GetScriptHandle().GetComponent<JBro::Handle::BoneAttachment3D>();
        attachmentHandle.SetSocket("Tip");
        frame(0.0f);
        Check(near3(at(sword), 10.0f, 1.5f, 5.0f), "the script handle sets the socket name");

        // 자세를 따른다: 1 초면 Spine 이 Z 둘레 90 도, Hip 은 y 2 다.
        Check(animation.Play(animator->GetInstanceId(), 0, assets.Load(clipId), false), "the clip plays");
        frame(1.0f);
        Check(near3(at(sword), 9.5f, 3.0f, 5.0f), "the attachment follows the animated bone, turning its offset with the bone");
        const JBro::Quaternion turned = canvas.FindComponentRaw<JBro::Component::Transform3D>(sword)->worldRotation;
        Check(Near(turned.z, 0.70710678f) && Near(turned.w, 0.70710678f), "and takes the bone's rotation");

        // **스크립트 서비스**: 이름을 번호로 한 번 풀고 그 번호로 읽는다.
        JBro::Framework3DSystemContext systems;
        systems.Animation3D = &animation;
        JBro::BindFramework3DSystemContext(systems);
        const JBro::Service::Animation3DService service;
        JBro::Handle::Animator3D animatorHandle = character->GetScriptHandle().GetComponent<JBro::Handle::Animator3D>();
        const JBro::Int32 tip = service.FindSocket(animatorHandle, "Tip");
        Check(tip >= 0 && service.FindSocket(animatorHandle, "Tip") == tip, "the service gives a socket number, the same one each time");
        Check(service.FindSocket(animatorHandle, "Nope") == -1, "and -1 for a name that is neither a socket nor a bone");
        JBro::Vector3 position;
        JBro::Quaternion rotation;
        JBro::Vector3 scale;
        Check(service.GetSocketTransform(animatorHandle, tip, position, rotation, scale) && near3(position, 9.5f, 3.0f, 5.0f),
            "and reads the socket's world place");
        Check(false == service.GetSocketTransform(animatorHandle, 99, position, rotation, scale), "an unknown number reads nothing");

        // 소켓을 고쳐 메타를 다시 쓰고 뼈대를 제자리에서 다시 읽으면 다음 프레임이 새 오프셋으로 다시 푼다.
        skeletonEntry->sockets[0].position = {0.0f, 1.0f, 0.0f};
        Check(JBro::SaveAssetMetaFile(platform, metaUtf8.c_str(), meta), "the edited meta saves");
        Check(assets.ReloadInPlace(skeletonId), "the skeleton reloads in place");
        frame(0.0f);
        Check(near3(at(sword), 9.0f, 3.0f, 5.0f), "a reloaded skeleton's socket is looked up again");
        Check(service.GetSocketTransform(animatorHandle, tip, position, rotation, scale) && near3(position, 9.0f, 3.0f, 5.0f),
            "and the script's socket number follows the new offset");
        JBro::BindFramework3DSystemContext({});

        // 애니메이터가 없으면 붙지 않는다.
        sword->SetParent(nullptr);
        frame(0.0f);
        Check(false == attachments.FindState(attachment->GetInstanceId())->found && near3(at(sword), 0.0f, 0.0f, 0.0f),
            "an object with no animator above it keeps its plain transform");
        canvas.DestroyObject(sword);
        canvas.FlushPendingDestroy();
        frame(0.0f);
        Check(attachments.FindState(attachment->GetInstanceId()) == nullptr, "a destroyed attachment's state is cleared");
        attachments.Shutdown(canvas);
        animation.Shutdown(canvas);
        assets.Shutdown();
        platform.Shutdown();
        fs::remove_all(root);
    }
}

JBro::Int32 RunAnimation3DTests()
{
    JBRO_CASE(TestThePoseSamplesTheClipAndSkins());
    JBRO_CASE(TestMasksBlendAndAddLayers());
    JBRO_CASE(TestTheSystemAnimatesAnAnimator());
    JBRO_CASE(TestABoneAttachmentFollowsItsSocket());
    std::cout << "Animation 3D tests passed.\n";
    return 0;
}
