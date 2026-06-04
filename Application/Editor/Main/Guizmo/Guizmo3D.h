#pragma once

#include "Editor/Main/Guizmo/GuizmoTypes.h"

class CGuizmo3D
{
public:
	GuizmoFrameResult UpdateAndDraw(const GuizmoFrameContext& context,
	                                 EGuizmoMode mode,
	                                 EGuizmoSpace space,
	                                 EGuizmoPivot pivot);
};
