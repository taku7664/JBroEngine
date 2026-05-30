#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ThirdParty/imgui/imgui.h"   // ImVec2, ImGuiWindowFlags

class IImPopupWindow;

// ── PopupHandle ──────────────────────────────────────────────────────────────
// CImEditor::OpenPopup 이 반환하는 식별자.
//   - INVALID_POPUP_HANDLE : OpenPopup 실패.
//   - 그 외                : ClosePopup / IsPopupOpen 에 사용.
using PopupHandle = std::uint64_t;
inline constexpr PopupHandle INVALID_POPUP_HANDLE = 0;

// 모달 / 비모달 구분.  현재 구현은 모달만 의미가 있지만, 향후 비모달
// 확장 여지를 위해 미리 분기를 노출해 둔다.
enum class EImPopupKind : std::uint8_t
{
	Modal,   // 백그라운드 클릭 차단 — BeginPopupModal
};

struct ImPopupDesc
{
	std::string			Title;
	// 동일 Id 를 가진 팝업이 이미 열려 있으면 OpenPopup 은 새 인스턴스를 만들지
	// 않고 기존 핸들을 그대로 반환한다 (중복 방지).
	// 빈 문자열이면 매번 새 인스턴스로 취급.
	std::string			Id;
	EImPopupKind		Kind = EImPopupKind::Modal;
	ImGuiWindowFlags	Flags = ImGuiWindowFlags_None;
	ImVec2				InitSize = ImVec2( 0 , 0 );
	std::function<void(IImPopupWindow&)> OnRenderEnterFunc = nullptr;
	std::function<void(IImPopupWindow&)> OnRenderStayFunc = nullptr;
	std::function<void(IImPopupWindow&)> OnRenderExitFunc = nullptr;
};

struct ImPopupContext
{
	std::string			Title;
	ImVec2				Size;
	ImGuiWindowFlags	Flags;
	bool				IsOpen = true;
};
