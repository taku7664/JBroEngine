#include "pch.h"
#include "EditorStatisticsTool.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Renderer/IRenderScene.h"
#include "Engine/GameFramework/Scene/Scene.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

void CEditorStatisticsTool::OnCreate()
{
	SetLocalizedTitleKey("window.editor_statistics");
}

void CEditorStatisticsTool::OnRenderStay()
{
	const ImGuiIO& io = ImGui::GetIO();
	const float frameMilliseconds = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
	SafePtr<CGameScene> scene = EditorContext::GetActiveScene();
	const std::size_t objectCount = scene.IsValid() ? scene->GetObjectCount() : 0;
	const std::uint32_t renderItemCount = Engine.RenderScene.IsValid()
		? Engine.RenderScene->GetRenderItemCount()
		: 0;

	if (ImGui::BeginTable("EditorStatistics", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
	{
		auto drawRow = [](const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		};

		char value[64] = {};
		std::snprintf(value, sizeof(value), "%.1f", io.Framerate);
		drawRow(Loc::Text("editor_statistics.fps"), value);
		std::snprintf(value, sizeof(value), "%.2f ms", frameMilliseconds);
		drawRow(Loc::Text("editor_statistics.frame_time"), value);
		std::snprintf(value, sizeof(value), "%zu", objectCount);
		drawRow(Loc::Text("editor_statistics.scene_objects"), value);
		std::snprintf(value, sizeof(value), "%u", renderItemCount);
		drawRow(Loc::Text("editor_statistics.render_items"), value);
		std::snprintf(value, sizeof(value), "%zu", Editor::GetSelectedEntities().size());
		drawRow(Loc::Text("editor_statistics.selection"), value);
		drawRow(Loc::Text("editor_statistics.undo"), Loc::Text(Editor::CommandManager.CanUndo() ? "common.yes" : "common.no"));
		drawRow(Loc::Text("editor_statistics.redo"), Loc::Text(Editor::CommandManager.CanRedo() ? "common.yes" : "common.no"));
		drawRow(Loc::Text("editor_statistics.dirty"), Loc::Text(Editor::CommandManager.IsDirty() ? "common.yes" : "common.no"));
		ImGui::EndTable();
	}
}

#endif
