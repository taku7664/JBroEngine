#pragma once

#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHICommandTypes.h"
#include "Core/RHI/RHIGraphicsTypes.h"   // ERHIBlendMode — 레이어 컴포짓 블렌드
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Canvas/GameLayer.h"   // ELayerSpace / EScreenScaleMode
#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Types/Color.h"

#include <cstdint>
#include <vector>

class CGameCanvas;
class IRHICommandContext;
class IRHITexture;
class IRenderer;
class IRenderScene;
class IRHIGpuTimer;

// GPU 타이머 구간 키 = 레이어 인덱스 + 1.
//   +1 오프셋: 프레임 전체 구간(키=nullptr=0)과 레이어 0 을 구별한다.
//   RHI 는 이 키를 불투명 포인터로만 다루고, GPU 프로파일러 창이 레이어 인덱스로 역매핑한다.
inline const void* GpuLayerKey(std::uint16_t layerIndex)
{
	return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(layerIndex) + 1u);
}

// 렌더타겟 진행 프리뷰 컷오프(에디터 GPU 프로파일러 진단). Active 면 드로우순서상 여기까지만 그린다:
//   · LayerIndex 보다 위(인덱스 큰) 레이어는 그리지 않는다.
//   · LayerIndex 레이어는 그 레이어의 드로우순서에서 ObjectDrawIndex(포함)까지만 그린다.
//     ObjectDrawIndex == UINT32_MAX 면 그 레이어 전체(= 레이어 단위 컷오프).
// 라이팅/컴포짓/톤맵 포스트체인은 부분 씬 위에 그대로 돈다(부분 씬 최종 화면). 파라미터로만
// 전달하므로 메인 게임뷰 렌더(컷오프 미전달)는 영향받지 않는다.
struct GpuRenderCutoff
{
	bool          Active = false;
	std::uint16_t LayerIndex = 0;
	std::uint32_t ObjectDrawIndex = 0xFFFFFFFFu;
};

// 뷰포트 1개의 렌더 스냅샷(프레임마다 수집). = 해석된 카메라 뷰 × 출력 렉트 × 레이어 필터.
// 렌더 코드가 캔버스/카메라 컴포넌트를 직접 들여다보지 않게 하는 경계(라이트 수집과 동일 규약).
struct GameRenderViewportDesc
{
	// 카메라 뷰(해석 완료) — 오너 트랜스폼 + OrthographicSize 에서 뽑는다.
	float PosX = 0.0f;
	float PosY = 0.0f;
	float OrthoSize = 5.0f;
	float OrthoSizeX = 5.0f;
	float CosR = 1.0f;
	float SinR = 0.0f;
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

struct GameRenderCameraStats
{
	const void* OwnerObject = nullptr;
	RenderCullingStats Culling;
};

// 레이어 컴포짓 스냅샷(POD). 캔버스 레이어 목록을 렌더가 쓰는 형태로 프레임마다 수집한다
// (렌더 코드가 CGameLayer/캔버스를 직접 들여다보지 않게 — 카메라/라이트 수집과 동일 규약).
// 순서 = 캔버스 순서(아래→위) = Index 오름차순.
struct GameRenderLayerDesc
{
	RenderLayerIndex Index = 0;
	ERHIBlendMode    BlendMode = ERHIBlendMode::LayerNormal;
	float            Opacity = 1.0f;
	// 사는 공간. Screen 이면 카메라를 아예 안 보고 화면 기준 투영으로 그린다.
	// **라이팅 이후에 그려진다** — Base 에 섞으면 월드 라이트맵이 UI 에 곱해진다.
	ELayerSpace      Space = ELayerSpace::World;
	EScreenScaleMode ScaleMode = EScreenScaleMode::FixedHeight;
	// World 스페이스에서만 의미. Screen 은 카메라를 안 따르므로 무관.
	float            ParallaxFactor = 1.0f;
	bool             Visible = true;
	bool             ForceOwnTexture = false;
	// 자기 RT 를 거쳐야 하는가 — 블렌드/Opacity/강제 중 하나라도 걸리면 true.
	// 아니면 대상에 직접 그린다(평범한 레이어에 RT 왕복 대역폭을 물리지 않는다).
	bool             NeedsOwnTexture = false;
};

// 월드 공간 Light2D 스냅샷(POD). RenderWeave LightMap 패스가 소비한다.
struct GameRenderLightDesc
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

// 캔버스의 뷰포트 목록을 렌더 스냅샷으로 해석한다(카메라 Ref 해석 + 렉트 계산 + 레이어 필터).
// 뷰포트가 하나도 없으면 풀스크린 기본 뷰포트 1개로 간주한다 — 스플릿을 안 쓰는 게임은
// 뷰포트를 저작하지 않아도 그대로 그려진다.
// canvas 을 const 로 받지 않는 이유: 카메라 Ref 해석 결과를 뷰포트에 캐시한다(매 프레임
// guid 문자열 파싱을 피하기 위함).
//
// 세 수집기 모두 **결과를 out 파라미터에 채운다**(append 아님 — 먼저 clear). 매 프레임 도는
// 경로라 vector 를 반환하면 호출마다 힙 할당이 붙기 때문이다. 호출자가 버퍼를 멤버로 들고
// 재사용하면 용량이 유지되어 프레임 할당이 사라진다(프레임 루프 힙 할당 금지 규칙).
void CollectGameRenderViewports(CGameCanvas& canvas, float renderWidth, float renderHeight,
	std::vector<GameRenderViewportDesc>& outViewports);

// 캔버스의 활성 Light2D 를 월드 공간 스냅샷으로 수집한다(카메라 수집과 동일 계층에서 호출).
void CollectGameRenderLights(const CGameCanvas& canvas, std::vector<GameRenderLightDesc>& outLights);

// 캔버스 레이어를 컴포짓 순서대로 수집한다(카메라/라이트 수집과 동일 계층에서 호출).
// forceOwnTextureAll = 전 레이어를 RT 경유로 강제(에디터 — 레이어별 썸네일·디버깅용).
void CollectGameRenderLayers(const CGameCanvas& canvas, bool forceOwnTextureAll,
	std::vector<GameRenderLayerDesc>& outLayers);

// 뷰포트 목록을 순서대로 그린다: 배경색 → for 뷰포트 { for 레이어: 드로우 → 렉트에 컴포짓 }.
// backgroundColor = 캔버스 바탕색(null 이면 투명).
void RenderGameViewports(
	IRHICommandContext& commandContext,
	IRenderer& renderer,
	IRenderScene& renderScene,
	const std::vector<GameRenderViewportDesc>& viewports,
	const RenderSurfaceSize& renderTargetSize,
	SafePtr<IRHITexture> renderTarget = nullptr,
	std::vector<GameRenderCameraStats>* outCameraStats = nullptr,
	const std::vector<GameRenderLightDesc>& lights = {},
	const std::vector<GameRenderLayerDesc>& layers = {},
	const Color* backgroundColor = nullptr,
	// null 이 아니면 레이어별 GPU 시간을 이 타이머로 잰다(게임뷰 렌더에서만 넘긴다 — 예외 5).
	IRHIGpuTimer* gpuTimer = nullptr,
	// Active 면 드로우순서상 컷오프까지만 그린다(프로파일러 렌더타겟 진행 프리뷰). 기본 비활성.
	GpuRenderCutoff cutoff = {},
	// 프로파일러 드로우순서 목록을 이 렌더에서 캡처할지. 게임뷰 렌더는 true, 프리뷰 렌더는
	// 게임뷰가 안 그려진 프레임에만 true(그래야 게임뷰 없이도 오브젝트 컷오프가 가능하다).
	// 컷오프가 걸려 있어도 캡처는 레이어 전체를 담는다(컷오프는 시각만 자른다).
	bool captureDrawOrder = false);
