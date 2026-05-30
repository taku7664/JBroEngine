#include "pch.h"
#include "ImporterWindowBase.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImWindow/ImWindowFlag.h"
#include "Engine/Editor/ImGuiUtillity.h"    // FormLayout, HoveredToolTip
#include "Engine/Editor/ImItem/ImText.h"    // ImText (라벨 + 설명 툴팁)
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetPath.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Logging/LoggerInternal.h"

#include <algorithm>
#include <filesystem>
#include <string>

void CImporterWindowBase::OnCreate()
{
	SetLocalizedTitleKey(GetTitleLocKey());
	SetVisible(false);   // 메뉴에서 열기 전까지는 숨김
	SetSize({ 520.0f, 380.0f });
	m_windowFlags = IMWINDOW_FLAG_FIXED_WINDOW_SIZE;
}

void CImporterWindowBase::OnRenderStay()
{
	DrawSourceRow();   // source + destination 두 행을 FormLayout 으로 함께 그린다.

	ImGui::Spacing();
	ImGui::SeparatorText(Loc::Text("importer.options"));
	DrawImportOptions();

	ImGui::Spacing();
	ImGui::Separator();
	DrawImportButton();

	if (false == m_lastMessage.empty())
	{
		const ImVec4 color = m_lastMessageIsError
			? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
			: ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
		ImGui::TextColored(color, "%s", m_lastMessage.c_str());
	}
}

void CImporterWindowBase::DrawSourceRow()
{
	ImGui::Utillity::FormLayout layout("##importer_paths", 4.0f, {2.0f, 1.0f}, 120.0f);
	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("importer.source_file.desc"));
			label(Loc::Text("importer.source_file"));
		},
		[&]() { ImGui::InputText("##importer.source", m_sourcePathBuf.data(), m_sourcePathBuf.size()); });
	ImGui::TextDisabled("%s: %s", Loc::Text("importer.source_hint"), GetSourceExtensionsCsv());

	layout.Row(
		[&]() {
			ImText label;
			label.SetHoveredTooltip(Loc::Text("importer.destination.desc"));
			label(Loc::Text("importer.destination"));
		},
		[&]() { ImGui::InputText("##importer.destination", m_destPathBuf.data(), m_destPathBuf.size()); });
	ImGui::TextDisabled("%s", Loc::Text("importer.destination_hint"));
}

void CImporterWindowBase::DrawDestinationRow()
{
	// DrawSourceRow() 가 source/destination 두 행을 함께 그리므로 여기는 비어있다.
	// (구조를 유지하려고 메서드만 남김 — 외부에서 호출되지 않음.)
}

namespace
{
	// 확장자 리스트(콤마 구분 ".png,.jpg")에 포함되는지 검사.
	bool MatchesExtension(const std::string& ext, const char* csv)
	{
		if (nullptr == csv) return false;
		std::string lower(ext);
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		std::string list(csv);
		std::transform(list.begin(), list.end(), list.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		std::size_t pos = 0;
		while (pos < list.size())
		{
			std::size_t comma = list.find(',', pos);
			std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			while (false == item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(item.begin());
			if (item == lower) return true;
			if (comma == std::string::npos) break;
			pos = comma + 1;
		}
		return false;
	}
}

void CImporterWindowBase::DrawImportButton()
{
	SafePtr<CProjectManager> pm = Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	const bool projectLoaded = pm && pm->IsProjectLoaded();

	const std::string sourcePath(m_sourcePathBuf.data());
	const std::string destRel   (m_destPathBuf.data());

	const bool canImport = projectLoaded && false == sourcePath.empty() && false == destRel.empty();
	ImGui::BeginDisabled(false == canImport);
	const bool importPressed = ImGui::Button(Loc::Text("importer.import"), { 120.0f, 0.0f });
	ImGui::Utillity::HoveredToolTip(Loc::Text("importer.import.desc"));
	if (importPressed)
	{
		m_lastMessage.clear();
		m_lastMessageIsError = false;

		std::error_code ec;
		std::filesystem::path src(sourcePath);
		if (false == std::filesystem::exists(src, ec))
		{
			m_lastMessage = Loc::Text("importer.error.source_not_found");
			m_lastMessageIsError = true;
		}
		else
		{
			const std::string srcExt = src.extension().generic_string();
			if (false == MatchesExtension(srcExt, GetSourceExtensionsCsv()))
			{
				m_lastMessage = Loc::Text("importer.error.unsupported_extension");
				m_lastMessageIsError = true;
			}
			else
			{
				// 출력 절대 경로 = AssetPath / destRel (+ 기본 확장자 보정)
				std::filesystem::path destAbs = std::filesystem::path(pm->GetAssetPath()) / std::filesystem::path(destRel);
				const char* defaultExt = GetDefaultDestinationExtension();
				if (defaultExt && defaultExt[0] != '\0' && destAbs.extension().empty())
				{
					destAbs += defaultExt;
				}
				// destRel 에 확장자가 없거나 다른 확장자가 들어있어도 원본 확장자를 강제 부여
				// (예: "BGM/main" + 원본 .wav → "BGM/main.wav")
				if (destAbs.extension().empty() || MatchesExtension(destAbs.extension().generic_string(), GetSourceExtensionsCsv()) == false)
				{
					destAbs.replace_extension(srcExt);
				}

				// 부모 폴더 생성
				std::filesystem::create_directories(destAbs.parent_path(), ec);
				if (ec)
				{
					m_lastMessage = std::string(Loc::Text("importer.error.create_directory_failed"))
						+ " (" + ec.message() + ")";
					m_lastMessageIsError = true;
					ec.clear();
				}
				else
				{
					std::string errorOut;
					if (ExecuteImport(File::Path(src), File::Path(destAbs), errorOut))
					{
						m_lastMessage = Loc::Text("importer.success");
						m_lastMessageIsError = false;
						CSystemLog::Info(std::string("Imported asset: ") + destAbs.generic_string());
					}
					else
					{
						m_lastMessage = errorOut.empty()
							? std::string(Loc::Text("importer.error.import_failed"))
							: errorOut;
						m_lastMessageIsError = true;
					}
				}
			}
		}
	}
	ImGui::EndDisabled();

	if (false == projectLoaded)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", Loc::Text("importer.no_project"));
	}
}
