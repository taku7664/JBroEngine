#include "pch.h"
#include "GpuProfilerWindow.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"                                // Editor::SelectEntity — 드로우순서 클릭 → 인스펙터
#include "Editor/EditorContext.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Debug/GpuProfiler.h"
#include "Engine/Core/Localization/LocalizationManager.h"
#include "Engine/Editor/ImEditor.h"                       // 렌더타겟 진행 프리뷰 요청/표시
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/GameLayer.h"
#include "Engine/GameFramework/Object/GameObject.h"       // 드로우순서 키(=CGameObject*) 이름 역해석
#include "Engine/GameFramework/Rendering/GameCamera.h"   // GpuLayerKey/GpuRenderCutoff — 키·컷오프

#include <algorithm>

namespace
{
	// 프리뷰 RT 렌더 높이(px). 폭은 프로젝트 해상도 종횡비로 ImEditor 가 계산. 진단 프리뷰라
	// 게임 해상도 원본이 아니라 이 정도로 충분하고, 두 번째 렌더 비용도 억제된다.
	constexpr std::uint32_t PREVIEW_RENDER_HEIGHT = 360;
}

namespace
{
	// 안정 포인터(CGameLayer*)로 현재 캔버스에서 레이어를 되찾는다. 프레임마다 레이어 배열이
	// 재정렬돼도 포인터는 유지되므로, 인덱스가 아니라 포인터로 선택을 매칭한다.
	const CGameLayer* FindLayerByKey(CGameCanvas& canvas, const void* key)
	{
		if (nullptr == key)
		{
			return nullptr;
		}
		const std::size_t count = canvas.GetLayerCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			const CGameLayer* layer = canvas.GetLayerAt(i);
			if (static_cast<const void*>(layer) == key)
			{
				return layer;
			}
		}
		return nullptr;
	}
}

void CGpuProfilerWindow::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowGpuProfiler);
}

void CGpuProfilerWindow::OnRenderStay()
{
	// GPU 프로파일이 꺼져 있으면 내용 없이 안내만 가운데 띄운다(디버그 메뉴에서 "사용" 을 켜라).
	if (false == (Engine.GpuProfiler.IsValid() && Engine.GpuProfiler->IsEnabled()))
	{
		// 꺼짐 = 비용 0. 프리뷰 RT 도 반납한다(두 번째 렌더도 멈춘다).
		if (Editor::ImEditor.IsValid())
		{
			Editor::ImEditor->RequestGpuProfilerPreview(0, GpuRenderCutoff{});
		}
		const char* hint = Loc::Text(EditorLocKeys::GpuProfilerDisabledHint);
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 textSize = ImGui::CalcTextSize(hint);
		ImGui::SetCursorPos(ImVec2(
			ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f,
			ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
		ImGui::TextDisabled("%s", hint);
		return;
	}

	if (false == Engine.GpuProfiler->IsBackendSupported())
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::GpuProfilerBackendUnsupported));
	}
	else
	{
		// 게임뷰 렌더 전체 GPU 시간(레이어 + 라이팅/컴포짓, key=nullptr). 게임뷰가 안 그려지면 0.
		// 레이어 합보다 큰 게 정상 — 라이팅/컴포짓 등 특정 레이어에 안 붙는 작업이 여기 포함된다.
		ImGui::Text(Loc::Text(EditorLocKeys::GpuProfilerGameViewGpu),
			Engine.GpuProfiler->GetMillisecondsForKey(nullptr));
	}

	SafePtr<CGameCanvas> canvas = EditorContext::GetActiveCanvas();
	if (false == canvas.IsValid())
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::GpuProfilerNoCanvas));
		// 선택 없음 → 프리뷰 요청 해제(RT 반납).
		if (Editor::ImEditor.IsValid())
		{
			Editor::ImEditor->RequestGpuProfilerPreview(0, GpuRenderCutoff{});
		}
		return;
	}

	// 상단: 렌더타겟 진행 프리뷰(선택 지점까지 그린 부분 씬 최종). 프리뷰 요청도 여기서 낸다.
	DrawPreview(*canvas);

	// 좌 레이어 목록 / 우 상세, 각각 독립 스크롤. 좌측 폭은 가용 폭의 40%.
	const float leftWidth = ImGui::GetContentRegionAvail().x * 0.4f;
	if (ImGui::BeginChild("##gpu_profiler_layers", ImVec2(leftWidth, 0.0f), true))
	{
		DrawLayerList(*canvas);
	}
	ImGui::EndChild();

	ImGui::SameLine();

	if (ImGui::BeginChild("##gpu_profiler_detail", ImVec2(0.0f, 0.0f), true))
	{
		DrawLayerDetail(*canvas);
	}
	ImGui::EndChild();
}

void CGpuProfilerWindow::DrawPreview(CGameCanvas& canvas)
{
	// 선택 상태 → 컷오프 계산. 레이어 미선택이면 프리뷰 없음(요청 0 → RT 반납).
	GpuRenderCutoff cutoff;
	if (const CGameLayer* layer = FindLayerByKey(canvas, m_selectedLayerKey))
	{
		cutoff.Active = true;
		cutoff.LayerIndex = layer->GetIndex();
		cutoff.ObjectDrawIndex = m_previewObjectIndex;   // UINT32_MAX = 레이어 전체까지
	}

	// 매 프레임 opt-in 요청 — 다음 OnPrepareRender 가 이 컷오프로 프리뷰 RT 를 그린다. 창이
	// 배경 탭이면 OnRenderStay 가 안 돌아 요청이 끊기고 RT 가 반납된다(낭비 방지).
	if (Editor::ImEditor.IsValid())
	{
		Editor::ImEditor->RequestGpuProfilerPreview(cutoff.Active ? PREVIEW_RENDER_HEIGHT : 0u, cutoff);
	}

	// 표시 스트립(고정 높이). 게임 화면 비율을 유지하도록 레터박스로 가운데 맞춘다.
	const float stripHeight = ImGui::GetTextLineHeightWithSpacing() * 10.0f;
	if (ImGui::BeginChild("##gpu_preview", ImVec2(0.0f, stripHeight), true))
	{
		void* tex = Editor::ImEditor.IsValid() ? Editor::ImEditor->GetGpuProfilerPreviewTextureID() : nullptr;
		const std::uint32_t pw = Editor::ImEditor.IsValid() ? Editor::ImEditor->GetGpuProfilerPreviewWidth()  : 0u;
		const std::uint32_t ph = Editor::ImEditor.IsValid() ? Editor::ImEditor->GetGpuProfilerPreviewHeight() : 0u;

		if (false == cutoff.Active || nullptr == tex || 0u == pw || 0u == ph)
		{
			// 아직 선택이 없거나 프리뷰가 준비되기 전 — 안내만 가운데.
			const char* hint = Loc::Text(EditorLocKeys::GpuProfilerPreviewHint);
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const ImVec2 textSize = ImGui::CalcTextSize(hint);
			ImGui::SetCursorPos(ImVec2(
				ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f,
				ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
			ImGui::TextDisabled("%s", hint);
		}
		else
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float texAspect = static_cast<float>(pw) / static_cast<float>(ph);
			const float availAspect = avail.x / std::max(1.0f, avail.y);
			float drawW = avail.x;
			float drawH = avail.y;
			if (texAspect > availAspect)
			{
				drawH = avail.x / std::max(0.0001f, texAspect);
			}
			else
			{
				drawW = avail.y * texAspect;
			}
			ImGui::SetCursorPos(ImVec2(
				ImGui::GetCursorPosX() + (avail.x - drawW) * 0.5f,
				ImGui::GetCursorPosY() + (avail.y - drawH) * 0.5f));
			ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(drawW, drawH));
		}
	}
	ImGui::EndChild();
}

void CGpuProfilerWindow::DrawLayerList(CGameCanvas& canvas)
{
	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::GpuProfilerLayersHeader));
	ImGui::Separator();

	// 계측이 켜지고 백엔드가 지원할 때만 실측값을 보여준다(꺼져 있으면 0 이 오해를 부른다).
	const bool showTimes = Engine.GpuProfiler.IsValid()
		&& Engine.GpuProfiler->IsEnabled()
		&& Engine.GpuProfiler->IsBackendSupported();

	const std::size_t count = canvas.GetLayerCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		const CGameLayer* layer = canvas.GetLayerAt(i);
		if (nullptr == layer)
		{
			continue;
		}
		const void* key = static_cast<const void*>(layer);
		const bool selected = (key == m_selectedLayerKey);

		// 레이어명은 사용자 문자열이라 그대로 ID 로 쓰지 못한다 — ## 뒤 인덱스로 위젯 ID 를 고정.
		char label[128] = {};
		std::snprintf(label, sizeof(label), "%s##gpu_layer_%zu", layer->GetName(), i);
		if (ImGui::Selectable(label, selected))
		{
			// 레이어를 바꾸면 오브젝트 컷오프는 해제 — 이 레이어 "전체까지" 프리뷰로 돌아간다.
			if (key != m_selectedLayerKey)
			{
				m_previewObjectIndex = 0xFFFFFFFFu;
			}
			m_selectedLayerKey = key;
		}

		ImGui::SameLine();
		if (showTimes)
		{
			// 레이어 인덱스로 GPU 결과를 역매핑(GpuLayerKey). 이번 프레임 안 그려졌으면 0.
			const double ms = Engine.GpuProfiler->GetMillisecondsForKey(GpuLayerKey(layer->GetIndex()));
			ImGui::TextDisabled("%.3f ms", ms);
		}
		else
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::GpuProfilerTimePending));
		}
	}
}

void CGpuProfilerWindow::DrawLayerDetail(CGameCanvas& canvas)
{
	const CGameLayer* layer = FindLayerByKey(canvas, m_selectedLayerKey);
	if (nullptr == layer)
	{
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::GpuProfilerSelectLayerHint));
		return;
	}

	ImGui::Text("%s", layer->GetName());
	ImGui::Separator();

	// 드로우순서는 게임뷰 렌더에서만 캡처된다 — 게임뷰가 안 켜져 있으면 목록이 비므로, "빈 레이어"로
	// 오해하지 않게 게임뷰를 켜라고 가운데에 명시한다(그래야 오브젝트 단위 컷오프 프리뷰도 쓸 수 있다).
	if (Editor::ImEditor.IsValid() && false == Editor::ImEditor->WasGameViewRenderedThisFrame())
	{
		const char* hint = Loc::Text(EditorLocKeys::GpuProfilerGameViewOff);
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 textSize = ImGui::CalcTextSize(hint);
		ImGui::SetCursorPos(ImVec2(
			ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f,
			ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f));
		ImGui::TextDisabled("%s", hint);
		return;
	}

	// OnRenderStay 가 계측 꺼짐일 때 조기 반환하므로, 여기 도달했으면 항상 계측 on 이다.
	const std::vector<GpuDrawOrderItem>* order = Engine.GpuProfiler->GetLayerDrawOrder(layer->GetIndex());
	if (nullptr == order || order->empty())
	{
		ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::GpuProfilerDrawOrderEmpty));
		return;
	}

	ImGui::TextUnformatted(Loc::Text(EditorLocKeys::GpuProfilerDrawOrderHeader));
	ImGui::Separator();

	const CGameObject* selectedObject = Editor::GetSelectedEntity();

	// 드로우순서 항목 하나를 Selectable 로 그린다(컬링=회색, 클릭=인스펙터 선택). 배치/단독 공용.
	// 수명 계약: 이 포인터들은 이번 프레임 게임뷰 렌더에서 담겼고 창은 같은 프레임에 그리므로,
	// 그 사이 오브젝트가 파괴되지 않아 이름/선택에 그대로 쓴다.
	auto drawOrderRow = [&](const GpuDrawOrderItem& drawItem, std::size_t displayIndex, std::size_t widgetId)
	{
		CGameObject* object = reinterpret_cast<CGameObject*>(const_cast<void*>(drawItem.Object));
		const char* name = object ? object->GetName() : "?";

		char label[192] = {};
		std::snprintf(label, sizeof(label), "%zu. %s##draworder%zu", displayIndex, name, widgetId);

		if (drawItem.Culled)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		const bool selected = (nullptr != object && object == selectedObject);
		if (ImGui::Selectable(label, selected))
		{
			// 이 오브젝트를 컷오프로 — 프리뷰가 드로우순서상 여기까지만 그린다(widgetId = 순서 인덱스).
			m_previewObjectIndex = static_cast<std::uint32_t>(widgetId);
			if (nullptr != object)
			{
				Editor::SelectEntity(object);
				// 계층뷰가 접혀 있어도 이 오브젝트가 보이도록 조상 트리·레이어를 펼치고 스크롤한다.
				Editor::RevealEntityInHierarchy(object);
			}
		}
		if (drawItem.Culled)
		{
			ImGui::PopStyleColor();
		}
	};

	// 같은 Group(비컬링) 연속 런 = RenderWithSkip 가 하나의 인스턴싱 드로우로 묶은 배치. 크기≥2 면
	// 배치 헤더 아래 멤버를 들여쓰고, 그 외(단독 드로우·컬링)는 평범히 나열한다.
	for (std::size_t i = 0; i < order->size(); )
	{
		const GpuDrawOrderItem& first = (*order)[i];
		std::size_t runEnd = i + 1;
		if (false == first.Culled && INVALID_DRAW_GROUP != first.Group)
		{
			while (runEnd < order->size()
				&& false == (*order)[runEnd].Culled
				&& (*order)[runEnd].Group == first.Group)
			{
				++runEnd;
			}
		}
		const std::size_t runLen = runEnd - i;

		if (runLen >= 2)
		{
			char header[96] = {};
			std::snprintf(header, sizeof(header), Loc::Text(EditorLocKeys::GpuProfilerBatchGroup), static_cast<int>(runLen));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.80f, 0.55f, 1.0f));
			ImGui::TextUnformatted(header);
			ImGui::PopStyleColor();
			ImGui::Indent();
			for (std::size_t k = i; k < runEnd; ++k)
			{
				drawOrderRow((*order)[k], k + 1, k);
			}
			ImGui::Unindent();
			i = runEnd;
		}
		else
		{
			drawOrderRow(first, i + 1, i);
			++i;
		}
	}
}

#endif
