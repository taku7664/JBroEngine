#pragma once

#include "Engine/Editor/ImWindow/ImCustomWindow.h"
#include "File/FilePath.h"

#include <array>
#include <string>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CImporterWindowBase ─ 임포터 다이얼로그 윈도우의 공통 베이스
//
//  공통 필드:
//    - 대상 파일 경로 (Source) — 외부 디스크 파일
//    - 에셋 출력 경로 (Destination) — 프로젝트 Assets 폴더 하위
//
//  파생 클래스가 채울 항목:
//    - 헤더(타이틀 로컬라이즈 키)
//    - 임포트 옵션 UI 그리기
//    - 임포트 실행: 대상 파일 → 출력 경로 복사 + .Jmeta 생성
//
//  실제 자산 등록은 ProjectManager 의 AssetWatcher 가 새 파일 감지 시 자동
//  수행 — 임포터는 디스크에 파일과 .Jmeta 만 떨어뜨리면 끝.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class CImporterWindowBase : public CImCustomWindow
{
public:
	using CImCustomWindow::CImCustomWindow;
	virtual ~CImporterWindowBase() = default;

protected:
	void OnCreate() override;
	void OnRenderStay() override;

	// ── 파생 클래스가 구현 ───────────────────────────────────────────────
	// 옵션 UI 영역에서 호출 — FormLayout 등 자유롭게 사용.
	virtual void DrawImportOptions() = 0;

	// 임포트 실행 — 대상 파일을 destPath 로 복사 + .Jmeta 작성.
	// 성공 시 true / 실패 시 false + errorOut 에 메시지.
	virtual bool ExecuteImport(const File::Path& sourcePath,
	                           const File::Path& destFilePath,
	                           std::string& errorOut) = 0;

	// 확장자 필터 — "Source 파일 선택" 다이얼로그 / 검증용. ".png,.jpg" 같은 콤마 구분.
	virtual const char* GetSourceExtensionsCsv() const = 0;

	// 자산 출력 시 사용할 기본 확장자 (원본 그대로 쓸 거면 빈 문자열 반환).
	// Sprite 는 원본(.png) 그대로, Audio 는 원본(.wav/.mp3 등) 그대로.
	virtual const char* GetDefaultDestinationExtension() const { return ""; }

	// 로컬라이즈 키 — 윈도우 타이틀
	virtual const char* GetTitleLocKey() const = 0;

protected:
	// ── 베이스가 그려주는 공통 UI ─────────────────────────────────────────
	// "대상 파일 / 에셋 출력 경로 / 임포트 옵션 / 임포트 버튼" 순서.
	void DrawSourceRow();
	void DrawDestinationRow();
	void DrawImportButton();

protected:
	// 대상 파일 절대 경로 (UTF-8 입력 버퍼).
	std::array<char, 1024> m_sourcePathBuf = {};

	// 출력 경로 — Assets 폴더 기준 상대 경로 (UTF-8 입력 버퍼).
	// 예: "Sounds/bgm.ogg" 또는 "Textures/player.png".
	std::array<char, 1024> m_destPathBuf = {};

	// 마지막 임포트 결과 메시지 (성공/오류 표시용).
	std::string m_lastMessage;
	bool        m_lastMessageIsError = false;
};
