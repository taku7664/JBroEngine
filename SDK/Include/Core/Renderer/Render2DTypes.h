#pragma once

#include "Core/RHI/RHIGraphicsTypes.h"   // ERHIBlendMode — 레이어 컴포짓 블렌드
#include "Core/Renderer/RendererTypes.h"
#include "Core/Renderer/ScreenSpaceProjection.h"

#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Render2D 프레임 입력 — 파이프라인이 한 프레임을 그리는 데 필요한 것 전부.
//
//  전부 POD 스냅샷이다. 파이프라인은 캔버스·카메라 컴포넌트·레이어 객체를 **절대 보지 않고**
//  이 값들만 본다. 채우는 쪽(GameFramework 수집기)과 그리는 쪽(Core 파이프라인)을 가르는
//  경계가 이 파일이다 — 그래서 파이프라인을 갈아끼워도(픽셀아트/저사양/에디터 전용) 수집기는
//  그대로고, 반대로 저작 개념이 바뀌어도 파이프라인은 안 바뀐다.
// ─────────────────────────────────────────────────────────────────────────────

// GPU 타이머 구간 키 = 레이어 인덱스 + 1.
//   +1 오프셋: 프레임 전체 구간(키=nullptr=0)과 레이어 0 을 구별한다.
//   RHI 는 이 키를 불투명 포인터로만 다루고, GPU 프로파일러 창이 레이어 인덱스로 역매핑한다.
inline const void* GpuLayerKey(std::uint16_t layerIndex)
{
	return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(layerIndex) + 1u);
}

// 직교 2D 뷰 하나. 카메라에서 뽑았든(World) 화면에서 뽑았든(Screen) 그리는 쪽엔 같은 값이다.
struct Render2DView
{
	float PosX = 0.0f;         // 뷰 중심 월드 x
	float PosY = 0.0f;         // 뷰 중심 월드 y
	float OrthoSize = 5.0f;    // 반높이(월드 유닛)
	float OrthoSizeX = 5.0f;   // 반폭(월드 유닛)
	float CosR = 1.0f;         // 뷰 회전
	float SinR = 0.0f;
};

// 뷰포트 1개의 렌더 스냅샷(프레임마다 수집). = 해석된 카메라 뷰 × 출력 렉트 × 레이어 필터.
struct Render2DViewportDesc
{
	// 카메라 뷰(해석 완료) — 오너 트랜스폼 + OrthographicSize 에서 뽑는다.
	// 화면 공간 레이어는 이 뷰를 쓰지 않는다(파이프라인이 렉트에서 다시 뽑는다).
	Render2DView View;
	// 출력 렉트(정규화 0~1).
	float RectX = 0.0f;
	float RectY = 0.0f;
	float RectW = 1.0f;
	float RectH = 1.0f;
	// 그릴 레이어 인덱스 — 캔버스 순서(아래→위)로 해석된 목록. 비면 전체 레이어.
	std::vector<RenderLayerIndex> Layers;
	// 통계 키 — 이 뷰포트가 쓴 카메라 오브젝트 주소(집합 비교 전용, 역참조 금지).
	const void* CameraOwnerObject = nullptr;
};

struct Render2DCameraStats
{
	const void* OwnerObject = nullptr;
	RenderCullingStats Culling;
};

// 레이어 컴포짓 스냅샷(POD). 순서 = 캔버스 순서(아래→위) = Index 오름차순.
struct Render2DLayerDesc
{
	RenderLayerIndex Index = 0;
	ERHIBlendMode    BlendMode = ERHIBlendMode::LayerNormal;
	float            Opacity = 1.0f;
	// 뷰를 카메라가 아니라 화면(뷰포트 렉트)에서 뽑는가. UI/HUD 가 이것이다.
	// **라이팅 이후에 그려진다** — Base 에 섞으면 월드 라이트맵이 UI 에 곱해진다.
	// (저작 개념 ELayerSpace::Screen 을 렌더가 쓰는 형태로 접은 값.)
	bool             ScreenSpace = false;
	// ScreenSpace 일 때만 의미. 뷰포트 픽셀 렉트에서 투영 범위를 정하는 정책.
	EScreenScaleMode ScaleMode = EScreenScaleMode::FixedHeight;
	// ScreenSpace 가 아닐 때만 의미. 카메라 **위치만** 배율(1=동일, 0=월드 원점 고정).
	float            ParallaxFactor = 1.0f;
	bool             Visible = true;
	// 자기 RT 를 거쳐야 하는가 — 블렌드/Opacity/강제 중 하나라도 걸리면 true.
	// 아니면 대상에 직접 그린다(평범한 레이어에 RT 왕복 대역폭을 물리지 않는다).
	bool             NeedsOwnTexture = false;
};

// 월드 공간 Light2D 스냅샷(POD). LightMap 패스가 소비한다.
struct Render2DLightDesc
{
	int   Type = 1;              // 0 = Directional(뷰포트 균일), 1 = Point(반경 감쇠), 2 = Spot(원뿔).
	float PosX = 0.0f;           // Point/Spot 월드 위치.
	float PosY = 0.0f;
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float Intensity = 1.0f;
	float Range = 5.0f;          // Point/Spot 반경(월드 유닛).
	float DirX = 1.0f;           // Spot/Directional 방향(월드, 오브젝트 회전 = 로컬 +X).
	float DirY = 0.0f;
	float InnerAngleRadians = 0.5f;   // Spot 내부 원뿔(전각) — 이 안은 완전 조명.
	float OuterAngleRadians = 1.0f;   // Spot 외부 원뿔(전각) — inner~outer 사이 부드러운 감쇠.
	bool  CastShadows = false;   // 켜면 Occluder→1D 섀도맵 샘플 경로로 렌더.
	float ShadowLength = 6.0f;    // Directional 그림자 도달 길이(월드 유닛) — 유한 그림자.
	float ShadowSoftness = 0.5f;  // 소프트 쉐도우 강도 0~1(0=하드 경계, 1=최대 블러).
};

// 렌더타겟 진행 프리뷰 컷오프(에디터 GPU 프로파일러 진단). Active 면 드로우순서상 여기까지만 그린다:
//   · LayerIndex 보다 위(인덱스 큰) 레이어는 그리지 않는다.
//   · LayerIndex 레이어는 그 레이어의 드로우순서에서 ObjectDrawIndex(포함)까지만 그린다.
//     ObjectDrawIndex == UINT32_MAX 면 그 레이어 전체(= 레이어 단위 컷오프).
// 라이팅/컴포짓/톤맵 포스트체인은 부분 씬 위에 그대로 돈다(부분 씬 최종 화면). 파라미터로만
// 전달하므로 메인 게임뷰 렌더(컷오프 미전달)는 영향받지 않는다.
struct Render2DCutoff
{
	bool          Active = false;
	std::uint16_t LayerIndex = 0;
	std::uint32_t ObjectDrawIndex = 0xFFFFFFFFu;
};
