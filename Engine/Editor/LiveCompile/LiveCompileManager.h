#pragma once

#include "Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Editor/LiveCompile/ILiveCompileManager.h"
#include "GameFramework/Component/ScriptComponent.h"

#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

// ── ScriptFieldSnapshot ───────────────────────────────────────────────────────
// 핫리로드 직전, 스크립트 인스턴스의 REFLECT_FIELD 값을 보존한다.
struct ScriptFieldSnapshot
{
	EntityId                        Entity;
	std::string                     TypeName; // 재로드 후 이름으로 타입 재탐색
	std::vector<ScriptPendingField> Fields;
};

class CLiveCompileManager final : public ILiveCompileManager
{
public:
	bool Initialize(const LiveCompileDesc& desc) override;
	void Finalize() override;
	void Tick(bool scanSourceChanges) override;
	// 비동기 컴파일 시작. 즉시 반환하며 진행 상태는 GetState() 로 조회.
	// 결과는 Tick() 에서 폴링되어 자동으로 DLL 교체 + 스냅샷 복원이 수행된다.
	// 컴파일 진행 중에 호출되면 무시(요청 큐잉 없음 — 디바운스 메커니즘이 후속 변경을 처리).
	LiveCompileResult RebuildAndReload() override;
	IGameModule* GetGameModule() const override;
	ELiveCompileState GetState() const override;
	std::string ConsumeLastFailureMessage() override;

	// 빌드 직전(매 컴파일 시작 전)에 호출되는 훅. ProjectManager 가 여기에 스크립트
	// 프로젝트 재생성(EnsureProject)을 걸어두면, 헤더를 어떻게 편집하든(프로퍼티
	// 추가/이름변경/삭제, 외부 파일 추가/삭제) 빌드 직전에 레지스트리/vcxproj 가
	// 항상 디스크 상태와 동기화된다.
	void SetPreBuildCallback(std::function<void()> callback) { m_preBuildCallback = std::move(callback); }

private:
	File::Path MakeLoadableLibraryPath() const;
	void DestroyCurrentModule();

	// 오래된 stamp 폴더(IntermediateDirectory/Debug/<stamp>, Release/<stamp>)와
	// IntermediateDirectory 안의 GameScript_<serial>.dll 들을 LastWriteTime
	// 기준으로 정렬해 keepMostRecent 개만 남기고 정리한다.
	// 잠금된(현재 로드중) 파일은 자동으로 skip.
	void CleanupOldArtifacts(int keepMostRecent) const;

	// ── 핫리로드 스크립트 필드 스냅샷 ──────────────────────────────────────
	// DestroyCurrentModule() 호출 전에 찍고, 새 모듈 로드 후 복원한다.
	void TakeScriptSnapshot();
	void RestoreScriptSnapshot();

	// ── 비동기 컴파일 상태머신 ─────────────────────────────────────────────
	// StartAsyncCompile : Compiling 진입, future 보관
	// PollAsyncCompile  : Tick 에서 호출, 완료 시 ApplyCompileResult
	// ApplyCompileResult: 메인 스레드에서 DLL 교체 + 스냅샷 복원 (블로킹 경로와 공유)
	void StartAsyncCompile();
	void PollAsyncCompile();
	LiveCompileResult ApplyCompileResult(LiveCompileResult result);

private:
	LiveCompileDesc             m_desc;
	OwnerPtr<IFileWatcher>      m_sourceWatcher;
	OwnerPtr<ICompilePipeline>  m_compilePipeline;
	OwnerPtr<IDynamicLibrary>   m_dynamicLibrary;
	IGameModule*                m_gameModule        = nullptr;
	DestroyGameModuleFunc       m_destroyGameModule = nullptr;
	GameModuleHostApi           m_hostApi;
	ELiveCompileState           m_state             = ELiveCompileState::Idle;
	bool                        m_isDirty           = false;
	std::chrono::steady_clock::time_point m_dirtyTime;
	std::uint64_t               m_reloadSerial      = 0;

	// 비동기 컴파일용 future. valid() 면 컴파일 진행 중.
	std::future<LiveCompileResult> m_pendingCompile;
	std::chrono::steady_clock::time_point m_compileStartedAt;

	// 핫리로드 간 스크립트 필드 유지용 스냅샷
	std::vector<ScriptFieldSnapshot> m_scriptSnapshots;

	// 마지막 실패 메시지 (소비형 — Consume 호출 시 비워짐).
	std::string m_lastFailureMessage;

	// 빌드 직전 훅(스크립트 프로젝트 재생성 등). 메인 스레드에서 동기 호출.
	std::function<void()> m_preBuildCallback;
};
