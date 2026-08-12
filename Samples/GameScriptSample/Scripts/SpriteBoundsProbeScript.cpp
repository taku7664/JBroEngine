#include "pch.h"
#include "Scripts/SpriteBoundsProbeScript.h"

#include "Core/Asset/SpriteAsset.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Component/ShapeRenderers2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	std::string Format(float value)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	std::string FormatRect(const Rect& rect)
	{
		return "[L " + Format(rect.Left) + " minY " + Format(rect.Top)
			+ " R " + Format(rect.Right) + " maxY " + Format(rect.Bottom) + "]";
	}

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[SpriteBoundsProbe] " + message);
		}
	}

	// 손으로 쓴 식 — 구현을 부르지 않는다.
	// 쿼드는 피벗 점이 (Offset) 에 오도록 놓이고, 반전은 피벗을 축으로 미러링한다.
	Rect ExpectedBounds(float framePixelW, float framePixelH, float pixelsPerUnit,
	                    const Vector2& sizeMultiplier, const Vector2& offset,
	                    float pivotX, float pivotY, bool flipX, bool flipY)
	{
		float sizeX = (framePixelW / pixelsPerUnit) * sizeMultiplier.x;
		float sizeY = (framePixelH / pixelsPerUnit) * sizeMultiplier.y;
		if (flipX) sizeX = -sizeX;
		if (flipY) sizeY = -sizeY;

		const float centerX = offset.x + (0.5f - pivotX) * sizeX;
		const float centerY = offset.y + (0.5f - pivotY) * sizeY;
		const float halfW = std::abs(sizeX) * 0.5f;
		const float halfH = std::abs(sizeY) * 0.5f;
		return Rect(centerX - halfW, centerY - halfH, centerX + halfW, centerY + halfH);
	}
}

void CSpriteBoundsProbeScript::Check(bool condition, const char* label)
{
	if (condition)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label);
}

void CSpriteBoundsProbeScript::CheckNear(float actual, float expected, float tolerance, const char* label)
{
	if (std::abs(actual - expected) <= tolerance)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + Format(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + Format(expected)
		+ " (+-" + Format(tolerance) + "), got " + Format(actual));
}

void CSpriteBoundsProbeScript::CheckRect(const Rect& actual, const Rect& expected, float tolerance, const char* label)
{
	const bool same =
		std::abs(actual.Left   - expected.Left)   <= tolerance &&
		std::abs(actual.Top    - expected.Top)    <= tolerance &&
		std::abs(actual.Right  - expected.Right)  <= tolerance &&
		std::abs(actual.Bottom - expected.Bottom) <= tolerance;
	if (same)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + FormatRect(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + FormatRect(expected)
		+ ", got " + FormatRect(actual));
}

void CSpriteBoundsProbeScript::TearDown()
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	if (false == canvas.IsValid())
	{
		return;
	}
	for (SafePtr<CGameObject>& spawned : m_spawned)
	{
		if (spawned.IsValid())
		{
			canvas->DestroyGameObject(spawned.TryGet());
		}
	}
	m_spawned.clear();
}

void CSpriteBoundsProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CSpriteBoundsProbeScript::RunProbe()
{
	ProbeLog("START");

	SafePtr<CGameCanvas> canvas = GetCanvas();

	// ── 도형 3종 — 자산이 없으므로 첫 프레임부터 경계를 낼 수 있다 ────────────────
	CGameObject* shapeObject = canvas->CreateGameObject("Probe.Shapes");
	m_spawned.push_back(shapeObject->SafeFromThis());

	Square2D* square = shapeObject->AddComponent<Square2D>();
	square->SetSize(Vector2(2.0f, 1.0f));
	square->SetOffset(Vector2(0.5f, -0.25f));
	CheckRect(square->GetLocalBounds(), Rect(-0.5f, -0.75f, 1.5f, 0.25f), 0.0001f,
		"Square2D bounds = Size around Offset");

	Circle2D* circle = shapeObject->AddComponent<Circle2D>();
	circle->SetRadius(0.75f);
	CheckRect(circle->GetLocalBounds(), Rect(-0.75f, -0.75f, 0.75f, 0.75f), 0.0001f,
		"Circle2D bounds = 2R box");

	// 정사각형이 되는 배치 — 꼭짓점 각도 0/90/180/270 이라 AABB 가 반지름 그대로다.
	Polygon2D* polygon = shapeObject->AddComponent<Polygon2D>();
	polygon->SetRadius(1.0f);
	polygon->SetVertexCount(4);
	polygon->SetStartAngle(Radian(0.0f));
	CheckRect(polygon->GetLocalBounds(), Rect(-1.0f, -1.0f, 1.0f, 1.0f), 0.0001f,
		"Polygon2D(4) bounds = circumradius box");

	// 세터가 같은 프레임에 캐시를 무효화하는가 — 더티가 실제로 도는지.
	polygon->SetRadius(2.0f);
	CheckRect(polygon->GetLocalBounds(), Rect(-2.0f, -2.0f, 2.0f, 2.0f), 0.0001f,
		"setter invalidates bounds in the same frame");

	// 렌더 시스템과 같은 클램프를 쓰는가 — VertexCount 2 는 삼각형으로 그려진다.
	polygon->SetVertexCount(2);
	Check(false == polygon->GetLocalBounds().IsEmpty(),
		"Polygon2D clamps vertex count like the render system (2 -> 3)");

	// ── 스프라이트 — 자산 해석 전/후 ───────────────────────────────────────────
	CGameObject* spriteObject = canvas->CreateGameObject("Probe.Sprite");
	m_spawned.push_back(spriteObject->SafeFromThis());

	if (Sheet.IsNull())
	{
		// 시트를 안 꽂았으면 스프라이트 구간은 검사할 수 없다. 도형 결과는 이미 나왔으므로
		// 조용히 넘기지 말고 왜 건너뛰는지 남긴다.
		ProbeLog("SKIP   sprite section — Sheet is empty (assign one on the script)");
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		TearDown();
		co_return;
	}

	SpriteRenderer2D* sprite = spriteObject->AddComponent<SpriteRenderer2D>();
	sprite->SetSpriteGuid(AssetGuid(Sheet.GetGuid().generic_string()));

	// 렌더 시스템이 아직 자산을 해석하지 않았다 → 경계 미상이어야 한다.
	// (0 크기 경계를 "원점의 점"으로 오해하면 안 된다는 계약.)
	Check(sprite->GetLocalBounds().IsEmpty(), "sprite bounds are empty before the asset resolves");

	co_await Wait::Frames(1);

	// 프레임 픽셀 크기와 유효 PPU 는 **사실**로 읽는다. 경계식은 위에서 손으로 썼다.
	const CSpriteAsset* asset = nullptr;
	if (sprite->SpriteAssetCache.IsValid() && EAssetType::Sprite == sprite->SpriteAssetCache->GetAssetType())
	{
		asset = static_cast<const CSpriteAsset*>(sprite->SpriteAssetCache.Get());
	}
	Check(nullptr != asset, "render system resolved the sprite asset into the component cache");
	Check(sprite->CachedPixelsPerUnit > 0.0f, "render system injected the effective pixels-per-unit");

	if (nullptr == asset || sprite->CachedPixelsPerUnit <= 0.0f)
	{
		ProbeLog("ABORT  sprite asset unavailable");
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		TearDown();
		co_return;
	}

	const std::vector<SpriteFrame>& frames = asset->GetFrames();
	Check(false == frames.empty(), "sheet is sliced into frames");
	const SpriteFrame& frame0 = frames.empty() ? SpriteFrame{} : frames.front();
	const float framePixelW = static_cast<float>(frame0.Width);
	const float framePixelH = static_cast<float>(frame0.Height);
	const float ppu = sprite->CachedPixelsPerUnit;

	ProbeLog("INFO   frame " + Format(framePixelW) + "x" + Format(framePixelH)
		+ " ppu " + Format(ppu)
		+ " pivot (" + Format(frame0.PivotX) + ", " + Format(frame0.PivotY) + ")");

	CheckRect(sprite->GetLocalBounds(),
		ExpectedBounds(framePixelW, framePixelH, ppu, Vector2(1.0f, 1.0f), Vector2(0.0f, 0.0f),
			frame0.PivotX, frame0.PivotY, false, false),
		0.0001f, "sprite bounds = frame pixels / ppu, pivot at the origin");

	// Size 는 절대 크기가 아니라 배수다.
	sprite->SetSize(Vector2(2.0f, 3.0f));
	CheckRect(sprite->GetLocalBounds(),
		ExpectedBounds(framePixelW, framePixelH, ppu, Vector2(2.0f, 3.0f), Vector2(0.0f, 0.0f),
			frame0.PivotX, frame0.PivotY, false, false),
		0.0001f, "Size multiplies the frame size");

	sprite->SetOffset(Vector2(1.0f, -2.0f));
	CheckRect(sprite->GetLocalBounds(),
		ExpectedBounds(framePixelW, framePixelH, ppu, Vector2(2.0f, 3.0f), Vector2(1.0f, -2.0f),
			frame0.PivotX, frame0.PivotY, false, false),
		0.0001f, "Offset moves the pivot point");

	// 반전은 피벗을 축으로 미러링한다 — 피벗이 중앙이면 경계가 그대로다.
	sprite->SetFlipX(true);
	CheckRect(sprite->GetLocalBounds(),
		ExpectedBounds(framePixelW, framePixelH, ppu, Vector2(2.0f, 3.0f), Vector2(1.0f, -2.0f),
			frame0.PivotX, frame0.PivotY, true, false),
		0.0001f, "FlipX mirrors about the pivot");
	sprite->SetFlipX(false);

	sprite->SetSize(Vector2(1.0f, 1.0f));
	sprite->SetOffset(Vector2(0.0f, 0.0f));

	// ── 월드 경계 — 회전이 걸리면 감싸는 상자가 커진다 ──────────────────────────
	const float halfW = (framePixelW / ppu) * 0.5f;
	const float halfH = (framePixelH / ppu) * 0.5f;

	spriteObject->Local.Position = Vector2(3.0f, 4.0f);
	co_await Wait::Frames(1);

	// 회전·스케일이 없으면 월드 경계 = 로컬 경계 + 위치다.
	// **중심이 위치와 같다고 쓰면 안 된다** — 피벗이 중앙이 아니면 틀린다(실제로 여기서 걸렸다).
	const Rect restLocal = ExpectedBounds(framePixelW, framePixelH, ppu, Vector2(1.0f, 1.0f),
		Vector2(0.0f, 0.0f), frame0.PivotX, frame0.PivotY, false, false);
	CheckRect(sprite->GetWorldBounds(),
		Rect(3.0f + restLocal.Left, 4.0f + restLocal.Top,
		     3.0f + restLocal.Right, 4.0f + restLocal.Bottom), 0.001f,
		"GetWorldBounds = local bounds + position");

	spriteObject->Local.RotationRadians = Radian(0.78539816f); // 45도
	co_await Wait::Frames(1);

	// 45도 회전한 정사각형의 축정렬 경계는 대각선 길이의 절반까지 커진다.
	const float rotatedHalf = (halfW + halfH) * 0.70710678f;
	const Rect rotatedBounds = sprite->GetWorldBounds();
	CheckNear(rotatedBounds.Right - rotatedBounds.Left, rotatedHalf * 2.0f, 0.001f,
		"rotated world bounds widen to the diagonal");

	Vector2 corners[4];
	sprite->GetWorldCorners(corners);
	const float edge = std::sqrt(
		(corners[1].x - corners[0].x) * (corners[1].x - corners[0].x) +
		(corners[1].y - corners[0].y) * (corners[1].y - corners[0].y));
	CheckNear(edge, halfW * 2.0f, 0.001f, "GetWorldCorners keeps the unrotated edge length");

	ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
		+ std::to_string(m_passCount + m_failCount) + " PASS");

	TearDown();
	co_return;
}
