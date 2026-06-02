#include "pch.h"
#include "Application.h"

#if JBRO_PLATFORM_WEB
#include <emscripten.h>

namespace
{
	CGameApplication* WebApplication = nullptr;

	void MainLoop()
	{
		if (nullptr == WebApplication)
		{
			return;
		}

		if (false == WebApplication->TickApplication())
		{
			WebApplication->FinalizeApplication();
			emscripten_cancel_main_loop();
		}
	}
}

int main()
{
	static CGameApplication application;
	WebApplication = &application;

	if (application.InitializeApplication())
	{
		emscripten_set_main_loop(MainLoop, 0, true);
	}
	else
	{
		application.FinalizeApplication();
	}

	return 0;
}

#endif
