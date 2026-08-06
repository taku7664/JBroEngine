#include "pch.h"
#include "Scripts/QueryProbeScript.h"

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Scripting/ScriptAPI_Physics.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	// 프로브 전용 충돌 레이어 비트. 모든 질의에 이 마스크를 넘겨 씬 콜라이더를 배제한다.
	constexpr std::uint32_t PROBE_LAYER = 0x00000100u;
	// 아무 콜라이더도 갖지 않는 비트 — "마스크로 걸러지면 못 찾는다" 검증용.
	constexpr std::uint32_t EMPTY_LAYER = 0x00000200u;

	// 본 게임 콘텐츠와 눈으로도 안 겹치게 멀리 배치한다.
	constexpr float PROBE_ORIGIN_X = 1000.0f;
	constexpr float PROBE_ORIGIN_Y = 1000.0f;

	// 부동소수 비교 허용오차. 질의 결과는 곱셈 몇 번 수준이라 이 정도면 충분히 빡빡하다.
	constexpr float TOLERANCE = 1e-3f;

	// 프로브 원점 기준 상대좌표 → 월드좌표.
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

	std::string Format(const Vector2& value)
	{
		return "(" + Format(value.x) + ", " + Format(value.y) + ")";
	}

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[QueryProbe] " + message);
		}
	}

	// 콜라이더 공통 설정 — 프로브 레이어로 좁혀 씬과 물리적으로도 섞이지 않게 한다.
	void ApplyProbeFilter(std::uint32_t& layer, std::uint32_t& mask)
	{
		layer = PROBE_LAYER;
		mask  = PROBE_LAYER;
	}
}

// ── 검사 기록 ────────────────────────────────────────────────────────────────

void CQueryProbeScript::Check(bool condition, const char* label)
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

void CQueryProbeScript::CheckNear(float actual, float expected, const char* label)
{
	if (std::abs(actual - expected) <= TOLERANCE)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + Format(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + Format(expected) + ", got " + Format(actual));
}

void CQueryProbeScript::CheckVector(const Vector2& actual, const Vector2& expected, const char* label)
{
	if (std::abs(actual.x - expected.x) <= TOLERANCE && std::abs(actual.y - expected.y) <= TOLERANCE)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + Format(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + Format(expected) + ", got " + Format(actual));
}

void CQueryProbeScript::CheckObject(const CGameObject* actual, const CGameObject* expected, const char* label)
{
	if (actual == expected)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	const std::string actualName   = nullptr != actual ? actual->GetName() : "<null>";
	const std::string expectedName = nullptr != expected ? expected->GetName() : "<null>";
	ProbeLog(std::string("FAIL  ") + label + "   expected " + expectedName + ", got " + actualName);
}

// ── 배치 ─────────────────────────────────────────────────────────────────────

CGameObject* CQueryProbeScript::SpawnBox(const char* name, const char* tag, const Vector2& position, const Vector2& size)
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject*         object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}

	object->Tag           = tag;
	object->Local.Position = position;
	// 콜라이더 로컬 박스는 단위 크기(±0.5)라 Scale 이 곧 박스의 가로/세로다.
	object->Local.Scale   = size;

	PolygonCollider2D* collider = canvas->AddComponent<PolygonCollider2D>(*object);
	ApplyProbeFilter(collider->CollisionLayer, collider->CollisionMask);

	m_spawned.push_back(object->SafeFromThis());
	return object;
}

CGameObject* CQueryProbeScript::SpawnCircle(const char* name, const char* tag, const Vector2& position, float radius)
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject*         object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}

	object->Tag            = tag;
	object->Local.Position = position;
	object->Local.Scale    = Vector2(1.0f, 1.0f);   // WorldRadius = Radius × max(scaleX, scaleY)

	CircleCollider2D* collider = canvas->AddComponent<CircleCollider2D>(*object);
	collider->Radius = radius;
	ApplyProbeFilter(collider->CollisionLayer, collider->CollisionMask);

	m_spawned.push_back(object->SafeFromThis());
	return object;
}

CGameObject* CQueryProbeScript::SpawnTwinCollider(const char* name, const char* tag, const Vector2& position)
{
	// 콜라이더 2개짜리 오브젝트 — 다건 질의가 오브젝트 단위로 중복 제거하는지 확인용.
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject*         object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}

	object->Tag            = tag;
	object->Local.Position = position;
	object->Local.Scale    = Vector2(2.0f, 2.0f);

	// Offset 은 로컬 공간에서 더해진 뒤 월드 변환된다 → 월드에서 ±2 만큼 벌어진 박스 두 개.
	PolygonCollider2D* left = canvas->AddComponent<PolygonCollider2D>(*object);
	left->Offset = Vector2(-1.0f, 0.0f);
	ApplyProbeFilter(left->CollisionLayer, left->CollisionMask);

	PolygonCollider2D* right = canvas->AddComponent<PolygonCollider2D>(*object);
	right->Offset = Vector2(1.0f, 0.0f);
	ApplyProbeFilter(right->CollisionLayer, right->CollisionMask);

	m_spawned.push_back(object->SafeFromThis());
	return object;
}

CGameObject* CQueryProbeScript::SpawnConcaveL(const char* name, const char* tag, const Vector2& position, float scale)
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	CGameObject*         object = canvas->CreateGameObject(name);
	if (nullptr == object)
	{
		return nullptr;
	}

	object->Tag            = tag;
	object->Local.Position = position;
	object->Local.Scale    = Vector2(scale, scale);

	PolygonCollider2D* collider = canvas->AddComponent<PolygonCollider2D>(*object);
	ApplyProbeFilter(collider->CollisionLayer, collider->CollisionMask);

	// L 자(오목). CCW 순서. 세로 막대(왼쪽) + 가로 막대(아래) — 오른쪽 위가 빈 노치다.
	collider->LocalPoints = {
		Vector2(-0.5f, -0.5f),
		Vector2( 0.5f, -0.5f),
		Vector2( 0.5f, -0.2f),
		Vector2(-0.2f, -0.2f),
		Vector2(-0.2f,  0.5f),
		Vector2(-0.5f,  0.5f),
	};
	// 직접 채운 LocalPoints 를 절차적 재빌드가 덮어쓰지 않게 "빌드 완료" 로 표시한다.
	// (에디터가 정점을 직접 편집했을 때와 같은 상태.)
	collider->MarkProceduralBuilt();

	m_spawned.push_back(object->SafeFromThis());
	return object;
}

void CQueryProbeScript::BuildLayout()
{
	//        y
	//        ↑            [Twin] (콜라이더 2개)          @ (0, 30)
	//        │
	//  [ConcaveL]         O ──ray──→  [WallNear]   [WallFar]
	//  x∈[-25,-15]        (0,0)       x∈[9,11]     x∈[19,21]
	//  노치 x∈(-22,-15]
	//        │            [Ball] r=0.5 @ (0, -10)
	m_wallNear = SpawnBox("QueryProbe.WallNear", "QueryProbe.Wall", At(10.0f, 0.0f), Vector2(2.0f, 10.0f))->SafeFromThis();
	m_wallFar  = SpawnBox("QueryProbe.WallFar",  "QueryProbe.Wall", At(20.0f, 0.0f), Vector2(2.0f, 10.0f))->SafeFromThis();
	m_ball     = SpawnCircle("QueryProbe.Ball",  "QueryProbe.Ball", At(0.0f, -10.0f), 0.5f)->SafeFromThis();
	m_twin     = SpawnTwinCollider("QueryProbe.Twin", "QueryProbe.Twin", At(0.0f, 30.0f))->SafeFromThis();
	m_concave  = SpawnConcaveL("QueryProbe.ConcaveL", "QueryProbe.Concave", At(-20.0f, 0.0f), 10.0f)->SafeFromThis();
}

void CQueryProbeScript::TearDownLayout()
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

// ── 실행 ─────────────────────────────────────────────────────────────────────

void CQueryProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CQueryProbeScript::RunProbe()
{
	ProbeLog("START — building probe layout");
	BuildLayout();

	// 콜라이더의 WorldPoints/WorldAABB/ConvexPieces 는 fixed step 에서 채워진다.
	// 두 번 기다리는 건 첫 스텝이 절차적 빌드/볼록 분해에 쓰일 수 있어서다.
	co_await Wait::FixedUpdate();
	co_await Wait::FixedUpdate();

	SafePtr<CGameCanvas> canvas  = GetCanvas();
	CPhysics2DSystem*    physics = canvas->GetPhysics2DSystem();
	if (nullptr == physics)
	{
		ProbeLog("ABORT — Physics2DSystem 없음");
		co_return;
	}

	const Vector2 origin = At(0.0f, 0.0f);
	const Vector2 right(1.0f, 0.0f);
	const Vector2 up(0.0f, 1.0f);
	const Vector2 down(0.0f, -1.0f);

	// ── 태그 / 이름 조회 ─────────────────────────────────────────────────────
	ProbeLog("--- tag / name ---");
	CheckObject(canvas->FindByTag("QueryProbe.Ball").TryGet(), m_ball.TryGet(), "FindByTag finds ball");
	CheckObject(canvas->FindByName("QueryProbe.WallNear").TryGet(), m_wallNear.TryGet(), "FindByName finds near wall");
	Check(false == canvas->FindByTag("QueryProbe.NoSuchTag").IsValid(), "FindByTag unknown tag = empty");
	Check(false == canvas->FindByTag("").IsValid(), "FindByTag empty string = empty");
	Check(false == canvas->FindByName("").IsValid(), "FindByName empty string = empty");

	std::vector<CGameObject*> found;
	canvas->FindAllByTag("QueryProbe.Wall", found);
	CheckNear(static_cast<float>(found.size()), 2.0f, "FindAllByTag wall count");

	canvas->FindAllByTag("QueryProbe.NoSuchTag", found);
	CheckNear(static_cast<float>(found.size()), 0.0f, "FindAllByTag unknown tag count");

	// ── Raycast ──────────────────────────────────────────────────────────────
	ProbeLog("--- raycast ---");
	RaycastHit2D hit;
	Check(physics->Raycast(origin, right, 30.0f, hit, PROBE_LAYER), "raycast hits");
	CheckObject(hit.Object, m_wallNear.TryGet(), "raycast picks nearest wall");
	CheckNear(hit.Distance, 9.0f, "raycast distance");
	CheckVector(hit.Normal, Vector2(-1.0f, 0.0f), "raycast normal");

	Check(false == physics->Raycast(origin, right, 30.0f, hit, EMPTY_LAYER), "raycast layer mask filters out");
	Check(false == physics->Raycast(origin, right, 5.0f, hit, PROBE_LAYER), "raycast short range misses");

	// ── RaycastAll ───────────────────────────────────────────────────────────
	ProbeLog("--- raycastAll ---");
	std::vector<RaycastHit2D> hits;
	physics->RaycastAll(origin, right, 30.0f, hits, PROBE_LAYER);
	CheckNear(static_cast<float>(hits.size()), 2.0f, "raycastAll hit count");
	if (2 == hits.size())
	{
		CheckNear(hits[0].Distance, 9.0f, "raycastAll first distance");
		CheckNear(hits[1].Distance, 19.0f, "raycastAll second distance");
		CheckObject(hits[0].Object, m_wallNear.TryGet(), "raycastAll sorted near first");
		CheckObject(hits[1].Object, m_wallFar.TryGet(), "raycastAll sorted far second");
	}

	// ── OverlapPoint / OverlapCircle ─────────────────────────────────────────
	ProbeLog("--- overlap point / circle ---");
	CheckObject(physics->OverlapPoint(At(10.0f, 0.0f), PROBE_LAYER), m_wallNear.TryGet(), "overlapPoint inside wall");
	CheckObject(physics->OverlapPoint(At(15.0f, 0.0f), PROBE_LAYER), nullptr, "overlapPoint in gap = null");

	physics->OverlapCircle(At(10.0f, 0.0f), 0.5f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "overlapCircle wall count");

	// 콜라이더 2개짜리 오브젝트가 한 번만 나와야 한다(중복 제거 계약).
	physics->OverlapCircle(At(0.0f, 30.0f), 5.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "overlapCircle dedupes twin collider object");

	// ── OverlapBox ───────────────────────────────────────────────────────────
	ProbeLog("--- overlap box ---");
	physics->OverlapBox(At(7.5f, 0.0f), Vector2(2.0f, 1.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "overlapBox reaching wall");

	physics->OverlapBox(At(6.0f, 0.0f), Vector2(2.0f, 1.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 0.0f, "overlapBox short of wall");

	// 같은 중심·같은 반변이라도 45° 회전하면 대각선이 길어져 벽에 닿는다(회전 반영 확인).
	physics->OverlapBox(At(6.9f, 0.0f), Vector2(2.0f, 1.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 0.0f, "overlapBox unrotated misses");

	physics->OverlapBox(At(6.9f, 0.0f), Vector2(2.0f, 1.0f), 3.14159265f * 0.25f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "overlapBox rotated 45deg reaches");

	physics->OverlapBox(At(0.0f, 30.0f), Vector2(5.0f, 5.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "overlapBox dedupes twin collider object");

	// ── CircleCast ───────────────────────────────────────────────────────────
	ProbeLog("--- circle cast ---");
	Check(physics->CircleCast(origin, 0.5f, right, 30.0f, hit, PROBE_LAYER), "circleCast hits wall");
	CheckObject(hit.Object, m_wallNear.TryGet(), "circleCast picks nearest wall");
	CheckNear(hit.Distance, 8.5f, "circleCast distance (9 - radius)");
	CheckVector(hit.Normal, Vector2(-1.0f, 0.0f), "circleCast normal");

	// 원 vs 원 — 중심 거리가 반지름 합(1.0)이 될 때 접촉.
	Check(physics->CircleCast(At(0.0f, -20.0f), 0.5f, up, 30.0f, hit, PROBE_LAYER), "circleCast hits ball");
	CheckObject(hit.Object, m_ball.TryGet(), "circleCast ball object");
	CheckNear(hit.Distance, 9.0f, "circleCast ball distance");
	CheckVector(hit.Normal, Vector2(0.0f, -1.0f), "circleCast ball normal");

	// 벽 안에서 출발 — 이미 겹쳤으므로 거리 0.
	Check(physics->CircleCast(At(10.0f, 0.0f), 0.5f, right, 5.0f, hit, PROBE_LAYER), "circleCast from inside hits");
	CheckNear(hit.Distance, 0.0f, "circleCast initial overlap distance 0");

	// ── BoxCast ──────────────────────────────────────────────────────────────
	ProbeLog("--- box cast ---");
	Check(physics->BoxCast(origin, Vector2(0.5f, 0.5f), 0.0f, right, 30.0f, hit, PROBE_LAYER), "boxCast hits wall");
	CheckObject(hit.Object, m_wallNear.TryGet(), "boxCast picks nearest wall");
	CheckNear(hit.Distance, 8.5f, "boxCast distance (9 - halfWidth)");
	CheckVector(hit.Normal, Vector2(-1.0f, 0.0f), "boxCast normal");

	// 45° 회전하면 진행 방향 최전방이 반대각선(0.5√2)이 된다.
	Check(physics->BoxCast(origin, Vector2(0.5f, 0.5f), 3.14159265f * 0.25f, right, 30.0f, hit, PROBE_LAYER),
	      "boxCast rotated hits wall");
	CheckNear(hit.Distance, 9.0f - 0.5f * std::sqrt(2.0f), "boxCast rotated distance (9 - halfDiagonal)");

	// 박스 스윕 vs 원 — 관점 뒤집기 경로.
	Check(physics->BoxCast(At(0.0f, -20.0f), Vector2(0.5f, 0.5f), 0.0f, up, 30.0f, hit, PROBE_LAYER), "boxCast hits ball");
	CheckObject(hit.Object, m_ball.TryGet(), "boxCast ball object");
	CheckNear(hit.Distance, 9.0f, "boxCast ball distance");
	CheckVector(hit.Normal, Vector2(0.0f, -1.0f), "boxCast ball normal");

	Check(false == physics->BoxCast(origin, Vector2(0.5f, 0.5f), 0.0f, right, 30.0f, hit, EMPTY_LAYER),
	      "boxCast layer mask filters out");

	// ── 오목 폴리곤 (ConvexPieces 경로) ──────────────────────────────────────
	// L 자: 세로 막대 x∈[-25,-22], 가로 막대 y∈[-5,-2], 노치(빈 공간) x∈(-22,-15] y∈(-2,5].
	// 볼록 껍질로 처리하면 노치가 "안쪽"으로 잘못 잡힌다 — 그걸 잡는 검사들이다.
	ProbeLog("--- concave polygon ---");
	CheckObject(physics->OverlapPoint(At(-20.0f, -3.5f), PROBE_LAYER), m_concave.TryGet(), "concave: point in bottom bar");
	CheckObject(physics->OverlapPoint(At(-23.5f, 3.0f), PROBE_LAYER), m_concave.TryGet(), "concave: point in vertical bar");
	CheckObject(physics->OverlapPoint(At(-18.0f, 2.0f), PROBE_LAYER), nullptr, "concave: point in notch = null");

	physics->OverlapBox(At(-20.0f, -3.5f), Vector2(1.0f, 1.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 1.0f, "concave: box in bottom bar");

	physics->OverlapBox(At(-18.0f, 2.0f), Vector2(1.0f, 1.0f), 0.0f, found, PROBE_LAYER);
	CheckNear(static_cast<float>(found.size()), 0.0f, "concave: box in notch = miss");

	// 노치를 통과해 떨어져 가로 막대 윗면(y = -2)에 착지.
	Check(physics->BoxCast(At(-18.0f, 10.0f), Vector2(0.5f, 0.5f), 0.0f, down, 30.0f, hit, PROBE_LAYER),
	      "concave: boxCast falls through notch");
	CheckObject(hit.Object, m_concave.TryGet(), "concave: boxCast object");
	CheckNear(hit.Distance, 11.5f, "concave: boxCast lands on bottom bar");
	CheckVector(hit.Normal, Vector2(0.0f, 1.0f), "concave: boxCast normal");

	Check(physics->CircleCast(At(-18.0f, 10.0f), 0.5f, down, 30.0f, hit, PROBE_LAYER),
	      "concave: circleCast falls through notch");
	CheckNear(hit.Distance, 11.5f, "concave: circleCast lands on bottom bar");
	CheckVector(hit.Normal, Vector2(0.0f, 1.0f), "concave: circleCast normal");

	// ── 총평 ─────────────────────────────────────────────────────────────────
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

	TearDownLayout();
	ProbeLog("END — probe objects destroyed");
	co_return;
}
