#pragma once

#include <JBro/Package/PackageFormat.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    // 패키지 한 벌을 메모리에 모아 한 번에 쓴다(D-232). 쓰는 곳은 이것 하나다 - 에디터의 게임 빌드와 (나중의) 명령줄 도구가 같은 것을 부른다
    // (기존 엔진은 C++ 와 빌드 스크립트의 C# 둘이 따로 써서 결과가 달랐다, package-plan §1.2 K1).
    //
    // 키는 빌드마다 뽑아 머리에 둔다. 게임이 읽으려면 키가 함께 가야 하므로 뜯는 사람을 막지 못한다 - 일반 도구로 열리지 않게 할 뿐이다(§2.3).
    class PackageWriter final
    {
    public:
        explicit PackageWriter(UInt64 key);

        // 블롭을 더한다. `Record` 는 `blob` 을 무시한다. 같은 (id, kind) 가 이미 있거나, id 가 비었거나, 경로가 너무 길면 거짓이다.
        Bool Add(const Entry& entry, ArrayView<const std::byte> blob);
        UInt32 GetEntryCount() const;
        // 모은 것을 파일 모양으로 만든다. 부를 때마다 처음부터 다시 만든다.
        void Build(Array<std::byte>& file) const;
        // `Build` 한 것을 플랫폼으로 쓴다. 실패하면 거짓이고 `error` 에 영어 글자다.
        Bool Save(IPlatform& platform, const char* utf8Path, String& error) const;

    private:
        UInt64 m_key = 0;
        Array<Entry> m_entries;
        // 블롭 평문이다. 항목의 `offset` 은 여기 안의 자리이고, `Build` 가 파일 자리로 바꾼다.
        Array<std::byte> m_blobs;
    };
}
