#pragma once

#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Component/GameObject.h"

#include <vector>

struct SceneObjectSnapshot
{
	EntityId Entity = INVALID_ENTITY_ID;
	File::Guid InstanceGuid;   // 오브젝트 안정 식별자 (Ref<T> 직렬화 키)
	char Name[GameObject::MAX_NAME_LENGTH + 1] = {};
	bool IsActive = true;
	std::uint32_t Layer = 0;
	EntityId Parent = INVALID_ENTITY_ID;

	bool HasTransform = false;
	Transform2D Transform;

	bool HasSpriteRenderer = false;
	bool SpriteRendererEnabled = true;
	AssetGuid SpriteGuid = INVALID_ASSET_GUID;
	AssetGuid MaterialGuid = INVALID_ASSET_GUID;
	Vector2 SpriteSize = Vector2(1.0f, 1.0f);
	Vector2 SpriteOffset = Vector2(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;

	bool HasCamera = false;
	Camera2D Camera;

	bool HasLight = false;
	Light2D Light;

	bool HasAudioPlayer = false;
	AudioPlayer AudioPlayerData;

	bool HasAudioListener = false;
	AudioListener AudioListenerData;

	bool HasRigidbody = false;
	Rigidbody2D Rigidbody;

	// 단일 인스턴스 ECS: 엔티티는 PolygonCollider2D 를 최대 1개 보유.
	// 벡터로 유지하는 이유는 기존 직렬화 포맷 호환성(0개 또는 1개 케이스 처리).
	std::vector<PolygonCollider2D> PolygonColliders;

	bool HasCircleCollider = false;
	CircleCollider2D CircleCollider;

	bool HasPrefabInstance = false;
	AssetGuid SourcePrefabGuid = INVALID_ASSET_GUID;
};

struct SceneSnapshot
{
	void Clear();
	std::size_t GetObjectCount() const;

	std::vector<SceneObjectSnapshot> Objects;
};
