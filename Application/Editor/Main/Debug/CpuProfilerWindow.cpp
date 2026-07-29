#include "pch.h"
#include "CpuProfilerWindow.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"                                 // Editor::SelectEntity/Reveal — 스크립트 클릭 → 인스펙터
#include "Editor/EditorContext.h"
#include "Editor/ImItem/ImSplitter.h"                      // ImGui::Utillity::VerticalSplitter — 좌/우 드래그 분할
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Debug/CpuProfiler.h"                 // 스크립트 인스턴스별 Update 시간
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Core/Renderer/IRenderScene.h"             // 렌더 아이템 수(일반 통계)
#include "Engine/Utillity/Types/FrameSectionProfiler.h"    // FrameSectionTiming — 풀별 순회시간
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"   // GetFrameSections / GetScriptMemoryPoolStats / GetCoroutineScheduler
#include "Engine/GameFramework/Object/GameObject.h"        // 스크립트 오브젝트 이름 역해석
#include "Engine/GameFramework/Scripting/CoroutineScheduler.h"   // 코루틴 활성 수/소유자별 분해
#include "Engine/GameFramework/Scripting/GameScript.h"     // 코루틴 owner → 오브젝트 이름/선택

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
	// 프레임 구간 라벨의 "class " 접두어를 벗긴다(generic 시스템은 typeid 이름). EditorStatistics 와 동일.
	const char* StripClassPrefix(const char* label)
	{
		return (label && std::strncmp(label, "class ", 6) == 0) ? label + 6 : label;
	}

	// 오브젝트별 드릴다운 대상인 스크립트 풀의 프레임 구간 라벨. Canvas.cpp 가 이 리터럴로 잰다.
	constexpr const char* SCRIPT_POOL_LABEL = "CScriptSystem";

	bool IsScriptPool(const char* label)
	{
		return label && std::strcmp(StripClassPrefix(label), SCRIPT_POOL_LABEL) == 0;
	}

	// 소유자별 코루틴 드릴다운 대상인 코루틴 스케줄러 풀의 라벨. Canvas.cpp 가 이 리터럴로 잰다.
	constexpr const char* COROUTINE_POOL_LABEL = "CCoroutineScheduler";

	bool IsCoroutinePool(const char* label)
	{
		return label && std::strcmp(StripClassPrefix(label), COROUTINE_POOL_LABEL) == 0;
	}
}

void CCpuProfilerWindow::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowCpuProfiler);
}

void CCpuProfilerWindow::OnRenderStay()
{
	// CPU 프로파일이 꺼져 있으면 내용 없이 안내만 가운데 띄운다(디버그 메뉴에서 켜라).
	if (false == (Engine.CpuProfiler.IsValid() && Engine.CpuProfiler->IsEnabled()))
	{
		const char* hint = Loc::Text(EditorLocKeys::CpuProfilerDisabledHint);
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 textSize = ImGui::CalcTextSize(hint);
		ImGui::SetCursorPos(ImVec2(
			ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f,
			ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
		ImGui::TextDisabled("%s", hint);
		return;
	}

	SafePtr<CGameCanvas> canvas = EditorContext::GetActiveCanvas();
	CGameCanvas* canvasPtr = canvas.TryGet();

	// 상단 일반통계는 높이를 제한한 스크롤 차일드에 가둔다 — 안 그러면 통계 + 엔진 프레임 구간이
	// 창을 다 먹어 아래 풀 목록/상세가 잘린다. 남은 높이는 풀 목록/상세 split 이 전부 채운다.
	const float statsHeight = ImGui::GetContentRegionAvail().y * 0.4f;
	if (ImGui::BeginChild("##cpu_profiler_stats", ImVec2(0.0f, statsHeight), true))
	{
		DrawGeneralStats(canvasPtr);
	}
	ImGui::EndChild();

	// 좌 풀 목록 / 우 상세 — 사이의 드래그 스플리터로 비율 조절(m_splitRatio). BuildSettings 와 동일 패턴:
	// 스플리터가 내부에서 SameLine 을 처리하므로 좌 자식 → 스플리터 → 우 자식 순으로만 부르면 된다.
	constexpr float SPLITTER_W = 3.0f;
	constexpr float MIN_RATIO = 0.2f;
	constexpr float MAX_RATIO = 0.8f;
	const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
	const float leftWidth = bodyAvail.x * m_splitRatio - SPLITTER_W * 0.5f;
	const float rightWidth = bodyAvail.x - leftWidth - SPLITTER_W;

	ImGui::BeginChild("##cpu_profiler_pools", ImVec2(leftWidth, bodyAvail.y), true);
	DrawPoolList(canvasPtr);
	ImGui::EndChild();

	ImGui::Utillity::VerticalSplitter("##CpuProfilerSplitter", m_splitRatio, bodyAvail, MIN_RATIO, MAX_RATIO, SPLITTER_W);

	ImGui::BeginChild("##cpu_profiler_detail", ImVec2(rightWidth, bodyAvail.y), true);
	DrawPoolDetail(canvasPtr);
	ImGui::EndChild();
}

void CCpuProfilerWindow::DrawGeneralStats(CGameCanvas* canvas)
{
	const ImGuiIO& io = ImGui::GetIO();
	const float frameMilliseconds = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
	const std::size_t objectCount = (nullptr != canvas) ? canvas->GetObjectCount() : 0;
	const std::uint32_t renderItemCount = Engine.RenderScene.IsValid()
		? Engine.RenderScene->GetRenderItemCount()
		: 0;

	if (false == ImGui::BeginTable("CpuProfilerGeneral", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
	{
		return;
	}

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

	const std::vector<CGameCanvas::ScriptMemoryPoolStats> scriptPoolStats = (nullptr != canvas)
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
	if (nullptr != canvas)
	{
		const std::size_t activeCoroutines = CCanvasRuntimeAccess::GetCoroutineScheduler(*canvas).GetActiveCount();
		std::snprintf(value, sizeof(value), "%zu", activeCoroutines);
		drawRow(Loc::Text(EditorLocKeys::CpuProfilerCoroutinesActive), value);
	}
	std::snprintf(value, sizeof(value), "%u", renderItemCount);
	drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRenderItems), value);
	std::snprintf(value, sizeof(value), "%zu", Editor::GetSelectedEntities().size());
	drawRow(Loc::Text(EditorLocKeys::EditorStatisticsSelection), value);
	drawRow(Loc::Text(EditorLocKeys::EditorStatisticsUndo),
		Loc::Text(Editor::CommandManager.CanUndo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
	drawRow(Loc::Text(EditorLocKeys::EditorStatisticsRedo),
		Loc::Text(Editor::CommandManager.CanRedo() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));
	drawRow(Loc::Text(EditorLocKeys::EditorStatisticsDirty),
		Loc::Text(Editor::CommandManager.IsDirty() ? EditorLocKeys::CommonYes : EditorLocKeys::CommonNo));

	// 엔진 프레임 구간 — 겹치지 않는 프레임 분해라 합이 프레임 시간에 가깝다(풀 순회는 좌측 목록).
	if (Engine.FrameProfiler.IsValid())
	{
		bool headerDrawn = false;
		for (const FrameSectionTiming& section : Engine.FrameProfiler->GetSections())
		{
			if (nullptr == section.Label || section.IsFixedStep)
			{
				continue;
			}
			if (false == headerDrawn)
			{
				drawRow(Loc::Text(EditorLocKeys::EditorStatisticsEngineTimes), "");
				headerDrawn = true;
			}
			std::snprintf(value, sizeof(value), "%.3f ms", section.AverageMicroseconds / 1000.0);
			drawRow(StripClassPrefix(section.Label), value);
		}
	}

	ImGui::EndTable();
}

void CCpuProfilerWindow::DrawPoolList(CGameCanvas* canvas)
{
	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::CpuProfilerPoolsHeader));
	ImGui::Separator();

	if (nullptr == canvas)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::GpuProfilerNoCanvas));
		return;
	}

	const std::vector<FrameSectionTiming>& sections = CCanvasRuntimeAccess::GetFrameSections(*canvas);

	// 컴포넌트 풀(= 캔버스 시스템) 순회시간. Update(비고정)·FixedUpdate(고정스텝)를 각 그룹으로.
	auto drawSectionGroup = [&](const char* headerKey, bool fixedStep)
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
				ImGui::TextDisabled("%s", Loc::Text(headerKey));
				headerDrawn = true;
			}

			const char* label = StripClassPrefix(section.Label);
			// 위젯 ID 는 라벨 포인터 + 고정여부로 고유화(같은 풀이 Update/FixedUpdate 양쪽에 나올 수 있다).
			char row[176] = {};
			std::snprintf(row, sizeof(row), "%s##pool_%p_%d",
				label, static_cast<const void*>(section.Label), section.IsFixedStep ? 1 : 0);
			// 선택 키 = 라벨 포인터(원본). 같은 풀의 Update/FixedUpdate 는 같은 리터럴이라 함께 선택된다.
			const bool selected = (section.Label == m_selectedPoolLabel);
			if (ImGui::Selectable(row, selected))
			{
				m_selectedPoolLabel = section.Label;
			}

			ImGui::SameLine();
			if (fixedStep)
			{
				char value[64] = {};
				std::snprintf(value, sizeof(value), "%.3f ms  x%.2f",
					section.AverageMicroseconds / 1000.0, section.AverageCallsPerFrame);
				ImGui::TextDisabled("%s", value);
			}
			else
			{
				ImGui::TextDisabled("%.3f ms", section.AverageMicroseconds / 1000.0);
			}
		}
	};

	drawSectionGroup(EditorLocKeys::EditorStatisticsSystemTimes, false);
	drawSectionGroup(EditorLocKeys::EditorStatisticsFixedStepTimes, true);
}

void CCpuProfilerWindow::DrawPoolDetail(CGameCanvas* canvas)
{
	if (nullptr == m_selectedPoolLabel)
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::CpuProfilerSelectPoolHint));
		return;
	}

	ImGui::TextUnformatted(StripClassPrefix(m_selectedPoolLabel));
	ImGui::Separator();

	if (IsScriptPool(m_selectedPoolLabel))
	{
		// Script 풀 선택 → 이 프레임 인스턴스별 캡처 요청(예외3, opt-in). 다음 프레임부터 값이 찬다.
		if (Engine.CpuProfiler.IsValid())
		{
			Engine.CpuProfiler->SetCaptureScripts(true);
		}

		const std::size_t count = Engine.CpuProfiler.IsValid() ? Engine.CpuProfiler->GetScriptTimingCount() : 0;
		if (0 == count)
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::CpuProfilerNoScripts));
			return;
		}

		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::CpuProfilerScriptObjectsHeader));
		ImGui::Separator();

		const std::vector<CpuScriptTiming>& timings = Engine.CpuProfiler->GetScriptTimings();
		// 비용 큰 순으로 — 인덱스만 정렬(UI 경로라 임시 벡터 허용).
		std::vector<std::size_t> order(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			order[i] = i;
		}
		std::sort(order.begin(), order.end(), [&timings](std::size_t a, std::size_t b)
		{
			return timings[a].Microseconds > timings[b].Microseconds;
		});

		const CGameObject* selectedObject = Editor::GetSelectedEntity();
		for (std::size_t rank = 0; rank < order.size(); ++rank)
		{
			const CpuScriptTiming& timing = timings[order[rank]];
			// 수명 계약: 포인터는 이번 프레임 스크립트 순회에서 담겼고 창은 같은 프레임에 그린다.
			CGameObject* object = reinterpret_cast<CGameObject*>(const_cast<void*>(timing.Object));
			const char* name = object ? object->GetName() : "?";

			char row[192] = {};
			std::snprintf(row, sizeof(row), "%s##cpuscript%zu", name, rank);
			const bool selected = (nullptr != object && object == selectedObject);
			if (ImGui::Selectable(row, selected) && nullptr != object)
			{
				Editor::SelectEntity(object);
				Editor::RevealEntityInHierarchy(object);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%.3f ms", timing.Microseconds / 1000.0);
		}
		return;
	}

	if (IsCoroutinePool(m_selectedPoolLabel))
	{
		if (nullptr == canvas)
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::GpuProfilerNoCanvas));
			return;
		}

		// 구간 시간(Update tick + FixedUpdate notify) — 다른 풀과 동일 표기.
		const std::vector<FrameSectionTiming>& coroutineSections = CCanvasRuntimeAccess::GetFrameSections(*canvas);
		for (const FrameSectionTiming& section : coroutineSections)
		{
			if (section.Label != m_selectedPoolLabel)
			{
				continue;
			}
			if (section.IsFixedStep)
			{
				ImGui::Text("%s: %.3f ms  x%.2f", Loc::Text(EditorLocKeys::CpuProfilerFixedUpdate),
					section.AverageMicroseconds / 1000.0, section.AverageCallsPerFrame);
			}
			else
			{
				ImGui::Text("%s: %.3f ms", Loc::Text(EditorLocKeys::CpuProfilerUpdate),
					section.AverageMicroseconds / 1000.0);
			}
		}

		const CCoroutineScheduler& scheduler = CCanvasRuntimeAccess::GetCoroutineScheduler(*canvas);
		ImGui::Text("%s: %zu", Loc::Text(EditorLocKeys::CpuProfilerCoroutinesActive), scheduler.GetActiveCount());

		// 소유자(스크립트)별 코루틴 수 집계 — 목록이 작아 선형 누적으로 충분하다(UI 경로).
		std::vector<std::pair<const CGameScript*, std::size_t>> owners;
		scheduler.ForEachActiveOwner([&owners](const CGameScript* owner)
		{
			for (std::pair<const CGameScript*, std::size_t>& entry : owners)
			{
				if (entry.first == owner)
				{
					++entry.second;
					return;
				}
			}
			owners.push_back({ owner, static_cast<std::size_t>(1) });
		});

		ImGui::Spacing();
		if (owners.empty())
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::CpuProfilerNoCoroutines));
			return;
		}

		// 개수 많은 순.
		std::sort(owners.begin(), owners.end(),
			[](const std::pair<const CGameScript*, std::size_t>& a, const std::pair<const CGameScript*, std::size_t>& b)
			{
				return a.second > b.second;
			});

		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::CpuProfilerCoroutineOwnersHeader));
		ImGui::Separator();

		const CGameObject* selectedObject = Editor::GetSelectedEntity();
		for (std::size_t i = 0; i < owners.size(); ++i)
		{
			// owner 파괴 대기 중이면 nullptr 일 수 있다(다음 프레임 정리) — 이름/선택은 오브젝트가 있을 때만.
			CGameScript* owner = const_cast<CGameScript*>(owners[i].first);
			CGameObject* object = owner ? owner->GetOwner().TryGet() : nullptr;
			const char* name = object ? object->GetName() : (owner ? owner->GetTypeName() : "?");

			char row[192] = {};
			std::snprintf(row, sizeof(row), "%s##coroutineowner%zu", name, i);
			const bool selected = (nullptr != object && object == selectedObject);
			if (ImGui::Selectable(row, selected) && nullptr != object)
			{
				Editor::SelectEntity(object);
				Editor::RevealEntityInHierarchy(object);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("x%zu", owners[i].second);
		}
		return;
	}

	// 그 외 풀 → 총 순회시간(Update/FixedUpdate). 배치 처리라 오브젝트별 분해는 없다.
	if (nullptr == canvas)
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::GpuProfilerNoCanvas));
		return;
	}

	const std::vector<FrameSectionTiming>& sections = CCanvasRuntimeAccess::GetFrameSections(*canvas);
	for (const FrameSectionTiming& section : sections)
	{
		if (section.Label != m_selectedPoolLabel)
		{
			continue;
		}
		if (section.IsFixedStep)
		{
			ImGui::Text("%s: %.3f ms  x%.2f", Loc::Text(EditorLocKeys::CpuProfilerFixedUpdate),
				section.AverageMicroseconds / 1000.0, section.AverageCallsPerFrame);
		}
		else
		{
			ImGui::Text("%s: %.3f ms", Loc::Text(EditorLocKeys::CpuProfilerUpdate),
				section.AverageMicroseconds / 1000.0);
		}
	}

	ImGui::Spacing();
	ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::CpuProfilerBatchNote));
}

#endif
