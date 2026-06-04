#include "pch.h"
#include "Guizmo2D.h"

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Editor.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

#include <cmath>

namespace
{
	constexpr float AXIS_LENGTH = 72.0f;
	constexpr float CENTER_RADIUS = 7.0f;
	constexpr float AXIS_HIT_RADIUS = 8.0f;
	constexpr float ARROW_SIZE = 8.0f;
	constexpr float LINE_THICKNESS = 2.5f;
	constexpr float ROTATE_RADIUS = 58.0f;
	constexpr float ROTATE_HIT_RADIUS = 8.0f;
	constexpr float SCALE_HANDLE_SIZE = 11.0f;
	constexpr float SCALE_UNIFORM_OFFSET = 50.0f;
	constexpr float MIN_SCALE_ABS = 0.001f;
	constexpr float MIN_SCALE_FACTOR = 0.001f;
	constexpr float ROTATE_SNAP_RADIANS = 0.2617993878f;

	constexpr ImU32 COLOR_X = IM_COL32(230, 70, 70, 255);
	constexpr ImU32 COLOR_Y = IM_COL32(80, 210, 110, 255);
	constexpr ImU32 COLOR_CENTER = IM_COL32(240, 210, 70, 255);
	constexpr ImU32 COLOR_ROTATE = IM_COL32(120, 170, 255, 255);
	constexpr ImU32 COLOR_SCALE = IM_COL32(245, 190, 85, 255);
	constexpr ImU32 COLOR_HOVER = IM_COL32(255, 255, 255, 255);
	constexpr ImU32 COLOR_SHADOW = IM_COL32(0, 0, 0, 140);

	float DistanceSq(const ImVec2& a, const ImVec2& b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return dx * dx + dy * dy;
	}

	float DistanceToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b)
	{
		const float abx = b.x - a.x;
		const float aby = b.y - a.y;
		const float lenSq = abx * abx + aby * aby;
		if (lenSq <= 0.0001f)
		{
			return DistanceSq(p, a);
		}

		const float t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq, 0.0f, 1.0f);
		const ImVec2 closest(a.x + abx * t, a.y + aby * t);
		return DistanceSq(p, closest);
	}

	bool IsSameTransform(const Transform2D& lhs, const Transform2D& rhs)
	{
		constexpr float EPSILON = 0.00001f;
		return std::abs(lhs.Position.x - rhs.Position.x) <= EPSILON
			&& std::abs(lhs.Position.y - rhs.Position.y) <= EPSILON
			&& std::abs(lhs.RotationRadians.Value - rhs.RotationRadians.Value) <= EPSILON
			&& std::abs(lhs.Scale.x - rhs.Scale.x) <= EPSILON
			&& std::abs(lhs.Scale.y - rhs.Scale.y) <= EPSILON;
	}

	ImRect SquareRect(const ImVec2& center, float size)
	{
		const float half = size * 0.5f;
		return ImRect(ImVec2(center.x - half, center.y - half),
		              ImVec2(center.x + half, center.y + half));
	}

	float PreserveScaleSignAndClamp(float initialScale, float factor)
	{
		const float sign = initialScale < 0.0f ? -1.0f : 1.0f;
		const float initialAbs = std::max(std::abs(initialScale), MIN_SCALE_ABS);
		const float clampedFactor = std::max(factor, MIN_SCALE_FACTOR);
		const float scaledAbs = std::max(initialAbs * clampedFactor, MIN_SCALE_ABS);
		return sign * scaledAbs;
	}

	float SafeRatio(float numerator, float denominator)
	{
		if (std::abs(denominator) <= 0.0001f)
		{
			return 1.0f;
		}
		return numerator / denominator;
	}

	bool IsScaleHandle(EGuizmoHandle2D handle)
	{
		return handle == EGuizmoHandle2D::ScaleX
			|| handle == EGuizmoHandle2D::ScaleY
			|| handle == EGuizmoHandle2D::ScaleXY;
	}

	float SnapAngleRadians(float radians)
	{
		return std::round(radians / ROTATE_SNAP_RADIANS) * ROTATE_SNAP_RADIANS;
	}

	Vector2 SafeNormalizedOrDefault(const Vector2& vector, const Vector2& fallback)
	{
		const float len = vector.Length();
		if (len <= 0.0001f)
		{
			return fallback;
		}
		return vector / len;
	}
}

GuizmoFrameResult CGuizmo2D::UpdateAndDraw(const GuizmoFrameContext& context,
	EGuizmoMode mode,
	EGuizmoSpace space,
	EGuizmoPivot pivot)
{
	GuizmoFrameResult result;
	if (m_dragging)
	{
		if (m_activeHandle == EGuizmoHandle2D::Rotate)
		{
			result = UpdateRotateDrag(context);
			DrawRotate(context, WorldToScreen(context, m_dragStartWorld), m_activeHandle);
		}
		else if (IsScaleHandle(m_activeHandle))
		{
			result = UpdateScaleDrag(context);
			DrawScale(context, WorldToScreen(context, m_dragStartWorld), m_activeHandle);
		}
		else
		{
			result = UpdateTranslateDrag(context);
			DrawTranslate(context,
			              WorldToScreen(context, m_dragCurrentWorld),
			              m_dragCurrentWorld,
			              m_activeHandle,
			              m_dragAxisX,
			              m_dragAxisY);
		}
		return result;
	}

	if (mode != EGuizmoMode::Translate
		&& mode != EGuizmoMode::Rotate
		&& mode != EGuizmoMode::Scale)
	{
		return result;
	}

	if (nullptr == context.Scene || nullptr == context.DrawList || context.Selection.empty())
	{
		return result;
	}

	const Vector2 pivotWorld =
		pivot == EGuizmoPivot::ActiveObject && context.ActiveObject != nullptr
		? GetWorldTransform(*context.ActiveObject).TransformPoint(Vector2(0.0f, 0.0f))
		: CalculatePivotWorld(context);
	const ImVec2 pivotScreen = WorldToScreen(context, pivotWorld);
	Vector2 translateAxisX(1.0f, 0.0f);
	Vector2 translateAxisY(0.0f, 1.0f);
	CalculateTranslateBasis(context, space, translateAxisX, translateAxisY);

	EGuizmoHandle2D hotHandle = EGuizmoHandle2D::None;
	if (context.IsSceneViewHovered && false == context.IsBlockedByOverlay)
	{
		if (mode == EGuizmoMode::Rotate)
		{
			hotHandle = HitTestRotate(context, pivotScreen);
		}
		else if (mode == EGuizmoMode::Scale)
		{
			hotHandle = HitTestScale(context, pivotScreen);
		}
		else
		{
			hotHandle = HitTestTranslate(context,
			                             pivotScreen,
			                             pivotWorld,
			                             translateAxisX,
			                             translateAxisY);
		}
	}

	if (mode == EGuizmoMode::Rotate)
	{
		DrawRotate(context, pivotScreen, hotHandle);
	}
	else if (mode == EGuizmoMode::Scale)
	{
		DrawScale(context, pivotScreen, hotHandle);
	}
	else
	{
		DrawTranslate(context, pivotScreen, pivotWorld, hotHandle, translateAxisX, translateAxisY);
	}

	if (hotHandle != EGuizmoHandle2D::None)
	{
		result.ConsumedMouse = ImGui::IsMouseDown(ImGuiMouseButton_Left)
			|| ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	}

	if (hotHandle != EGuizmoHandle2D::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hotHandle == EGuizmoHandle2D::Rotate)
		{
			BeginRotateDrag(context, pivotWorld);
		}
		else if (IsScaleHandle(hotHandle))
		{
			BeginScaleDrag(context, hotHandle, pivotWorld);
		}
		else
		{
			m_dragAxisX = translateAxisX;
			m_dragAxisY = translateAxisY;
			BeginTranslateDrag(context, hotHandle, pivotWorld);
		}
		result.ConsumedMouse = true;
		result.IsActive = true;
	}
	return result;
}

void CGuizmo2D::CancelDrag()
{
	for (const GuizmoTransformSnapshot& snapshot : m_dragSnapshots)
	{
		if (snapshot.Object)
		{
			snapshot.Object->GetTransform() = snapshot.InitialTransform;
		}
	}
	m_dragging = false;
	m_activeHandle = EGuizmoHandle2D::None;
	m_dragSnapshots.clear();
	m_dragNewTransforms.clear();
}

EGuizmoHandle2D CGuizmo2D::HitTestTranslate(const GuizmoFrameContext& context,
                                            const ImVec2& pivotScreen,
                                            const Vector2& pivotWorld,
                                            const Vector2& axisX,
                                            const Vector2& axisY) const
{
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	if (false == context.ViewportRect.Contains(mouse))
	{
		return EGuizmoHandle2D::None;
	}

	if (DistanceSq(mouse, pivotScreen) <= CENTER_RADIUS * CENTER_RADIUS)
	{
		return EGuizmoHandle2D::MoveXY;
	}

	const ImVec2 xDir = WorldToScreen(context, pivotWorld + axisX);
	const ImVec2 yDir = WorldToScreen(context, pivotWorld + axisY);
	Vector2 xScreenDelta(xDir.x - pivotScreen.x, xDir.y - pivotScreen.y);
	Vector2 yScreenDelta(yDir.x - pivotScreen.x, yDir.y - pivotScreen.y);
	xScreenDelta = SafeNormalizedOrDefault(xScreenDelta, Vector2(1.0f, 0.0f));
	yScreenDelta = SafeNormalizedOrDefault(yScreenDelta, Vector2(0.0f, -1.0f));

	const ImVec2 xEnd(pivotScreen.x + xScreenDelta.x * AXIS_LENGTH,
	                  pivotScreen.y + xScreenDelta.y * AXIS_LENGTH);
	const ImVec2 yEnd(pivotScreen.x + yScreenDelta.x * AXIS_LENGTH,
	                  pivotScreen.y + yScreenDelta.y * AXIS_LENGTH);
	if (DistanceToSegmentSq(mouse, pivotScreen, xEnd) <= AXIS_HIT_RADIUS * AXIS_HIT_RADIUS)
	{
		return EGuizmoHandle2D::MoveX;
	}
	if (DistanceToSegmentSq(mouse, pivotScreen, yEnd) <= AXIS_HIT_RADIUS * AXIS_HIT_RADIUS)
	{
		return EGuizmoHandle2D::MoveY;
	}
	return EGuizmoHandle2D::None;
}

EGuizmoHandle2D CGuizmo2D::HitTestRotate(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const
{
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	if (false == context.ViewportRect.Contains(mouse))
	{
		return EGuizmoHandle2D::None;
	}

	const float dist = std::sqrt(DistanceSq(mouse, pivotScreen));
	if (std::abs(dist - ROTATE_RADIUS) <= ROTATE_HIT_RADIUS)
	{
		return EGuizmoHandle2D::Rotate;
	}
	return EGuizmoHandle2D::None;
}

EGuizmoHandle2D CGuizmo2D::HitTestScale(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const
{
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	if (false == context.ViewportRect.Contains(mouse))
	{
		return EGuizmoHandle2D::None;
	}

	const ImVec2 xEnd(pivotScreen.x + AXIS_LENGTH, pivotScreen.y);
	const ImVec2 yEnd(pivotScreen.x, pivotScreen.y - AXIS_LENGTH);
	const ImVec2 uniformEnd(pivotScreen.x + SCALE_UNIFORM_OFFSET, pivotScreen.y - SCALE_UNIFORM_OFFSET);
	const float hitRadiusSq = AXIS_HIT_RADIUS * AXIS_HIT_RADIUS;
	if (SquareRect(uniformEnd, SCALE_HANDLE_SIZE + AXIS_HIT_RADIUS).Contains(mouse))
	{
		return EGuizmoHandle2D::ScaleXY;
	}
	if (SquareRect(xEnd, SCALE_HANDLE_SIZE + AXIS_HIT_RADIUS).Contains(mouse))
	{
		return EGuizmoHandle2D::ScaleX;
	}
	if (SquareRect(yEnd, SCALE_HANDLE_SIZE + AXIS_HIT_RADIUS).Contains(mouse))
	{
		return EGuizmoHandle2D::ScaleY;
	}
	if (DistanceToSegmentSq(mouse, pivotScreen, uniformEnd) <= hitRadiusSq)
	{
		return EGuizmoHandle2D::ScaleXY;
	}
	if (DistanceToSegmentSq(mouse, pivotScreen, xEnd) <= hitRadiusSq)
	{
		return EGuizmoHandle2D::ScaleX;
	}
	if (DistanceToSegmentSq(mouse, pivotScreen, yEnd) <= hitRadiusSq)
	{
		return EGuizmoHandle2D::ScaleY;
	}
	return EGuizmoHandle2D::None;
}

void CGuizmo2D::DrawTranslate(const GuizmoFrameContext& context,
                              const ImVec2& pivotScreen,
                              const Vector2& pivotWorld,
                              EGuizmoHandle2D hotHandle,
                              const Vector2& axisX,
                              const Vector2& axisY) const
{
	ImDrawList* dl = context.DrawList;
	if (nullptr == dl)
	{
		return;
	}

	const ImVec2 xDir = WorldToScreen(context, pivotWorld + axisX);
	const ImVec2 yDir = WorldToScreen(context, pivotWorld + axisY);
	Vector2 xScreenDelta(xDir.x - pivotScreen.x, xDir.y - pivotScreen.y);
	Vector2 yScreenDelta(yDir.x - pivotScreen.x, yDir.y - pivotScreen.y);
	xScreenDelta = SafeNormalizedOrDefault(xScreenDelta, Vector2(1.0f, 0.0f));
	yScreenDelta = SafeNormalizedOrDefault(yScreenDelta, Vector2(0.0f, -1.0f));

	const ImVec2 xEnd(pivotScreen.x + xScreenDelta.x * AXIS_LENGTH,
	                  pivotScreen.y + xScreenDelta.y * AXIS_LENGTH);
	const ImVec2 yEnd(pivotScreen.x + yScreenDelta.x * AXIS_LENGTH,
	                  pivotScreen.y + yScreenDelta.y * AXIS_LENGTH);
	const ImU32 xColor = hotHandle == EGuizmoHandle2D::MoveX ? COLOR_HOVER : COLOR_X;
	const ImU32 yColor = hotHandle == EGuizmoHandle2D::MoveY ? COLOR_HOVER : COLOR_Y;
	const ImU32 centerColor = hotHandle == EGuizmoHandle2D::MoveXY ? COLOR_HOVER : COLOR_CENTER;

	dl->PushClipRect(context.ViewportRect.Min, context.ViewportRect.Max, true);

	dl->AddLine(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	            ImVec2(xEnd.x + 1.0f, xEnd.y + 1.0f),
	            COLOR_SHADOW, LINE_THICKNESS + 1.0f);
	dl->AddLine(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	            ImVec2(yEnd.x + 1.0f, yEnd.y + 1.0f),
	            COLOR_SHADOW, LINE_THICKNESS + 1.0f);

	dl->AddLine(pivotScreen, xEnd, xColor, LINE_THICKNESS);
	dl->AddTriangleFilled(
		xEnd,
		ImVec2(xEnd.x - xScreenDelta.x * ARROW_SIZE - xScreenDelta.y * ARROW_SIZE * 0.65f,
		       xEnd.y - xScreenDelta.y * ARROW_SIZE + xScreenDelta.x * ARROW_SIZE * 0.65f),
		ImVec2(xEnd.x - xScreenDelta.x * ARROW_SIZE + xScreenDelta.y * ARROW_SIZE * 0.65f,
		       xEnd.y - xScreenDelta.y * ARROW_SIZE - xScreenDelta.x * ARROW_SIZE * 0.65f),
		xColor);

	dl->AddLine(pivotScreen, yEnd, yColor, LINE_THICKNESS);
	dl->AddTriangleFilled(
		yEnd,
		ImVec2(yEnd.x - yScreenDelta.x * ARROW_SIZE - yScreenDelta.y * ARROW_SIZE * 0.65f,
		       yEnd.y - yScreenDelta.y * ARROW_SIZE + yScreenDelta.x * ARROW_SIZE * 0.65f),
		ImVec2(yEnd.x - yScreenDelta.x * ARROW_SIZE + yScreenDelta.y * ARROW_SIZE * 0.65f,
		       yEnd.y - yScreenDelta.y * ARROW_SIZE - yScreenDelta.x * ARROW_SIZE * 0.65f),
		yColor);

	dl->AddCircleFilled(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	                    CENTER_RADIUS + 1.0f, COLOR_SHADOW, 20);
	dl->AddCircleFilled(pivotScreen, CENTER_RADIUS, centerColor, 20);
	dl->AddCircle(pivotScreen, CENTER_RADIUS, IM_COL32(20, 24, 30, 220), 20, 1.0f);

	dl->PopClipRect();
}

void CGuizmo2D::DrawRotate(const GuizmoFrameContext& context,
                           const ImVec2& pivotScreen,
                           EGuizmoHandle2D hotHandle) const
{
	ImDrawList* dl = context.DrawList;
	if (nullptr == dl)
	{
		return;
	}

	const ImU32 color = hotHandle == EGuizmoHandle2D::Rotate ? COLOR_HOVER : COLOR_ROTATE;
	dl->PushClipRect(context.ViewportRect.Min, context.ViewportRect.Max, true);
	dl->AddCircle(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	              ROTATE_RADIUS, COLOR_SHADOW, 64, LINE_THICKNESS + 1.0f);
	dl->AddCircle(pivotScreen, ROTATE_RADIUS, color, 64, LINE_THICKNESS);
	dl->AddCircleFilled(pivotScreen, 3.5f, color, 16);
	dl->PopClipRect();
}

void CGuizmo2D::DrawScale(const GuizmoFrameContext& context,
                          const ImVec2& pivotScreen,
                          EGuizmoHandle2D hotHandle) const
{
	ImDrawList* dl = context.DrawList;
	if (nullptr == dl)
	{
		return;
	}

	const ImVec2 xEnd(pivotScreen.x + AXIS_LENGTH, pivotScreen.y);
	const ImVec2 yEnd(pivotScreen.x, pivotScreen.y - AXIS_LENGTH);
	const ImVec2 uniformEnd(pivotScreen.x + SCALE_UNIFORM_OFFSET, pivotScreen.y - SCALE_UNIFORM_OFFSET);
	const ImU32 xColor = hotHandle == EGuizmoHandle2D::ScaleX ? COLOR_HOVER : COLOR_X;
	const ImU32 yColor = hotHandle == EGuizmoHandle2D::ScaleY ? COLOR_HOVER : COLOR_Y;
	const ImU32 uniformColor = hotHandle == EGuizmoHandle2D::ScaleXY ? COLOR_HOVER : COLOR_SCALE;

	dl->PushClipRect(context.ViewportRect.Min, context.ViewportRect.Max, true);

	dl->AddLine(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	            ImVec2(xEnd.x + 1.0f, xEnd.y + 1.0f),
	            COLOR_SHADOW, LINE_THICKNESS + 1.0f);
	dl->AddLine(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	            ImVec2(yEnd.x + 1.0f, yEnd.y + 1.0f),
	            COLOR_SHADOW, LINE_THICKNESS + 1.0f);
	dl->AddLine(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	            ImVec2(uniformEnd.x + 1.0f, uniformEnd.y + 1.0f),
	            COLOR_SHADOW, LINE_THICKNESS + 1.0f);

	dl->AddLine(pivotScreen, xEnd, xColor, LINE_THICKNESS);
	dl->AddLine(pivotScreen, yEnd, yColor, LINE_THICKNESS);
	dl->AddLine(pivotScreen, uniformEnd, uniformColor, LINE_THICKNESS);

	const ImRect xRect = SquareRect(xEnd, SCALE_HANDLE_SIZE);
	const ImRect yRect = SquareRect(yEnd, SCALE_HANDLE_SIZE);
	const ImRect uniformRect = SquareRect(uniformEnd, SCALE_HANDLE_SIZE);
	dl->AddRectFilled(ImVec2(xRect.Min.x + 1.0f, xRect.Min.y + 1.0f),
	                  ImVec2(xRect.Max.x + 1.0f, xRect.Max.y + 1.0f),
	                  COLOR_SHADOW, 1.5f);
	dl->AddRectFilled(ImVec2(yRect.Min.x + 1.0f, yRect.Min.y + 1.0f),
	                  ImVec2(yRect.Max.x + 1.0f, yRect.Max.y + 1.0f),
	                  COLOR_SHADOW, 1.5f);
	dl->AddRectFilled(ImVec2(uniformRect.Min.x + 1.0f, uniformRect.Min.y + 1.0f),
	                  ImVec2(uniformRect.Max.x + 1.0f, uniformRect.Max.y + 1.0f),
	                  COLOR_SHADOW, 1.5f);
	dl->AddRectFilled(xRect.Min, xRect.Max, xColor, 1.5f);
	dl->AddRectFilled(yRect.Min, yRect.Max, yColor, 1.5f);
	dl->AddRectFilled(uniformRect.Min, uniformRect.Max, uniformColor, 1.5f);
	dl->AddRect(xRect.Min, xRect.Max, IM_COL32(20, 24, 30, 220), 1.5f);
	dl->AddRect(yRect.Min, yRect.Max, IM_COL32(20, 24, 30, 220), 1.5f);
	dl->AddRect(uniformRect.Min, uniformRect.Max, IM_COL32(20, 24, 30, 220), 1.5f);
	dl->AddCircleFilled(pivotScreen, 3.5f, uniformColor, 16);

	dl->PopClipRect();
}

void CGuizmo2D::BeginTranslateDrag(const GuizmoFrameContext& context,
                                   EGuizmoHandle2D handle,
                                   const Vector2& pivotWorld)
{
	m_dragging = true;
	m_activeHandle = handle;
	m_dragStartMouse = ImGui::GetIO().MousePos;
	m_dragStartWorld = pivotWorld;
	m_dragCurrentWorld = pivotWorld;
	m_dragSnapshots.clear();
	std::vector<CGameObject*> dragObjects = Editor::GetSelectedTopLevel();
	m_dragNewTransforms.clear();

	m_dragSnapshots.reserve(dragObjects.size());
	for (CGameObject* object : dragObjects)
	{
		if (nullptr == object)
		{
			continue;
		}
		m_dragSnapshots.push_back({ object, object->GetTransform() });
	}

	(void)context;
}

void CGuizmo2D::BeginRotateDrag(const GuizmoFrameContext& context, const Vector2& pivotWorld)
{
	m_dragging = true;
	m_activeHandle = EGuizmoHandle2D::Rotate;
	m_dragStartMouse = ImGui::GetIO().MousePos;
	m_dragStartWorld = pivotWorld;
	m_dragCurrentWorld = pivotWorld;
	m_dragStartAngle = CalculateScreenAngle(context, m_dragStartMouse, pivotWorld);
	m_dragSnapshots.clear();
	std::vector<CGameObject*> dragObjects = Editor::GetSelectedTopLevel();
	m_dragNewTransforms.clear();

	m_dragSnapshots.reserve(dragObjects.size());
	for (CGameObject* object : dragObjects)
	{
		if (nullptr == object)
		{
			continue;
		}
		m_dragSnapshots.push_back({ object, object->GetTransform() });
	}
}

void CGuizmo2D::BeginScaleDrag(const GuizmoFrameContext& context,
                               EGuizmoHandle2D handle,
                               const Vector2& pivotWorld)
{
	m_dragging = true;
	m_activeHandle = handle;
	m_dragStartMouse = ImGui::GetIO().MousePos;
	m_dragStartWorld = pivotWorld;
	m_dragCurrentWorld = pivotWorld;
	m_dragSnapshots.clear();
	std::vector<CGameObject*> dragObjects = Editor::GetSelectedTopLevel();
	m_dragNewTransforms.clear();

	m_dragSnapshots.reserve(dragObjects.size());
	for (CGameObject* object : dragObjects)
	{
		if (nullptr == object)
		{
			continue;
		}
		m_dragSnapshots.push_back({ object, object->GetTransform() });
	}

	(void)context;
}

GuizmoFrameResult CGuizmo2D::UpdateTranslateDrag(const GuizmoFrameContext& context)
{
	GuizmoFrameResult result;
	result.ConsumedMouse = true;
	result.IsActive = true;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		EndDrag(context, false);
		return result;
	}

	const Vector2 startWorld = ScreenToWorld(context, m_dragStartMouse);
	const Vector2 currentWorld = ScreenToWorld(context, ImGui::GetIO().MousePos);
	const Vector2 constrainedDelta = ApplyHandleConstraint(m_activeHandle,
	                                                       currentWorld - startWorld,
	                                                       m_dragAxisX,
	                                                       m_dragAxisY);
	m_dragCurrentWorld = m_dragStartWorld + constrainedDelta;

	m_dragNewTransforms.clear();
	m_dragNewTransforms.reserve(m_dragSnapshots.size());
	for (const GuizmoTransformSnapshot& snapshot : m_dragSnapshots)
	{
		if (nullptr == snapshot.Object)
		{
			continue;
		}
		Transform2D next = TranslateObjectToWorldDelta(*snapshot.Object,
		                                               snapshot.InitialTransform,
		                                               constrainedDelta);
		snapshot.Object->GetTransform() = next;
		m_dragNewTransforms.push_back(next);
	}

	result.ChangedTransform = false == m_dragNewTransforms.empty();

	if (false == ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		EndDrag(context, true);
	}
	return result;
}

GuizmoFrameResult CGuizmo2D::UpdateRotateDrag(const GuizmoFrameContext& context)
{
	GuizmoFrameResult result;
	result.ConsumedMouse = true;
	result.IsActive = true;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		EndDrag(context, false);
		return result;
	}

	const float currentAngle = CalculateScreenAngle(context, ImGui::GetIO().MousePos, m_dragStartWorld);
	float deltaRadians = currentAngle - m_dragStartAngle;
	if (ImGui::GetIO().KeyShift)
	{
		deltaRadians = SnapAngleRadians(deltaRadians);
	}

	m_dragNewTransforms.clear();
	m_dragNewTransforms.reserve(m_dragSnapshots.size());
	for (const GuizmoTransformSnapshot& snapshot : m_dragSnapshots)
	{
		if (nullptr == snapshot.Object)
		{
			continue;
		}
		Transform2D next = RotateObjectAroundPivot(*snapshot.Object,
		                                           snapshot.InitialTransform,
		                                           m_dragStartWorld,
		                                           deltaRadians);
		snapshot.Object->GetTransform() = next;
		m_dragNewTransforms.push_back(next);
	}

	result.ChangedTransform = false == m_dragNewTransforms.empty();

	if (false == ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		EndDrag(context, true);
	}
	return result;
}

GuizmoFrameResult CGuizmo2D::UpdateScaleDrag(const GuizmoFrameContext& context)
{
	GuizmoFrameResult result;
	result.ConsumedMouse = true;
	result.IsActive = true;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		EndDrag(context, false);
		return result;
	}

	const Vector2 startWorld = ScreenToWorld(context, m_dragStartMouse);
	const Vector2 currentWorld = ScreenToWorld(context, ImGui::GetIO().MousePos);
	const Vector2 startOffset = startWorld - m_dragStartWorld;
	const Vector2 currentOffset = currentWorld - m_dragStartWorld;

	float factorX = 1.0f;
	float factorY = 1.0f;
	if (m_activeHandle == EGuizmoHandle2D::ScaleX)
	{
		factorX = SafeRatio(currentOffset.x, startOffset.x);
	}
	else if (m_activeHandle == EGuizmoHandle2D::ScaleY)
	{
		factorY = SafeRatio(currentOffset.y, startOffset.y);
	}
	else if (m_activeHandle == EGuizmoHandle2D::ScaleXY)
	{
		const float startDistance = std::sqrt(startOffset.x * startOffset.x + startOffset.y * startOffset.y);
		const float currentDistance = std::sqrt(currentOffset.x * currentOffset.x + currentOffset.y * currentOffset.y);
		const float uniformFactor = SafeRatio(currentDistance, startDistance);
		factorX = uniformFactor;
		factorY = uniformFactor;
	}

	const Vector2 scaleFactor(std::max(factorX, MIN_SCALE_FACTOR), std::max(factorY, MIN_SCALE_FACTOR));

	m_dragNewTransforms.clear();
	m_dragNewTransforms.reserve(m_dragSnapshots.size());
	for (const GuizmoTransformSnapshot& snapshot : m_dragSnapshots)
	{
		if (nullptr == snapshot.Object)
		{
			continue;
		}
		Transform2D next = ScaleObjectAroundPivot(*snapshot.Object,
		                                          snapshot.InitialTransform,
		                                          m_dragStartWorld,
		                                          scaleFactor);
		snapshot.Object->GetTransform() = next;
		m_dragNewTransforms.push_back(next);
	}

	result.ChangedTransform = false == m_dragNewTransforms.empty();

	if (false == ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		EndDrag(context, true);
	}
	return result;
}

void CGuizmo2D::EndDrag(const GuizmoFrameContext& context, bool commit)
{
	std::vector<Transform2D> oldTransforms;
	oldTransforms.reserve(m_dragSnapshots.size());
	std::vector<Transform2D> newTransforms;
	newTransforms.reserve(m_dragSnapshots.size());
	std::vector<CGameObject*> objects;
	objects.reserve(m_dragSnapshots.size());

	bool changed = false;
	for (const GuizmoTransformSnapshot& snapshot : m_dragSnapshots)
	{
		if (nullptr == snapshot.Object)
		{
			continue;
		}
		objects.push_back(snapshot.Object);
		oldTransforms.push_back(snapshot.InitialTransform);
		newTransforms.push_back(snapshot.Object->GetTransform());
		changed = changed || false == IsSameTransform(snapshot.InitialTransform, snapshot.Object->GetTransform());
		snapshot.Object->GetTransform() = snapshot.InitialTransform;
	}

	if (commit && changed && nullptr != context.Scene)
	{
		Editor::CommandManager.ExecuteCommand(
			MakeOwnerPtr<CSetObjectTransformsCommand>(
				context.Scene->SafeFromThis(),
				objects,
				oldTransforms,
				newTransforms));
	}

	m_dragging = false;
	m_activeHandle = EGuizmoHandle2D::None;
	m_dragSnapshots.clear();
	m_dragNewTransforms.clear();
}

Vector2 CGuizmo2D::CalculatePivotWorld(const GuizmoFrameContext& context) const
{
	Vector2 sum(0.0f, 0.0f);
	int count = 0;
	for (CGameObject* object : context.Selection)
	{
		if (nullptr == object)
		{
			continue;
		}
		const Matrix3x2 world = GetWorldTransform(*object);
		sum += world.TransformPoint(Vector2(0.0f, 0.0f));
		++count;
	}
	if (count <= 0)
	{
		return Vector2(0.0f, 0.0f);
	}
	return sum / static_cast<float>(count);
}

Vector2 CGuizmo2D::ScreenToWorld(const GuizmoFrameContext& context, const ImVec2& screen) const
{
	const ImVec2 size = context.ViewportRect.GetSize();
	const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
	const float ndcX = ((screen.x - context.ViewportRect.Min.x) / std::max(size.x, 1.0f)) * 2.0f - 1.0f;
	const float ndcY = 1.0f - ((screen.y - context.ViewportRect.Min.y) / std::max(size.y, 1.0f)) * 2.0f;
	return Vector2(
		ndcX * context.CameraSize * aspect + context.CameraPosition.x,
		ndcY * context.CameraSize + context.CameraPosition.y);
}

ImVec2 CGuizmo2D::WorldToScreen(const GuizmoFrameContext& context, const Vector2& world) const
{
	const ImVec2 size = context.ViewportRect.GetSize();
	const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
	const float ndcX = (world.x - context.CameraPosition.x) / (context.CameraSize * aspect);
	const float ndcY = (world.y - context.CameraPosition.y) / context.CameraSize;
	return ImVec2(
		context.ViewportRect.Min.x + (ndcX + 1.0f) * 0.5f * size.x,
		context.ViewportRect.Min.y + (1.0f - ndcY) * 0.5f * size.y);
}

float CGuizmo2D::CalculateScreenAngle(const GuizmoFrameContext& context,
                                      const ImVec2& screen,
                                      const Vector2& pivotWorld) const
{
	const Vector2 world = ScreenToWorld(context, screen);
	const Vector2 delta = world - pivotWorld;
	return std::atan2(delta.y, delta.x);
}

void CGuizmo2D::CalculateTranslateBasis(const GuizmoFrameContext& context,
                                        EGuizmoSpace space,
                                        Vector2& outAxisX,
                                        Vector2& outAxisY) const
{
	outAxisX = Vector2(1.0f, 0.0f);
	outAxisY = Vector2(0.0f, 1.0f);
	if (space != EGuizmoSpace::Local || nullptr == context.ActiveObject)
	{
		return;
	}

	const Matrix3x2 world = GetWorldTransform(*context.ActiveObject);
	outAxisX = SafeNormalizedOrDefault(Vector2(world.M11, world.M12), Vector2(1.0f, 0.0f));
	outAxisY = SafeNormalizedOrDefault(Vector2(world.M21, world.M22), Vector2(0.0f, 1.0f));
}

Vector2 CGuizmo2D::ApplyHandleConstraint(EGuizmoHandle2D handle,
                                         const Vector2& delta,
                                         const Vector2& axisX,
                                         const Vector2& axisY) const
{
	if (handle == EGuizmoHandle2D::MoveX)
	{
		return axisX * delta.Dot(axisX);
	}
	if (handle == EGuizmoHandle2D::MoveY)
	{
		return axisY * delta.Dot(axisY);
	}
	return delta;
}

Vector2 CGuizmo2D::RotateAroundPivot(const Vector2& point, const Vector2& pivot, float radians) const
{
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	const Vector2 local = point - pivot;
	return Vector2(
		pivot.x + local.x * c - local.y * s,
		pivot.y + local.x * s + local.y * c);
}

Vector2 CGuizmo2D::WorldPositionToLocalPosition(const CGameObject& object, const Vector2& worldPosition) const
{
	CGameObject* parent = object.GetParent().TryGet();
	if (nullptr == parent)
	{
		return worldPosition;
	}

	Matrix3x2 parentWorldInverse;
	if (GetWorldTransform(*parent).TryInvert(parentWorldInverse))
	{
		return parentWorldInverse.TransformPoint(worldPosition);
	}
	return object.GetTransform().Position;
}

Transform2D CGuizmo2D::TranslateObjectToWorldDelta(CGameObject& object,
                                                   const Transform2D& initialTransform,
                                                   const Vector2& worldDelta) const
{
	Transform2D next = initialTransform;
	const Vector2 initialLocalPosition = initialTransform.Position;
	CGameObject* parent = object.GetParent().TryGet();
	if (nullptr == parent)
	{
		next.Position = initialLocalPosition + worldDelta;
		return next;
	}

	const Matrix3x2 parentWorld = GetWorldTransform(*parent);
	const Vector2 initialWorldPosition = parentWorld.TransformPoint(initialLocalPosition);
	const Vector2 targetWorldPosition = initialWorldPosition + worldDelta;

	Matrix3x2 parentWorldInverse;
	if (parentWorld.TryInvert(parentWorldInverse))
	{
		next.Position = parentWorldInverse.TransformPoint(targetWorldPosition);
	}
	else
	{
		next.Position = initialLocalPosition;
	}
	return next;
}

Transform2D CGuizmo2D::RotateObjectAroundPivot(CGameObject& object,
                                               const Transform2D& initialTransform,
                                               const Vector2& pivotWorld,
                                               float deltaRadians) const
{
	Transform2D next = initialTransform;
	const Vector2 initialWorldPosition = object.GetParent().IsValid()
		? GetWorldTransform(*object.GetParent().TryGet()).TransformPoint(initialTransform.Position)
		: initialTransform.Position;
	const Vector2 targetWorldPosition = RotateAroundPivot(initialWorldPosition, pivotWorld, deltaRadians);

	next.Position = WorldPositionToLocalPosition(object, targetWorldPosition);
	next.RotationRadians.Value = initialTransform.RotationRadians.Value + deltaRadians;
	return next;
}

Transform2D CGuizmo2D::ScaleObjectAroundPivot(CGameObject& object,
                                              const Transform2D& initialTransform,
                                              const Vector2& pivotWorld,
                                              const Vector2& scaleFactor) const
{
	Transform2D next = initialTransform;
	const Vector2 initialWorldPosition = object.GetParent().IsValid()
		? GetWorldTransform(*object.GetParent().TryGet()).TransformPoint(initialTransform.Position)
		: initialTransform.Position;
	const Vector2 fromPivot = initialWorldPosition - pivotWorld;
	const Vector2 targetWorldPosition(
		pivotWorld.x + fromPivot.x * scaleFactor.x,
		pivotWorld.y + fromPivot.y * scaleFactor.y);

	next.Position = WorldPositionToLocalPosition(object, targetWorldPosition);
	next.Scale.x = PreserveScaleSignAndClamp(initialTransform.Scale.x, scaleFactor.x);
	next.Scale.y = PreserveScaleSignAndClamp(initialTransform.Scale.y, scaleFactor.y);
	return next;
}
