#pragma once

class IImPopupWindow;

struct ImPopupDesc
{
	std::string			Title;
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

