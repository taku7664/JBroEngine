#pragma once

#include <JBro/Core/Core.h>

namespace JBro
{
    class EditorApplication;

    // 패널이 처음 뜰 때 어디에 붙고 싶은지다. **자리를 아는 것은 패널 자신이고,
    // 프레임워크는 패널이 무엇인지 몰라도 된다** - 에디터가 "Inspector 는 오른쪽"
    // 같은 목록을 들고 있으면 패널을 더할 때마다 프레임워크를 고쳐야 한다.
    //
    // 처음 한 번만이다. 그 뒤로는 사용자가 옮긴 자리를 따른다.
    enum class EditorDock : std::uint8_t
    {
        Center,
        Left,
        Right,
        Bottom
    };

    // 에디터 창 하나다(D-70).
    //
    // **훅이 다섯뿐인 이유는 재 봤기 때문이다.** 기존 엔진의 `IImWindow` 는 스물한 개를
    // 두었는데, 그 위에 올린 패널 열세 개가 실제로 override 하는 것은 다섯이었다 —
    // 포커스·렌더·클립의 Enter/Stay/Exit 삼종 세트는 아홉 개가 한 번도 쓰이지 않았다.
    // 필요해지면 그때 넣는다. 안 쓰는 훅을 나중에 지우는 것보다 넣는 것이 쉽다.
    class EditorPanel
    {
    public:
        EditorPanel() = default;
        virtual ~EditorPanel() = default;
        EditorPanel(const EditorPanel&) = delete;
        EditorPanel& operator=(const EditorPanel&) = delete;

        // 패널의 **안정된 이름**이다. ImGui 가 창을 식별하는 값이고, 레지스트리가
        // 중복을 거르는 값이며, `FindPanel` 이 찾는 값이다.
        // **살아 있는 동안 바뀌지 않아야 한다** - 바뀌면 ImGui 가 다른 창으로 보고
        // 도킹 자리와 크기를 잃는다. 그러므로 **번역하지 않는다.**
        virtual const char* GetTitle() const = 0;

        // 탭에 **보이는** 이름이다. 이쪽은 번역한다(ProjectRule §11.2).
        //
        // 둘을 나누는 이유: ImGui 는 창을 이름으로 식별하는데, 보이는 이름을 그대로
        // 쓰면 언어를 바꾸는 순간 모든 창이 처음 보는 창이 되어 배치가 날아간다.
        // 기존 엔진과 같은 수를 쓴다 - `보이는이름###안정된이름` 으로 넘기면
        // ImGui 는 `###` 뒤만 해싱하므로 앞쪽은 마음대로 바뀌어도 된다.
        virtual const char* GetDisplayTitle() const
        {
            return GetTitle();
        }

        // 제목줄에 닫기 단추를 둘 것인가(ProjectRule §11.3).
        //
        // **창마다 고른다.** 기존 엔진도 `IMWINDOW_FLAG_NO_CLOSE_BUTTON` 으로
        // 창이 정하고, 기본은 단추가 있는 쪽이다 - 도크 뿌리창만 그것을 끈다.
        // 닫을 수 없어야 하는 패널이 있고, 모두에 일률적으로 다는 것은 그
        // 구분을 지우는 것이다.
        virtual bool HasCloseButton() const
        {
            return true;
        }

        // 에디터가 들일 때 한 번. 거짓을 돌려주면 패널이 붙지 않는다.
        virtual bool OnCreate(EditorApplication& editor)
        {
            (void)editor;
            return true;
        }
        // 내보낼 때 한 번. GPU 자원을 놓는 자리다.
        virtual void OnDestroy() {}
        // 매 프레임 그리기 전에. **닫혀 있어도 돈다** - 보이지 않아도 해야 하는
        // 일이 있다(파일 감시, 빌드 진행 같은 것).
        virtual void OnUpdate(float deltaTime)
        {
            (void)deltaTime;
        }
        // 창 안에서 부른다. `ImGui::Begin` 과 `End` 는 에디터가 부르므로 여기서
        // 다시 열지 않는다.
        virtual void OnDraw() = 0;
        // 창의 메뉴바 안에서 부른다. 에디터가 `BeginMenuBar` 를 열어 두었으므로
        // 여기서 다시 열지 않는다. `HasMenuBar` 가 참일 때만 불린다.
        virtual void OnMenuBar() {}
        virtual bool HasMenuBar() const
        {
            return false;
        }
        virtual EditorDock GetPreferredDock() const
        {
            return EditorDock::Center;
        }

        // 닫은 패널은 그리지 않지만 파기하지도 않는다 - 다시 열면 그대로 이어진다.
        bool IsOpen() const
        {
            return m_open;
        }
        void SetOpen(bool open)
        {
            m_open = open;
        }

    private:
        bool m_open = true;
    };
}
