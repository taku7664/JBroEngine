#include "pch.h"
#include "ImGuiIniMigration.h"

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"   // ImHashStr — Selected= 해시가 창 이름 해시다.

#include <cstdio>

namespace
{
	// 도킹 노드는 어느 창이 선택돼 있는지를 **창 이름의 해시**로 적는다(`Selected=0x........`).
	// 창 ID 를 바꾸면 이 해시도 같이 바뀌어야 탭 선택이 유지된다.
	std::string HashToken(const char* id)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "Selected=0x%08X", ImHashStr(id));
		return std::string(buffer);
	}

	void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
	{
		if (from.empty())
		{
			return;
		}
		std::size_t pos = 0;
		while ((pos = text.find(from, pos)) != std::string::npos)
		{
			text.replace(pos, from.size(), to);
			pos += to.size();
		}
	}
}

std::string EditorImGuiIni::MigrateWindowId(const std::string& iniText, const char* oldId, const char* newId)
{
	if (iniText.empty() || nullptr == oldId || nullptr == newId)
	{
		return iniText;
	}

	std::string result = iniText;
	ReplaceAll(result, std::string("[Window][") + oldId + "]", std::string("[Window][") + newId + "]");
	ReplaceAll(result, HashToken(oldId), HashToken(newId));
	return result;
}

std::string EditorImGuiIni::MigrateKnownWindowIds(const std::string& iniText)
{
	// 캔버스-레이어 개편의 창 리네임 — 표시 이름은 진작 옮겼고 안정 ID 만 남아 있었다.
	std::string result = iniText;
	// 왼쪽은 **옛 ID** 다 — 리네임 스윕이 건드리면 마이그레이션이 제자리걸음이 된다.
	result = MigrateWindowId(result, "SceneView", "CanvasView");
	result = MigrateWindowId(result, "Hierarchy", "Layers");
	return result;
}
