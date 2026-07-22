#include "pch.h"
#include "EditorStatisticsTool.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Renderer/IRenderScene.h"
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

void CEditorStatisticsTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowEditorStatistics);
}

void CEditorStatisticsTool::OnRenderStay()
{
	const ImGuiIO& io = ImGui::GetIO();
	const float frameMilliseconds = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
	SafePtr<CGameCanvas> canvas = EditorContext::GetActiveCanvas();
	const std::size_t objectCount = canvas.IsValid() ? canvas->GetObjectCount() : 0;
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
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsCanvasObjects), value);
		const std::vector<CGameCanvas::ScriptMemoryPoolStats> scriptPoolStats = canvas.IsValid()
			? CCanvasRuntimeAccess::GetScriptMemoryPoolStats(*canvas)
			: std::vector<CGameCanvas::ScriptMemoryPoolStats>();
		std::size_t expansionCount = 0;
		for (const CGameCanvas::ScriptMemoryPoolStats& stats : scriptPoolStats)
		{
			expansionCount += stats.ExpansionCount;
		}
		std::snprintf(value, sizeof(value), "%zu", scriptPoolStats.size());
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsScriptPools), value);
		std::snprintf(value, sizeof(value), "%zu", expansionCount);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsScriptPoolExpansions), value);
		std::snprintf(value, sizeof(value), "%u", renderItemCount);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRenderItems), value);
		std::snprintf(value, sizeof(value), "%zu", Editor::GetSelectedEntities().size());
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsSelection), value);
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsUndo), Loc::Text(Editor::CommandManager.CanUndo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRedo), Loc::Text(Editor::CommandManager.CanRedo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
		drawRow(Loc::Text(EditorLocKeys::EditorStatisticsDirty), Loc::Text(Editor::CommandManager.IsDirty() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));

		// 프레임 구간별 소요 시간. 가변(Update)과 고정 스텝(FixedUpdate)을 나눠 보여 준다.
		// 편집 모드에서는 ShouldUpdateInEditMode 인 시스템만 돌고, 고정 스텝은 아예 안 돈다.
		if (canvas.IsValid())
		{
			const std::vector<FrameSectionTiming>& sections =
				CCanvasRuntimeAccess::GetFrameSections(*canvas);

			auto drawSections = [&](const char* headerKey, bool fixedStep)
			{
				bool headerDrawn = false;
				for (const FrameSectionTiming& section : sections)
				{
					if (nullptr == section.Label || section.IsFixedStep != fixedStep)
					{
						continue;
					}
					if (false == headerDrawn)
					{
						drawRow(Loc::Text(headerKey), "");
						headerDrawn = true;
					}
					if (fixedStep)
					{
						// 고정 스텝은 프레임당 횟수가 달라진다 — 합계와 횟수를 함께 봐야 읽힌다.
						std::snprintf(value, sizeof(value), "%.3f ms  x%u",
							section.AverageMicroseconds / 1000.0, section.CallsPerFrame);
					}
					else
					{
						std::snprintf(value, sizeof(value), "%.3f ms",
							section.AverageMicroseconds / 1000.0);
					}
					// typeid 이름은 "class CShapeRenderSystem" 형태 — 접두사를 건너뛴다.
					const char* label = std::strncmp(section.Label, "class ", 6) == 0
						? section.Label + 6
						: section.Label;
					drawRow(label, value);
				}
			};

			drawSections(EditorLocKeys::EditorStatisticsSystemTimes, false);
			drawSections(EditorLocKeys::EditorStatisticsFixedStepTimes, true);
		}

		ImGui::EndTable();
	}
}

#endif
