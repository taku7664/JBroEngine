#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Core/Asset/FontAsset.h"
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Component/Renderer2DComponent.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"
#include "Utillity/Types/String.h"

#include <cstdint>

enum class ETextHorizontalAlignment : std::uint8_t { Left, Center, Right };
enum class ETextVerticalAlignment : std::uint8_t { Top, Middle, Baseline, Bottom };
enum class ETextOverflowMode : std::uint8_t { Overflow, Wrap, Clip };

class CReflectionRegistry;

// Text2D 는 경계 규약의 유일한 예외다 — 크기가 폰트 셰이핑 결과라 컴포넌트 혼자 낼 수 없다.
// CTextRenderSystem 이 셰이핑을 끝낸 뒤 결과를 여기에 써 준다(write-back).
// 셰이핑 입력 필드(Text/폰트/크기/줄바꿈/정렬 …)는 그래서 세터로 닫지 않는다:
// 시스템이 이미 BuildSignature 로 변경을 감지해 다시 셰이핑하고, 그 결과가 다시 써진다.
// **Offset 만 예외다** — 셰이핑에 영향을 주지 않아 시그니처에 안 들어가는데 경계는 바꾼다.
//
// 아직 한 번도 셰이핑되지 않았으면(생성 직후 프레임, 폰트 미로딩) 경계는 빈 Rect 다.
class Text2D final : public CRenderer2DComponent
{
	JBRO_COMPONENT_BASE(Text2D, CRenderer2DComponent)
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
	std::int32_t SortOrder = 0;

	const Vector2& GetOffset() const { return m_offset; }
	void SetOffset(const Vector2& offset) { m_offset = offset; MarkBoundsDirty(); }

protected:
	void ComputeLocalBounds(Rect& outBounds) const override;

private:
	friend void RegisterBuiltinComponents(CReflectionRegistry&);
	friend class CTextRenderSystem;

	// 셰이핑 결과 주입. 값이 실제로 달라졌을 때만 경계 캐시를 무효화한다
	// (시스템이 매 프레임 부르지만 텍스트는 대개 그대로다).
	void SetShapedBounds(float centerX, float centerY, float width, float height);

	Vector2 m_offset = Vector2(0.0f, 0.0f);
	Vector2 m_shapedCenter = Vector2(0.0f, 0.0f);
	float m_shapedWidth = 0.0f;
	float m_shapedHeight = 0.0f;
	bool m_hasShapedBounds = false;
};
