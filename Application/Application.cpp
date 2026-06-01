#include "pch.h"
#include "Application.h" 
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/Editor.h"
#include "Editor/Helper/ImGuiHelper.h"
#endif
void CGameApplication::OnPreInitialize()
{
}

void CGameApplication::OnPostInitialize()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	CEngine* engine = GetEngine();
	if (engine)
	{
		m_editor = MakeOwnerPtr<CImEditor>();
		Editor::ImEditor = m_editor.GetSafePtr();
		if (m_editor)
		{
			engine->InitializeModule(*m_editor, "ImEditor");
			Editor::RootDockWindow = m_editor->CreateImWindow<CRootDockWindow>("RootDockWindow");
			ImGuiHelper::SetDarkThemeColor();
			ImGuiHelper::SetDefaultThemeStyle();
		}
	}
#endif
}

void CGameApplication::OnPreFinalize()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	CEngine* engine = GetEngine();
	if (engine && m_editor)
	{
		engine->FinalizeModule(*m_editor);
		m_editor.Reset();
	}
#endif
}

void CGameApplication::OnPostFinalize()
{
}
