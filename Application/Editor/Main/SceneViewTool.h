#pragma once

#include "Engine/Editor/ImWindow/ImWindow.h"
#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Utillity/Vector2T.h"

class CScene;

class CSceneViewTool : public CImWindow
{
public:
	using CImWindow::CImWindow;
	virtual ~CSceneViewTool() = default;

	// 에디터 카메라 상태 getter (저장용: target 값 반환)
	Vector2<float> GetEditorCameraPos()  const { return m_targetCameraPos; }
	float          GetEditorCameraSize() const { return m_targetCameraSize; }

	// 에디터 카메라 즉시 이동 (프로젝트 로드 시 적용)
	void SetEditorCamera(float x, float y, float size);

	// 지정 엔티티를 화면 중앙에 포커싱 (더블클릭 포커스용)
	// 스무딩을 타므로 부드럽게 이동함
	void FocusOnEntity(EntityId entity, const CScene& scene);

private:
	void OnCreate()     override;
	void OnDestroy()    override;
	void OnUpdate()     override;
	void OnRenderStay() override;

private:
	// Editor camera — target (modified by input) and displayed (smoothed toward target).
	// Adjust CAMERA_SMOOTH_SPEED in SceneViewTool.cpp to control responsiveness.
	Vector2<float> m_targetCameraPos  = Vector2<float>(0.0f, 0.0f);
	float          m_targetCameraSize = 5.0f;

	Vector2<float> m_cameraPos  = Vector2<float>(0.0f, 0.0f); // smoothed display value
	float          m_cameraSize = 5.0f;                        // smoothed display value
};

