#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Animator3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace JBro::System
{
    class Animation3DSystem;
}

namespace JBro::Component
{
    // **애니메이터**(D-457, D-459, skeletal-plan §3.3). 뼈대의 자세(뼈 번호 → 위치·회전·크기)를 들고 클립을 재생한다. 뼈는 오브젝트가 아니다.
    // 스킨 메시는 자기나 가장 가까운 조상의 애니메이터 자세로 그린다(D-458). 자세와 재생 상태는 `Animation3DSystem` 이 든다 - 여기는 저장되는 설정과,
    // 스크립트가 남긴 명령(다음 프레임에 시스템이 처리한다)과, 시스템이 돌려 적은 레이어 상태뿐이다(캐릭터 컨트롤러의 `Move` 와 같은 꼴).
    //
    // 클립은 `animationClipIds` 의 목록이고 스크립트는 그 **번호**로 고른다 - 프레임 경로에서 이름을 견주지 않는다. 레이어 1 이상(상하체 분리)과 마스크는 스크립트가 건다.
    class Animator3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Animator3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Animator3D)

        // 뼈대(모델의 하위 Skeleton)다. 비면 이 오브젝트나 자식의 스킨 메시가 쓰는 뼈대다.
        JBRO_FIELD(AssetId,            skeletonId);
        JBRO_FIELD(AssetHandle,        skeleton,       NoSerialize() | ReadOnly() | Tooltip("skeletonId 에서 해석된 값") | Hidden());
        // 이 애니메이터가 재생하는 클립들(모델의 하위 AnimationClip)이다. 스크립트는 번호로 고른다.
        JBRO_FIELD(Array<AssetId>,     animationClipIds);
        JBRO_FIELD(Array<AssetHandle>, animationClips, NoSerialize() | ReadOnly() | Tooltip("animationClipIds 에서 해석된 값") | Hidden());
        // 레이어 0(전신 바탕)이 시작할 때 재생하는 클립의 번호다. -1 이면 아무것도 재생하지 않는다.
        JBRO_FIELD(Int32,              startClip) = 0;
        JBRO_FIELD(Bool,               playOnStart) = true;
        JBRO_FIELD(Bool,               loop) = true;
        // 모든 레이어의 재생 속도에 곱한다. 0 이면 멈춘다.
        JBRO_FIELD(Float,              speed) = 1.0f;

        // ── 스크립트의 명령(D-459) ─────────────────────────────────────────
        // 레이어 수와 한 프레임에 쌓아 둘 명령 수다. 넘치는 명령은 버리고 거짓이다.
        // 배열의 크기라 `std::size_t` 다(UInt32 의 값은 상수식이 아니다).
        static constexpr std::size_t LayerCount = 4;
        static constexpr std::size_t CommandCapacity = 16;
        static constexpr std::size_t BoneNameCapacity = 64;

        enum class CommandKind : std::uint8_t
        {
            Play,
            CrossFade,
            Stop,
            SetWeight,
            SetAdditive,
            SetMask
        };

        struct Command
        {
            CommandKind kind = CommandKind::Play;
            UInt32 layer = 0;
            Int32 clip = -1;
            // `CrossFade` 의 초, `SetWeight` 의 가중치, `SetMask` 의 섞는 깊이다.
            Float value = 0.0f;
            Bool flag = false;
            // `SetMask` 의 뿌리 뼈 이름이다(비면 마스크를 지운다). DLL 경계를 넘으므로 고정 크기 글자다.
            char bone[BoneNameCapacity] = {};
        };

        // 레이어에서 그 번호의 클립을 처음부터 재생한다.
        Bool Play(UInt32 layer, Int32 clip, Bool loopClip)
        {
            Command command;
            command.kind = CommandKind::Play;
            command.layer = layer;
            command.clip = clip;
            command.flag = loopClip;
            return Push(command);
        }

        // 지금 클립에서 그 번호의 클립으로 `seconds` 초에 걸쳐 넘어간다.
        Bool CrossFade(UInt32 layer, Int32 clip, Float seconds, Bool loopClip)
        {
            Command command;
            command.kind = CommandKind::CrossFade;
            command.layer = layer;
            command.clip = clip;
            command.value = seconds;
            command.flag = loopClip;
            return Push(command);
        }

        Bool Stop(UInt32 layer)
        {
            Command command;
            command.kind = CommandKind::Stop;
            command.layer = layer;
            return Push(command);
        }

        // 레이어의 가중치(0..1)다. 레이어 0 은 보지 않는다.
        Bool SetLayerWeight(UInt32 layer, Float weight)
        {
            Command command;
            command.kind = CommandKind::SetWeight;
            command.layer = layer;
            command.value = weight;
            return Push(command);
        }

        // 참이면 더하는 레이어(쉬는 자세에서 벗어난 만큼을 아래 자세에 더한다), 거짓이면 덮어쓰는 레이어다.
        Bool SetLayerAdditive(UInt32 layer, Bool additive)
        {
            Command command;
            command.kind = CommandKind::SetAdditive;
            command.layer = layer;
            command.flag = additive;
            return Push(command);
        }

        // 그 뼈와 그 가지만 섞는다(`blendDepth` 뼈에 걸쳐 서서히). 이름이 비면 마스크를 지운다. 이름이 길면(63 바이트 넘음) 거짓이다.
        Bool SetLayerMask(UInt32 layer, std::string_view rootBone, UInt32 blendDepth)
        {
            if (rootBone.size() >= BoneNameCapacity)
            {
                return false;
            }
            Command command;
            command.kind = CommandKind::SetMask;
            command.layer = layer;
            command.value = static_cast<JBro::Float>(blendDepth.Get());
            if (false == rootBone.empty())
            {
                std::memcpy(command.bone, rootBone.data(), rootBone.size());
            }
            return Push(command);
        }

        // 마지막으로 셈한 프레임의 레이어 상태다(시스템이 적는다).
        Float GetLayerTime(UInt32 layer) const
        {
            return layer.Get() < LayerCount ? m_layerTime[layer.Get()] : Float(0.0f);
        }
        Bool IsPlaying(UInt32 layer) const
        {
            return layer.Get() < LayerCount && m_layerPlaying[layer.Get()];
        }
        Int32 GetLayerClip(UInt32 layer) const
        {
            return layer.Get() < LayerCount ? m_layerClip[layer.Get()] : Int32(-1);
        }

    private:
        friend class System::Animation3DSystem;

        Bool Push(const Command& command)
        {
            if (m_commandCount.Get() >= CommandCapacity || command.layer.Get() >= LayerCount)
            {
                return false;
            }
            m_commands[m_commandCount.Get()] = command;
            ++m_commandCount;
            return true;
        }

        Command m_commands[CommandCapacity];
        UInt32 m_commandCount = 0;
        Float m_layerTime[LayerCount] = {};
        Bool m_layerPlaying[LayerCount] = {};
        Int32 m_layerClip[LayerCount] = {-1, -1, -1, -1};
    };
}
