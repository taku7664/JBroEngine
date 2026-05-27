#include "pch.h"
#include "ScriptModuleLoader.h"

#include "Core/Game/IGameModule.h"
#include "Editor/LiveCompile/IDynamicLibrary.h"
#include "Editor/LiveCompile/Windows/WindowsDynamicLibrary.h"

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

CScriptModuleLoader::CScriptModuleLoader()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	m_library = MakeOwnerPtr<CWindowsDynamicLibrary>();
	m_hostApi.Allocate = &AllocateModuleMemory;
	m_hostApi.Free = &FreeModuleMemory;
#endif
}

CScriptModuleLoader::~CScriptModuleLoader()
{
	Unload();
}

bool CScriptModuleLoader::Load(const char* dllPath, const GameModuleContext& context)
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	if (nullptr == dllPath || '\0' == dllPath[0])
	{
		return false;
	}

	Unload();

	if (!m_library)
	{
		m_library = MakeOwnerPtr<CWindowsDynamicLibrary>();
	}

	if (false == m_library->Load(dllPath))
	{
		return false;
	}

	const auto createFunc  = reinterpret_cast<CreateGameModuleFunc>(m_library->GetSymbol("CreateGameModule"));
	const auto destroyFunc = reinterpret_cast<DestroyGameModuleFunc>(m_library->GetSymbol("DestroyGameModule"));

	if (nullptr == createFunc || nullptr == destroyFunc)
	{
		m_library->Unload();
		return false;
	}

	IGameModule* module = createFunc(&m_hostApi);
	if (nullptr == module)
	{
		m_library->Unload();
		return false;
	}

	if (false == module->Initialize(context))
	{
		destroyFunc(module, &m_hostApi);
		m_library->Unload();
		return false;
	}

	m_gameModule        = module;
	m_destroyGameModule = destroyFunc;
	m_loadedPath        = dllPath;
	return true;
#else
	(void)dllPath;
	(void)context;
	return false;
#endif
}

void CScriptModuleLoader::Unload()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	if (m_gameModule)
	{
		m_gameModule->Finalize();
		if (m_destroyGameModule)
		{
			m_destroyGameModule(m_gameModule, &m_hostApi);
		}
		m_gameModule        = nullptr;
		m_destroyGameModule = nullptr;
	}

	if (m_library)
	{
		m_library->Unload();
	}

	m_loadedPath.clear();
#endif
}
