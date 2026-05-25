#pragma once

#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Component/GameObject.h"

#include <vector>

struct SceneObjectSnapshot
{
	EntityId Entity = INVALID_ENTITY_ID;
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
	Vector2<float> SpriteSize = Vector2<float>(1.0f, 1.0f);
	Vector2<float> SpriteOffset = Vector2<float>(0.0f, 0.0f);
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	RenderLayerMask LayerMask = 0xffffffffu;

	bool HasCamera = false;
	Camera2D Camera;

	bool HasLight = false;
	Light2D Light;

	bool HasRigidbody = false;
	Rigidbody2D Rigidbody;

	// An entity may hold multiple PolygonCollider2D components (AllowDuplicates=true).
	// An empty vector means no polygon collider on this entity.
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
