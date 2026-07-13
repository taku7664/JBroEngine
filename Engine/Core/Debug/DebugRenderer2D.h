#pragma once

#include "Utillity/Pointer/SafePtr.h"

#include <unordered_set>

class IRHIDevice;
class IRHICommandContext;
class IRHIProgram;
class IRHIGraphicsPipeline;
class IDebugDraw2D;

// GPU-side renderer for IDebugDraw2D world-space primitives.
class CDebugRenderer2D
{
public:
	bool Initialize(SafePtr<IRHIDevice> rhiDevice);
	void Finalize();

	// 전체 렌더 (기존 동작 유지, 전역 + 엔티티 태깅 모두 포함).
	void Render(
		SafePtr<IRHICommandContext> commandContext,
		const IDebugDraw2D& debugDraw,
		float camX, float camY, float camSize,
		int viewWidth, int viewHeight);

	// Entity == INVALID_DEBUG_OBJECT_ID 인 도형만 렌더 (그리드 전용 패스).
	void RenderGlobal(
		SafePtr<IRHICommandContext> commandContext,
		const IDebugDraw2D& debugDraw,
		float camX, float camY, float camSize,
		int viewWidth, int viewHeight);

	// Entity != INVALID_DEBUG_OBJECT_ID 인 도형 렌더 (콜라이더 패스).
	//   filter == nullptr → 엔티티 태깅된 모든 도형 (루트 모드)
	//   filter 비어있지 않음 → 해당 엔티티만 (포커스 모드)
	void RenderEntities(
		SafePtr<IRHICommandContext> commandContext,
		const IDebugDraw2D& debugDraw,
		float camX, float camY, float camSize,
		int viewWidth, int viewHeight,
		const std::unordered_set<DebugObjectId>* filter = nullptr);

private:
	bool CreatePipeline();

	SafePtr<IRHIDevice>            m_rhiDevice;
	OwnerPtr<IRHIProgram>          m_vertexProgram;
	OwnerPtr<IRHIProgram>          m_pixelProgram;
	OwnerPtr<IRHIGraphicsPipeline> m_pipeline;
	bool m_isInitialized = false;
};
