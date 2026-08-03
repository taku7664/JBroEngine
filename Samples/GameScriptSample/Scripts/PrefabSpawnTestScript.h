#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CPrefabSpawnTestScript ────────────────────────────────────────────────────
// 런타임 프리팹 스폰 예제 겸 자가검증 스크립트.
//
// 사용법: 오브젝트에 붙이고 인스펙터의 Prefab 슬롯에 `.jprefab` 을 드래그한 뒤
//         재생하고 스페이스바를 누른다.
//
// 판정(Debug Log 창):
//  · 스폰마다 `spawned '<이름>' guid=<...>` 가 찍히고 **guid 가 서로 다르면** 통과.
//    프리팹 파일에 적힌 InstanceGuid 는 하나뿐인데, 직렬화가 "이미 캔버스에 살아 있으면
//    새로 발급" 하기 때문에 갈라진다. 같은 guid 가 두 번 나오면 guid 인덱스가 덮어써진 것이고,
//    그러면 Ref 해석과 뷰포트 카메라 지목이 깨진다.
//  · 스폰된 오브젝트가 요청한 위치에 뜨면 위치 지정 오버로드가 반영된 것.
//  · 스폰된 오브젝트에 스크립트가 붙어 있으면 그 OnStart 는 **다음 프레임**에 찍힌다.
//    이건 정상이다 — 스크립트 순회 중에는 실행순서 캐시를 재빌드하지 않는 계약이다.
//
// 음성 케이스:
//  · Prefab 을 비워 두고 누르면 "no prefab assigned" 만 찍히고 아무것도 생기지 않아야 한다.
//  · `.jprefab` 이 아닌 에셋을 넣으면 스폰이 실패하고 "asset is not a prefab" 이 남아야 한다.
//
// 이 샘플 프로젝트는 스크립트 등록을 GameModule.cpp 에서 손으로 한다(에디터 코드 생성기를
// 쓰지 않는다). 그래서 Prefab 슬롯을 인스펙터에 노출하려면 그쪽 ScriptPropertyDesc 목록에도
// 항목이 있어야 한다 — 프로퍼티를 추가/삭제하면 양쪽을 같이 고칠 것.
class CPrefabSpawnTestScript final
	: public CGameScript
	, public InputHandler<"Game">
{
	SCRIPT_CLASS(CPrefabSpawnTestScript)

public:
	// 스폰 대상은 **에셋 참조**로 저작한다. `Asset`(File::Guid = fs::path 파생)으로 들면
	// 호스트↔게임 DLL 경계 POD 규칙을 깨지만, RefBase 는 고정 char 버퍼라 안전하다.
	// 타입을 CPrefabAsset 으로 못박아 두면 인스펙터 피커가 `.jprefab` 만 받는다.
	Ref<CPrefabAsset> Prefab;
	// 스페이스바 한 번에 스폰할 개수.
	Int   SpawnCount   = 3;
	// 스폰 간 가로 간격(월드 유닛). 겹쳐 있으면 개수도 위치 반영도 눈으로 확인이 안 된다.
	Float SpawnSpacing = 1.5f;

protected:
	void OnStart() override;
	bool HandleInput(const InputDeviceContext& ctx) override;

private:
	void SpawnBurst();

	// 지금까지 스폰한 총 개수 — 스폰 위치를 어긋나게 놓는 데 쓴다.
	int m_spawnSerial = 0;
};
