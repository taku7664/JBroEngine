#pragma once

#include "Engine/Framework.h"

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
