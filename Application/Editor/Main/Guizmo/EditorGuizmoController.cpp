#include "pch.h"
#include "EditorGuizmoController.h"

GuizmoFrameResult CEditorGuizmoController::UpdateAndDraw(const GuizmoFrameContext& context)
{
	if (m_dimension == EGuizmoDimension::ThreeD)
	{
		return m_guizmo3D.UpdateAndDraw(context, m_mode, m_space, m_pivot);
	}
	return m_guizmo2D.UpdateAndDraw(context, m_mode, m_space, m_pivot);
}
