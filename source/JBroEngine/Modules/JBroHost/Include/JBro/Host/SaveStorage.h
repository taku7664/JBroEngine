#pragma once

#include <JBro/SaveTypes/System/ISaveStorage.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class IPlatform;

    // `ISaveStorage` 의 호스트 구현이다(D-218, 기존 `CSaveStorage`). 엔진이 소유하고 파일은 플랫폼으로만 만진다.
    //
    // 기존 엔진과 다른 것:
    //   - 쓰기는 옆 파일(`<슬롯>.writing`)에 다 쓴 뒤 바꿔 넣는다. 기존은 제자리에 덮어써 쓰는 중에 꺼지면 세이브가 반쯤 남았다.
    //   - 슬롯 이름은 Windows 가 예약한 이름·끝의 점과 공백·제어 문자·`<>:"/\|?*` 까지 거절한다. 기존은 구분자와 `..` 만 막아
    //     `NUL` 에 쓴 세이브가 조용히 사라졌다.
    //   - 게임 DLL 의 컨테이너를 키우지 않는다(인터페이스 주석).
    // 메인 스레드 전용이다.
    class SaveStorage final : public System::ISaveStorage
    {
    public:
        explicit SaveStorage(IPlatform& platform);
        SaveStorage(const SaveStorage&) = delete;
        SaveStorage& operator=(const SaveStorage&) = delete;

        // 저장 폴더를 정한다. 폴더는 처음 쓸 때 만든다. 다시 부르면 뿌리를 옮긴다(에디터가 다른 프로젝트를 연다). 빈 경로면 거짓이다.
        bool Open(const char* folder);
        void Close();
        const String& GetFolder() const;

        // `<앱 데이터>/<제품명>/Saves`(에디터는 `EditorSaves`)다. 제품명은 파일 이름에 쓸 수 있게 다듬고, 비었으면
        // `JBroEngine-Unnamed` 다 - 이름 없이 돌려 본 세이브가 실제 게임의 것과 섞이지 않는다. 앱 데이터 폴더가 없으면 빈 글자다.
        static String MakeFolder(const char* userDataFolder, const char* productName, bool editor);
        static bool IsValidSlotName(const char* slot);

        bool IsReady() const noexcept override;
        bool Write(const char* slot, const void* data, std::size_t size) noexcept override;
        bool GetSize(const char* slot, std::size_t& outSize) const noexcept override;
        bool Read(const char* slot, void* buffer, std::size_t capacity, std::size_t& outSize) const noexcept override;
        bool Exists(const char* slot) const noexcept override;
        bool Remove(const char* slot) noexcept override;
        bool Flush() noexcept override;

    private:
        // 이름을 검사하고 파일 경로를 만든다. 거절하면 빈 글자이고 한 번 경고한다.
        String ResolveSlot(const char* slot) const;

        IPlatform& m_platform;
        String m_folder;
        bool m_ready = false;
    };
}
