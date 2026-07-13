#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>
#include <vector>

// ── DebugColor ────────────────────────────────────────────────────────────────
// RGBA packed as 0xAABBGGRR, layout identical to ImU32 / IM_COL32.
using DebugColor = std::uint32_t;

// 디버그 도형을 제출한 오브젝트의 불투명 키(GameFramework CGameObject 의 주소).
// Core 레이어이므로 GameFramework 타입을 몰라도 되게 void* 로 둔다(집합 비교 전용,
// 역참조 금지, 프레임 한정). nullptr = 전역 도형(그리드 등).
using DebugObjectId = const void*;
inline constexpr DebugObjectId INVALID_DEBUG_OBJECT_ID = nullptr;

inline constexpr DebugColor DebugColorRGBA(
	std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
	return (static_cast<DebugColor>(a) << 24) |
	       (static_cast<DebugColor>(b) << 16) |
	       (static_cast<DebugColor>(g) <<  8) |
	        static_cast<DebugColor>(r);
}

// ── Primitive structs ─────────────────────────────────────────────────────────

struct DebugLine2D
{
	Vector2 A;
	Vector2 B;
	DebugColor     Color     = 0xFFFFFFFF;
	float          Thickness = 1.0f;
	// 이 선을 제출한 엔티티. INVALID = 그리드 등 전역 도형.
	DebugObjectId       Entity    = INVALID_DEBUG_OBJECT_ID;
};

struct DebugCircle2D
{
	Vector2 Center;
	float          Radius    = 1.0f;
	DebugColor     Color     = 0xFFFFFFFF;
	float          Thickness = 1.0f;
	int            Segments  = 32;
	// 이 원을 제출한 엔티티. INVALID = 전역 도형.
	DebugObjectId       Entity    = INVALID_DEBUG_OBJECT_ID;
};

// ── IDebugDraw2D ──────────────────────────────────────────────────────────────

class IDebugDraw2D : public EnableSafeFromThis<IDebugDraw2D>
{
public:
	virtual ~IDebugDraw2D() = default;

	// ── 전역 도형 (그리드 등, DebugObjectId = INVALID) ────────────────────────────
	virtual void DrawLine(
		const Vector2& a, const Vector2& b,
		DebugColor color, float thickness = 1.0f) = 0;

	virtual void DrawCircle(
		const Vector2& center, float radius,
		DebugColor color, float thickness = 1.0f, int segments = 32) = 0;

	virtual void DrawPolygon(
		const Vector2* points, int count,
		DebugColor color, float thickness = 1.0f) = 0;

	// ── 엔티티 태깅 도형 (콜라이더 등, 포커스 필터링에 사용) ─────────────────
	virtual void DrawLine(
		DebugObjectId entity,
		const Vector2& a, const Vector2& b,
		DebugColor color, float thickness = 1.0f) = 0;

	virtual void DrawCircle(
		DebugObjectId entity,
		const Vector2& center, float radius,
		DebugColor color, float thickness = 1.0f, int segments = 32) = 0;

	virtual void DrawPolygon(
		DebugObjectId entity,
		const Vector2* points, int count,
		DebugColor color, float thickness = 1.0f) = 0;

	// Read-access to accumulated commands (used by the editor renderer).
	virtual const std::vector<DebugLine2D>&   GetLines()   const = 0;
	virtual const std::vector<DebugCircle2D>& GetCircles() const = 0;

	// Clear all accumulated commands.  Call once per frame (e.g. in BeginFrame).
	virtual void Clear() = 0;
};

// ── CDebugDraw2D ──────────────────────────────────────────────────────────────

class CDebugDraw2D final : public IDebugDraw2D
{
public:
	void DrawLine(
		const Vector2& a, const Vector2& b,
		DebugColor color, float thickness = 1.0f) override;
	void DrawLine(
		DebugObjectId entity,
		const Vector2& a, const Vector2& b,
		DebugColor color, float thickness = 1.0f) override;

	void DrawCircle(
		const Vector2& center, float radius,
		DebugColor color, float thickness = 1.0f, int segments = 32) override;
	void DrawCircle(
		DebugObjectId entity,
		const Vector2& center, float radius,
		DebugColor color, float thickness = 1.0f, int segments = 32) override;

	void DrawPolygon(
		const Vector2* points, int count,
		DebugColor color, float thickness = 1.0f) override;
	void DrawPolygon(
		DebugObjectId entity,
		const Vector2* points, int count,
		DebugColor color, float thickness = 1.0f) override;

	const std::vector<DebugLine2D>&   GetLines()   const override { return m_lines;   }
	const std::vector<DebugCircle2D>& GetCircles() const override { return m_circles; }

	void Clear() override;

private:
	std::vector<DebugLine2D>   m_lines;
	std::vector<DebugCircle2D> m_circles;
};
