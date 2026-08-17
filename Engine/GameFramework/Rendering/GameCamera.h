#pragma once

#include "Core/Renderer/Render2DTypes.h"

#include <vector>

class CGameCanvas;

// ─────────────────────────────────────────────────────────────────────────────
//  게임 렌더 수집기 — 캔버스를 읽어 렌더 파이프라인이 쓰는 POD 스냅샷으로 바꾼다.
//
//  여기는 "무엇을 그릴지"만 정한다. "어떻게 그릴지"(패스 구성·라이팅·컴포짓)는
//  Core/Renderer/Render2DPipeline.h 의 RenderScene2D 가 소유한다. 그 경계 덕에
//  파이프라인을 갈아끼워도 이 파일은 그대로다.
//
//  세 수집기 모두 **결과를 out 파라미터에 채운다**(append 아님 — 먼저 clear). 매 프레임 도는
//  경로라 vector 를 반환하면 호출마다 힙 할당이 붙기 때문이다. 호출자가 버퍼를 멤버로 들고
//  재사용하면 용량이 유지되어 프레임 할당이 사라진다(프레임 루프 힙 할당 금지 규칙).
// ─────────────────────────────────────────────────────────────────────────────

// 캔버스의 뷰포트 목록을 렌더 스냅샷으로 해석한다(카메라 Ref 해석 + 렉트 계산 + 레이어 필터).
// 뷰포트가 하나도 없으면 풀스크린 기본 뷰포트 1개로 간주한다 — 스플릿을 안 쓰는 게임은
// 뷰포트를 저작하지 않아도 그대로 그려진다.
// canvas 을 const 로 받지 않는 이유: 카메라 Ref 해석 결과를 뷰포트에 캐시한다(매 프레임
// guid 문자열 파싱을 피하기 위함).
void CollectGameRenderViewports(CGameCanvas& canvas, float renderWidth, float renderHeight,
	std::vector<Render2DViewportDesc>& outViewports);

// 캔버스의 활성 Light2D 를 월드 공간 스냅샷으로 수집한다(카메라 수집과 동일 계층에서 호출).
void CollectGameRenderLights(const CGameCanvas& canvas, std::vector<Render2DLightDesc>& outLights);

// 캔버스 레이어를 컴포짓 순서대로 수집한다(카메라/라이트 수집과 동일 계층에서 호출).
// forceOwnTextureAll = 전 레이어를 RT 경유로 강제(에디터 — 레이어별 썸네일·디버깅용).
void CollectGameRenderLayers(const CGameCanvas& canvas, bool forceOwnTextureAll,
	std::vector<Render2DLayerDesc>& outLayers);
