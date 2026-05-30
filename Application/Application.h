#pragma once

#include "Engine/Framework.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Engine/Editor/ImEditor.h"   // OwnerPtr<CImEditor> 멤버
#endif

class CGameApplication : public CApplication
{
public:
	void OnPreInitialize() override;
	void OnPostInitialize() override;
	void OnPreFinalize() override;
	void OnPostFinalize() override;

private:
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	OwnerPtr<CImEditor> m_editor;
#endif
};
