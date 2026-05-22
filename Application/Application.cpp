#include "pch.h"
#include "Application.h" 
#include "Editor/Editor.h"

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
		}
		{
			Editor::RootDockWindow = m_editor->CreateImWindow<CRootDockWindow>("RootDockWindow");
		}
		{
			Editor::MainDockWindow = m_editor->CreateImWindow<CMainDockWindow>("MainDockWindow");
			if (Editor::RootDockWindow && Editor::MainDockWindow)
			{
				Editor::RootDockWindow->AddChildImWindow(Editor::MainDockWindow);
			}
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
