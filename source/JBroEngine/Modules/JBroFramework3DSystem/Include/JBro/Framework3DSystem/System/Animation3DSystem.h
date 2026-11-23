#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Matrix4x4.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    namespace Component
    {
        class Animator3D;
    }
}

namespace JBro::System
{
    // 애니메이터 하나의 레이어 수다(D-453 - 레이어 배열은 상한이 정해져 있다, 프레임 경로라 할당하지 않는다).
    inline constexpr UInt32 MaxAnimatorLayers = 4;

    // 레이어가 아래 레이어와 어떻게 섞이는가다(Unity 의 Override·Additive).
    enum class AnimatorLayerMode : std::uint8_t
    {
        Override,
        Additive
    };

    // **`Animator3D` 의 자세를 셈한다**(D-457). 스크립트(200) 뒤, 렌더 추출(400) 앞이다. 레이어마다 클립을 뽑아 아래에서 위로 섞고(덮어쓰기는 가중치 x 뼈 마스크,
    // 더하기는 마지막에), 뼈의 모델 행렬과 스키닝 행렬을 짓는다. 시간은 시뮬레이션이 돌 때만 흐른다(멈추면 델타가 0 이라 그 자세에 선다).
    //
    // 재생 상태와 자세는 애니메이터마다 이 시스템이 든다(컴포넌트는 저장되는 설정뿐). 배열은 애니메이터를 처음 만나거나 뼈대·클립이 바뀔 때만 잡는다.
    // 뼈대는 애니메이터의 `skeleton` 이거나, 비면 이 오브젝트·자식의 스킨 메시가 잡은 뼈대다(`MeshData::skeletonAsset`) - 프레임 경로에서 싣지 않는다.
    class Animation3DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 350;

        struct Layer
        {
            AssetHandle clip;
            // 애니메이터의 `animationClips` 에서의 번호다(스크립트가 고른 것, D-459). 시스템 함수로 핸들을 직접 건 것은 -1.
            Int32 clipIndex = -1;
            UInt32 clipGeneration = 0;
            Array<Int32> tracks;
            Float time = 0.0f;
            Float weight = 1.0f;
            Float speed = 1.0f;
            Bool loop = true;
            Bool playing = false;
            AnimatorLayerMode mode = AnimatorLayerMode::Override;
            // 뼈마다 0..1 이다. 비면 모든 뼈가 1 이다.
            Array<Float> mask;
            // 마스크를 다시 지을 때 쓰는 값이다(뼈대가 바뀌면 다시 짓는다).
            Int32 maskRoot = -1;
            UInt32 maskDepth = 0;
            // 크로스페이드: 앞 클립에서 `fade` 초에 걸쳐 넘어간다.
            AssetHandle fromClip;
            UInt32 fromGeneration = 0;
            Array<Int32> fromTracks;
            Float fromTime = 0.0f;
            Bool fromLoop = true;
            Float fade = 0.0f;
            Float fadeElapsed = 0.0f;
        };

        struct State
        {
            InstanceId instance = InvalidInstanceId;
            UInt32 stamp = 0;
            AssetHandle skeleton;
            UInt32 skeletonGeneration = 0;
            UInt32 boneCount = 0;
            Bool started = false;
            // `MaxAnimatorLayers` 칸이다. 상태를 처음 만들 때 한 번 잡는다.
            Array<Layer> layers;
            Array<BonePose> rest;
            Array<BonePose> pose;
            Array<BonePose> sample;
            Array<BonePose> blend;
            Array<Matrix4x4> model;
            Array<Matrix4x4> skin;
        };

        Int32 GetExecutionOrder() const override;
        void SetAssets(AssetSystem* assets);

        // 그 애니메이터의 상태다. 아직 만나지 않았으면 null.
        const State* FindState(InstanceId animator) const;
        State* FindState(InstanceId animator);
        // 그 애니메이터의 스키닝 행렬(뼈마다 하나)이다. 뼈대가 서지 않았으면 null.
        const Array<Matrix4x4>* FindSkinMatrices(InstanceId animator) const;

        // **스크립트가 부르는 조작**(1 단계의 스크립트 핸들이 이것을 부른다). 레이어 번호가 넘치거나 클립이 비면 거짓이다.
        Bool Play(InstanceId animator, UInt32 layer, AssetHandle clip, Bool loop);
        Bool CrossFade(InstanceId animator, UInt32 layer, AssetHandle clip, Float seconds, Bool loop);
        Bool SetLayerWeight(InstanceId animator, UInt32 layer, Float weight);
        Bool SetLayerMode(InstanceId animator, UInt32 layer, AnimatorLayerMode mode);
        // 뿌리 뼈(이름)와 그 가지를 섞는 마스크다. 이름이 비면 마스크를 지운다(모든 뼈). 이름은 여기서 한 번 번호로 푼다.
        Bool SetLayerMask(InstanceId animator, UInt32 layer, std::string_view rootBone, UInt32 blendDepth);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        AssetHandle FindSkeleton(Canvas& canvas, const Component::Animator3D& animator) const;
        // 컴포넌트에 쌓인 스크립트의 명령을 처리하고 비운다(D-459).
        void ApplyCommands(State& state, Component::Animator3D& animator);
        // 셈한 레이어 상태를 컴포넌트에 적는다(스크립트가 읽는다).
        void WriteBack(const State& state, Component::Animator3D& animator) const;
        Bool Prepare(State& state, AssetHandle skeleton);
        Bool BindLayerClip(const State& state, AssetHandle clip, UInt32& generation, Array<Int32>& tracks);
        void Evaluate(State& state, const Component::Animator3D& animator, Float deltaTime);

        AssetSystem* m_assets = nullptr;
        Array<State> m_states;
        Table<InstanceId, UInt32> m_stateOf;
        UInt32 m_stamp = 0;
    };
}
