#pragma once

#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/AudioTypes/ServiceContext.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/ScriptModule.h>

// 오디오 컨텍스트를 D-37 확장 블록으로 내고 찾는 도우미다. 네트워크의 것과 같은 모양이다. 블록은 **호스트가** 만든다 -
// 믹서는 캔버스보다 오래 살고 두 차원이 같은 것을 쓴다. 스크립트 DLL 은 Load 에서 `FindAudio*Context` 로 찾아
// `BindAudio*Context` 로 자기 사본에 묶는다.
namespace JBro
{
    inline constexpr ScriptContextTypeId AudioServiceContextTypeId = MakeStableTypeId("JBro.Audio.ServiceContext");

    inline constexpr ScriptContextRequirement AudioServiceContextRequirement =
    {
        AudioServiceContextTypeId,
        AudioServiceContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(AudioServiceContext))
    };

    ScriptContextBlock MakeAudioServiceContextBlock(const AudioServiceContext& context) noexcept;
    const AudioServiceContext* FindAudioServiceContext(const ScriptModuleLoadContext& context) noexcept;

    inline constexpr ScriptContextTypeId AudioSystemContextTypeId = MakeStableTypeId("JBro.Audio.SystemContext");

    inline constexpr ScriptContextRequirement AudioSystemContextRequirement =
    {
        AudioSystemContextTypeId,
        AudioSystemContextAbiVersion,
        static_cast<std::uint32_t>(sizeof(AudioSystemContext))
    };

    ScriptContextBlock MakeAudioSystemContextBlock(const AudioSystemContext& context) noexcept;
    const AudioSystemContext* FindAudioSystemContext(const ScriptModuleLoadContext& context) noexcept;
}
