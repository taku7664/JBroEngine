#pragma once

namespace JBro::EditorNames
{
    // **화면에 이름을 내놓는 한 벌**이다(D-180, 기존 `Localization/EditorReflectionLabels`).
    //
    // 타입 이름을 다듬는 세 줄이 인스펙터 안에만 있었고, 오브젝트 메뉴의 `컴포넌트 추가`가
    // 넷째 벌을 쓸 뻔했다. 이름을 보이는 자리는 전부 여기를 지난다.

    // 이름공간을 떼고 타입 이름만 돌려준다(`JBro::Transform2D` → `Transform2D`).
    // **입력 안을 가리킨다** - 복사하지 않으므로 그 글자가 사는 동안만 쓴다.
    //
    // 컴포넌트 이름은 번역하지 않는다(ProjectRule §11.2). 사용자가 스크립트에서 쓰는
    // 이름과 인스펙터에 보이는 이름이 달라지면, 화면에서 본 이름으로는 코드를 찾을 수 없다.
    const char* DisplayTypeName(const char* typeName);

    // 컴포넌트 갈래의 보이는 이름이다. 키는 `component_category.<갈래>` 이고,
    // 번역이 없으면 갈래 이름이 그대로 나온다(기존 `GetCategoryLabel` 과 같은 수).
    //
    // 갈래는 이름과 달리 **번역한다.** 코드에서 부르는 이름이 아니라 목록을 묶어 보여
    // 주는 말이기 때문이다.
    const char* ComponentCategoryLabel(const char* category);
}
