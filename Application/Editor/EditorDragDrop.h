#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/GameFramework/Reflection/ReflectionTypes.h" // TypeId
#include "Utillity/File/FilePath.h"

class CGameObject;
class CGameLayer;

namespace EditorDragDrop
{
	constexpr const char* ASSET_PAYLOAD_TYPE = "JBRO_ASSET";

	// ── 하이어라키 드래그-드랍 페이로드 (Ref 프로퍼티 드롭 타깃이 받는다) ──────────
	// 오브젝트 1개를 드래그: CGameObject* (드래그 중 생존하는 raw 포인터) 가 실린다.
	constexpr const char* HIERARCHY_ENTITY_PAYLOAD = "HIERARCHY_ENTITY";

	// 레이어 1개를 드래그(컴포짓 순서 재배치): CGameLayer* 가 실린다.
	// 오브젝트 페이로드와 타입이 달라야 레이어 행이 오브젝트 드롭과 섞이지 않는다.
	constexpr const char* HIERARCHY_LAYER_PAYLOAD = "HIERARCHY_LAYER";

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

	// 캔버스 뷰에서 끌어온 레이어를 받는다(에셋 브라우저의 에셋화 드롭).
	// AcceptAssetDragDropPayload 와 마찬가지로 안에서 Begin/EndDragDropTarget 을 직접 부르므로
	// 호출부가 또 BeginDragDropTarget 으로 감싸면 안 된다.
	bool AcceptLayerDragDropPayload(CGameLayer*& outLayer, ImGuiDragDropFlags flags = 0);
	File::Guid GetGuid(const AssetPayload& payload);
	File::Path GetRelativePath(const AssetPayload& payload);
}

#endif
