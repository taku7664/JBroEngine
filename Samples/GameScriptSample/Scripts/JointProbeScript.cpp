#include "pch.h"
#include "Scripts/JointProbeScript.h"

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/Physics2DJoints.h"
#include "GameFramework/Scripting/ScriptAPI_Physics.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 씬과 겹치지 않게 멀리 잡는다.
	constexpr float PROBE_ORIGIN_X = -2000.0f;
	constexpr float PROBE_ORIGIN_Y = 2000.0f;

	// 조인트가 수렴할 시간. 50Hz 고정 스텝 기준 120 스텝 = 2.4초.
	constexpr int SETTLE_STEPS = 120;

	Vector2 At(float x, float y)
	{
		return Vector2(PROBE_ORIGIN_X + x, PROBE_ORIGIN_Y + y);
	}

	std::string Format(float value)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[JointProbe] " + message);
		}
	}

	float DistanceBetween(const Vector2& a, const Vector2& b)
	{
		return (a - b).Length();
	}
}

void CJointProbeScript::Check(bool condition, const char* label)
{
	if (condition)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label);
}

void CJointProbeScript::CheckNear(float actual, float expected, float tolerance, const char* label)
{
	if (std::abs(actual - expected) <= tolerance)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + Format(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + Format(expected)
		+ " (+-" + Format(tolerance) + "), got " + Format(actual));
}

CGameObject* CJointProbeScript::SpawnStatic(const char* name, const Vector2& position)
{
	// Rigidbody2D 를 달지 않으면 역질량 0 — 조인트가 밀어도 움직이지 않는 쪽이 된다.
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject* object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}
	object->Local.Position = position;
	m_spawned.push_back(object->SafeFromThis());
	return object;
}

CGameObject* CJointProbeScript::SpawnDynamic(const char* name, const Vector2& position, bool useGravity)
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject* object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}
	object->Local.Position = position;

	Rigidbody2D* body = canvas->AddComponent<Rigidbody2D>(*object);
	body->BodyType   = EPhysics2DBodyType::Dynamic;
	body->UseGravity = useGravity;
	body->Mass       = 1.0f;
	body->SetMass(1.0f);
	// 감쇠 없이 봐야 구속이 실제로 잡는 것인지 감쇠가 멈춘 것인지 구분된다.
	body->LinearDamping = 0.0f;

	m_spawned.push_back(object->SafeFromThis());
	return object;
}

void CJointProbeScript::TearDown()
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	for (SafePtr<CGameObject>& object : m_spawned)
	{
		if (object.IsValid())
		{
			canvas->DestroyGameObject(object.TryGet());
		}
	}
	m_spawned.clear();
}

void CJointProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CJointProbeScript::RunProbe()
{
	ProbeLog("START — building joint rigs");

	SafePtr<CGameCanvas> canvas = GetCanvas();

	// ── 1) 월드 고정점에 매단 거리 조인트(진자) ──────────────────────────────
	// ConnectedObject 를 비우면 ConnectedAnchor 가 월드 좌표로 해석된다.
	CGameObject* pendulum = SpawnDynamic("Joint.Pendulum", At(0.0f, -2.0f), true);
	DistanceJoint2D* pendulumJoint = canvas->AddComponent<DistanceJoint2D>(*pendulum);
	pendulumJoint->ConnectedAnchor       = At(0.0f, 0.0f);
	pendulumJoint->AutoConfigureDistance = true;

	// ── 2) 정적 오브젝트에 매단 거리 조인트 ──────────────────────────────────
	CGameObject* hookAnchor = SpawnStatic("Joint.HookAnchor", At(10.0f, 0.0f));
	CGameObject* hooked     = SpawnDynamic("Joint.Hooked", At(10.0f, -3.0f), true);
	DistanceJoint2D* hookJoint = canvas->AddComponent<DistanceJoint2D>(*hooked);
	hookJoint->ConnectedObject.SetGuid(hookAnchor->GetInstanceGuid());
	hookJoint->AutoConfigureDistance = true;

	// ── 3) 로프(MaxDistanceOnly) — 늘어졌을 땐 밀지 않아야 한다 ──────────────
	// 중력을 끄고 쉬는 길이보다 훨씬 가깝게 둔다. 강체 구속이면 3 으로 튕겨 나가고,
	// 로프면 그 자리에 그대로 있어야 한다.
	CGameObject* slack = SpawnDynamic("Joint.SlackRope", At(20.0f, -1.0f), false);
	DistanceJoint2D* slackJoint = canvas->AddComponent<DistanceJoint2D>(*slack);
	slackJoint->ConnectedAnchor       = At(20.0f, 0.0f);
	slackJoint->AutoConfigureDistance = false;
	slackJoint->Distance              = 3.0f;
	slackJoint->MaxDistanceOnly       = true;

	// ── 4) 로프 — 끝까지 떨어지면 그 길이에서 멈춰야 한다 ────────────────────
	CGameObject* taut = SpawnDynamic("Joint.TautRope", At(30.0f, -1.0f), true);
	DistanceJoint2D* tautJoint = canvas->AddComponent<DistanceJoint2D>(*taut);
	tautJoint->ConnectedAnchor       = At(30.0f, 0.0f);
	tautJoint->AutoConfigureDistance = false;
	tautJoint->Distance              = 3.0f;
	tautJoint->MaxDistanceOnly       = true;

	// ── 5) 스프링 모드 — 늘어난 채 놓으면 쉬는 길이로 수렴해야 한다 ──────────
	CGameObject* spring = SpawnDynamic("Joint.Spring", At(40.0f, -5.0f), false);
	DistanceJoint2D* springJoint = canvas->AddComponent<DistanceJoint2D>(*spring);
	springJoint->ConnectedAnchor       = At(40.0f, 0.0f);
	springJoint->AutoConfigureDistance = false;
	springJoint->Distance              = 2.0f;
	springJoint->Frequency             = 4.0f;
	springJoint->DampingRatio          = 1.0f;   // 임계 감쇠 — 진동 없이 수렴

	// ── 6) 힌지 — 앵커 점이 붙어 있어야 한다 ─────────────────────────────────
	CGameObject* hingeAnchor = SpawnStatic("Joint.HingeAnchor", At(50.0f, 0.0f));
	CGameObject* hinged      = SpawnDynamic("Joint.Hinged", At(50.0f, 0.0f), true);
	HingeJoint2D* hinge = canvas->AddComponent<HingeJoint2D>(*hinged);
	hinge->ConnectedObject.SetGuid(hingeAnchor->GetInstanceGuid());
	hinge->AutoConfigureConnectedAnchor = true;

	// 첫 고정 스텝에서 AutoConfigure 가 확정된다.
	co_await Wait::FixedUpdate();

	ProbeLog("--- auto configure ---");
	CheckNear(pendulumJoint->Distance, 2.0f, 0.01f, "pendulum auto distance from placement");
	CheckNear(hookJoint->Distance, 3.0f, 0.01f, "hooked auto distance from placement");
	Check(hinge->RuntimeConfigured, "hinge auto configured");

	// 굴린다.
	for (int i = 0; i < SETTLE_STEPS; ++i)
	{
		co_await Wait::FixedUpdate();
	}

	ProbeLog("--- distance joint ---");
	// 진자는 흔들려도 반지름은 유지되어야 한다.
	const float pendulumRadius = DistanceBetween(pendulum->Local.Position, At(0.0f, 0.0f));
	CheckNear(pendulumRadius, 2.0f, 0.2f, "pendulum keeps its radius under gravity");
	Check(pendulum->Local.Position.y < At(0.0f, 0.0f).y, "pendulum hangs below its anchor");

	const float hookedDistance = DistanceBetween(hooked->Local.Position, hookAnchor->Local.Position);
	CheckNear(hookedDistance, 3.0f, 0.2f, "hooked body keeps distance to static anchor");
	CheckNear(hookAnchor->Local.Position.x, At(10.0f, 0.0f).x, 0.001f, "static anchor did not move");
	CheckNear(hookAnchor->Local.Position.y, At(10.0f, 0.0f).y, 0.001f, "static anchor did not move (y)");

	ProbeLog("--- rope (max distance only) ---");
	const float slackDistance = DistanceBetween(slack->Local.Position, At(20.0f, 0.0f));
	// 강체 구속이었다면 3 으로 밀려났을 자리다.
	CheckNear(slackDistance, 1.0f, 0.05f, "slack rope does not push when shorter than rest");

	const float tautDistance = DistanceBetween(taut->Local.Position, At(30.0f, 0.0f));
	CheckNear(tautDistance, 3.0f, 0.25f, "taut rope stops at its length");
	Check(tautDistance > 1.5f, "taut rope actually fell before catching");

	ProbeLog("--- spring ---");
	const float springDistance = DistanceBetween(spring->Local.Position, At(40.0f, 0.0f));
	CheckNear(springDistance, 2.0f, 0.35f, "spring converges to rest length");

	ProbeLog("--- hinge ---");
	const float hingeGap = DistanceBetween(hinged->Local.Position, hingeAnchor->Local.Position);
	CheckNear(hingeGap, 0.0f, 0.2f, "hinged body stays pinned to anchor point");
	CheckNear(hingeAnchor->Local.Position.y, At(50.0f, 0.0f).y, 0.001f, "hinge anchor did not move");

	const int total = m_passCount + m_failCount;
	if (0 == m_failCount)
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/" + std::to_string(total) + " PASS");
	}
	else
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/" + std::to_string(total)
			+ " PASS  (" + std::to_string(m_failCount) + " FAIL)");
	}

	TearDown();
	ProbeLog("END — probe objects destroyed");
	co_return;
}
