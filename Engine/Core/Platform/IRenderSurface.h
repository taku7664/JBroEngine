#pragma once

#include "Utillity/Pointer/SafePtr.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/Platform/RenderSurfaceTypes.h"

#include <cstddef>
#include <utility>
#include <vector>

class IRenderSurface : public EnableSafeFromThis<IRenderSurface>
{
public:
	virtual ~IRenderSurface() = default;

public:
	virtual bool Create(const RenderSurfaceCreateDesc& desc) = 0;
	virtual void Destroy() = 0;
	virtual void PollEvents(PlatformEvent& platformEvent) = 0;

	virtual RenderSurfaceSize GetSize() const = 0;
	virtual NativeSurfaceHandle GetNativeSurfaceHandle() const = 0;
	virtual void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) = 0;

	// ── 윈도우 이벤트 구독(단일 채널) ────────────────────────────────────────
	// 윈도우 메세지(포커스 등)를 핸들러로 푸시한다. 구독 1번으로 모든 종류를 받고
	// SurfaceEvent::Type 으로 분기한다. 토큰으로 해지. 호스트 전용(DLL 경계 안 넘김).
	SurfaceEventToken Subscribe(SurfaceEventHandler handler)
	{
		const SurfaceEventToken token = ++m_nextSurfaceEventToken;
		m_surfaceEventHandlers.push_back({ token, std::move(handler) });
		return token;
	}

	void Unsubscribe(SurfaceEventToken token)
	{
		for (std::size_t i = 0; i < m_surfaceEventHandlers.size(); ++i)
		{
			if (m_surfaceEventHandlers[i].first == token)
			{
				m_surfaceEventHandlers.erase(m_surfaceEventHandlers.begin() + i);
				return;
			}
		}
	}

protected:
	// 플랫폼별 surface 구현이 윈도우 메세지를 해석한 뒤 호출한다(메인 스레드, PollEvents 내).
	void DispatchSurfaceEvent(const SurfaceEvent& surfaceEvent) const
	{
		// 콜백 도중 구독 변경에 안전하도록 스냅샷 순회.
		const std::vector<std::pair<SurfaceEventToken, SurfaceEventHandler>> snapshot = m_surfaceEventHandlers;
		for (const auto& entry : snapshot)
		{
			if (entry.second)
			{
				entry.second(surfaceEvent);
			}
		}
	}

private:
	std::vector<std::pair<SurfaceEventToken, SurfaceEventHandler>> m_surfaceEventHandlers;
	SurfaceEventToken m_nextSurfaceEventToken = 0;
};
