#pragma once

#include <algorithm>
#include <cstddef>

class InputDeviceContext;

// ─────────────────────────────────────────────────────────────────────────────
//  입력 핸들러
//
//  · 스크립트는 CGameScript 와 InputHandler<Layer, Order> 를 **둘 다** 상속한다.
//    (InputHandler 가 CGameScript 를 상속하는 게 아니다.)
//  · 매 프레임 레이어 순으로 HandleInput(ctx) 가 불린다(입력 변화 없어도 호출).
//  · 반환 = "처리했나(handled)". true = consume → 하위 레이어 전부 차단. false = 통과(기본).
//  · 필터 없음 — 핸들러 내부에서 ctx 로 직접 폴링한다.
//  · 등록/해제는 엔진(ScriptComponent 수명)이 자동 처리한다 — 사용자 개입 0.
// ─────────────────────────────────────────────────────────────────────────────
class IInputHandler
{
public:
	virtual ~IInputHandler() = default;

	virtual bool        HandleInput(const InputDeviceContext& ctx) = 0;
	virtual const char* GetInputLayer() const = 0;
	virtual int         GetInputOrder() const = 0;
};

// ── LayerName ─────────────────────────────────────────────────────────────────
// 문자열을 템플릿 인자로 받기 위한 래퍼(C++20 NTTP). InputHandler<"UI", 10> 처럼 사용.
template<std::size_t N>
struct LayerName
{
	char value[N] = {};
	constexpr LayerName(const char (&str)[N])
	{
		std::copy_n(str, N, value);
	}
};

// ── InputHandler<Layer, Order> ────────────────────────────────────────────────
// 레이어/오더 게터를 템플릿 인자로부터 자동 구현. CGameScript 와 무관.
//
//   class CPauseMenu : public CGameScript, public InputHandler<"UI", 10>
//   {
//       bool HandleInput(const InputDeviceContext& ctx) override { ... }
//   };
//
template<LayerName Layer = LayerName("Game"), int Order = 0>
class InputHandler : public IInputHandler
{
public:
	const char* GetInputLayer() const override { return Layer.value; }
	int         GetInputOrder() const override { return Order; }
};
