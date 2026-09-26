#pragma once

#include <cstddef>
#include <cstdint>

namespace JBro::System
{
    // 게임 문자열 표의 조회다(D-226). 서비스가 호스트의 구현에 닿는 인터페이스이고(ProjectRule §10.3), 스크립트는 이것을 보지 않는다 -
    // 프렐류드가 include 하지 않고, 서비스 .cpp 만 `LocalizationSystemContext` 로 받아 부른다.
    //
    // **인자는 POD 뿐이다.** 찾은 글자는 호스트 메모리를 가리키는 포인터와 길이로 돌려주고, 서비스가 제 사본(게임 DLL)의 힙으로 복사한다 -
    // 호스트가 게임 DLL 의 컨테이너를 키우지 않는다.
    //
    // 표는 `.jstrings` 에셋이고 로케일은 그 `.jmeta` 의 것이다. 찾는 순서는 지금 로케일의 표 → 폴백 로케일의 표다. 둘 다 없으면 거짓이고,
    // 무엇을 보일지(키 그대로)는 부르는 쪽이 정한다. 메인 스레드 전용이다.
    class ILocalization
    {
    public:
        // 지금 로케일 이름(`ko-KR`)을 `buffer` 에 NUL 로 끝내 쓴다. 돌려주는 값은 NUL 을 뺀 전체 길이다 - `capacity` 보다 크거나 같으면
        // 잘린 것이므로 부르는 쪽이 더 큰 버퍼로 다시 부른다.
        virtual std::size_t GetLocale(char* buffer, std::size_t capacity) const noexcept = 0;
        // 로케일을 바꾼다. 표에 없는 로케일도 받는다(그때는 폴백만 찾힌다). 곧바로 찾는 결과가 바뀌고 판번호가 오른다.
        // 빈 이름이나 null 은 거절한다.
        virtual bool SetLocale(const char* locale) noexcept = 0;
        // 키의 글자다. 찾으면 참이고 `text` 는 호스트 메모리를 가리킨다 - 다음 `SetLocale`·표 재로드까지만 유효하므로 곧바로 복사한다.
        virtual bool Find(const char* key, std::size_t keyLength, const char*& text, std::size_t& textLength) const noexcept = 0;
        // 찾는 결과가 바뀔 때마다 오른다(로케일 변경·표 재로드·표 추가와 삭제). 1 부터다. 텍스트 시스템이 다시 레이아웃할지 이것으로 정한다.
        virtual std::uint32_t GetRevision() const noexcept = 0;

    protected:
        ~ILocalization() = default;
    };
}
