#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

// 짧은 안내/경고를 확인 버튼 하나짜리 모달로 띄운다. 로그로만 남기면 사용자가 놓치는
// "왜 안 됐는지"류 피드백(예: 이미 추가된 레이어 에셋을 또 드롭)에 쓴다.
//
// 매번 ImPopupDesc + OnRenderStayFunc 람다를 짜는 중복을 없앤다 — 그 패턴은 본문/버튼이
// 필요한 큰 팝업(컴파일 로그 등)에나 어울리고, 한 줄 알림엔 과하다.
namespace EditorMessagePopup
{
	// title/message 는 이미 현지화된 문자열(호출자가 Loc::Text 로 넘긴다). 값 복사로 들고
	// 있으므로 임시 문자열을 넘겨도 안전하다.
	void ShowInfo(const std::string& title, const std::string& message);
}

#endif
