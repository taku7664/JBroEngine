#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <string_view>
#include <JBro/Types/Table.h>

namespace JBro
{
    class IPlatform;

    // 레지스트리가 아는 에셋 하나다.
    struct AssetRecord
    {
        AssetId id;
        AssetType type = AssetType::Unknown;
        // 에셋 폴더 기준 상대경로. 구분자는 `/` 다. 이미지의 Sprite 레코드는 Texture 와 같은 경로를 갖는다.
        String relativePath;
        // 이미지에서 함께 선 Sprite 레코드가 자기 Texture 를 가리킨다. 그 외에는 비어 있다.
        AssetId owner;
    };

    struct AssetScanOptions
    {
        // 파일 이름 또는 상대경로에 맞는 것을 건너뛴다. `*` 와 `?` 를 받고 대소문자를 가리지 않는다.
        // `.` 으로 시작하는 폴더는 이것과 무관하게 언제나 건너뛴다.
        JArrayView<String> ignorePatterns;
        // 참이면 메타가 없는 파일에 메타를 만든다. **에디터만 참으로 준다** - 게임 실행은 파일을 새로 쓰지 않는다.
        bool createMissingMeta = false;
    };

    struct AssetScanReport
    {
        std::uint32_t registered = 0;
        std::uint32_t metaCreated = 0;
        // 확장자로 타입을 알 수 없어 건너뛴 파일이다.
        std::uint32_t unknownType = 0;
        // 메타는 없고 만들지도 않기로 해서 건너뛴 파일이다.
        std::uint32_t missingMeta = 0;
        // 짝 파일이 없는 메타다. 등록하지 않는다.
        std::uint32_t orphanMeta = 0;
        // 읽히지 않은 메타다. 그 파일은 등록하지 않는다.
        std::uint32_t invalidMeta = 0;
        // 이미 다른 파일이 쓰는 아이디를 든 메타다. 뒤에 온 파일은 등록하지 않는다.
        std::uint32_t duplicateId = 0;
        std::uint32_t ignored = 0;
    };

    // 에셋 메타데이터 보관소다. 로드·캐시 소유는 `AssetSystem` 이 따로 가진다(D-50).
    //
    // **프로젝트를 열 때 한 번 스캔하고, 그 뒤에는 편집 시점에만 바뀐다.** 문자열은 이 층에서만 산다 -
    // 프레임 경로는 `AssetHandle` 만 들고 이 표를 보지 않는다(asset-plan §2.2). 스캔은 에셋 폴더 아래를
    // 전부 돌되 `.` 으로 시작하는 폴더와 무시 패턴은 들어가지 않는다 - 기존 엔진은 그것을 걸러 내지 않아
    // 숨김 폴더의 파일이 에셋으로 등록됐다.
    class AssetRegistry final
    {
    public:
        // `assetRoot` 아래를 스캔해 등록한다. 이전 내용은 비운다. 폴더가 없으면 false 다. 파일은 플랫폼이 연다(D-112).
        bool Scan(IPlatform& platform, const char* assetRoot, const AssetScanOptions& options, AssetScanReport& report);

        // 스캔 없이 하나를 넣는다. 같은 아이디나 같은 (경로, 타입)이 있으면 false 다.
        bool Register(const AssetRecord& record);
        bool Unregister(AssetId id);
        // 파일이 옮겨졌다. 그 경로의 레코드(이미지면 Texture 와 Sprite 둘)의 경로만 바꾼다. 아이디는 그대로다.
        bool Rename(std::string_view oldRelativePath, std::string_view newRelativePath);
        void Clear();

        const AssetRecord* Find(AssetId id) const;
        // 경로로 찾는다. 이미지면 Texture 레코드다.
        const AssetRecord* FindByPath(std::string_view relativePath) const;
        // `Find` 와 같되 값 타입 요약으로 준다. 없으면 false 다.
        bool GetMetadata(AssetId id, AssetMetadata& metadata) const;

        std::size_t GetCount() const;
        const AssetRecord& GetRecord(std::size_t index) const;
        // 내용이 바뀔 때마다 오르는 번호다. 프로세스 안의 모든 레지스트리가 한 줄로 세므로 표를 통째로 바꿔 끼워도
        // 같은 번호가 다시 나오지 않는다. 에디터가 목록을 다시 모을지 이것으로 정한다 - 프레임마다 전부 걷지 않게.
        std::uint64_t GetRevision() const;

        // 파일 이름·상대경로에 대한 무시 패턴 판정이다. 스캔과 파일 감시가 같은 것을 쓴다.
        static bool MatchesIgnorePattern(std::string_view relativePath, JArrayView<String> patterns);

    private:
        // 레코드는 배열에 살고 두 표가 자리를 가리킨다. 지울 때는 끝을 당겨 채우고 그 자리의 표를 고친다.
        void Touch();

        std::uint64_t m_revision = 0;
        Array<AssetRecord> m_records;
        Table<AssetId, std::uint32_t> m_byId;
        Table<String, std::uint32_t> m_byPath;
    };
}
