#include "pch.h"
#include "PrefabSpawnTestScript.h"

#include <string>

namespace
{
	// 헬퍼 이름은 SpawnLog — 엔진의 Core/Logging 에 class Log 가 있어 Log 는 이름 충돌한다.
	void SpawnLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[Prefab Spawn] " + message);
		}
	}
}

void CPrefabSpawnTestScript::OnStart()
{
	if (Prefab.IsNull())
	{
		SpawnLog("no prefab assigned - drop a .jprefab into the Prefab slot, then press Space.");
		return;
	}
	SpawnLog(std::string("ready - press Space to spawn. prefab guid=") + Prefab.GuidText());
}

bool CPrefabSpawnTestScript::HandleInput(const InputDeviceContext& ctx)
{
	const Keyboard& keyboard = ctx.GetKeyboard();
	if (false == keyboard.IsPressed(EKeyCode::Space))
	{
		return false;
	}

	SpawnBurst();
	return true;
}

void CPrefabSpawnTestScript::SpawnBurst()
{
	if (Prefab.IsNull())
	{
		SpawnLog("no prefab assigned - nothing spawned.");
		return;
	}
	if (false == Script.PrefabSpawner.IsValid())
	{
		SpawnLog("prefab spawner is unavailable.");
		return;
	}

	CGameObject* owner = GetOwner().TryGet();
	const Vector2 origin = owner ? owner->GetTransform().Position : Vector2(0.0f, 0.0f);

	const int count = static_cast<int>(SpawnCount);
	for (int i = 0; i < count; ++i)
	{
		const float offsetX = static_cast<float>(m_spawnSerial) * static_cast<float>(SpawnSpacing);
		const Vector2 position(origin.x + offsetX, origin.y);

		CGameObject* spawned = Script.PrefabSpawner->Spawn(Prefab.GuidText(), position);
		if (nullptr == spawned)
		{
			// 실패 사유(미등록 guid / 프리팹 아님 / 파싱 실패)는 스포너가 이미 로그로 남긴다.
			SpawnLog("spawn failed - see the error above for the reason.");
			return;
		}

		++m_spawnSerial;
		// guid 를 함께 찍는 이유: 같은 프리팹을 두 번 스폰했을 때 값이 갈라지는지가 이 테스트의 핵심이다.
		SpawnLog(std::string("spawned '") + spawned->GetName()
			+ "' guid=" + spawned->GetInstanceGuid().generic_string()
			+ " at x=" + std::to_string(position.x));
	}
}
