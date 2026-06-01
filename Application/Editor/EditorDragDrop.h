#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/GameFramework/ECS/EntityTypes.h"      // EntityId
#include "Engine/GameFramework/Reflection/ReflectionTypes.h" // TypeId
#include "Utillity/File/FilePath.h"

namespace EditorDragDrop
{
	constexpr const char* ASSET_PAYLOAD_TYPE = "JBRO_ASSET";

	// ── 하이어라키 드래그-드랍 페이로드 (Ref 프로퍼티 드롭 타깃이 받는다) ──────────
	// 오브젝트 1개를 드래그: EntityId 가 실린다.
	constexpr const char* HIERARCHY_ENTITY_PAYLOAD = "HIERARCHY_ENTITY";
	// 컴포넌트/스크립트를 드래그: 아래 구조가 실린다.
	constexpr const char* HIERARCHY_COMPONENT_PAYLOAD = "HIERARCHY_COMPONENT";

	struct HierarchyComponentPayload
	{
		EntityId Entity   = INVALID_ENTITY_ID;
		TypeId   TypeId   = INVALID_TYPE_ID;
		bool     IsScript = false;   // true = 스크립트(ScriptComponent.Instance), false = 일반 컴포넌트
	};
	constexpr std::size_t MAX_GUID_TEXT_LENGTH = 64;
	constexpr std::size_t MAX_PATH_TEXT_LENGTH = 260;

	struct AssetPayload
	{
		char Guid[MAX_GUID_TEXT_LENGTH] = {};
		char RelativePath[MAX_PATH_TEXT_LENGTH] = {};
		EAssetType Type = EAssetType::Unknown;
		bool IsDirectory = false;
	};

	struct AssetPayloadDesc
	{
		File::Guid Guid = File::NULL_GUID;
		File::Path RelativePath = File::NULL_PATH;
		EAssetType Type = EAssetType::Unknown;
		bool IsDirectory = false;
		const char* PreviewLabel = nullptr;
		// 드래그 툴팁에 아이콘 + 이름을 아이콘 모드처럼 보여주기 위한 옵션.
		// PreviewTextureID 가 0 이 아니면 Image 가 먼저 그려지고 그 아래 이름이 가운데 정렬.
		ImTextureID PreviewTextureID = 0;
		float       PreviewSize      = 56.0f;
	};

	bool BeginAssetDragDropSource(const AssetPayloadDesc& desc);
	bool AcceptAssetDragDropPayload(AssetPayload& outPayload, ImGuiDragDropFlags flags = 0);
	File::Guid GetGuid(const AssetPayload& payload);
	File::Path GetRelativePath(const AssetPayload& payload);
}

#endif
