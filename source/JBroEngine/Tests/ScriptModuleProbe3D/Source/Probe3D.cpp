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
    // ProbeContact3D 가 센 것(D-417).
    JBro::UInt32 g_collisionEnters = 0;
    JBro::UInt32 g_collisionStays = 0;
    JBro::UInt32 g_triggerEnters = 0;
    JBro::UInt32 g_triggerExits = 0;
    JBro::UInt32 g_otherValid = 0;
    JBro::UInt32 g_otherStatic = 0;
    JBro::Float g_collisionNormalY = 0.0f;
    JBro::Float g_rayDistance = -1.0f;
    JBro::UInt32 g_listenerEnters = 0;
    JBro::UInt32 g_listenerOtherValid = 0;
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

// 3D 물리의 훅과 질의가 DLL 경계를 넘는지 본다(D-417). 충돌·트리거 훅을 세고, 매 프레임 제 자리에서 아래로 반직선을 쏜다 -
// 자기 콜라이더는 레이어 비트 2 에 두고 마스크에서 빼므로 바닥에 맞는다.
class ProbeContact3D final : public GameScript3D
{
    JBRO_SCRIPT_BODY(ProbeContact3D)
protected:
    void OnUpdate() override
    {
        const Vector3 position = GetGameObject().GetComponent<Transform3D>().GetPosition();
        RaycastHit3D hit;
        if (GetFramework3DServices().Physics3D.Raycast(position, Vector3{0.0f, -1.0f, 0.0f}, 100.0f, hit, ~JBro::UInt32(0x4u)))
        {
            g_rayDistance = hit.distance;
        }
    }

    void OnCollisionEnter(const Collision3D& hit) override
    {
        ++g_collisionEnters;
        g_collisionNormalY = hit.normal.y;
        g_otherValid = hit.other.IsValid() ? 1u : 0u;
        g_otherStatic = hit.bodyType == Component::BodyType3D::Static ? 1u : 0u;
    }

    void OnCollisionStay(const Collision3D& hit) override
    {
        (void)hit;
        ++g_collisionStays;
    }

    void OnTriggerEnter(const Collision3D& hit) override
    {
        (void)hit;
        ++g_triggerEnters;
    }

    void OnTriggerExit(const Collision3D& hit) override
    {
        (void)hit;
        ++g_triggerExits;
    }
};

JBRO_REGISTER_SCRIPT_3D(ProbeContact3D);

// 부딪힌 상대 쪽(바닥)에 붙는다. 사건의 양쪽 모두가 훅을 받고, 저마다 상대를 안다는 것을 본다.
class ProbeListener3D final : public GameScript3D
{
    JBRO_SCRIPT_BODY(ProbeListener3D)
protected:
    void OnCollisionEnter(const Collision3D& hit) override
    {
        ++g_listenerEnters;
        g_listenerOtherValid += hit.other.IsValid() ? 1u : 0u;
    }
};

JBRO_REGISTER_SCRIPT_3D(ProbeListener3D);

JBRO_SCRIPT_MODULE_3D()

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe3D_GetStartCount() noexcept
{
    return g_startCount;
}

extern "C" __declspec(dllexport) JBro::UInt32 JBroScriptProbe3D_GetUpdateCount() noexcept
{
    return g_updateCount;
}

// 접촉 프로브가 센 것을 한 번에 내준다. 시험은 이 순서로 읽는다.
struct JBroScriptProbe3DContacts
{
    JBro::UInt32 collisionEnters;
    JBro::UInt32 collisionStays;
    JBro::UInt32 triggerEnters;
    JBro::UInt32 triggerExits;
    JBro::UInt32 otherValid;
    JBro::UInt32 otherStatic;
    JBro::Float collisionNormalY;
    JBro::Float rayDistance;
    JBro::UInt32 listenerEnters;
    JBro::UInt32 listenerOtherValid;
};

extern "C" __declspec(dllexport) JBroScriptProbe3DContacts JBroScriptProbe3D_GetContacts() noexcept
{
    return {g_collisionEnters, g_collisionStays, g_triggerEnters, g_triggerExits, g_otherValid, g_otherStatic, g_collisionNormalY, g_rayDistance,
        g_listenerEnters, g_listenerOtherValid};
}
