#pragma once

#include "Core/Platform/PlatformTypes.h"
#include "Core/RHI/RHICommandTypes.h"
#include "Core/RHI/RHIGraphicsTypes.h"   // ERHIBlendMode — 레이어 컴포짓 블렌드
#include "Core/Renderer/RendererTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <vector>

class CGameCanvas;
class IRHICommandContext;
class IRHITexture;
class IRenderer;
class IRenderScene;

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
	float            ParallaxFactor = 1.0f;
	bool             Visible = true;
	bool             Static = false;
	bool             ForceOwnTexture = false;
	// 자기 RT 를 거쳐야 하는가 — 블렌드/Opacity/Static/강제 중 하나라도 걸리면 true.
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
std::vector<GameRenderViewportDesc> CollectGameRenderViewports(CGameCanvas& canvas, float renderWidth, float renderHeight);

// 캔버스의 활성 Light2D 를 월드 공간 스냅샷으로 수집한다(카메라 수집과 동일 계층에서 호출).
std::vector<GameRenderLightDesc> CollectGameRenderLights(const CGameCanvas& canvas);

// 캔버스 레이어를 컴포짓 순서대로 수집한다(카메라/라이트 수집과 동일 계층에서 호출).
// forceOwnTextureAll = 전 레이어를 RT 경유로 강제(에디터 — 레이어별 썸네일·디버깅용).
std::vector<GameRenderLayerDesc> CollectGameRenderLayers(const CGameCanvas& canvas, bool forceOwnTextureAll = false);

// 뷰포트 목록을 순서대로 그린다: 배경색 → for 뷰포트 { for 레이어: 드로우 → 렉트에 컴포짓 }.
// backgroundColor = 캔버스 바탕색(float[4], null 이면 투명).
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
	const float* backgroundColor = nullptr);
