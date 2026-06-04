#pragma once

#include "Editor/Main/Guizmo/Guizmo2D.h"
#include "Editor/Main/Guizmo/Guizmo3D.h"

class CEditorGuizmoController
{
public:
	GuizmoFrameResult UpdateAndDraw(const GuizmoFrameContext& context);

	void SetDimension(EGuizmoDimension dimension) { m_dimension = dimension; }
	void SetMode(EGuizmoMode mode) { m_mode = mode; }
	void SetSpace(EGuizmoSpace space) { m_space = space; }
	void SetPivot(EGuizmoPivot pivot) { m_pivot = pivot; }

	EGuizmoDimension GetDimension() const { return m_dimension; }
	EGuizmoMode GetMode() const { return m_mode; }
	EGuizmoSpace GetSpace() const { return m_space; }
	EGuizmoPivot GetPivot() const { return m_pivot; }

private:
	EGuizmoDimension m_dimension = EGuizmoDimension::TwoD;
	EGuizmoMode m_mode = EGuizmoMode::Translate;
	EGuizmoSpace m_space = EGuizmoSpace::World;
	EGuizmoPivot m_pivot = EGuizmoPivot::SelectionCenter;
	CGuizmo2D m_guizmo2D;
	CGuizmo3D m_guizmo3D;
};
