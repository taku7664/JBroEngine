#pragma once

#include "Utillity/Math/Vector2T.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameObject;

// ─────────────────────────────────────────────────────────────────────────────
//  IPrefabSpawner — 런타임 프리팹 인스턴스화.
//
//  왜 인터페이스인가:
//    프리팹을 푸는 일은 결국 YAML 파싱(Serialization 계층)이다. 그런데 게임 스크립트
//    DLL 은 yaml-cpp 를 링크하지 않는다 — `CCanvasManager` 나 `CGameCanvas` 에 이 기능을
//    out-of-line 메서드로 달면 그 obj 가 DLL 링크 클로저에 끌려오고 연쇄로 yaml-cpp 까지
//    끌고 와 링크가 깨진다(CanvasManager.h 의 인라인 강제 주석이 같은 이야기다).
//    추상 인터페이스면 DLL 은 vtable 을 통해 부르기만 하므로 구현 obj 가 안 끌려온다.
//    IAssetManager/IDebugDraw2D 가 스크립트에 노출되는 방식과 같다.
//
//  스폰 시점:
//    **즉시** 실행된다. 캔버스 전환·레이어 로드처럼 프레임 끝으로 미루지 않는다 —
//    스폰한 오브젝트를 그 자리에서 설정해야(총알 방향 등) 쓸모가 있기 때문이고,
//    캔버스가 순회 중 스폰을 이미 전제하기 때문이다(CScriptSystem::OnUpdate 이
//    ScriptIterationGuard 를 걸고, AddScript 는 실행순서 캐시를 dirty 로만 표시한다).
//    그 계약대로 **스폰된 오브젝트의 스크립트는 다음 프레임부터** OnStart/Update 가 돈다.
//
//  주의:
//    · 복원된 오브젝트는 캔버스의 기본 레이어에 붙는다. 다른 레이어로 보내는 건 호출자 몫
//      (`canvas.MoveObjectToLayer`). 부모 연결도 호출자 몫(`obj->SetParent`).
//    · 파괴는 `canvas.DestroyGameObject(obj)` — 별도 API 를 두지 않는다.
//    · 프리팹 내부의 상호 참조(형제를 가리키는 Ref)는 재매핑되지 않는다. 같은 프리팹을
//      두 번 스폰하면 InstanceGuid 는 새로 발급되지만(ObjectSerializer 가 충돌을 감지),
//      그 guid 를 가리키던 Ref 는 따라오지 않는다. 복사/붙여넣기와 같은 기존 한계다.
// ─────────────────────────────────────────────────────────────────────────────
class IPrefabSpawner : public EnableSafeFromThis<IPrefabSpawner>
{
public:
	virtual ~IPrefabSpawner() = default;

	// prefabAssetGuidText = `.jprefab` 에셋의 guid 문자열.
	// 스크립트는 `JPROP() Ref<CPrefabAsset> Bullet;` 으로 저작하고 `Bullet.GuidText()` 를 넘긴다
	// (RefBase 는 고정 크기 char 버퍼라 호스트↔게임 DLL 경계를 넘어도 안전하다).
	// 실패하면 nullptr — guid 미등록, 자산 타입 불일치, 파싱 실패가 원인이고 전부 로그를 남긴다.
	virtual CGameObject* Spawn(const char* prefabAssetGuidText) = 0;

	// 위치를 지정하는 편의 오버로드. 가상이 아니다 — 가상 함수의 기본 인자는 정적 바인딩이라
	// 파생에서 다른 값을 주면 조용히 어긋난다. 스폰 직후 같은 호출 스택에서 트랜스폼을 쓰므로
	// "원점에 한 프레임 보였다가 이동" 같은 틈은 생기지 않는다.
	// 회전/스케일은 반환된 오브젝트의 `GetTransform()` 을 직접 만지면 된다.
	CGameObject* Spawn(const char* prefabAssetGuidText, const Vector2& position);

	// 캐시된 프리팹 텍스트를 버린다. 에디터에서 `.jprefab` 을 고친 뒤 다시 재생할 때
	// 옛 내용이 스폰되지 않게 시뮬레이션 정지 시 호출된다.
	virtual void ClearCache() = 0;
};
