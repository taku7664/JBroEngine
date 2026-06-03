#pragma once

#include <cstdint>

// 씬 내 오브젝트의 불투명 런타임 식별자. CGameObject::GetId() 가 돌려주는 값
// (현재는 객체 주소 기반)으로, 에디터 선택/커맨드/렌더 필터 등 프레임 내 지목에 쓴다.
// 직렬화/영속 키가 아니다(그건 InstanceGuid). ECS index/generation 핸들 아님.
using ObjectId = std::uint64_t;
constexpr ObjectId INVALID_OBJECT_ID = 0;

enum class ESceneSerializeResult
{
	Success,
	InvalidArgument,
	IoError,
	ParseError
};

enum class ESceneSimulationState
{
	Edit,
	Playing,
	Paused,
};
