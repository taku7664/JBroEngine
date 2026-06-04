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
	EGuizmoHandle2D HitTestTranslate(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const;
	void DrawTranslate(const GuizmoFrameContext& context, const ImVec2& pivotScreen, EGuizmoHandle2D hotHandle) const;

	void BeginTranslateDrag(const GuizmoFrameContext& context,
	                        EGuizmoHandle2D handle,
	                        const Vector2& pivotWorld);
	GuizmoFrameResult UpdateTranslateDrag(const GuizmoFrameContext& context);
	void EndTranslateDrag(const GuizmoFrameContext& context, bool commit);

	Vector2 CalculatePivotWorld(const GuizmoFrameContext& context) const;
	Vector2 ScreenToWorld(const GuizmoFrameContext& context, const ImVec2& screen) const;
	ImVec2 WorldToScreen(const GuizmoFrameContext& context, const Vector2& world) const;
	Vector2 ApplyHandleConstraint(EGuizmoHandle2D handle, const Vector2& delta) const;
	Transform2D TranslateObjectToWorldDelta(CGameObject& object,
	                                        const Transform2D& initialTransform,
	                                        const Vector2& worldDelta) const;

private:
	bool m_dragging = false;
	EGuizmoHandle2D m_activeHandle = EGuizmoHandle2D::None;
	ImVec2 m_dragStartMouse = ImVec2(0.0f, 0.0f);
	Vector2 m_dragStartWorld = Vector2(0.0f, 0.0f);
	std::vector<GuizmoTransformSnapshot> m_dragSnapshots;
	std::vector<Transform2D> m_dragNewTransforms;
};
