#include "pch.h"
#include "IPrefabSpawner.h"

#include "GameFramework/Object/GameObject.h"

// 이 파일이 존재하는 이유: 위치 지정 오버로드가 CGameObject 의 완전 타입을 필요로 한다.
// 헤더에 인라인으로 두려면 GameObject.h 를 끌어와야 하는데, 그러면 인터페이스 헤더를
// 쓰는 쪽마다 GameFramework 를 딸려 보내게 된다. 반대로 여기 두는 건 안전하다 —
// GameObject.obj 는 게임 스크립트 DLL 이 이미 링크하고 있고(스크립트가 오브젝트를 직접
// 만진다), 이 파일은 직렬화 계층을 건드리지 않으므로 yaml-cpp 를 끌어오지 않는다.

CGameObject* IPrefabSpawner::Spawn(const char* prefabAssetGuidText, const Vector2& position)
{
	CGameObject* spawned = Spawn(prefabAssetGuidText);
	if (nullptr == spawned)
	{
		return nullptr;
	}

	// 프리팹에 저장된 회전/스케일은 그대로 두고 위치만 덮어쓴다.
	spawned->GetTransform().Position = position;
	return spawned;
}
