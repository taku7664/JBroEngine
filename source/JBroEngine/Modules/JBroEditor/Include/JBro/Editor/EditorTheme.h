#pragma once

namespace JBro
{
    // 에디터의 생김새다(D-73).
    //
    // **기존 엔진에서 그대로 옮겼다.** 색 일흔 개와 모서리·탭·트리선 치수는 눈으로
    // 맞춰 가며 깎은 값이고, 비슷하게 새로 고르면 비슷하지 않다. 바꿀 이유가
    // 생기면 그 이유를 여기 적고 바꾼다.
    namespace EditorTheme
    {
        // 색만. ImGui 컨텍스트가 있어야 한다.
        void ApplyColors();
        // 모서리 반지름, 탭, 트리선, 도킹 분리선 같은 치수.
        void ApplyLayout();
        // 글꼴을 얹는다. 없으면 ImGui 기본 글꼴로 남는다 - 실패가 아니다.
        //
        // 기본 글꼴에는 한글이 없다. 이 코드베이스의 주석도 화면에 나올 이름도
        // 한글이라, 글꼴이 없으면 네모가 늘어선다.
        bool ApplyFont();
        // 아이콘 글꼴 파일의 경로(UTF-8). `ApplyFont` 가 본문 글꼴에 합친다. 널이면 합치지
        // 않는다. `Apply` 보다 먼저 부른다.
        void SetIconFontPath(const char* path);
        // 아이콘 글꼴이 합쳐졌는가. 없는 기계에서는 거짓이고 아이콘은 네모다.
        bool HasIconFont();

        // 셋 다.
        void Apply();
    }
}
