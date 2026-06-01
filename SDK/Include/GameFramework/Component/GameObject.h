#pragma once

#include "Utillity/File/FilePath.h"   // File::Guid

#include <cstddef>
#include <cstdint>

struct GameObject
{
	static constexpr std::size_t MAX_NAME_LENGTH = 63;

	GameObject();
	explicit GameObject(const char* name);

	void SetName(const char* name);
	void CopyNameTo(char* buffer, std::size_t bufferLength) const;

	char Name[MAX_NAME_LENGTH + 1];
	bool IsActive = true;
	std::uint32_t Layer = 0;

	// 오브젝트의 "안정적 식별자". 씬 저장/로드 사이에서도 유지되며, 다른 오브젝트가
	// 이 오브젝트(또는 그 컴포넌트/스크립트)를 Ref<T> 로 참조할 때의 직렬화 키가 된다.
	// CScene::CreateGameObject 에서 발급하고, 씬 파일에 함께 저장된다.
	File::Guid InstanceGuid;
};
