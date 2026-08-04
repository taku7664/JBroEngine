#include "pch.h"
#include "EditorCanvasCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"
#include "Engine/GameFramework/Canvas/Canvas.h"

#include <utility>

namespace
{
	CanvasViewport* ResolveViewport(const SafePtr<CGameCanvas>& canvas, std::size_t index)
	{
		return canvas.IsValid() ? canvas->GetViewportAt(index) : nullptr;
	}

}

// ── ViewportSnapshot ─────────────────────────────────────────────────────────

ViewportSnapshot ViewportSnapshot::Capture(const CanvasViewport& viewport)
{
	ViewportSnapshot snapshot;
	snapshot.Name = viewport.Name;
	snapshot.CameraObjectGuid = viewport.CameraObjectGuid;
	snapshot.Position = viewport.Position;
	snapshot.Size = viewport.Size;
	snapshot.LayerFilter = viewport.LayerFilter;
	snapshot.Active = viewport.Active;
	return snapshot;
}

void ViewportSnapshot::ApplyTo(CanvasViewport& viewport) const
{
	viewport.Name = Name;
	viewport.CameraObjectGuid = CameraObjectGuid;
	viewport.Position = Position;
	viewport.Size = Size;
	viewport.LayerFilter = LayerFilter;
	viewport.Active = Active;
	// 캐시 무효화 — 다음 렌더가 새 guid 로 다시 해석한다.
	viewport.ResolvedCamera = SafePtr<CGameObject>();
}

// ── CSetCanvasBackgroundColorCommand ─────────────────────────────────────────

CSetCanvasBackgroundColorCommand::CSetCanvasBackgroundColorCommand(
	SafePtr<CGameCanvas> canvas, const Color& oldColor, const Color& newColor)
	: m_canvas(canvas)
{
	m_oldColor = oldColor;
	m_newColor = newColor;
}

const char* CSetCanvasBackgroundColorCommand::GetName() const
{
	return "Set Canvas Background Color";
}

bool CSetCanvasBackgroundColorCommand::Execute()
{
	return Apply(m_newColor);
}

void CSetCanvasBackgroundColorCommand::Undo()
{
	Apply(m_oldColor);
}

void CSetCanvasBackgroundColorCommand::Redo()
{
	Apply(m_newColor);
}

bool CSetCanvasBackgroundColorCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetCanvasBackgroundColorCommand* other = dynamic_cast<const CSetCanvasBackgroundColorCommand*>(&newer);
	if (nullptr == other)
	{
		return false;
	}
	// old 는 드래그 시작값 유지, new 만 최신값으로 교체.
	m_newColor = other->m_newColor;
	return true;
}

bool CSetCanvasBackgroundColorCommand::Apply(const Color& color)
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}
	m_canvas->SetBackgroundColor(color.R, color.G, color.B, color.A);
	return true;
}

// ── CSetViewportPropertyCommand ──────────────────────────────────────────────

CSetViewportPropertyCommand::CSetViewportPropertyCommand(
	SafePtr<CGameCanvas> canvas,
	std::size_t index,
	EField field,
	ViewportSnapshot oldProperties,
	ViewportSnapshot newProperties)
	: m_canvas(canvas)
	, m_index(index)
	, m_field(field)
	, m_oldProperties(std::move(oldProperties))
	, m_newProperties(std::move(newProperties))
{
}

const char* CSetViewportPropertyCommand::GetName() const
{
	return "Set Viewport Property";
}

bool CSetViewportPropertyCommand::Execute()
{
	return Apply(m_newProperties);
}

void CSetViewportPropertyCommand::Undo()
{
	Apply(m_oldProperties);
}

void CSetViewportPropertyCommand::Redo()
{
	Apply(m_newProperties);
}

bool CSetViewportPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetViewportPropertyCommand* other = dynamic_cast<const CSetViewportPropertyCommand*>(&newer);
	if (nullptr == other)
	{
		return false;
	}
	if (m_index != other->m_index || m_field != other->m_field)
	{
		return false;
	}
	m_newProperties = other->m_newProperties;
	return true;
}

bool CSetViewportPropertyCommand::Apply(const ViewportSnapshot& properties)
{
	CanvasViewport* viewport = ResolveViewport(m_canvas, m_index);
	if (nullptr == viewport)
	{
		return false;
	}
	properties.ApplyTo(*viewport);
	return true;
}

bool EditorCanvasActions::SetViewportProperty(
	CGameCanvas& canvas,
	std::size_t index,
	CSetViewportPropertyCommand::EField field,
	const ViewportSnapshot& newProperties)
{
	const CanvasViewport* viewport = canvas.GetViewportAt(index);
	if (nullptr == viewport)
	{
		return false;
	}

	auto command = MakeOwnerPtr<CSetViewportPropertyCommand>(
		canvas.SafeFromThis(), index, field, ViewportSnapshot::Capture(*viewport), newProperties);
	return Editor::CommandManager.ExecuteCommand(std::move(command));
}

bool EditorCanvasActions::SetBackgroundColor(CGameCanvas& canvas, const Color& newColor)
{
	const Color oldColor = canvas.GetBackgroundColor();

	auto command = MakeOwnerPtr<CSetCanvasBackgroundColorCommand>(canvas.SafeFromThis(), oldColor, newColor);
	return Editor::CommandManager.ExecuteCommand(std::move(command));
}

// ── CCreateViewportCommand ───────────────────────────────────────────────────

CCreateViewportCommand::CCreateViewportCommand(SafePtr<CGameCanvas> canvas)
	: m_canvas(canvas)
{
}

const char* CCreateViewportCommand::GetName() const
{
	return "Create Viewport";
}

bool CCreateViewportCommand::Execute()
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}

	CanvasViewport* viewport = m_canvas->CreateViewport(m_name.empty() ? nullptr : m_name.c_str());
	if (nullptr == viewport)
	{
		return false;
	}

	m_name = viewport->Name;   // 자동 이름("Viewport N")도 redo 시 동일하게.
	m_index = m_canvas->GetViewportCount() - 1;
	m_created = true;
	return true;
}

void CCreateViewportCommand::Undo()
{
	if (false == m_created || false == m_canvas.IsValid())
	{
		return;
	}
	m_canvas->DestroyViewport(m_index);
	m_created = false;
}

void CCreateViewportCommand::Redo()
{
	if (false == m_created)
	{
		Execute();
	}
}

// ── CDeleteViewportCommand ───────────────────────────────────────────────────

CDeleteViewportCommand::CDeleteViewportCommand(SafePtr<CGameCanvas> canvas, std::size_t index)
	: m_canvas(canvas)
	, m_index(index)
{
	if (const CanvasViewport* viewport = ResolveViewport(m_canvas, m_index))
	{
		m_properties = ViewportSnapshot::Capture(*viewport);
	}
}

const char* CDeleteViewportCommand::GetName() const
{
	return "Delete Viewport";
}

bool CDeleteViewportCommand::Execute()
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}
	m_deleted = m_canvas->DestroyViewport(m_index);
	return m_deleted;
}

void CDeleteViewportCommand::Undo()
{
	if (false == m_deleted || false == m_canvas.IsValid())
	{
		return;
	}

	// 원래 자리에 되살린다 — 뷰포트 순서가 곧 그리는 순서다.
	CanvasViewport* viewport = m_canvas->InsertViewport(m_index, m_properties.Name.c_str());
	if (nullptr == viewport)
	{
		return;
	}
	m_properties.ApplyTo(*viewport);
	m_deleted = false;
}

void CDeleteViewportCommand::Redo()
{
	if (false == m_deleted)
	{
		Execute();
	}
}

#endif
