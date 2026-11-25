#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Framework3D/System/IAnimation3DSystem.h>
#include <JBro/Framework3DSystem/Animation/AnimationPose.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/Matrix4x4.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    class Canvas;
    class GameScript3D;
    struct AnimationClipData;
    struct ModelClipEvent;
    namespace Component
    {
        class Animator3D;
    }
    namespace Object
    {
        class GameObject;
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
    //
    // **소켓**(D-460): 이름을 뼈 번호와 오프셋으로 푸는 것(`ResolveSocket`)과 그 월드 자리(`ComputeSocketWorld`)도 여기다 - 붙이기 시스템과 스크립트 서비스가 같이 쓴다.
    class Animation3DSystem final : public GameSystem, public IAnimation3DSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 350;

        // 이름을 푼 소켓이다. 뼈 번호와 그 뼈에서의 오프셋(뼈 이름이면 오프셋 없음).
        struct ResolvedSocket
        {
            Int32 bone = -1;
            Vector3 position;
            Quaternion rotation;
            Vector3 scale{1.0f, 1.0f, 1.0f};
        };

        // 스크립트가 `FindSocket` 으로 받은 번호 하나다. 소켓 출처가 바뀌면(판이 다르면) 같은 이름으로 다시 푼다.
        struct ScriptSocket
        {
            String name;
            ResolvedSocket socket;
            UInt32 revision = 0;
            Bool found = false;
        };

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
            Bool playing = false;
            // 다음 이벤트 훑기가 0 초의 이벤트를 넣는가다(D-462). 재생을 시작하면 참이고, 한 번 훑거나 시간을 놓으면 거짓이다.
            Bool eventsFromStart = true;
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
            // 스크립트가 찾은 소켓(D-460). `FindSocket` 이 더한다 - 프레임 경로가 아니다.
            Array<ScriptSocket> scriptSockets;
        };

        Int32 GetExecutionOrder() const override;
        void SetAssets(AssetSystem* assets);

        // 그 애니메이터의 상태다. 아직 만나지 않았으면 null.
        const State* FindState(InstanceId animator) const;
        State* FindState(InstanceId animator);
        // 그 애니메이터의 스키닝 행렬(뼈마다 하나)이다. 뼈대가 서지 않았으면 null.
        const Array<Matrix4x4>* FindSkinMatrices(InstanceId animator) const;

        // **스크립트가 부르는 조작**(1 단계의 스크립트 핸들이 이것을 부른다). 레이어 번호가 넘치거나 클립이 비면 거짓이다.
        // 반복은 클립이 정한다(D-462).
        Bool Play(InstanceId animator, UInt32 layer, AssetHandle clip);
        Bool CrossFade(InstanceId animator, UInt32 layer, AssetHandle clip, Float seconds);
        Bool SetLayerWeight(InstanceId animator, UInt32 layer, Float weight);
        Bool SetLayerMode(InstanceId animator, UInt32 layer, AnimatorLayerMode mode);
        // 뿌리 뼈(이름)와 그 가지를 섞는 마스크다. 이름이 비면 마스크를 지운다(모든 뼈). 이름은 여기서 한 번 번호로 푼다.
        Bool SetLayerMask(InstanceId animator, UInt32 layer, std::string_view rootBone, UInt32 blendDepth);
        // 그 레이어의 시간을 놓는다(모델 에디터의 타임라인 끌기, D-461). 크로스페이드는 끝낸다. 다음 셈이 그 자리의 자세를 짓는다.
        // 레이어가 넘치거나 시간이 수가 아니면 거짓이다.
        Bool SetLayerTime(InstanceId animator, UInt32 layer, Float time);

        // **소켓 출처의 판**(D-460)이다. 뼈대(핸들과 세대)와 이 애니메이터를 따르는 메시들(핸들과 세대·수)에서 짓고, 소켓·뼈대가 다시 읽히거나
        // 메시가 바뀌면 달라진다. 이름을 견주지 않으므로 붙이기가 프레임마다 이것으로 다시 풀지를 정한다. 자세가 서지 않았으면 0 이다.
        UInt32 GetSocketSourceRevision(Canvas& canvas, const Component::Animator3D& animator) const;
        // 이름을 그 애니메이터의 소켓으로 푼다: 뼈대 소켓 → 이 애니메이터를 따르는 메시(이 오브젝트와 자식, 다른 애니메이터 아래는 빼고)의 소켓 →
        // 뼈 이름(오프셋 없음). 소켓이 가리키는 뼈가 뼈대에 없으면 그 소켓은 없는 것이다. 이름을 견주므로 프레임마다 부르지 않는다. 빈 이름은 거짓이다.
        Bool ResolveSocket(Canvas& canvas, const Component::Animator3D& animator, std::string_view name, ResolvedSocket& socket) const;
        // 푼 소켓의 월드 자리다: 애니메이터 오브젝트의 월드 x 뼈의 모델 행렬(마지막으로 셈한 자세) x 소켓 오프셋. 자세가 서지 않았으면 거짓이다.
        Bool ComputeSocketWorld(Canvas& canvas, const Component::Animator3D& animator, const ResolvedSocket& socket,
            Vector3& position, Quaternion& rotation, Vector3& scale) const;

        // 붙이기가 고를 수 있는 이름들이다(에디터의 소켓 칸, D-460): 뼈대 소켓, 따르는 메시의 소켓, 뼈 이름 차례. 빈 이름은 빼고 겹치는 이름은 한 번이다.
        // 할당한다 - 프레임 경로가 아니다. 자세가 서지 않았으면 비운다.
        void CollectSocketNames(Canvas& canvas, const Component::Animator3D& animator, Array<String>& names) const;

        // IAnimation3DSystem(스크립트 서비스). 캔버스는 마지막으로 갱신한 것이다.
        Int32 FindSocket(const Component::Animator3D& animator, const char* name) override;
        Bool GetSocketTransform(const Component::Animator3D& animator, Int32 socket, Vector3& position, Quaternion& rotation, Vector3& scale) override;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        AssetHandle FindSkeleton(Canvas& canvas, const Component::Animator3D& animator) const;
        // 컴포넌트에 쌓인 스크립트의 명령을 처리하고 비운다(D-459).
        void ApplyCommands(State& state, Component::Animator3D& animator);
        // 셈한 레이어 상태를 컴포넌트에 적는다(스크립트가 읽는다).
        void WriteBack(const State& state, Component::Animator3D& animator) const;
        // 레이어가 이번에 지난 이벤트를 모은다(D-462). `from`·`to` 는 감기 전 시간이다.
        void CollectEvents(Object::GameObject* owner, UInt32 layerIndex, Layer& layer, const AnimationClipData& clip, Float from, Float to);
        // 모은 이벤트를 애니메이터 오브젝트의 스크립트에 부른다. 갱신 끝에 한 번이다.
        void DispatchEvents(Canvas& canvas);
        Bool Prepare(State& state, AssetHandle skeleton);
        Bool BindLayerClip(const State& state, AssetHandle clip, UInt32& generation, Array<Int32>& tracks);
        void Evaluate(State& state, const Component::Animator3D& animator, Float deltaTime);

        AssetSystem* m_assets = nullptr;
        // 스크립트 서비스가 부를 때 쓰는 캔버스다. 갱신마다 적는다.
        Canvas* m_canvas = nullptr;
        Array<State> m_states;
        Table<InstanceId, UInt32> m_stateOf;
        UInt32 m_stamp = 0;
        // 이번 갱신에 지난 이벤트다(D-462). 처음에 `MaxFiredEvents` 만큼 잡고 넘치면 버린다 - 프레임 경로라 늘리지 않는다.
        struct FiredEvent
        {
            Object::GameObject* owner = nullptr;
            const ModelClipEvent* event = nullptr;
            UInt32 layer = 0;
            Int32 clip = -1;
        };
        static constexpr std::size_t MaxFiredEvents = 256;
        Array<FiredEvent> m_fired;
        Bool m_firedOverflow = false;
        Array<GameScript3D*> m_hookTargets;
    };
}
