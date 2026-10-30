#pragma once

#include <JBro/Types/NameTable.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    // **화면 UI 컴포넌트를 표에 넣는다**(D-362). 트랜스폼·스프라이트·텍스트와 위젯(버튼~탐색 연결)이다 - 2D 프레임워크와 3D 프레임워크가
    // 함께 부르고, 한 프로세스에서 한 번만 실제로 넣는다(표는 같은 이름을 거절한다). 2D 의 나머지(카메라·라이트·물리·오디오 리스너)는
    // `RegisterBuiltinComponentTypes2D` 가 넣는다.
    Bool RegisterScreenUiComponentTypes();

    // 이 타입이 위 묶음의 것인가. 3D 캔버스는 이 타입을 화면 레이어에만 둔다(에디터의 붙이기 목록).
    Bool IsScreenUiComponentType(NameId name);
}
