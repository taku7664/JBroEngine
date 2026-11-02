#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/Scripting/ScriptModule.h>
#include <JBro/AudioTypes/Internal/ScriptModuleContext.h>
#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/Network/Internal/ScriptModuleContext.h>
#include <JBro/Network/Internal/SystemContext.h>
#include <JBro/InputTypes/Internal/ScriptModuleContext.h>
#include <JBro/SaveTypes/Internal/ScriptModuleContext.h>
#include <JBro/LocalizationTypes/Internal/ScriptModuleContext.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/ScriptGuard.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

// 내보내기는 `extern "C"`(이름을 꾸미지 않으려고)이고 시그니처는 엔진 값 타입이다(D-290). MSVC 는 C 연결에서 클래스를
// 돌려주면 C4190("C 와 호환되지 않음")을 낸다 - 부르는 쪽은 C 가 아니라 같은 타입으로 선언한 C++(호스트·시험)이라 해당하지 않는다.
#pragma warning(disable : 4190)
#include <cstring>

#ifndef JBRO_SCRIPT_PROBE_REVISION
#define JBRO_SCRIPT_PROBE_REVISION 1
#endif

namespace
{
// 호스트가 이름으로 만들 수 있는 스크립트다. 이 타입은 DLL 안에만 있고
// 호스트는 그 정의를 보지 못한다 — 그게 이 경로의 요점이다(H5).
    class ProbeRegisteredScript final : public JBro::GameScript2D
{
    JBRO_SCRIPT_BODY(ProbeRegisteredScript)
public:
    void OnStart() override
    {
        m_started = true;
    }

    // 호스트가 이 필드를 표로 읽고 써야 한다 - 캔버스 파일·인스펙터·되돌리기가 모두 이 길이다(cpp-script-plan §3.1).
    JBRO_FIELD(JBro::Float, Speed) = 2.5f;

public:
    bool m_started = false;
    // 호스트 쪽 슬롯 크기가 DLL 쪽 sizeof 와 맞는지 보려고 일부러 채운다.
    double m_padding[4] = {1.0, 2.0, 3.0, 4.0};
};


// ── 네트워크를 쓰는 예시 스크립트 ───────────────────────────────────────────────────────────────
//
// **멀티플레이 게임 스크립트가 어떻게 생기는지 보여 주는 자리다**(network-plan §3-9). 호스트가 넘긴 네트워크 블록이
// 이 DLL 사본에 묶이고, 스크립트는 값 서비스 둘로만 망을 만진다 - 소켓도 트랜스포트도 보지 않는다.
//
// 규율 셋이 전부다.
//  (1) **상태는 복제가 나른다.** 위치·체력처럼 매 틱 변하는 값은 서버의 컴포넌트 풀에 쓰고, 복제가 클라이언트에 입힌다.
//      스크립트가 그 값을 메시지로 보내지 않는다.
//  (2) **사건은 POD 메시지로 보낸다.** 점수가 올랐다·문이 열렸다처럼 한 번 일어나는 것이다. 호스트와 DLL 이 다른
//      컴파일러일 수 있어 직렬화는 `memcpy` 고정이고, 그래서 메시지 타입은 POD 여야 한다(서비스가 컴파일할 때 검사한다).
//  (3) **권한 있는 쪽만 바꾼다.** `HasAuthority` 가 거짓인 쪽에서 상태를 바꾸면 다음 스냅숏이 그것을 덮어쓴다 -
//      지금은 서버만 참이다(소유 위임은 열어 둔 것이다).
struct ProbeScoreEvent
{
    JBro::UInt32 round = 0;
    JBro::Float value = 0.0f;
};

// 1..0xFDFF 가 게임 몫이다. 0xFE00 부터는 복제와 세션이 쓴다.
inline constexpr JBro::Network::MessageId ProbeScoreMessageId = 77;

class ProbeNetworkedScript final : public JBro::GameScript2D
{
    JBRO_SCRIPT_BODY(ProbeNetworkedScript)
public:
    // 서버가 올리고 클라이언트가 메시지로 받아 적는 값이다.
    JBRO_FIELD(JBro::Float, Score) = 0.0f;
    JBRO_FIELD(JBro::UInt32, Round) = 0;

protected:
    void OnUpdate() override
    {
        const JBro::NetworkServiceContext& net = JBro::GetNetworkServices();

        // 받은 것을 먼저 꺼낸다. 뷰가 가리키는 바이트는 이번 프레임까지만 산다 - 쓸 값은 복사해 둔다.
        JBro::Network::MessageView inbox[8];
        const JBro::UInt32 taken = net.Network.TakeMessages(inbox, 8);
        for (JBro::UInt32 index = 0; index < taken; ++index)
        {
            const JBro::Network::MessageView& view = inbox[index];
            if (view.messageId != ProbeScoreMessageId || view.size != sizeof(ProbeScoreEvent))
            {
                continue;
            }
            ProbeScoreEvent event;
            std::memcpy(&event, view.data, sizeof(event));
            Round = event.round;
            Score = event.value;
        }

        // 권한이 없으면 여기서 끝이다. 서버가 보낸 것을 받아 쓰는 쪽이다.
        if (false == net.Network.HasAuthority(GetGameObject().GetInstanceId()))
        {
            return;
        }

        // 서버다. 값을 올리고 사건을 한 번 알린다.
        Round = Round + 1;
        Score = Score + 1.0f;
        ProbeScoreEvent event;
        event.round = Round;
        event.value = Score;
        net.Network.Broadcast(ProbeScoreMessageId, event);
    }
};

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetScriptRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::ScriptRegistry::Get());
}

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetScriptProperties() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::PropertyRegistry::Script());
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_GetRegisteredScriptSize() noexcept
{
    return static_cast<JBro::UInt32>(sizeof(ProbeRegisteredScript));
}


}

// 스크립트 등록과 진입점은 한 줄씩이다(cpp-script-plan §3.2). 컨텍스트 검증·바인딩·해제는 엔진(`JBroFramework2D`)이 한다 -
// 이 파일이 손으로 하던 때는 호스트가 넘기는 오디오·네트워크를 묶지 않았다.
JBRO_REGISTER_SCRIPT_2D(ProbeRegisteredScript);
JBRO_REGISTER_SCRIPT_2D(ProbeNetworkedScript);

JBRO_SCRIPT_MODULE_2D()

// A generated callback uses the same guard path. Keep the exception inside
// this DLL boundary while the host verifies its own Log ring received it.
extern "C" __declspec(dllexport) JBro::Bool JBroScriptProbe_TriggerInvalidRef() noexcept
{
    try
    {
        JBro::Nullable<JBro::Int32> missing;
        JBro::ScriptGuard::Require(missing, "probe:7:target", "probe target is invalid");
    }
    catch (const JBro::ScriptGuard::AbortCallback&)
    {
        return true;
    }
    catch (...)
    {
    }
    return false;
}

// 모듈의 `Load` 가 돌아 등록까지 마쳤는지 본다. 등록은 호스트 표를 묶은 뒤에만 한다.
extern "C" __declspec(dllexport) JBro::Bool JBroScriptProbe_IsLoaded() noexcept
{
    return JBro::ScriptRegistry::Get().Find(ProbeRegisteredScript::StaticTypeName()) != nullptr;
}

// 호스트가 넘긴 서비스가 이 DLL 사본에 묶였는지 본다(cpp-script-plan §3.2). `which` 번째 컨텍스트를 바이트로 옮기고,
// 호스트가 제 것과 견준다. 묶이지 않았다면 이 DLL 의 기본값이 나온다.
extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_CopyContext(
    JBro::UInt32 which, void* out, JBro::UInt32 capacity) noexcept
{
    struct Entry
    {
        const void*   data;
        JBro::UInt32 size;
    };
    const Entry contexts[] =
    {
        {&JBro::GetInputServices(), static_cast<JBro::UInt32>(sizeof(JBro::InputServiceContext))},
        {&JBro::GetInputSystems(), static_cast<JBro::UInt32>(sizeof(JBro::InputSystemContext))},
        {&JBro::GetSaveServices(), static_cast<JBro::UInt32>(sizeof(JBro::SaveServiceContext))},
        {&JBro::GetSaveSystems(), static_cast<JBro::UInt32>(sizeof(JBro::SaveSystemContext))},
        {&JBro::GetLocalizationServices(), static_cast<JBro::UInt32>(sizeof(JBro::LocalizationServiceContext))},
        {&JBro::GetLocalizationSystems(), static_cast<JBro::UInt32>(sizeof(JBro::LocalizationSystemContext))},
        {&JBro::GetAudioServices(), static_cast<JBro::UInt32>(sizeof(JBro::AudioServiceContext))},
        {&JBro::GetAudioSystems(), static_cast<JBro::UInt32>(sizeof(JBro::AudioSystemContext))},
        {&JBro::GetNetworkServices(), static_cast<JBro::UInt32>(sizeof(JBro::NetworkServiceContext))},
        {&JBro::GetNetworkSystems(), static_cast<JBro::UInt32>(sizeof(JBro::NetworkSystemContext))},
    };
    if (which >= sizeof(contexts) / sizeof(contexts[0]) || contexts[which].size > capacity)
    {
        return 0;
    }
    std::memcpy(out, contexts[which].data, contexts[which].size);
    return contexts[which].size;
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_GetSystemAbi() noexcept
{
    return JBro::GetSystemContext().AbiVersion;
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_GetServiceAbi() noexcept
{
    return JBro::GetServiceContext().AbiVersion;
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_GetFramework2DAbi() noexcept
{
    return JBro::GetFramework2DServices().AbiVersion;
}

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetPhysicsSystem() noexcept
{
    return reinterpret_cast<std::uintptr_t>(JBro::GetFramework2DSystems().Physics2D);
}

// 호스트가 넘긴 레지스트리에 실제로 붙었는지 본다. 붙지 않았다면 이 DLL 사본 주소가 나온다.
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::Internal::InstanceRegistry::Get());
}

extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetLocalRegistry() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::Internal::InstanceRegistry::Local());
}

// 이름표도 같은 함정이 있다. 붙지 않았다면 이 DLL 사본 주소가 나오고,
// 호스트가 지은 태그의 원문을 이 안에서는 되찾지 못한다(D-51).
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetNameTable() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::NameTable::Get());
}

// 글자 저장소도 같다(D-211). 붙지 않았다면 DLL 사본의 빈 저장소가 나온다.
extern "C" __declspec(dllexport) std::uintptr_t JBroScriptProbe_GetTextStore() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&JBro::TextStore::Get());
}

// 호스트가 만든 글자를 **DLL 쪽 코덱으로** 읽는다. 스크립트 타입의 `TextId` 필드가 파일에 적힐 때 지나는 길이다.
extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_WriteText(
    JBro::UInt32 index, JBro::UInt32 generation, char* buffer, JBro::UInt32 capacity) noexcept
{
    JBro::TextId id;
    id.index = index;
    id.generation = generation;
    std::size_t required = 0;
    if (false == JBro::GetTextIdCodec().ToText(&id, buffer, capacity, required))
    {
        return 0;
    }
    return static_cast<JBro::UInt32>(required);
}

// 호스트가 이미 보관한 원문을 DLL 안에서 되찾을 수 있는지 직접 본다.
extern "C" __declspec(dllexport) const char* JBroScriptProbe_ResolveName(JBro::UInt64 id) noexcept
{
    return JBro::NameTable::Get().Resolve(id);
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe_GetRevision() noexcept
{
    return JBRO_SCRIPT_PROBE_REVISION;
}

// DLL 안의 스크립트가 서비스로 읽는 키보드다(D-214). 호스트가 접은 이번 프레임의 입력이 이 DLL 사본에 닿는지 본다.
extern "C" __declspec(dllexport) JBro::Bool JBroScriptProbe_IsKeyDown(std::uint16_t key) noexcept
{
    return JBro::GetInputServices().Input.Keyboard().IsDown(static_cast<JBro::Key>(key));
}

// 시간과 난수가 DLL 까지 닿는다(D-242). 공통 컨텍스트는 `BindScriptModuleContexts` 가 묶는다 - 확장 블록이 필요 없다.
extern "C" __declspec(dllexport) JBro::Float JBroScriptProbe_GetDeltaTime() noexcept
{
    return JBro::GetServiceContext().Time.DeltaTime();
}

extern "C" __declspec(dllexport) JBro::UInt64 JBroScriptProbe_GetFrameCount() noexcept
{
    return JBro::GetServiceContext().Time.FrameCount();
}

extern "C" __declspec(dllexport) void JBroScriptProbe_SetRandomSeed(JBro::UInt64 seed) noexcept
{
    JBro::GetServiceContext().Random.SetSeed(seed);
}

extern "C" __declspec(dllexport) JBro::Int32 JBroScriptProbe_RandomRange(JBro::Int32 min, JBro::Int32 max) noexcept
{
    return JBro::GetServiceContext().Random.Range(min, max);
}

// 디버그 선이 DLL 에서 호스트의 저장소로 간다(D-243). 서비스는 공통 시스템 컨텍스트의 저장소에 쌓는다.
extern "C" __declspec(dllexport) void JBroScriptProbe_DrawLine() noexcept
{
    JBro::GetFramework2DServices().DebugDraw.Line({0.0f, 0.0f}, {1.0f, 0.0f});
}

// DLL 안의 스크립트가 세이브를 쓰고 되읽는다(D-218). 읽은 바이트는 이 DLL 의 힙에 놓인다 - 호스트가 DLL 의 컨테이너를 키우지 않는지 본다.
extern "C" __declspec(dllexport) JBro::Bool JBroScriptProbe_SaveRoundTrip(const char* slot, const char* text) noexcept
{
    const JBro::Service::SaveService& save = JBro::GetSaveServices().Save;
    const JBro::String written(text);
    if (false == save.WriteText(slot, written))
    {
        return false;
    }
    JBro::String read;
    return save.ReadText(slot, read) && read == written;
}

// DLL 안의 스크립트가 로케일을 바꾸고 키의 글자를 제 힙에 받는다(D-226). 돌려주는 것은 글자 길이이고, 로케일을 못 바꾸면 0 이다.
extern "C" __declspec(dllexport) std::size_t JBroScriptProbe_Localize(const char* locale, const char* key, char* buffer,
    std::size_t capacity) noexcept
{
    const JBro::Service::LocalizationService& localization = JBro::GetLocalizationServices().Localization;
    if (false == localization.SetLocale(locale) || localization.GetLocale() != locale)
    {
        return 0;
    }
    const JBro::String text = localization.GetText(key);
    if (text.size() > capacity)
    {
        return 0;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        buffer[index] = text[index];
    }
    return text.size();
}

// DLL 안의 스크립트가 리바인딩을 글자로 받는다(D-218). 서비스가 크기를 묻고 제 힙에 버퍼를 키운다.
extern "C" __declspec(dllexport) std::size_t JBroScriptProbe_WriteBindingOverrides(char* buffer, std::size_t capacity) noexcept
{
    JBro::String text;
    if (false == JBro::GetInputServices().Input.WriteBindingOverrides(text) || text.size() > capacity)
    {
        return 0;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        buffer[index] = text[index];
    }
    return text.size();
}
