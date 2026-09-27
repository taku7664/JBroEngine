#pragma once

#include <imgui.h>

namespace JBro
{
    class IPlatform;

    // 에디터의 생김새다. 색·치수·글꼴이 모두 여기서 나온다.
    namespace EditorTheme
    {
        // ── 팔레트 ────────────────────────────────────────────────────────
        //
        // **밝기 계층**이다. 깊은 자리에서 얕은 자리로 올라간다. 이 순서가 깨지면
        // 어느 면이 위에 있는지 눈이 읽지 못한다.
        //
        //   Workspace  가장 깊은 작업 공간(메뉴 줄·빈 도킹 노드·선택 안 된 탭)
        //   Panel      패널 바탕
        //   Raised     떠 있는 면(팝업·선택된 탭·표 머리글·컴포넌트 머리)
        //   Input      글자 칸과 단추
        //   Hover      그 위에 마우스가 올라간 상태
        //   Pressed    눌린 상태. Input 보다 어둡다
        constexpr ImVec4 Workspace(0.067f, 0.082f, 0.102f, 1.00f);    // #11151A
        constexpr ImVec4 Panel(0.090f, 0.110f, 0.137f, 1.00f);        // #171C23
        constexpr ImVec4 Raised(0.125f, 0.153f, 0.192f, 1.00f);       // #202731
        constexpr ImVec4 Input(0.145f, 0.180f, 0.224f, 1.00f);        // #252E39
        constexpr ImVec4 Hover(0.188f, 0.231f, 0.286f, 1.00f);        // #303B49
        constexpr ImVec4 Pressed(0.106f, 0.133f, 0.169f, 1.00f);      // #1B222B

        // 탭 띠와 탭이다. **띠·탭·패널이 서로 다른 면이어야 한다** - 도크에 초점이
        // 있을 때 띠(`TitleBgActive`)와 고른 탭(`TabSelected`)이 같은 색이면, 어느
        // 탭이 열려 있는지가 사라진다. 실제로 둘 다 `Raised` 였을 때 그랬다.
        //
        //   TabStrip        탭 띠(초점 없음). 가장 깊다
        //   TabStripActive  탭 띠(초점 있음). 한 단만 올린다
        //   TabIdle         고르지 않은 탭. 띠보다 밝아 탭으로 보인다
        //   TabTop          고른 탭. 패널보다 밝아 위로 올라와 보인다
        constexpr ImVec4 TabStrip(0.067f, 0.082f, 0.102f, 1.00f);     // #11151A
        constexpr ImVec4 TabStripActive(0.086f, 0.110f, 0.141f, 1.00f);  // #161C24
        constexpr ImVec4 TabIdle(0.106f, 0.133f, 0.169f, 1.00f);      // #1B222B
        constexpr ImVec4 TabTop(0.165f, 0.200f, 0.247f, 1.00f);       // #2A333F

        // 선이다. 테두리는 **불투명하다** - 반투명한 테두리는 뒤에 무엇이 오느냐에
        // 따라 두께가 달라 보인다.
        constexpr ImVec4 Line(0.173f, 0.208f, 0.259f, 1.00f);         // #2C3542
        constexpr ImVec4 LineStrong(0.224f, 0.271f, 0.322f, 1.00f);   // #394552

        constexpr ImVec4 Ink(0.894f, 0.910f, 0.933f, 1.00f);          // #E4E8EE
        constexpr ImVec4 InkMuted(0.478f, 0.518f, 0.576f, 1.00f);     // #7A8493

        // **파랑은 상호작용에만 쓴다.** 고름·초점·체크·슬라이더·도킹·키보드 탐색,
        // 그리고 주 동작. 장식으로 쓰면 무엇이 눌리는 것인지 알 수 없게 된다.
        constexpr ImVec4 Accent(0.231f, 0.510f, 0.965f, 1.00f);       // #3B82F6
        constexpr ImVec4 AccentBright(0.357f, 0.608f, 1.000f, 1.00f); // #5B9BFF

        // **호박색은 주의에만 쓴다.** 경고와 저장 안 됨 표시. 체크 표시처럼 늘
        // 켜져 있는 자리에 두면 경고가 경고로 보이지 않는다.
        constexpr ImVec4 Amber(0.878f, 0.639f, 0.235f, 1.00f);        // #E0A33C

        // **빨강은 오류와 되돌릴 수 없는 동작에만 쓴다.**
        constexpr ImVec4 Danger(0.878f, 0.353f, 0.322f, 1.00f);       // #E05A52
        constexpr ImVec4 Success(0.322f, 0.780f, 0.478f, 1.00f);      // #52C77A

        constexpr ImVec4 Clear(0.0f, 0.0f, 0.0f, 0.0f);

        constexpr ImVec4 Fade(const ImVec4& color, float alpha)
        {
            return ImVec4(color.x, color.y, color.z, alpha);
        }

        // 뷰포트 바탕이다. **패널 배경(`WindowBg`)보다 한 단 더 깊다** - 가운데
        // 뷰포트가 가장 깊은 작업 공간으로 읽혀야 한다. 패널을 이만큼 어둡게
        // 내리면 대신 패널 위의 글자 대비가 무너지므로, 이 색은 전역 스타일이
        // 아니라 뷰포트를 그리는 자리에서만 쓴다.
        constexpr ImVec4 ViewportBackground(0.055f, 0.067f, 0.082f, 1.0f); // #0E1115

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
        // 아이콘 글꼴의 경로와, 그 파일을 읽어 줄 플랫폼이다(D-176). 플랫폼이 널이면 아이콘 없이 간다 -
        // 경로를 C 런타임으로 열면 한글이 든 폴더에서 못 찾는다.
        void SetIconFontPath(const char* path, IPlatform* platform);
        // 아이콘 글꼴이 합쳐졌는가. 없는 기계에서는 거짓이고 아이콘은 네모다.
        bool HasIconFont();

        // 셋 다.
        void Apply();
    }
}
