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

	constexpr ImU32 COLOR_X = IM_COL32(230, 70, 70, 255);
	constexpr ImU32 COLOR_Y = IM_COL32(80, 210, 110, 255);
	constexpr ImU32 COLOR_CENTER = IM_COL32(240, 210, 70, 255);
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
}

GuizmoFrameResult CGuizmo2D::UpdateAndDraw(const GuizmoFrameContext& context,
                                           EGuizmoMode mode,
                                           EGuizmoSpace /*space*/,
                                           EGuizmoPivot pivot)
{
	GuizmoFrameResult result;
	if (mode != EGuizmoMode::Translate)
	{
		return result;
	}

	if (m_dragging)
	{
		result = UpdateTranslateDrag(context);
		DrawTranslate(context, WorldToScreen(context, m_dragStartWorld), m_activeHandle);
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
	EGuizmoHandle2D hotHandle = EGuizmoHandle2D::None;
	if (context.IsSceneViewHovered && false == context.IsBlockedByOverlay)
	{
		hotHandle = HitTestTranslate(context, pivotScreen);
	}

	DrawTranslate(context, pivotScreen, hotHandle);

	if (hotHandle != EGuizmoHandle2D::None)
	{
		result.ConsumedMouse = ImGui::IsMouseDown(ImGuiMouseButton_Left)
			|| ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	}

	if (hotHandle != EGuizmoHandle2D::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		BeginTranslateDrag(context, hotHandle, pivotWorld);
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

EGuizmoHandle2D CGuizmo2D::HitTestTranslate(const GuizmoFrameContext& context, const ImVec2& pivotScreen) const
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

	const ImVec2 xEnd(pivotScreen.x + AXIS_LENGTH, pivotScreen.y);
	const ImVec2 yEnd(pivotScreen.x, pivotScreen.y - AXIS_LENGTH);
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

void CGuizmo2D::DrawTranslate(const GuizmoFrameContext& context,
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
		ImVec2(xEnd.x - ARROW_SIZE, xEnd.y - ARROW_SIZE * 0.65f),
		ImVec2(xEnd.x - ARROW_SIZE, xEnd.y + ARROW_SIZE * 0.65f),
		xColor);

	dl->AddLine(pivotScreen, yEnd, yColor, LINE_THICKNESS);
	dl->AddTriangleFilled(
		yEnd,
		ImVec2(yEnd.x - ARROW_SIZE * 0.65f, yEnd.y + ARROW_SIZE),
		ImVec2(yEnd.x + ARROW_SIZE * 0.65f, yEnd.y + ARROW_SIZE),
		yColor);

	dl->AddCircleFilled(ImVec2(pivotScreen.x + 1.0f, pivotScreen.y + 1.0f),
	                    CENTER_RADIUS + 1.0f, COLOR_SHADOW, 20);
	dl->AddCircleFilled(pivotScreen, CENTER_RADIUS, centerColor, 20);
	dl->AddCircle(pivotScreen, CENTER_RADIUS, IM_COL32(20, 24, 30, 220), 20, 1.0f);

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
		EndTranslateDrag(context, false);
		return result;
	}

	const Vector2 startWorld = ScreenToWorld(context, m_dragStartMouse);
	const Vector2 currentWorld = ScreenToWorld(context, ImGui::GetIO().MousePos);
	const Vector2 constrainedDelta = ApplyHandleConstraint(m_activeHandle, currentWorld - startWorld);

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
		EndTranslateDrag(context, true);
	}
	return result;
}

void CGuizmo2D::EndTranslateDrag(const GuizmoFrameContext& context, bool commit)
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

Vector2 CGuizmo2D::ApplyHandleConstraint(EGuizmoHandle2D handle, const Vector2& delta) const
{
	if (handle == EGuizmoHandle2D::MoveX)
	{
		return Vector2(delta.x, 0.0f);
	}
	if (handle == EGuizmoHandle2D::MoveY)
	{
		return Vector2(0.0f, delta.y);
	}
	return delta;
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
