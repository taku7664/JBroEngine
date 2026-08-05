#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ImNameListEdit ─ "한 줄 = 한 이름" 목록 편집기 (헤더 전용 inline).
//
//  프로젝트 세팅의 입력 레이어 / 오디오 버스처럼 **순서가 있는 이름 목록**을 편집한다.
//  추가는 줄을 쓰고 제거는 줄을 지우는 것 — 별도 +/- 버튼 없이 텍스트로 다룬다.
//
//  백킹 버퍼를 호출자가 멤버로 들고 있어야 하는 이유:
//  ImGui::InputTextMultiline 은 편집 중인 문자열 버퍼가 프레임을 넘어 살아 있어야 한다.
//  매 프레임 벡터에서 새로 만들면 커서/선택이 초기화된다. 그래서 창을 열 때 한 번
//  BuildBuffer 로 채우고, 그 뒤로는 버퍼가 원본이고 벡터가 파생이다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "ThirdParty/imgui/imgui.h"

#include <string>
#include <vector>

namespace ImGui::Utillity
{
	// 이름 벡터 → 편집 버퍼(줄바꿈 구분). 창을 열 때 한 번 부른다.
	inline void BuildNameListBuffer(const std::vector<std::string>& names, std::string& outBuffer)
	{
		outBuffer.clear();
		for (const std::string& name : names)
		{
			outBuffer += name;
			outBuffer.push_back('\n');
		}
	}

	// 편집 버퍼 → 이름 벡터. 빈 줄과 앞뒤 공백은 버린다.
	inline void ParseNameListBuffer(const std::string& buffer, std::vector<std::string>& outNames)
	{
		outNames.clear();
		std::size_t start = 0;
		for (std::size_t i = 0; i <= buffer.size(); ++i)
		{
			if (i != buffer.size() && '\n' != buffer[i] && '\r' != buffer[i])
			{
				continue;
			}
			if (i > start)
			{
				std::string line(buffer, start, i - start);
				while (false == line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
				while (false == line.empty() && (line.back()  == ' ' || line.back()  == '\t')) line.pop_back();
				if (false == line.empty()) outNames.push_back(std::move(line));
			}
			start = i + 1;
		}
	}

	// 목록 편집 박스. 편집이 있었으면 outNames 를 갱신하고 true.
	// id 는 ImGui 위젯 id, lineCount 는 박스 높이(줄 수).
	inline bool NameListEditor(const char* id, std::string& buffer, std::vector<std::string>& outNames,
	                           float lineCount = 12.0f)
	{
		const ImVec2 boxSize(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() * lineCount);
		// InputTextMultiline 은 capacity 까지만 쓴다. 콜백 resize 를 쓰더라도 여유를 미리 준다.
		buffer.reserve(buffer.size() + 1024);
		const bool changed = ImGui::InputTextMultiline(id,
			buffer.data(), buffer.capacity(),
			boxSize, ImGuiInputTextFlags_CallbackResize,
			[](ImGuiInputTextCallbackData* data) -> int
			{
				if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
				{
					std::string* target = static_cast<std::string*>(data->UserData);
					target->resize(data->BufTextLen);
					data->Buf = target->data();
				}
				return 0;
			}, &buffer);

		if (changed)
		{
			ParseNameListBuffer(buffer, outNames);
		}
		return changed;
	}
}
