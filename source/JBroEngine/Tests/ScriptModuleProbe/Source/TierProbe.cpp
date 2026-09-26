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
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Task
//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=TextRendering
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

#if defined(JBRO_TIER_PROBE_TASK)
// 태스크 관리자는 Tier E 다(D-209). 워커가 SafePtr 를 못 만지는 계약을 스크립트에 넘기지 않는다.
#include <JBro/Task/TaskManager.h>
#endif

#if defined(JBRO_TIER_PROBE_TEXT_RENDERING)
// 텍스트 라이브러리는 Tier E 다(D-222). 렌더러와 에셋 시스템을 쥐므로 스크립트에 넘기지 않는다.
#include <JBro/TextRendering/TextLibrary.h>
#endif

//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Input
#if defined(JBRO_TIER_PROBE_INPUT)
// 입력을 접는 시스템은 Tier E 다(D-214). 스크립트는 상태와 뷰만 본다.
#include <JBro/Input/InputSystem.h>
#endif

//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Save
#if defined(JBRO_TIER_PROBE_SAVE)
// 파일을 만지는 세이브 구현은 호스트의 것이다(D-218). 스크립트는 `SaveService` 만 본다.
#include <JBro/Host/SaveStorage.h>
#endif

//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Localization
#if defined(JBRO_TIER_PROBE_LOCALIZATION)
// 문자열 표를 모으는 구현은 호스트의 것이다(D-226). 스크립트는 `LocalizationService` 만 본다.
#include <JBro/Host/GameLocalization.h>
#endif

//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=LocalizationSystem
#if defined(JBRO_TIER_PROBE_LOCALIZATION_SYSTEM)
// 조회 인터페이스도 프렐류드가 내놓지 않는다. 경로로는 막을 수 없는 Tier S 헤더라 프렐류드에 없다는 것을 쓰임으로 본다:
// 프렐류드만 include 한 번역 단위에서 `System::ILocalization` 은 알려지지 않은 이름이어야 한다(C2039/C3083).
using ProbeLocalizationInterface = JBro::System::ILocalization;
#endif

//   msbuild JBroEngine.slnx /p:Configuration=Debug /p:Platform=x64 /p:JBroTierProbe=Package
#if defined(JBRO_TIER_PROBE_PACKAGE)
// 에셋 패키지는 Tier E 다(D-227). 스크립트는 패키지를 모르고 에셋 서비스만 본다.
#include <JBro/Package/PackageReader.h>
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
