#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Asset/FontAsset.h"
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Component.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"
#include "Utillity/Types/String.h"

#include <cstdint>

enum class ETextHorizontalAlignment : std::uint8_t { Left, Center, Right };
enum class ETextVerticalAlignment : std::uint8_t { Top, Middle, Baseline, Bottom };
enum class ETextOverflowMode : std::uint8_t { Overflow, Wrap, Clip };

class Text2D final : public CComponent
{
	JBRO_COMPONENT(Text2D)
public:
	String Text = "New Text";
	AssetGuid FontFamilyGuid = INVALID_ASSET_GUID;
	EFontStyle FontStyle = EFontStyle::Regular;
	float FontSizePixels = 32.0f;
	float WidthPixels = 0.0f;
	float HeightPixels = 0.0f;
	ETextOverflowMode OverflowMode = ETextOverflowMode::Wrap;
	bool AutoSizeEnabled = false;
	float MinFontSizePixels = 8.0f;
	float MaxFontSizePixels = 32.0f;
	ETextHorizontalAlignment HorizontalAlignment = ETextHorizontalAlignment::Left;
	ETextVerticalAlignment VerticalAlignment = ETextVerticalAlignment::Baseline;
	float LineSpacing = 1.0f;
	float LetterSpacingPixels = 0.0f;
	bool FillEnabled = true;
	Color FillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool OutlineEnabled = false;
	Color OutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	float OutlineWidthPixels = 0.0f;
	bool PixelSnap = false;
	Vector2 Offset = Vector2(0.0f, 0.0f);
	std::int32_t SortOrder = 0;
};
