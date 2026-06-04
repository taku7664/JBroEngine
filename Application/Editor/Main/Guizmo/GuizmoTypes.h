#pragma once

#include "Engine/GameFramework/Component/Transform2D.h"
#include "Utillity/Math/Vector2T.h"

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"

#include <vector>

class CGameObject;
class CScene;

enum class EGuizmoDimension
{
	TwoD,
	ThreeD
};

enum class EGuizmoMode
{
	Translate,
	Rotate,
	Scale
};

enum class EGuizmoSpace
{
	World,
	Local
};

enum class EGuizmoPivot
{
	SelectionCenter,
	ActiveObject
};

enum class EGuizmoHandle2D
{
	None,
	MoveX,
	MoveY,
	MoveXY,
	Rotate,
	ScaleX,
	ScaleY,
	ScaleXY
};

struct GuizmoFrameContext
{
	CScene* Scene = nullptr;
	ImRect ViewportRect;
	ImDrawList* DrawList = nullptr;
	Vector2 CameraPosition = Vector2(0.0f, 0.0f);
	float CameraSize = 1.0f;
	float PixelsPerUnit = 100.0f;
	std::vector<CGameObject*> Selection;
	CGameObject* ActiveObject = nullptr;
	bool IsSceneViewHovered = false;
	bool IsSceneViewActive = false;
	bool IsBlockedByOverlay = false;
};

struct GuizmoFrameResult
{
	bool ConsumedMouse = false;
	bool IsActive = false;
	bool ChangedTransform = false;
};

struct GuizmoTransformSnapshot
{
	CGameObject* Object = nullptr;
	Transform2D InitialTransform;
};
