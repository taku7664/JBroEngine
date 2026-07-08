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
	SetLocalizedTitleKey(EditorLocKeys::WindowEditorStatistics);
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
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsFps), value);
		std::snprintf(value, sizeof(value), "%.2f ms", frameMilliseconds);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsFrameTime), value);
		std::snprintf(value, sizeof(value), "%zu", objectCount);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsSceneObjects), value);
		std::snprintf(value, sizeof(value), "%u", renderItemCount);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRenderItems), value);
		std::snprintf(value, sizeof(value), "%zu", Editor::GetSelectedEntities().size());
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsSelection), value);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsUndo), Loc::Text(Editor::CommandManager.CanUndo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRedo), Loc::Text(Editor::CommandManager.CanRedo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsDirty), Loc::Text(Editor::CommandManager.IsDirty() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
		ImGui::EndTable();
	}
}

#endif
