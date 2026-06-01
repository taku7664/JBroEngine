#include "pch.h"
#include "GameModuleLoader.h"

#include "Core/Game/IGameModule.h"

#include <cstdlib>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	void* AllocateModuleMemory(std::size_t size, std::size_t alignment)
	{
		const std::size_t effectiveSize = std::max<std::size_t>(size, 1);
		const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));
#if defined(_MSC_VER)
		return _aligned_malloc(effectiveSize, effectiveAlignment);
#else
		const std::size_t remainder = effectiveSize % effectiveAlignment;
		const std::size_t alignedSize = 0 == remainder ? effectiveSize : effectiveSize + (effectiveAlignment - remainder);
		return std::aligned_alloc(effectiveAlignment, alignedSize);
#endif
	}

	void FreeModuleMemory(void* ptr, std::size_t, std::size_t)
	{
		if (nullptr == ptr)
		{
			return;
		}
#if defined(_MSC_VER)
		_aligned_free(ptr);
#else
		std::free(ptr);
#endif
	}
}

CGameModuleLoader::CGameModuleLoader()
{
	m_hostApi.Allocate = &AllocateModuleMemory;
	m_hostApi.Free = &FreeModuleMemory;
}

CGameModuleLoader::~CGameModuleLoader()
{
	Unload();
}

bool CGameModuleLoader::LoadDynamicLibrary(const File::Path& libraryPath, const GameModuleContext& context)
{
#if JBRO_PLATFORM_WINDOWS
	if (libraryPath.empty())
	{
		return false;
	}

	Unload();

	HMODULE libraryHandle = LoadLibraryW(libraryPath.wstring().c_str());
	if (nullptr == libraryHandle)
	{
		return false;
	}

	const auto createFunc = reinterpret_cast<CreateGameModuleFunc>(GetProcAddress(libraryHandle, "CreateGameModule"));
	const auto destroyFunc = reinterpret_cast<DestroyGameModuleFunc>(GetProcAddress(libraryHandle, "DestroyGameModule"));
	if (nullptr == createFunc || nullptr == destroyFunc)
	{
		FreeLibrary(libraryHandle);
		return false;
	}

	IGameModule* module = createFunc(&m_hostApi);
	if (nullptr == module)
	{
		FreeLibrary(libraryHandle);
		return false;
	}

	if (false == module->Initialize(context))
	{
		destroyFunc(module, &m_hostApi);
		FreeLibrary(libraryHandle);
		return false;
	}

	m_libraryHandle = libraryHandle;
	m_gameModule = module;
	m_destroyGameModule = destroyFunc;
	m_loadedPath = libraryPath;
	return true;
#else
	(void)libraryPath;
	(void)context;
	return false;
#endif
}

void CGameModuleLoader::Tick()
{
	if (m_gameModule)
	{
		m_gameModule->Tick();
	}
}

void CGameModuleLoader::Unload()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_gameModule)
	{
		m_gameModule->Finalize();
		if (m_destroyGameModule)
		{
			m_destroyGameModule(m_gameModule, &m_hostApi);
		}
		m_gameModule = nullptr;
		m_destroyGameModule = nullptr;
	}

	if (m_libraryHandle)
	{
		FreeLibrary(static_cast<HMODULE>(m_libraryHandle));
		m_libraryHandle = nullptr;
	}
#else
	m_gameModule = nullptr;
	m_destroyGameModule = nullptr;
	m_libraryHandle = nullptr;
#endif
	m_loadedPath.clear();
}
