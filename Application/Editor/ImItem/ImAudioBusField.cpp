#include "pch.h"
#include "ImAudioBusField.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/EditorContext.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Core/Localization/LocalizationManager.h"

#include "ThirdParty/imgui/imgui.h"

namespace AudioBusUI
{
	std::vector<std::string> GetSelectableBusNames()
	{
		std::vector<std::string> names;
		// Master 는 프로젝트 목록에 없어도 항상 존재하는 루트라 언제나 고를 수 있어야 한다.
		names.emplace_back(AUDIO_MASTER_BUS_NAME);

		SafePtr<CProjectManager> pm = EditorContext::GetProjectManager();
		if (false == static_cast<bool>(pm))
		{
			return names;
		}

		// **적용된** 프로젝트 설정을 읽는다(편집 중인 프로젝트 세팅 버퍼가 아니라).
		// 인스펙터는 저장된 상태를 보여야 하고, 아직 취소될 수 있는 값을 미리 반영하면
		// 사용자가 고른 버스가 적용 취소와 함께 사라진다.
		for (const AudioBusDef& bus : pm->GetAudioBuses())
		{
			if (bus.Name.empty())
			{
				continue;
			}
			if (IsSameAudioBusName(bus.Name.c_str(), AUDIO_MASTER_BUS_NAME))
			{
				continue;
			}
			names.push_back(bus.Name);
		}
		return names;
	}

	bool IsBusNameInvalid(const std::string& name, const std::vector<AudioBusDef>& all)
	{
		if (name.empty())
		{
			return true;
		}

		// "Master" 행은 **막지 않는다.** 백엔드의 ConfigureBuses 가 그 행을 루트 버스의
		// 시작 음량으로 받아들이므로(무시하지 않는다) 유효한 사용법이다.
		// 두 개 이상 적으면 아래 중복 검사에 걸린다.
		int count = 0;
		for (const AudioBusDef& bus : all)
		{
			if (bus.Name == name)
			{
				++count;
			}
		}
		return count > 1;
	}

	std::vector<std::string> FindRemovedBusNames(
		const std::vector<AudioBusDef>& current,
		const std::vector<std::string>& baseline)
	{
		std::vector<std::string> removed;
		for (const std::string& previous : baseline)
		{
			if (previous.empty())
			{
				continue;
			}

			bool stillPresent = false;
			for (const AudioBusDef& bus : current)
			{
				if (bus.Name == previous)
				{
					stillPresent = true;
					break;
				}
			}
			if (false == stillPresent)
			{
				removed.push_back(previous);
			}
		}
		return removed;
	}

	bool IsBusNameRenamed(
		const std::string& name,
		const std::vector<AudioBusDef>& current,
		const std::vector<std::string>& baseline)
	{
		if (name.empty())
		{
			return false;
		}

		// baseline 에 그대로 있는 이름은 손대지 않은 행이다.
		for (const std::string& previous : baseline)
		{
			if (previous == name)
			{
				return false;
			}
		}

		// 여기까지 왔으면 새 이름이다. 사라진 이름이 하나도 없다면 순수 추가이므로
		// 경고할 것이 없다 — 아무도 참조하지 않던 이름이 늘어난 것뿐이다.
		return false == FindRemovedBusNames(current, baseline).empty();
	}

	bool DrawBusCombo(const char* id, std::string& busName)
	{
		const std::vector<std::string> names = GetSelectableBusNames();

		// 빈 값은 엔진 규약상 Master 로 해석된다 — 미등록이 아니다.
		const bool isEmpty = busName.empty();
		bool isKnown = isEmpty;
		for (const std::string& name : names)
		{
			if (name == busName)
			{
				isKnown = true;
				break;
			}
		}

		std::string missingLabel;
		if (false == isKnown)
		{
			missingLabel = busName + " " + Loc::Text(EditorLocKeys::InspectorAudioBusMissing);
		}

		const char* preview = AUDIO_MASTER_BUS_NAME;
		if (false == isEmpty)
		{
			preview = isKnown ? busName.c_str() : missingLabel.c_str();
		}

		bool changed = false;
		if (ImGui::BeginCombo(nullptr != id ? id : "##audio_bus", preview))
		{
			for (const std::string& name : names)
			{
				const bool selected = isEmpty
					? IsSameAudioBusName(name.c_str(), AUDIO_MASTER_BUS_NAME)
					: (name == busName);
				if (ImGui::Selectable(name.c_str(), selected))
				{
					busName = name;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			if (false == isKnown)
			{
				// 목록에 없는 값도 **보여는 준다**. 고를 수는 없지만, 무엇이 적혀 있는지
				// 모른 채 첫 항목으로 스냅되는 것보다 낫다(라우팅이 조용히 바뀐다).
				ImGui::Separator();
				ImGui::TextDisabled("%s", missingLabel.c_str());
			}
			ImGui::EndCombo();
		}
		return changed;
	}
}

#endif
