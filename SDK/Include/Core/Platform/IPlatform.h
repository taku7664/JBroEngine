#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/Platform/IRenderSurface.h"

class IPlatform : public EnableSafeFromThis<IPlatform>
{
public:
	virtual ~IPlatform() = default;

public:
	virtual bool Initialize(const PlatformDesc& desc) = 0;
	virtual void PollEvents(PlatformEvent& platformEvent) = 0;
	virtual void Finalize() = 0;

	virtual SafePtr<IRenderSurface> GetMainRenderSurface() const = 0;
	virtual EPlatformType GetPlatformType() const = 0;
	virtual const wchar_t* GetName() const = 0;

	// 디스플레이 회전(0/90/180/270, 시계방향). 모바일에서 패널 네이티브 방향 대비 현재 표시
	// 방향을 나타낸다(Android 는 Display.getRotation() 기반). 회전 미대응 플랫폼은 0.
	virtual int GetDisplayRotationDegrees() const { return 0; }

	// 게임이 요구하는 화면 방향(빌드설정). 회전 보정의 권위 신호. 미지원 플랫폼은 Auto.
	virtual EScreenOrientation GetDesiredOrientation() const { return EScreenOrientation::Auto; }
	virtual void SetDesiredOrientation(EScreenOrientation orientation) { (void)orientation; }

	// 안전영역 인셋(렌더 표면 픽셀). 노치·펀치홀·둥근 모서리·제스처바가 가리는 가장자리다.
	// 미지원 플랫폼은 전부 0 = 안전영역이 곧 화면 전체 → 켜도 꺼도 결과가 같다.
	//
	// **지금은 전 플랫폼이 0 을 돌려준다.** 값을 채우는 건 플랫폼 레이어의 별도 작업이다
	// (안드로이드 WindowInsets/DisplayCutout JNI, 웹 env(safe-area-inset-*), iOS safeAreaInsets).
	// 그럼에도 지금 뚫어 두는 이유: 화면 공간 앵커가 이 개념 없이 먼저 출하되면, 나중에 인셋이
	// 들어오는 순간 기존 프로젝트의 앵커된 UI 가 **전부** 움직인다. 지금은 bool 하나 값이고
	// 나중엔 마이그레이션이다.
	virtual SafeAreaInsets GetSafeAreaInsets() const { return SafeAreaInsets{}; }
};
