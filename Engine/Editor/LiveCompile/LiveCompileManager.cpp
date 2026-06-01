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
#include "Core/Logging/LoggerInternal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

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

	// 누적된 stamp 폴더 / 핫리로드 DLL 정리.  최근 10 빌드만 유지.
	CleanupOldArtifacts(10);

	m_state = ELiveCompileState::Idle;
	return true;
}

void CLiveCompileManager::CleanupOldArtifacts(int keepMostRecent) const
{
	if (m_desc.IntermediateDirectory.empty()) return;
	if (keepMostRecent < 1) keepMostRecent = 1;

	std::error_code ec;
	const std::filesystem::path root(m_desc.IntermediateDirectory);
	if (false == std::filesystem::exists(root, ec) || ec)
	{
		return;
	}

	auto pruneByMTime = [](std::vector<std::filesystem::path> entries, int keep)
	{
		std::sort(entries.begin(), entries.end(),
			[](const std::filesystem::path& a, const std::filesystem::path& b)
			{
				std::error_code e1, e2;
				const auto ta = std::filesystem::last_write_time(a, e1);
				const auto tb = std::filesystem::last_write_time(b, e2);
				if (e1 || e2) return false;
				return ta > tb;   // 최신순
			});
		for (std::size_t i = static_cast<std::size_t>(keep); i < entries.size(); ++i)
		{
			std::error_code re;
			std::filesystem::remove_all(entries[i], re);
			// 잠금된 파일(현재 로드된 DLL 등) 은 제거 실패 — 무시.
		}
	};

	// 1) IntermediateDirectory 직속 GameScript_<serial>.dll / .pdb / .ilk 등 정리
	{
		std::vector<std::filesystem::path> reloadFiles;
		for (const auto& entry : std::filesystem::directory_iterator(root, ec))
		{
			if (ec) { ec.clear(); break; }
			if (entry.is_regular_file(ec))
			{
				const std::string name = entry.path().filename().generic_string();
				// "GameScript_<숫자>" prefix 검사 — Loadable DLL 패밀리.
				if (name.rfind("GameScript_", 0) == 0)
				{
					reloadFiles.push_back(entry.path());
				}
			}
			ec.clear();
		}
		pruneByMTime(std::move(reloadFiles), keepMostRecent);
	}

	// 2) IntermediateDirectory/Debug, Release 안의 stamp 폴더 정리
	const char* configurations[] = { "Debug", "Release" };
	for (const char* cfg : configurations)
	{
		const std::filesystem::path cfgDir = root / cfg;
		if (false == std::filesystem::exists(cfgDir, ec))
		{
			ec.clear();
			continue;
		}
		std::vector<std::filesystem::path> stampFolders;
		for (const auto& entry : std::filesystem::directory_iterator(cfgDir, ec))
		{
			if (ec) { ec.clear(); break; }
			if (entry.is_directory(ec))
			{
				stampFolders.push_back(entry.path());
			}
			ec.clear();
		}
		pruneByMTime(std::move(stampFolders), keepMostRecent);
	}
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

void CLiveCompileManager::Tick(bool autoRebuildEnabled)
{
	// 진행 중인 비동기 컴파일은 항상 폴링 — 자동 리빌드가 OFF 로 바뀌어도
	// 직전에 시작된 컴파일은 마무리되어야 DLL 이 안전하게 교체된다.
	if (m_pendingCompile.valid())
	{
		PollAsyncCompile();
	}

	// 자동 리빌드 OFF 면 새 변경 감지/빌드 시작을 모두 멈춤.
	// (m_isDirty 는 reset 하지 않는다 — ON 으로 다시 켜면 누적분 처리.
	//  주의: 이 함수는 호출 시점이 매 프레임이라 reset 하면 디바운스가 안 흘러간다.)
	if (false == autoRebuildEnabled)
	{
		return;
	}

	if (m_sourceWatcher)
	{
		m_sourceWatcher->Poll();
		std::vector<FileWatchEvent> events;
		if (m_sourceWatcher->TakeEvents(events))
		{
			m_isDirty    = true;
			m_dirtyTime  = std::chrono::steady_clock::now();
		}
	}

	// 디바운스 경과 + 컴파일 진행 중 아니면 새 빌드 시작
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

	// 빌드 직전 스크립트 프로젝트 재생성(레지스트리/vcxproj ↔ 디스크 동기화).
	if (m_preBuildCallback)
	{
		m_preBuildCallback();
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
	// 빌드 직전 스크립트 프로젝트 재생성 — 헤더 편집(프로퍼티 추가/이름변경/삭제,
	// 외부 파일 추가/삭제)을 항상 레지스트리/vcxproj 에 반영한 뒤 컴파일한다.
	if (m_preBuildCallback)
	{
		m_preBuildCallback();
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
	LiveCompileResult applied = ApplyCompileResult(std::move(result));
	// 자동(파일변경 트리거) 컴파일 실패도 시스템 로그에 명시적으로 남긴다.
	// (수동 RebuildScriptModule 경로는 호출자가 직접 LogLiveCompileFailure 를 호출.)
	if (false == applied.Succeeded)
	{
		CSystemLog::Error("Script auto-compile failed.");
	}
}

// 메인 스레드 전용: 컴파일 결과를 받아 DLL 교체 + 모듈 재로드 + 스냅샷 복원.
// 워커 스레드에서 절대 호출하면 안 됨 (Reflection/SceneManager 접근).
// 함수 끝의 모든 실패 경로에서 같은 처리(상태 = Failed + 메시지 저장)를 보장하기 위한
// 헬퍼.  RAII / NRVO 에 의존하지 않고 명시적으로 호출.
#define MARK_FAILURE() do { \
    m_state              = ELiveCompileState::Failed; \
    m_lastFailureMessage = result.Message; \
} while(0)

LiveCompileResult CLiveCompileManager::ApplyCompileResult(LiveCompileResult result)
{
	if (false == result.Succeeded)
	{
		MARK_FAILURE();
		return result;
	}

	if (!m_dynamicLibrary)
	{
		result.Succeeded = false;
		result.Message   = "LiveCompile is not initialized.";
		MARK_FAILURE();
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
		MARK_FAILURE();
		return result;
	}

	// ── 스냅샷 → 모듈 파괴 → 로드 → 복원 ──────────────────────────────────
	TakeScriptSnapshot();
	DestroyCurrentModule();
	m_dynamicLibrary->Unload();

	// .string() 은 시스템 ANSI(CP949 등) 변환 — 한글 사용자 경로에서 깨진다.
	// wstring() 으로 wide path 를 그대로 LoadLibraryW 에 전달.
	if (false == m_dynamicLibrary->Load(loadablePath.wstring().c_str()))
	{
		result.Succeeded = false;
		result.Message   = "Failed to load compiled game module.";
		MARK_FAILURE();
		return result;
	}

	CreateGameModuleFunc  createGameModule  = reinterpret_cast<CreateGameModuleFunc>(m_dynamicLibrary->GetSymbol("CreateGameModule"));
	m_destroyGameModule = reinterpret_cast<DestroyGameModuleFunc>(m_dynamicLibrary->GetSymbol("DestroyGameModule"));
	if (nullptr == createGameModule || nullptr == m_destroyGameModule)
	{
		result.Succeeded = false;
		result.Message   = "Game module exports were not found.";
		MARK_FAILURE();
		return result;
	}

	m_gameModule = createGameModule(&m_hostApi);
	if (nullptr == m_gameModule || false == m_gameModule->Initialize(m_desc.ModuleContext))
	{
		DestroyCurrentModule();
		result.Succeeded = false;
		result.Message   = "Game module initialization failed.";
		MARK_FAILURE();
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

std::string CLiveCompileManager::ConsumeLastFailureMessage()
{
	std::string out;
	out.swap(m_lastFailureMessage);
	return out;
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
				const void* src = CReflectionRegistry::GetPropertyAddress(static_cast<const void*>(script.Instance), prop);
				if (nullptr == src)
				{
					continue;
				}

				ScriptPendingField field;
				field.Name = prop.Name ? prop.Name : "";
				field.Type = prop.Type;   // 복원 시 ApplyPendingFields 가 타입별로 올바르게 적용하도록.

				if (EReflectPropertyType::AssetGuid == prop.Type)
				{
					// AssetGuid 는 File::Guid(= std::filesystem::path 파생, 내부 포인터 보유) —
					// raw memcpy 금지. 문자열로 보존하고 복원 시 재구성한다.
					field.Text = static_cast<const File::Guid*>(src)->generic_string();
				}
				else if (EReflectPropertyType::Ref == prop.Type)
				{
					// Ref 는 RefBase 의 POD 버퍼 — 문자열로 그대로 읽는다.
					field.Text = static_cast<const RefBase*>(src)->GuidText();
				}
				else if (EReflectPropertyType::String == prop.Type)
				{
					field.Text = *static_cast<const std::string*>(src);
				}
				else
				{
					// trivially-copyable 타입(bool/int/uint/float/Vector2)만 raw bytes 로 보존.
					field.Data.resize(prop.Size);
					std::memcpy(field.Data.data(), src, prop.Size);
				}
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
