#include "pch.h"
#include "WindowsDynamicLibrary.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

bool CWindowsDynamicLibrary::Load(const wchar_t* path)
{
	Unload();

	if (nullptr == path)
	{
		return false;
	}

	m_handle = LoadLibraryW(path);
	return nullptr != m_handle;
}

void CWindowsDynamicLibrary::Unload()
{
	if (m_handle)
	{
		FreeLibrary(static_cast<HMODULE>(m_handle));
		m_handle = nullptr;
	}
}

void* CWindowsDynamicLibrary::GetSymbol(const char* name) const
{
	if (nullptr == m_handle || nullptr == name)
	{
		return nullptr;
	}

	return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name));
}

bool CWindowsDynamicLibrary::IsLoaded() const
{
	return nullptr != m_handle;
}

#else

bool CWindowsDynamicLibrary::Load(const wchar_t* path)
{
	(void)path;
	return false;
}

void CWindowsDynamicLibrary::Unload()
{
	m_handle = nullptr;
}

void* CWindowsDynamicLibrary::GetSymbol(const char* name) const
{
	(void)name;
	return nullptr;
}

bool CWindowsDynamicLibrary::IsLoaded() const
{
	return false;
}

#endif

