#include "pch.h"
#include "LiveCompileManager.h"

#include "Core/Core.h"
#include "Core/EngineCore.h"
#include "Core/Game/IGameModule.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scene/SceneManager.h"
#include "Editor/LiveCompile/CompilePipeline.h"
#include "Editor/LiveCompile/Windows/WindowsDynamicLibrary.h"

#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	CSceneManager* GetModuleSceneManager(const GameModuleContext& context)
	{
		return context.HostEngine ? context.HostEngine->SceneManager.TryGet() : nullptr;
	}

	CReflectionRegistry* GetModuleReflection(const GameModuleContext& context)
	{
		return context.HostEngine ? context.HostEngine->Reflection.TryGet() : nullptr;
	}

	void* AllocateModuleMemory(std::size_t size, std::size_t alignment)
	{
		const std::size_t effectiveSize      = std::max<std::size_t>(size, 1);
		const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));
#if defined(_MSC_VER)
		return _aligned_malloc(effectiveSize, effectiveAlignment);
#else
		const std::size_t remainder  = effectiveSize % effectiveAlignment;
		const std::size_t alignedSize = 0 == remainder
			? effectiveSize
			: effectiveSize + (effectiveAlignment - remainder);
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

bool CLiveCompileManager::Initialize(const LiveCompileDesc& desc)
{
	Finalize();
	m_desc = desc;
	m_hostApi.Allocate = &AllocateModuleMemory;
	m_hostApi.Free     = &FreeModuleMemory;
	m_compilePipeline  = MakeOwnerPtr<CCompilePipeline>();
	m_dynamicLibrary   = MakeOwnerPtr<CWindowsDynamicLibrary>();
	m_sourceWatcher    = MakeOwnerPtr<CWindowsFileWatcher>();
	if (!m_compilePipeline || !m_dynamicLibrary || !m_sourceWatcher)
	{
		return false;
	}

	if (false == desc.SourceDirectory.empty())
	{
		FileWatcherDesc watcherDesc;
		watcherDesc.RootPath  = File::Path(desc.SourceDirectory);
		watcherDesc.Recursive = true;
		m_sourceWatcher->Watch(watcherDesc);
	}

	m_state = ELiveCompileState::Idle;
	return true;
}

void CLiveCompileManager::Finalize()
{
	// 진행 중인 비동기 컴파일이 있으면 완료까지 대기 (취소 불가 — Compile 프로세스가 자체 종료해야 함)
	if (m_pendingCompile.valid())
	{
		m_pendingCompile.wait();
		m_pendingCompile = {};
	}

	DestroyCurrentModule();
	if (m_dynamicLibrary)
	{
		m_dynamicLibrary->Unload();
	}
	if (m_sourceWatcher)
	{
		m_sourceWatcher->Stop();
	}
	m_sourceWatcher.Reset();
	m_dynamicLibrary.Reset();
	m_compilePipeline.Reset();
	m_scriptSnapshots.clear();
	m_state   = ELiveCompileState::Idle;
	m_isDirty = false;
}

void CLiveCompileManager::Tick(bool scanSourceChanges)
{
	if (scanSourceChanges && m_sourceWatcher)
	{
		m_sourceWatcher->Poll();
		std::vector<FileWatchEvent> events;
		if (m_sourceWatcher->TakeEvents(events))
		{
			m_isDirty    = true;
			m_dirtyTime  = std::chrono::steady_clock::now();
		}
	}

	// 1) 진행 중인 비동기 컴파일 폴링 → 완료 시 메인 스레드에서 DLL 교체
	if (m_pendingCompile.valid())
	{
		PollAsyncCompile();
	}

	// 2) 디바운스 경과 + 컴파일 진행 중 아니면 새 빌드 시작
	if (m_isDirty && false == m_pendingCompile.valid())
	{
		const std::chrono::duration<float> elapsed =
			std::chrono::steady_clock::now() - m_dirtyTime;
		if (elapsed.count() >= m_desc.DebounceSeconds)
		{
			m_isDirty = false;
			StartAsyncCompile();
		}
	}
}

// 동기 RebuildAndReload — 외부 강제 트리거(메뉴 명령 등)용.
// 비동기 컴파일이 이미 진행 중이면 그 결과를 기다린 뒤 새로 시작하지 않고 반환.
LiveCompileResult CLiveCompileManager::RebuildAndReload()
{
	if (!m_compilePipeline || !m_dynamicLibrary)
	{
		LiveCompileResult result;
		result.Message = "LiveCompile is not initialized.";
		return result;
	}

	// 이미 비동기 컴파일이 진행 중이면 합쳐서 처리.
	if (m_pendingCompile.valid())
	{
		m_pendingCompile.wait();
		LiveCompileResult pending = m_pendingCompile.get();
		m_pendingCompile = {};
		return ApplyCompileResult(std::move(pending));
	}

	m_state = ELiveCompileState::Compiling;
	m_compileStartedAt = std::chrono::steady_clock::now();
	LiveCompileResult result = m_compilePipeline->Compile(m_desc);
	return ApplyCompileResult(std::move(result));
}

void CLiveCompileManager::StartAsyncCompile()
{
	if (!m_compilePipeline)
	{
		return;
	}
	m_state = ELiveCompileState::Compiling;
	m_compileStartedAt = std::chrono::steady_clock::now();
	m_pendingCompile = m_compilePipeline->CompileAsync(m_desc);
}

void CLiveCompileManager::PollAsyncCompile()
{
	if (false == m_pendingCompile.valid())
	{
		return;
	}

	// 0ms 폴링 — 메인 스레드를 절대 블로킹하지 않음
	if (std::future_status::ready != m_pendingCompile.wait_for(std::chrono::seconds(0)))
	{
		return;
	}

	LiveCompileResult result = m_pendingCompile.get();
	m_pendingCompile = {};
	ApplyCompileResult(std::move(result));
}

// 메인 스레드 전용: 컴파일 결과를 받아 DLL 교체 + 모듈 재로드 + 스냅샷 복원.
// 워커 스레드에서 절대 호출하면 안 됨 (Reflection/SceneManager 접근).
LiveCompileResult CLiveCompileManager::ApplyCompileResult(LiveCompileResult result)
{
	if (false == result.Succeeded)
	{
		m_state = ELiveCompileState::Failed;
		return result;
	}

	if (!m_dynamicLibrary)
	{
		result.Succeeded = false;
		result.Message   = "LiveCompile is not initialized.";
		m_state          = ELiveCompileState::Failed;
		return result;
	}

	const File::Path loadablePath = MakeLoadableLibraryPath();
	std::error_code errorCode;
	std::filesystem::create_directories(loadablePath.parent_path(), errorCode);
	std::filesystem::copy_file(
		m_desc.OutputLibraryPath, loadablePath,
		std::filesystem::copy_options::overwrite_existing, errorCode);
	if (errorCode)
	{
		result.Succeeded = false;
		result.Message   = errorCode.message();
		m_state          = ELiveCompileState::Failed;
		return result;
	}

	// ── 스냅샷 → 모듈 파괴 → 로드 → 복원 ──────────────────────────────────
	TakeScriptSnapshot();
	DestroyCurrentModule();
	m_dynamicLibrary->Unload();

	if (false == m_dynamicLibrary->Load(loadablePath.string().c_str()))
	{
		result.Succeeded = false;
		result.Message   = "Failed to load compiled game module.";
		m_state          = ELiveCompileState::Failed;
		return result;
	}

	CreateGameModuleFunc  createGameModule  = reinterpret_cast<CreateGameModuleFunc>(m_dynamicLibrary->GetSymbol("CreateGameModule"));
	m_destroyGameModule = reinterpret_cast<DestroyGameModuleFunc>(m_dynamicLibrary->GetSymbol("DestroyGameModule"));
	if (nullptr == createGameModule || nullptr == m_destroyGameModule)
	{
		result.Succeeded = false;
		result.Message   = "Game module exports were not found.";
		m_state          = ELiveCompileState::Failed;
		return result;
	}

	m_gameModule = createGameModule(&m_hostApi);
	if (nullptr == m_gameModule || false == m_gameModule->Initialize(m_desc.ModuleContext))
	{
		DestroyCurrentModule();
		result.Succeeded = false;
		result.Message   = "Game module initialization failed.";
		m_state          = ELiveCompileState::Failed;
		return result;
	}

	// 새 DLL 로드 + 초기화 완료 → 스냅샷 복원
	RestoreScriptSnapshot();

	result.OutputLibraryPath = loadablePath.generic_string();
	m_state = ELiveCompileState::Loaded;
	return result;
}

IGameModule* CLiveCompileManager::GetGameModule() const
{
	return m_gameModule;
}

ELiveCompileState CLiveCompileManager::GetState() const
{
	return m_state;
}

File::Path CLiveCompileManager::MakeLoadableLibraryPath() const
{
	++const_cast<CLiveCompileManager*>(this)->m_reloadSerial;
	std::filesystem::path intermediate = m_desc.IntermediateDirectory.empty()
		? std::filesystem::path("Intermediate/LiveCompile")
		: std::filesystem::path(m_desc.IntermediateDirectory);
	return File::Path(intermediate / ("GameScript_" + std::to_string(m_reloadSerial) + ".dll"));
}

void CLiveCompileManager::DestroyCurrentModule()
{
	if (CSceneManager* sceneManager = GetModuleSceneManager(m_desc.ModuleContext))
	{
		sceneManager->DestroyScriptInstances();
	}

	if (m_gameModule)
	{
		m_gameModule->Finalize();
		if (m_destroyGameModule)
		{
			m_destroyGameModule(m_gameModule, &m_hostApi);
		}
		m_gameModule = nullptr;
	}
	m_destroyGameModule = nullptr;
}

// ── TakeScriptSnapshot ────────────────────────────────────────────────────────
// DLL 언로드 직전에 활성 씬의 모든 ScriptComponent 필드를 raw bytes 로 저장한다.
// Reflection 정보가 있는 REFLECT_FIELD 만 보존되며, 내부 런타임 상태는 초기화된다.
void CLiveCompileManager::TakeScriptSnapshot()
{
	m_scriptSnapshots.clear();

	CSceneManager* sceneMgr  = GetModuleSceneManager(m_desc.ModuleContext);
	CReflectionRegistry* reg = GetModuleReflection(m_desc.ModuleContext);
	if (!sceneMgr || !reg)
	{
		return;
	}

	SafePtr<CScene> scene = sceneMgr->GetActiveScene();
	if (!scene)
	{
		return;
	}

	scene->ForEach<ScriptComponent>(
		[&](EntityId entity, ScriptComponent& script)
		{
			if (!script.Instance || script.ScriptTypeId == INVALID_TYPE_ID)
			{
				return;
			}

			const ScriptTypeInfo* info = reg->FindScript(script.ScriptTypeId);
			if (!info || info->Properties.empty())
			{
				return;
			}

			ScriptFieldSnapshot snapshot;
			snapshot.Entity   = entity;
			snapshot.TypeName = info->Type.Name ? info->Type.Name : "";

			for (const ReflectPropertyInfo& prop : info->Properties)
			{
				ScriptPendingField field;
				field.Name = prop.Name ? prop.Name : "";
				field.Data.resize(prop.Size);
				const void* src = CReflectionRegistry::GetPropertyAddress(static_cast<const void*>(script.Instance), prop);
				if (nullptr == src)
				{
					continue;
				}
				std::memcpy(field.Data.data(), src, prop.Size);
				snapshot.Fields.push_back(std::move(field));
			}

			m_scriptSnapshots.push_back(std::move(snapshot));
		});
}

// ── RestoreScriptSnapshot ─────────────────────────────────────────────────────
// 새 DLL 이 로드된 후, 이전 스냅샷을 ScriptComponent::PendingFields 에 채운다.
// ScriptSystem 은 다음 프레임에 인스턴스를 생성하면서 PendingFields 를 자동 적용한다.
void CLiveCompileManager::RestoreScriptSnapshot()
{
	if (m_scriptSnapshots.empty())
	{
		return;
	}

	CSceneManager* sceneMgr  = GetModuleSceneManager(m_desc.ModuleContext);
	CReflectionRegistry* reg = GetModuleReflection(m_desc.ModuleContext);
	if (!sceneMgr || !reg)
	{
		m_scriptSnapshots.clear();
		return;
	}

	SafePtr<CScene> scene = sceneMgr->GetActiveScene();
	if (!scene)
	{
		m_scriptSnapshots.clear();
		return;
	}

	for (ScriptFieldSnapshot& snapshot : m_scriptSnapshots)
	{
		ScriptComponent* sc = scene->GetComponent<ScriptComponent>(snapshot.Entity);
		if (!sc)
		{
			continue;
		}

		// 새 DLL 에서 같은 이름으로 타입 재확인
		const ScriptTypeInfo* info = reg->FindScriptByName(snapshot.TypeName.c_str());
		if (info)
		{
			sc->ScriptTypeId = info->Type.Id;
		}
		// 인스턴스는 아직 없음(DestroyCurrentModule 이 ResetInstance 했음)
		// PendingFields 에 채워두면 ScriptSystem 이 다음 프레임에 적용한다.
		sc->PendingFields = std::move(snapshot.Fields);
	}

	m_scriptSnapshots.clear();
}

#else

bool CLiveCompileManager::Initialize(const LiveCompileDesc& desc)
{
	(void)desc;
	return false;
}

void CLiveCompileManager::Finalize() {}

void CLiveCompileManager::Tick(bool scanSourceChanges)
{
	(void)scanSourceChanges;
}

LiveCompileResult CLiveCompileManager::RebuildAndReload()
{
	LiveCompileResult result;
	result.Message = "LiveCompile is editor-only.";
	return result;
}

IGameModule* CLiveCompileManager::GetGameModule() const
{
	return nullptr;
}

ELiveCompileState CLiveCompileManager::GetState() const
{
	return ELiveCompileState::Failed;
}

File::Path CLiveCompileManager::MakeLoadableLibraryPath() const
{
	return File::NULL_PATH;
}

void CLiveCompileManager::DestroyCurrentModule() {}
void CLiveCompileManager::TakeScriptSnapshot()   {}
void CLiveCompileManager::RestoreScriptSnapshot() {}
void CLiveCompileManager::StartAsyncCompile() {}
void CLiveCompileManager::PollAsyncCompile() {}
LiveCompileResult CLiveCompileManager::ApplyCompileResult(LiveCompileResult result)
{
	result.Succeeded = false;
	result.Message   = "LiveCompile is editor-only.";
	m_state          = ELiveCompileState::Failed;
	return result;
}

#endif
