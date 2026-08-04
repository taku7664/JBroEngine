#pragma once

#include "LoggerTypes.h"

#include <fstream>
#include <mutex>
#include <string_view>
#include <vector>

class CSystemLog;

class CLogger final
{
public:
	CLogger() = default;
	~CLogger() = default;

	CLogger(const CLogger&) = delete;
	CLogger& operator=(const CLogger&) = delete;
	CLogger(CLogger&&) = delete;
	CLogger& operator=(CLogger&&) = delete;

public:
	void Clear();
	void GetEntriesSnapshot(std::vector<LogEntry>& outEntries) const;
	void GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries) const;
	bool SaveToFile(const File::Path& path) const;

	// ── 파일 싱크 ─────────────────────────────────────────────────────────────
	// 출시된 게임에는 콘솔도 디버거도 없다. 이 싱크가 없으면 유저 쪽에서 무슨 일이
	// 있었는지 알 방법이 링버퍼(프로세스와 함께 사라진다)뿐이다.
	//
	// 여는 순간 **지금까지 쌓인 항목을 먼저 쏟고** 이후를 이어서 흘린다 — 제품명을 아는
	// 시점이 부팅보다 늦어서(빌드 매니페스트 로드 후) 그 전 로그가 버려지면 안 되기 때문이다.
	// 기존 파일은 `.prev` 로 한 세대 밀어 둔다. 크래시 후 재실행이 증거를 덮어쓰는 걸 막는다.
	//
	// 줄마다 flush 한다 — 이 파일은 "죽고 난 뒤에 읽는 것"이 목적이라 마지막 줄이 가장
	// 중요하다. 레벨로 나누면 Info 로 남긴 단서를 잃는다.
	bool OpenFile(const File::Path& path);
	void CloseFile();
	bool IsFileOpen() const;
	const File::Path& GetFilePath() const;

	std::size_t GetEntryCount() const;
	std::uint64_t GetRevision() const;

private:
	void Write(ELogSource source, ELogLevel level, std::string_view message);

private:
	friend class Log;
	friend class CSystemLog;

	mutable std::mutex m_mutex;
	std::vector<LogEntry> m_entries;
	std::uint64_t m_nextSequence = 1;
	std::uint64_t m_revision = 0;
	// m_mutex 가 함께 지킨다 — Write 가 링버퍼와 스트림을 한 임계구역에서 다루므로
	// 워커 스레드가 로그를 남겨도 줄이 섞이지 않는다.
	std::ofstream m_file;
	File::Path m_filePath;
};

class Log final
{
public:
	static void Trace(std::string_view message);
	static void Debug(std::string_view message);
	static void Info(std::string_view message);
	static void Warning(std::string_view message);
	static void Error(std::string_view message);
	static void Critical(std::string_view message);

	static void GetEntriesSnapshot(std::vector<LogEntry>& outEntries);
	static void GetEntriesByLevel(ELogLevel level, std::vector<LogEntry>& outEntries);
	static bool SaveToFile(const File::Path& path);
	static SafePtr<CLogger> GetLogger();

	// ── Host logger 주입 ──────────────────────────────────────────────────────
	// GameScript 같은 DLL 은 Engine.lib 을 정적 링크하므로 Engine.Logger static SafePtr
	// 가 dll 안에 별도 인스턴스로 존재 → 호스트(Application)의 Logger 와 분리된다.
	// 호스트가 자신의 CLogger* 를 SetHostLogger 로 주입하면 dll 측 Log::Debug 등의
	// 호출이 호스트 Logger 로 라우팅되어 LogTool 에 즉시 표시된다.
	// nullptr 전달 시 라우팅 해제 (Engine.Logger 또는 fallback 으로 복귀).
	static void SetHostLogger(CLogger* logger);

private:
	static void WriteExternal(ELogLevel level, std::string_view message);
};
