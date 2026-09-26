#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

#include <bit>
#include <cstddef>
#include <cstdint>

// 에셋 패키지 `.jpak` 의 모양이다(D-232, package-plan §2.1).
//
//     [머리 64 B] [블롭들 - 16 바이트 정렬, 난독화] [색인 - 끝, 난독화]
//
// 한 에셋에 블롭이 여럿이다(`PackageBlobKind`). 해시는 평문의 64 비트 FNV-1a 이다. 난독화는 파일 안의 절대 위치로 정해지는 열쇠 흐름이라
// 어느 자리에서든 풀 수 있다 - 창 스트림이 앞에서부터 읽지 않아도 된다.
namespace JBro::Package
{
    static_assert(std::endian::native == std::endian::little, "the package format is written little-endian");

    inline constexpr char Magic[8] = { 'J', 'B', 'R', 'O', 'P', 'A', 'K', '\0' };
    inline constexpr std::uint32_t FormatVersion = 1;
    inline constexpr std::uint32_t HeaderSize = 64;
    inline constexpr std::uint64_t BlobAlignment = 16;
    // 색인 레코드의 고정 부분이다. 뒤에 경로 바이트가 온다.
    inline constexpr std::uint32_t RecordFixedSize = 64;
    // 경로는 에셋 폴더 기준 상대경로다. 이보다 긴 경로는 쓰지 않는다(읽을 때도 거절한다).
    inline constexpr std::uint32_t MaxPathBytes = 1024;

    enum class BlobKind : std::uint8_t
    {
        Record = 0,        // 블롭이 없다. 레지스트리의 레코드만 있다(이미지의 Sprite)
        Meta = 1,          // `.jmeta` 원문
        Source = 2,        // 원본 파일 바이트
        CookedTexture = 3, // 디코드한 RGBA8(`JBro::WriteCookedTexture` 의 모양)
        FontAtlas = 4,     // 미리 뜬 글리프 아틀라스(D-232 4 단계)
    };

    inline constexpr std::uint8_t BlobKindCount = 5;

    // 색인의 한 줄이다. 메모리의 모양이고, 파일에는 `RecordFixedSize` 바이트 + 경로로 적는다.
    struct Entry
    {
        AssetId id;
        AssetType type = AssetType::Unknown;
        BlobKind kind = BlobKind::Record;
        AssetId owner;
        std::uint64_t offset = 0; // 파일 안의 절대 자리. `Record` 는 0 이다
        std::uint64_t size = 0;
        std::uint64_t hash = 0;   // 평문의 FNV-1a. `Record` 는 0 이다
        String path;
    };

    // 64 비트 FNV-1a 다. 길이 0 이면 오프셋 기저값이다.
    std::uint64_t Hash(const void* data, std::size_t size) noexcept;

    // 파일 안의 절대 자리 `position` 부터 `size` 바이트를 섞는다(같은 호출이 푼다). 키가 같고 자리가 같으면 같은 바이트가 나온다.
    void Obfuscate(std::uint64_t key, std::uint64_t position, void* data, std::size_t size) noexcept;

    // (id, kind) 의 차례다. 색인은 이 차례로 정렬되어 한 에셋의 블롭이 이어진다.
    bool EntryLess(const Entry& left, const Entry& right) noexcept;
}
