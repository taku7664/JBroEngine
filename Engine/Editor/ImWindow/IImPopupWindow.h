#pragma once

#include "Editor/ImWindow/ImWindowContext.h"

#include <string_view>

class IImPopupWindow
{
public:
	virtual ~IImPopupWindow() = default;

	// 사용자 측 닫기 요청 — 다음 프레임에 IsAlive=false 가 된다.
	virtual void Close() = 0;

	virtual PopupHandle      GetHandle() const = 0;
	virtual std::string_view GetId()     const = 0;
	virtual bool             IsAlive()   const = 0;
};
