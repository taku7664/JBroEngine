#pragma once

#include "Editor/Main/Guizmo/GuizmoTypes.h"

class CGuizmo2D
{
public:
	GuizmoFrameResult UpdateAndDraw(const GuizmoFrameContext& context,
	                                 EGuizmoMode mode,
	                                 EGuizmoSpace space,
	                                 EGuizmoPivot pivot);
	void CancelDrag();

private:
	EGuizmoHandle2D HitTestTranslate(const GuizmoFrameContext& context,
	                                 const ImVec2& pivotScreen,
	                                 const Vector2& pivotWorld,
	                                 const Vector2& axisX,
	                                 const Vector2& axisY) const;
	EGuizmoHandle2D HitTestRotate(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const;
	EGuizmoHandle2D HitTestScale(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const;
	void DrawTranslate(const GuizmoFrameContext& context,
	                   const ImVec2& pivotScreen,
	                   const Vector2& pivotWorld,
	                   EGuizmoHandle2D hotHandle,
	                   const Vector2& axisX,
	                   const Vector2& axisY) const;
	void DrawRotate(const GuizmoFrameContext& context, const ImVec2& pivotScreen, EGuizmoHandle2D hotHandle) const;
	void DrawScale(const GuizmoFrameContext& context, const ImVec2& pivotScreen, EGuizmoHandle2D hotHandle) const;

	void BeginTranslateDrag(const GuizmoFrameContext& context,
	                        EGuizmoHandle2D handle,
	                        const Vector2& pivotWorld);
	void BeginRotateDrag(const GuizmoFrameContext& context, const Vector2& pivotWorld);
	void BeginScaleDrag(const GuizmoFrameContext& context,
	                    EGuizmoHandle2D handle,
	                    const Vector2& pivotWorld);
	GuizmoFrameResult UpdateTranslateDrag(const GuizmoFrameContext& context);
	GuizmoFrameResult UpdateRotateDrag(const GuizmoFrameContext& context);
	GuizmoFrameResult UpdateScaleDrag(const GuizmoFrameContext& context);
	void EndDrag(const GuizmoFrameContext& context, bool commit);

	Vector2 CalculatePivotWorld(const GuizmoFrameContext& context) const;
	Vector2 ScreenToWorld(const GuizmoFrameContext& context, const ImVec2& screen) const;
	ImVec2 WorldToScreen(const GuizmoFrameContext& context, const Vector2& world) const;
	float CalculateScreenAngle(const GuizmoFrameContext& context, const ImVec2& screen, const Vector2& pivotWorld) const;
	void CalculateTranslateBasis(const GuizmoFrameContext& context,
	                             EGuizmoSpace space,
	                             Vector2& outAxisX,
	                             Vector2& outAxisY) const;
	Vector2 ApplyHandleConstraint(EGuizmoHandle2D handle,
	                              const Vector2& delta,
	                              const Vector2& axisX,
	                              const Vector2& axisY) const;
	Vector2 RotateAroundPivot(const Vector2& point, const Vector2& pivot, float radians) const;
	Vector2 WorldPositionToLocalPosition(const CGameObject& object, const Vector2& worldPosition) const;
	Transform2D TranslateObjectToWorldDelta(CGameObject& object,
	                                        const Transform2D& initialTransform,
	                                        const Vector2& worldDelta) const;
	Transform2D RotateObjectAroundPivot(CGameObject& object,
	                                    const Transform2D& initialTransform,
	                                    const Vector2& pivotWorld,
	                                    float deltaRadians) const;
	Transform2D ScaleObjectAroundPivot(CGameObject& object,
	                                   const Transform2D& initialTransform,
	                                   const Vector2& pivotWorld,
	                                   const Vector2& scaleFactor) const;

private:
	bool m_dragging = false;
	EGuizmoHandle2D m_activeHandle = EGuizmoHandle2D::None;
	ImVec2 m_dragStartMouse = ImVec2(0.0f, 0.0f);
	Vector2 m_dragStartWorld = Vector2(0.0f, 0.0f);
	Vector2 m_dragAxisX = Vector2(1.0f, 0.0f);
	Vector2 m_dragAxisY = Vector2(0.0f, 1.0f);
	float m_dragStartAngle = 0.0f;
	std::vector<GuizmoTransformSnapshot> m_dragSnapshots;
	std::vector<Transform2D> m_dragNewTransforms;
};
