#include "TestCases.h"
#include "TestModels.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
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
        animator->clip = assets.Load(clipId);
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
        Check(system.Play(instance, 1, animator->clip, false), "a script plays a clip on layer 1");
        Check(system.SetLayerMask(instance, 1, "Spine", 0), "and masks it to Spine");
        Check(false == system.SetLayerMask(instance, 1, "Tail", 0), "a bone the skeleton does not have is refused");
        Check(system.SetLayerWeight(instance, 1, 1.0f), "at full weight");
        system.Update(canvas, 0.0f);
        Check(Near(state->pose[1].rotation.z, 0.0f) && Near(state->pose[0].position.y, 2.0f),
            "layer 1 at time 0 puts Spine back at rest and leaves Hip to layer 0 (held at its end, lifted)");
        system.SetLayerWeight(instance, 1, 0.0f);
        Check(system.CrossFade(instance, 0, animator->clip, 1.0f, true), "a cross-fade starts");
        system.Update(canvas, 0.5f);
        Check(state->pose[1].rotation.z > 0.0f, "half way through the fade the pose mixes the old and the new time");

        canvas.DestroyObject(character);
        canvas.FlushPendingDestroy();
        system.Update(canvas, 0.0f);
        Check(system.FindState(instance) == nullptr, "a destroyed animator's state is cleared");
        system.Shutdown(canvas);
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
    std::cout << "Animation 3D tests passed.\n";
    return 0;
}
