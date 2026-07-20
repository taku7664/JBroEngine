#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace ImItemLocKeys
{
	constexpr const char* CommonFilter = "common.filter";
	constexpr const char* CommonRemove = "common.remove";
	constexpr const char* CommonSearch = "common.search";
	constexpr const char* AssetDropRejected = "asset_drop.rejected";
	constexpr const char* AssetDropExpected = "asset_drop.expected";
	constexpr const char* AssetDropDragged = "asset_drop.dragged";
	constexpr const char* AssetDropDirectoriesNotAllowed = "asset_drop.directories_not_allowed";
	constexpr const char* InspectorRefMissing = "inspector.ref_missing";
	constexpr const char* InspectorRefNone = "inspector.ref_none";
	constexpr const char* ListAddElement = "list.add_element";
}

#endif
