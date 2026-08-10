#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Math/Matrix3x2.h"
#include "Utillity/Math/RectT.h"

#include <algorithm>

class IRHIDevice;
class IRHIGraphicsPipeline;
class IRHISampler;
class IRHITexture;
class IRenderMesh;
class IRenderMaterial;

// 렌더 아이템이 속한 레이어의 순서(캔버스 내 인덱스, 0 = 맨 아래).
// Core 렌더러는 이 값으로 정렬·범위 분할만 하고 레이어 객체는 모른다(GameFramework 타입 무의존).
using RenderLayerIndex = std::uint16_t;

// 렌더 아이템을 제출한 오브젝트의 불투명 키(GameFramework CGameObject 의 주소).
// Core 렌더러는 이 키로 집합 비교(필터/아웃라인 마스크)만 하고 절대 역참조하지 않으므로
// GameFramework 타입을 몰라도 된다(void*). 프레임 한정 식별 — 저장/직렬화 대상 아님.
using RenderObjectId = const void*;

enum class ERenderQueue
{
	Background,
	Opaque,
	Transparent,
	Overlay
};

struct RendererDesc
{
	SafePtr<IRHIDevice> RHIDevice;
};

struct RenderCullingStats
{
	std::uint32_t SubmittedCount = 0;
	std::uint32_t DrawnCount = 0;
	std::uint32_t CulledCount = 0;
};

struct RenderItem
{
	SafePtr<IRenderMesh> Mesh;
	SafePtr<IRenderMaterial> Material;
	SafePtr<IRHIGraphicsPipeline> Pipeline;
	SafePtr<IRHITexture> Texture;
	SafePtr<IRHISampler> Sampler;
	ERenderQueue Queue = ERenderQueue::Opaque;
	// 소속 레이어 순서 — 정렬 1순위 키(레이어별 드로우 범위가 연속이 되도록).
	RenderLayerIndex LayerIndex = 0;
	Matrix3x2 Transform;
	// 메시의 로컬 중심 기준 AABB 반경. 기본값은 기존 스프라이트 단위 쿼드 계약.
	float LocalHalfExtents[2] = { 0.5f, 0.5f };
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float SecondaryColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float ShaderParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	// UV sub-rect (uMin, vMin, uScale, vScale). 스프라이트시트 프레임 선택용.
	// 항등값 {0,0,1,1} = 전체 텍스처. 셰이더에서 uv = uv*scale + min.
	float UvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	std::int32_t SortOrder = 0;
	// 이 렌더 아이템을 제출한 오브젝트의 불투명 키(CGameObject 주소).
	// 필터 렌더링/아웃라인 마스크에 사용(집합 비교 전용, 역참조 금지). nullptr = 무관 아이템.
	RenderObjectId Entity = nullptr;
	// 그림자 캐스터 — OccluderMap 패스가 이 아이템만 골라 월드공간 공유 오클루더 맵에 알파 실루엣을 그린다.
	bool CastShadow = false;

	// 렌더러 컴포넌트의 로컬 경계로 컬링 반경을 채운다.
	//
	// LocalHalfExtents 는 Transform 원점 기준 **대칭** 반경인데 경계는 대칭이 아닐 수 있다
	// (정삼각형처럼 꼭짓점 배치가 한쪽으로 쏠린 도형). 그래서 양쪽 중 **먼 쪽**으로 덮는다 —
	// 작게 잡으면 실제로 그려지는 부분이 화면 가장자리에서 일찍 잘린다.
	//
	// transformOrigin 은 Transform 의 이동 성분에 해당하는 로컬 지점이다(도형은 Offset,
	// 텍스트는 Offset + 셰이핑 중심). 경계와 같은 공간이어야 한다.
	//
	// 빈 경계(= 아직 알 수 없음)는 무시하고 기존 값을 남긴다. 빈 Rect 를 그대로 먹이면
	// 원점까지의 거리가 반경이 되어 엉뚱한 상자가 나온다.
	void SetCullBoundsFromLocal(const Rect& localBounds, const Vector2& transformOrigin)
	{
		if (localBounds.IsEmpty())
		{
			return;
		}
		LocalHalfExtents[0] = std::max(std::abs(localBounds.Left - transformOrigin.x),
		                               std::abs(localBounds.Right - transformOrigin.x));
		LocalHalfExtents[1] = std::max(std::abs(localBounds.Top - transformOrigin.y),
		                               std::abs(localBounds.Bottom - transformOrigin.y));
	}
};
