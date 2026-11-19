// 3D 스크립트 DLL 의 시험 프로브다(cpp-script-plan §3.7, D-412). 사용자의 3D 스크립트 프로젝트와 같은 모양이다 -
// 프렐류드 한 줄, `GameScript3D` 를 잇는 스크립트, 스크립트마다 등록 한 줄, 진입점 한 줄.
#include <JBro/ScriptAPI.h>
#include <JBro/Framework3D/Scripting/ScriptModule.h>

// 내보내기는 `extern "C"` 이고 시그니처는 엔진 값 타입이다(D-290). C4190 은 부르는 쪽이 같은 타입의 C++ 이라 해당하지 않는다.
#pragma warning(disable : 4190)

namespace
{
    JBro::UInt32 g_startCount = 0;
    JBro::UInt32 g_updateCount = 0;
}

// 매 프레임 제 오브젝트를 +X 로 `Speed` 만큼(초당) 민다. 같은 오브젝트에 `Light3D` 가 있으면 세기를 `Speed` 로 둔다 -
// 3D 에만 있는 핸들이 DLL 에서 엔진의 컴포넌트에 닿는지 본다.
class ProbeMover3D final : public GameScript3D
{
    JBRO_SCRIPT_BODY(ProbeMover3D)
public:
    JBRO_FIELD(Float, Speed) = 2.0f;

protected:
    void OnStart() override
    {
        ++g_startCount;
    }

    void OnUpdate() override
    {
        ++g_updateCount;
        Transform3D transform = GetGameObject().GetComponent<Transform3D>();
        Vector3 position = transform.GetPosition();
        position.x = position.x + Speed * GetServiceContext().Time.DeltaTime();
        transform.SetPosition(position);
        Light3D light = GetGameObject().GetComponent<Light3D>();
        if (light.IsValid())
        {
            light.SetIntensity(Speed);
        }
    }
};

JBRO_REGISTER_SCRIPT_3D(ProbeMover3D);

JBRO_SCRIPT_MODULE_3D()

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe3D_GetStartCount() noexcept
{
    return g_startCount;
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe3D_GetUpdateCount() noexcept
{
    return g_updateCount;
}
