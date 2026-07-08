#include "pch.h"
#include "ShortcutReferenceTool.h"

#include "Editor/Editor.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

void CShortcutReferenceTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowShortcutReference);
}

void CShortcutReferenceTool::OnRenderStay()
{
	m_filter.Draw(Loc::Text(EditorLocKeys::CommonSearch), -1.0f);
	ImGui::Spacing();
	ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::ShortcutReferenceTextInputNote));
	ImGui::Separator();

	const ImGuiTableFlags flags = ImGuiTableFlags_Borders
		| ImGuiTableFlags_RowBg
		| ImGuiTableFlags_Resizable
		| ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("ShortcutReference", 4, flags))
	{
		ImGui::TableSetupColumn(Loc::Text(EditorLocKeys::ShortcutReferenceCategory));
		ImGui::TableSetupColumn(Loc::Text(EditorLocKeys::ShortcutReferenceAction));
		ImGui::TableSetupColumn(Loc::Text(EditorLocKeys::ShortcutReferenceBinding));
		ImGui::TableSetupColumn(Loc::Text(EditorLocKeys::ShortcutReferenceStatus));
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
				? EditorLocKeys::ShortcutReferenceAvailable
				: EditorLocKeys::ShortcutReferenceUnavailable));
			if (false == available)
			{
				ImGui::EndDisabled();
			}
		}
		ImGui::EndTable();
	}
}

#endif
