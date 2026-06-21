#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Utillity/Math/SizeT.h"   // Size<int> (윈도우 이벤트 인자 — Utillity 타입)

#include <cstdint>
#include <functional>

#if !JBRO_PLATFORM_WINDOWS
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(parameter) (void)(parameter)
#endif
#endif

// 게임이 요구하는 화면 방향. 모바일 회전 보정의 권위 신호다(버퍼 방향과 비교해 회전량 결정).
// Auto 는 회전을 게임이 강제하지 않음 → 플랫폼의 실제 디스플레이 회전(JNI getRotation)을 따른다.
enum class EScreenOrientation
{
	Auto,
	Landscape,
	Portrait
};

struct PlatformDesc
{
	const wchar_t* ApplicationName = L"JBroEngine";
	int WindowWidth = 1280;
	int WindowHeight = 720;
	bool IsEditor = false;
	EScreenOrientation DesiredOrientation = EScreenOrientation::Auto;
};

// PollEvents 가 메인 루프에 돌려주는 프레임 단위 상태(루프 제어 전용).
// 포커스/리사이즈 등 이산 이벤트는 PlatformEvent 가 아니라 윈도우 이벤트 채널(아래)로 푸시된다.
struct PlatformEvent
{
	bool WantsExit = false;
};

// ── 윈도우 이벤트 채널 (호스트 전용, 단일 구독 채널) ──────────────────────────
// 이벤트 종류가 늘어도 구독 지점은 1개다 — enum 값만 추가하면 된다(콜백 필드 폭증 없음).
// 구독자(예: 에디터)는 IRenderSurface::Subscribe 로 핸들러 1개를 걸고 Type 으로 분기한다.
enum class ESurfaceEventType
{
	FocusGained,
	FocusLost,
	Resized,
};

struct SurfaceEvent
{
	ESurfaceEventType Type;
	Size<int>        ClientSize;   // Resized 일 때만 유효(클라이언트 영역 픽셀 크기).
};

using SurfaceEventHandler = std::function<void(const SurfaceEvent&)>;
using SurfaceEventToken   = std::uint64_t;

enum class EPlatformType
{
	Unknown,
	Windows,
	Web,
	Android,
	IOS
};
