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
};
