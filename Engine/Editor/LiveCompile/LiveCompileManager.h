#pragma once

#include "Editor/FileSystem/Windows/WindowsFileWatcher.h"
#include "Editor/LiveCompile/ILiveCompileManager.h"
#include "GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/File/FilePath.h" // File::Guid (오브젝트 안정 식별자)

#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

struct ScriptFieldValue
{
	std::string Name;
	EReflectPropertyType Type = EReflectPropertyType::Float;
	std::string Text;
	std::vector<std::uint8_t> Data;
};

// ── ScriptFieldSnapshot ───────────────────────────────────────────────────────
// 핫리로드 직전, 스크립트 인스턴스의 REFLECT_FIELD 값을 보존한다.
struct ScriptFieldSnapshot
{
	std::string                     CanvasName;
	File::Guid                      OwnerGuid; // 오브젝트 안정 식별자(주소 아님 — 리로드 넘어 유효)
	File::Guid                      ComponentGuid;
	std::string                     TypeName;  // 재로드 후 이름으로 타입 재탐색
	std::vector<ScriptFieldValue>   Fields;
	std::size_t                     ComponentIndex = 0;
	bool                            IsEnabled = true;
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
	bool TriggerRebuildIfDirty() override;
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

	// 오래된 stamp 폴더(IntermediateDirectory/Debug/<stamp>, Release/<stamp>), 링커
	// stamp PDB(Debug/GameScript_<stamp>.pdb), IntermediateDirectory 안의
	// GameScript_<serial>.dll 들을 LastWriteTime 기준으로 정렬해 keepMostRecent 개만
	// 남기고 정리한다.
	// 잠금된(현재 로드중) 파일은 자동으로 skip.
	void CleanupOldArtifacts(int keepMostRecent) const;

	// 프로젝트 오픈 시 1회 + 빌드 성공마다 유지할 라이브컴파일 산출물 개수.
	// 최신순 유지이므로 "현재 로드된 DLL/PDB" 는 항상 살아남는다(1 로 줄이지 말 것).
	static constexpr int ARTIFACT_KEEP_COUNT = 10;

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

	// 빌드마다 JBroLiveCompileStamp 를 증가시킨 desc 사본을 만든다.
	// stamp 는 링커 PDB 이름($(IntDir)GameScript_<stamp>.pdb)을 결정한다.
	// 세션당 고정 stamp 를 쓰면 매 빌드가 같은 PDB 를 노려, 직전 빌드로 로드된 DLL 의
	// PDB 를 디버거/mspdbsrv 가 쥐고 있을 때 LNK1201(PDB 못씀)이 난다.
	// stamp 를 N-슬롯 링으로 돌리는 것도 답이 아니다 — 디버거(VS)는 로드된 DLL 의 디버그
	// 디렉터리에 박힌 PDB 를 열고 모듈 언로드 후에도 프로세스 수명 동안 핸들을 놓지 않으므로,
	// "이 세션에서 한 번 쓴 stamp" 는 전부 잠긴 상태다. 링은 첫 한 바퀴째에 무조건 터진다.
	// 따라서 재사용하지 않는 단조 증가 시리얼을 쓰고, 누적되는 PDB 는
	// CleanupOldArtifacts 가 최근 ARTIFACT_KEEP_COUNT 개만 남겨 상한을 잡는다.
	LiveCompileDesc MakeBuildDesc();

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
	std::uint32_t               m_buildStampSerial  = 0;   // 빌드별 stamp (단조 증가, 재사용 금지)

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
