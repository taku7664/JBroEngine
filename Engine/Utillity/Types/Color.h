#pragma once

#include <cstddef>
#include <type_traits>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Color ─ 엔진 공통 RGBA 색
//
//  채널은 선형 [0,1] float 4개다. 메모리 배치가 `float[4]` 와 **같다는 것이 계약**이다 —
//  인스펙터(ImGui::ColorEdit4), 직렬화, 상수버퍼가 전부 이 자리를 float* 로 읽는다.
//  아래 static_assert 가 그 계약을 붙잡는다.
//
//  · 집합체(aggregate)로 남긴다. `Color{ 1.0f, 0.0f, 0.0f, 1.0f }` 초기화가 코드 전반에
//    이미 있고, 생성자를 달면 그게 전부 깨진다.
//  · operator[] 가 있는 건 `float Color[4]` 시절의 인덱스 접근(`item.Color[i] = c[i]`)을
//    그대로 받기 위해서다. 색을 순회하는 자리는 대개 4채널 복사라 이름보다 인덱스가 낫다.
//
//  이 타입은 **저작·저장되는 색**의 타입이다. 렌더러의 파라미터 블록(RenderItem 의
//  Color/SecondaryColor/ShaderParams/UvRect)과 셰이더 구조체를 그대로 베낀 상수버퍼는
//  raw float[4] 로 둔다 — 그쪽은 색이라서가 아니라 셰이더 레이아웃이라서 float4 다.
//  RHI 도 마찬가지로 이 타입을 모른다(RenderPassDesc 는 float[4] 를 받는다).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct Color
{
	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;

	// 연속된 4채널의 시작 주소. RHI / ImGui / 상수버퍼로 넘길 때 쓴다.
	float*       Data()       noexcept { return &R; }
	const float* Data() const noexcept { return &R; }

	float&       operator[](std::size_t index)       noexcept { return Data()[index]; }
	const float& operator[](std::size_t index) const noexcept { return Data()[index]; }

	friend bool operator==(const Color& lhs, const Color& rhs) noexcept
	{
		return lhs.R == rhs.R && lhs.G == rhs.G && lhs.B == rhs.B && lhs.A == rhs.A;
	}
};

static_assert(sizeof(Color) == sizeof(float) * 4);
static_assert(alignof(Color) == alignof(float));
static_assert(std::is_trivially_copyable_v<Color>);
static_assert(std::is_standard_layout_v<Color>);
static_assert(std::is_aggregate_v<Color>);
