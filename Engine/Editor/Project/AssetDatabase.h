#pragma once

#include "Core/Asset/AssetTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 비권위적 자산 복구 캐시 한 항목.  .Jmeta 가 사라지거나 자산이 외부 탐색기에서
// 이동/이름변경 되어도 같은 GUID 로 복구하기 위한 "힌트" 데이터.  진실의 원천은
// 디스크의 .Jmeta 이며, 이 DB 는 지워져도 정확성이 깨지지 않는다(복구 편의만 잃음).
struct AssetDbEntry
{
	AssetGuid     Guid;
	std::string   RelativePath;        // Assets 기준 정규화 상대경로 (generic)
	std::uint64_t ContentHash = 0;     // 파일 내용 FNV-1a 64
	std::uint64_t FileSize    = 0;     // 증분 해시 스킵용
	std::int64_t  ModifiedTime = 0;    // file_time_type epoch count (증분 해시 스킵용)
	EAssetType    Type = EAssetType::Unknown;
	std::uint32_t Version = 1;
	std::string   DisplayName;
	std::string   Importer;
	std::string   ImportOptionsYaml;   // 메타 손실 시 임포트 옵션까지 복구
};

// 프로젝트 레벨 자산 복구 인덱스.  Reconcile 패스가 매 로드마다 갱신/저장한다.
class CAssetDatabase final
{
public:
	// dbFilePath 가 없거나 파싱 실패해도 빈 상태로 성공 취급(복구 캐시는 옵션).
	bool Load(const File::Path& dbFilePath);
	bool Save(const File::Path& dbFilePath) const;
	void Clear();

	const AssetDbEntry* FindByGuid(const AssetGuid& guid) const;
	const AssetDbEntry* FindByPath(const std::string& relativePath) const;
	// 같은 해시가 여러 개면(내용 동일 파일) 마지막 upsert 가 이긴다 — 드문 케이스.
	const AssetDbEntry* FindByHash(std::uint64_t contentHash) const;

	void Upsert(const AssetDbEntry& entry);
	void RemoveByGuid(const AssetGuid& guid);

	std::size_t Size() const { return m_byGuid.size(); }

	// ── 콘텐츠 해시 ─────────────────────────────────────────────────────────
	// 파일 전체를 FNV-1a 64 로 해싱. 실패 시 false. outSize/outMtime 채움.
	static bool HashFile(const File::Path& absolutePath, std::uint64_t& outHash, std::uint64_t& outSize, std::int64_t& outMtime);
	// 파일의 (size, mtime) 만 조회 — 증분 해시 판단용.
	static bool QueryStat(const File::Path& absolutePath, std::uint64_t& outSize, std::int64_t& outMtime);

private:
	void RebuildSecondaryIndexes();

private:
	std::unordered_map<AssetGuid, AssetDbEntry> m_byGuid;
	std::unordered_map<std::string, AssetGuid>  m_byPath;
	std::unordered_map<std::uint64_t, AssetGuid> m_byHash;
};
