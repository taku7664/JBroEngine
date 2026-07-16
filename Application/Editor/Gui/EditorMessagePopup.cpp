#include "pch.h"
#include "EditorMessagePopup.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/ImWindow/IImPopupWindow.h"

#include <memory>

void EditorMessagePopup::ShowInfo(const std::string& title, const std::string& message)
{
	if (false == Editor::ImEditor.IsValid())
	{
		return;
	}

	// 람다가 프레임을 넘겨 살아있으므로 메시지를 shared_ptr 로 캡처한다(호출자 임시 소멸 대비).
	auto text = std::make_shared<std::string>(message);

	ImPopupDesc desc;
	desc.Title    = title;
	// 빈 Id = 매번 새 인스턴스. 같은 안내를 연달아 띄워도 각각 뜬다(중복 억제는 필요 없다).
	// AlwaysAutoResize 는 쓰지 않는다 — 그 플래그가 켜지면 팝업이 InitSize 를 무시하고
	// TextWrapped 가 좁은 폭으로 접혀 세로로 길쭉해진다. 고정 폭을 줘야 문구가 가로로 퍼진다.
	desc.InitSize = ImVec2(380.0f, 150.0f);
	desc.Flags    = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

	desc.OnRenderStayFunc = [text](IImPopupWindow& popup)
	{
		// 본문은 위쪽에 두고, 확인 버튼은 팝업 하단에 고정한다(내용 높이에 따라 버튼이
		// 떠다니지 않게). 버튼 줄 높이만큼 남기고 그 위 영역을 본문이 채운다.
		const float footerH = ImGui::GetFrameHeightWithSpacing();
		const ImVec2 avail = ImGui::GetContentRegionAvail();

		ImGui::BeginChild("##message_body", ImVec2(avail.x, avail.y - footerH), false);
		ImGui::TextWrapped("%s", text->c_str());
		ImGui::EndChild();

		constexpr float BTN_W = 90.0f;
		const float footerAvailW = ImGui::GetContentRegionAvail().x;
		if (footerAvailW > BTN_W)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + footerAvailW - BTN_W);
		}
		if (ImGui::Button(Loc::Text(EditorLocKeys::CommonOk), ImVec2(BTN_W, 0.0f)))
		{
			popup.Close();
		}
	};

	Editor::ImEditor->OpenPopup(desc);
}

#endif
