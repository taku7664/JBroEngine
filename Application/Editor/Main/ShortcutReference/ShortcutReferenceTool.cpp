#include "pch.h"
#include "ShortcutReferenceTool.h"

#include "Editor/Editor.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

void CShortcutReferenceTool::OnCreate()
{
	SetLocalizedTitleKey("window.shortcut_reference");
}

void CShortcutReferenceTool::OnRenderStay()
{
	m_filter.Draw(Loc::Text("common.search"), -1.0f);
	ImGui::Spacing();
	ImGui::TextDisabled("%s", Loc::Text("shortcut_reference.text_input_note"));
	ImGui::Separator();

	const ImGuiTableFlags flags = ImGuiTableFlags_Borders
		| ImGuiTableFlags_RowBg
		| ImGuiTableFlags_Resizable
		| ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("ShortcutReference", 4, flags))
	{
		ImGui::TableSetupColumn(Loc::Text("shortcut_reference.category"));
		ImGui::TableSetupColumn(Loc::Text("shortcut_reference.action"));
		ImGui::TableSetupColumn(Loc::Text("shortcut_reference.binding"));
		ImGui::TableSetupColumn(Loc::Text("shortcut_reference.status"));
		ImGui::TableHeadersRow();

		for (const EditorShortcutDescriptor& descriptor : Editor::ShortcutManager.GetDescriptors())
		{
			const char* category = Loc::Text(descriptor.CategoryKey);
			const char* action = Loc::Text(descriptor.NameKey);
			const std::string binding = Editor::ShortcutManager.GetShortcutText(descriptor.Id);
			if (false == m_filter.PassFilter(category)
				&& false == m_filter.PassFilter(action)
				&& false == m_filter.PassFilter(binding.c_str()))
			{
				continue;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(category);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(binding.c_str());
			ImGui::TableSetColumnIndex(3);
			const bool available = Editor::ShortcutManager.CanExecute(descriptor.Id);
			if (false == available)
			{
				ImGui::BeginDisabled();
			}
			ImGui::TextUnformatted(Loc::Text(available
				? "shortcut_reference.available"
				: "shortcut_reference.unavailable"));
			if (false == available)
			{
				ImGui::EndDisabled();
			}
		}
		ImGui::EndTable();
	}
}

#endif
