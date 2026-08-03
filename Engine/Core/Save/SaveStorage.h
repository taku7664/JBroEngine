#pragma once

#include "Core/Save/ISaveStorage.h"
#include "Utillity/File/FilePath.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CSaveStorage — ISaveStorage 의 호스트 구현.
//
//  ⚠ 호스트 전용이다. 게임 스크립트 DLL 은 ISaveStorage 만 본다.
//
//  뿌리 위치:
//    · Windows — `%LOCALAPPDATA%\<제품명>\Saves`
//    · Web     — `/UserData/Saves` (IDBFS 마운트 → Flush 가 IndexedDB 로 넘긴다)
//    · 그 외    — `$HOME/.local/share/<제품명>/Saves`
//    제품명이 비어 있으면 DEFAULT_PRODUCT_NAME 을 쓴다. 에디터에서 제품명을 모른 채로
//    테스트해도 저장이 동작하게 하되, 실제 게임의 세이브와 섞이지 않도록 이름을 구분한다.
// ─────────────────────────────────────────────────────────────────────────────
class CSaveStorage final : public ISaveStorage
{
public:
	// 제품명이 정해지는 시점이 플랫폼마다 다르다(게임=빌드 매니페스트, 에디터=프로젝트).
	// 그래서 생성이 아니라 이 호출로 뿌리를 확정한다. 다시 부르면 뿌리를 옮긴다
	// (에디터에서 프로젝트를 바꿔 여는 경우).
	bool Initialize(const char* productName);
	void Finalize();

	bool IsReady() const override;
	bool WriteBytes(const char* slot, const void* data, std::size_t size) override;
	bool ReadBytes(const char* slot, std::vector<std::uint8_t>& outData) const override;
	bool Exists(const char* slot) const override;
	bool Remove(const char* slot) override;
	bool Flush() override;

	// 진단/도구용 — 저장 뿌리의 실제 경로.
	const File::Path& GetRootPath() const { return m_rootPath; }

private:
	// slot 을 검증하고 실제 파일 경로로 바꾼다. 거부되면 빈 경로를 돌려준다.
	// 거부 사유(빈 이름 / 경로 구분자 / `..` / `:`)는 호출부에서 로그로 남긴다.
	File::Path ResolveSlotPath(const char* slot) const;

	File::Path m_rootPath;
	bool       m_isReady = false;
};
