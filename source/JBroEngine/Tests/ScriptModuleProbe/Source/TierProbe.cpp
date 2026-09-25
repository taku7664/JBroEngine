// 스크립트 타깃의 경계를 실제 스크립트 프로젝트 설정에서 확인하는 번역 단위다.
//
// 양성: 프렐류드 한 줄(<JBro/ScriptAPI.h>)만으로 스크립트가 서는지 본다. 이 파일은 엔진 include
// 경로를 하나도 받지 않으므로, 컴파일된다는 사실 자체가 D-18 의 한 줄 include 계약을 증명한다.
//
// 음성: 아래 세 블록은 각각 Tier E 모듈의 헤더를 집는다. 스크립트 타깃에는 그 경로가 없으므로
// C1083 으로 실패해야 한다(ProjectRule §11). 확인 방법은 아래 세 줄이며, 셋 다 실패해야 정상이다.
//
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Canvas
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Host
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=FrameworkSystem
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=GameObject
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Audio
//
// 마지막 것만 C1083 이 아니라 #error 다. GameObject.h 는 스크립트 DLL 이 링크하는 모듈에
// 있어 경로로는 막을 수 없고, 프렐류드를 거쳤는지로 막는다(§9.5).

#include <JBro/ScriptAPI.h>

#if defined(JBRO_TIER_PROBE_CANVAS)
#include <JBro/Canvas/Canvas.h>
#endif

#if defined(JBRO_TIER_PROBE_HOST)
#include <JBro/Host/EngineInstance.h>
#endif

#if defined(JBRO_TIER_PROBE_FRAMEWORK_SYSTEM)
#include <JBro/Framework2DSystem/Framework2D.h>
#endif

#if defined(JBRO_TIER_PROBE_AUDIO)
// 오디오 믹서는 Tier E 다(D-197). 스크립트는 값 서비스만 본다.
#include <JBro/Audio/AudioMixer.h>
#endif

#if defined(JBRO_TIER_PROBE_GAME_OBJECT)
// 프렐류드를 거치지 않고 직접 집는 모양을 흉내낸다. 표식이 없으므로 #error 여야 한다.
#undef JBRO_SCRIPT_PRELUDE
#include <JBro/Runtime/GameObject.h>
#endif

#include <type_traits>

namespace
{
    // 사용자가 쓰는 형태 그대로다. 소유 오브젝트는 핸들로 받고, 컴포넌트는 Ref<T> 로 저장한다.
    JBRO_SCRIPT(TierProbeScript) final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Probe::TierProbeScript";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate(float deltaTime) override
        {
            m_elapsed += deltaTime;
            if (Component::Transform2D* transform = m_transform.Get())
            {
                transform->position.x = m_elapsed;
            }
        }

    private:
        GameObjectHandle           m_target;
        Ref<Component::Transform2D> m_transform;
        float                      m_elapsed = 0.0f;
    };

    // 스크립트가 보는 참조의 형태를 고정한다(D-5, D-44).
    static_assert(sizeof(GameObjectHandle) == 16,
        "a script must see the 16-byte object handle");
    static_assert(sizeof(Ref<Component::Transform2D>) == 24,
        "a script must see the 24-byte component reference");
    static_assert(std::is_same_v<decltype(TierProbeScript{}.GetOwner()), GameObjectHandle>,
        "a script must receive its owner as a handle, not a raw object");
}
