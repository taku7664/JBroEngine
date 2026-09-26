#pragma once

#include <JBro/Package/PackageFormat.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

#include <cstddef>
#include <cstdint>

namespace JBro::Package
{
    // 패키지 하나를 열어 둔다(D-232). 파일은 플랫폼의 스트림으로만 연다(D-112) - 기존 엔진은 `std::ifstream` 이었다(package-plan §1.2 K3).
    //
    // 열 때 머리와 색인을 검사한다: 표지·판·크기·색인 해시·레코드마다 블롭이 머리와 색인 사이에 있는지·같은 (id, kind) 가 없는지. 하나라도 틀리면
    // 열지 않는다 - 깨진 패키지를 조용히 쓰지 않는다. 블롭은 읽을 때 해시를 본다. **메인 스레드 전용이다.** 다른 스레드는 `OpenBlobStream` 이 준
    // 제 스트림을 쓴다.
    class PackageReader final
    {
    public:
        bool Open(IPlatform& platform, const char* utf8Path, String& error);
        void Close();
        bool IsOpen() const;

        std::uint32_t GetEntryCount() const;
        const Entry& GetEntry(std::uint32_t index) const;
        const Entry* Find(AssetId id, BlobKind kind) const;
        // 블롭을 풀어 `out` 에 둔다. 해시가 틀리거나 파일을 읽지 못하면 거짓이고 `out` 은 빈다.
        bool ReadBlob(const Entry& entry, Array<std::byte>& out) const;
        // 블롭 하나만 보이는 스트림이다(자기 파일 핸들). 범위 밖은 읽지 않는다. 해시는 보지 않는다 - 흘려 읽는 쪽(디스크 스트리밍 오디오)이 쓴다.
        // 어느 스레드에서 열어도 되고, 연 스레드만 쓴다.
        OwnerPtr<IFileStream> OpenBlobStream(const Entry& entry) const;
        const String& GetPath() const;

    private:
        IPlatform* m_platform = nullptr;
        String m_path;
        OwnerPtr<IFileStream> m_file;
        std::uint64_t m_key = 0;
        Array<Entry> m_entries;
        // 에셋마다 첫 블롭의 번호다. 색인은 (id, kind) 차례라 한 에셋의 블롭이 이어진다.
        Table<AssetId, std::uint32_t> m_firstById;
    };
}
