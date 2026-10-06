#pragma once

#include <cstddef>
#include <JBro/Types/Bool.h>

namespace JBro::System
{
    // 게임이 쓸 수 있는 유일한 저장소다(D-218). 서비스가 호스트의 구현에 닿는 인터페이스이고(ProjectRule §10.3), 스크립트는 이것을 보지 않는다 -
    // 프렐류드가 include 하지 않고, 서비스 .cpp 만 `SaveSystemContext` 로 받아 부른다.
    //
    // **인자는 POD 뿐이다.** 기존 엔진은 `std::vector` 를 넘겨 호스트가 게임 DLL 의 벡터를 키웠다 - 두 모듈의 힙이 다르면 깨진다.
    // 여기는 부르는 쪽 버퍼에 읽고, 버퍼의 크기는 `GetSize` 로 먼저 묻는다(서비스가 그 둘을 이어 준다).
    //
    // 슬롯은 파일 하나를 가리키는 납작한 이름이다. 폴더 구분자·`..`·드라이브 표기·Windows 가 예약한 이름(`CON`·`NUL` 따위)·
    // 제어 문자가 든 이름은 거절한다 - 스크립트가 저장소 밖의 파일을 덮는 길을 열지 않는다. 확장자는 자유다(`slot0.yaml`).
    // 메인 스레드 전용이다.
    class ISaveStorage
    {
    public:
        // 저장 폴더를 확보했는가. 거짓이면 아래는 모두 실패한다.
        virtual Bool IsReady() const noexcept = 0;

        // 슬롯을 통째로 쓴다. **다 쓰기 전에는 옛 내용이 그대로다** - 옆 파일에 쓴 뒤 바꿔 넣는다. 기존 엔진은 제자리에 덮어써
        // 쓰는 중에 꺼지면 세이브를 잃었다.
        virtual Bool Write(const char* slot, const void* data, std::size_t size) noexcept = 0;
        // 슬롯의 바이트 수다. 없으면 거짓이다(첫 실행이 흔한 경우라 로그를 남기지 않는다).
        virtual Bool GetSize(const char* slot, std::size_t& outSize) const noexcept = 0;
        // 슬롯을 `buffer` 에 읽는다. `capacity` 보다 크면 읽지 않고 거짓이다.
        virtual Bool Read(const char* slot, void* buffer, std::size_t capacity, std::size_t& outSize) const noexcept = 0;
        virtual Bool Exists(const char* slot) const noexcept = 0;
        // 지운다. 없던 슬롯도 참이다 - 부르는 쪽이 바란 상태이기 때문이다.
        virtual Bool Remove(const char* slot) noexcept = 0;
        // 쓴 것을 영속 저장소로 민다. 데스크톱은 쓰기가 이미 디스크에 닿아 확인뿐이고, 웹은(설 때) IndexedDB 로 넘기는 비싼 일이다.
        // 매 프레임이 아니라 저장한 뒤에 부른다.
        virtual Bool Flush() noexcept = 0;

    protected:
        ~ISaveStorage() = default;
    };
}
