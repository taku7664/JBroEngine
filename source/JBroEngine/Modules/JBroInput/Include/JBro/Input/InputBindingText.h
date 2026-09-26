#pragma once

#include <JBro/InputTypes/InputAction.h>
#include <JBro/Types/String.h>

#include <cstddef>

namespace JBro::System
{
    // 리바인딩을 글자로 쓰고 읽는다(D-218). 모양은 `<JBro/InputTypes/InputRebinding.h>` 의 주석에 있다.
    //
    // **호스트에서 돈다.** 견줄 프로젝트의 표와 고칠 살아 있는 표가 둘 다 입력 시스템(호스트)에 있다. 서비스는 바이트만 나르고,
    // 표를 게임 DLL 로 복사해 오지 않는다.

    // `live` 에서 `project` 와 바인딩이 다른 액션만 적는다. 이름을 모르는 액션(이름표에 없다)은 적지 않는다.
    void WriteBindingOverrides(const InputActionMap& live, const InputActionMap& project, String& out);

    // 글자를 읽어 `live` 의 바인딩을 바꾼다. 줄 하나가 틀리면 그 액션은 그대로 두고 나머지를 읽으며, 거짓을 돌려주고 줄마다 한 번 경고한다.
    // 프로젝트에 없는 액션(지운 액션)은 조용히 건너뛴다 - 옛 세이브가 새 게임에서 열려도 된다.
    bool ReadBindingOverrides(const char* text, std::size_t length, InputActionMap& live);
}
