#include "pch.h"
#include "Application.h"

#if JBRO_PLATFORM_WINDOWS
#if JBRO_EDITOR
#include <Objbase.h>

namespace
{
	class CEditorComApartmentScope
	{
	public:
		CEditorComApartmentScope()
		{
			const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			m_shouldUninitialize = SUCCEEDED(result);
		}

		~CEditorComApartmentScope()
		{
			if (m_shouldUninitialize)
			{
				CoUninitialize();
			}
		}

	private:
		bool m_shouldUninitialize = false;
	};
}
#endif

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

#if JBRO_EDITOR
	CEditorComApartmentScope editorComScope;
#endif

	CGameApplication app;
	app.RunNative();
	return 0;
}

#endif
