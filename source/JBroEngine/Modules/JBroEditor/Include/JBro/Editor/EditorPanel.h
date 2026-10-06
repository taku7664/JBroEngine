#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Uuid.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    class EditorApplication;

    // **패널은 고유이거나 비고유다**(D-284). 고유 패널은 종류마다 하나이고 다시 만들면 있던 것이 앞으로 온다 -
    // 계층·인스펙터 같은 도구 창이다. 비고유 패널은 만들 때마다 새로 서고 닫으면 사라진다 - 그림마다 여는 뷰어 같은 것이다.
    enum class EditorPanelKind : std::uint8_t
    {
        Unique,
        Instance
    };

    // 메인 도크의 이름이다. 도구 창이 붙는 안쪽 도크(D-134)이고, 따로 말하지 않은 패널은 여기 소속이다.
    inline constexpr const char* MainDockArea = "Main";

    // 패널이 처음 뜰 때 소속 도크 안의 어디에 붙고 싶은지다. **자리를 아는 것은 패널 자신이고,
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
    //
    // 직접 상속하지 않는다. `UniquePanel` 이나 `InstancePanel` 을 상속한다.
    class EditorPanel
    {
    public:
        virtual ~EditorPanel() = default;
        EditorPanel(const EditorPanel&) = delete;
        EditorPanel& operator=(const EditorPanel&) = delete;

        EditorPanelKind GetKind() const
        {
            return m_kind;
        }
        // 에디터가 들일 때 매긴다. 고유 패널은 종류 이름에서 정해지는 값이라 언제나 같고, 비고유 패널은 들일 때마다 새 값이다.
        // 들이기 전에는 빈 값이다.
        const Uuid& GetId() const
        {
            return m_id;
        }

        // 패널의 **종류 이름**이다. 패널 종류 표(`EditorPanelRegistry`)의 이름이고, `FindPanel`·`FindPanels` 가 찾는 값이며,
        // 단축키 범위다. 고유 패널에게는 ImGui 가 창을 식별하는 값이기도 하다(비고유 패널은 뒤에 UUID 가 붙는다).
        // **살아 있는 동안 바뀌지 않아야 한다** - 바뀌면 ImGui 가 다른 창으로 보고
        // 도킹 자리와 크기를 잃는다. 그러므로 **번역하지 않는다.**
        virtual const char* GetTitle() const = 0;

        // **소속 도크**다(D-284). 모든 패널은 도크 하나에 붙는다 - 뿌리 도크에는 도크만 붙고 패널은 붙지 못한다.
        // 패널이 말하지 않는다. 에디터가 들일 때 패널 종류 표에서 정해 적는다(표에 없는 종류는 메인 도크다).
        const char* GetDockArea() const
        {
            return m_dockArea;
        }

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
        virtual Bool HasCloseButton() const
        {
            return true;
        }

        // 에디터가 들일 때 한 번. 거짓을 돌려주면 패널이 붙지 않는다.
        virtual Bool OnCreate(EditorApplication& editor)
        {
            (void)editor;
            return true;
        }
        // 내보낼 때 한 번. GPU 자원을 놓는 자리다.
        virtual void OnDestroy() {}
        // 매 프레임 그리기 전에. **닫혀 있어도 돈다** - 보이지 않아도 해야 하는
        // 일이 있다(파일 감시, 빌드 진행 같은 것).
        virtual void OnUpdate(Float deltaTime)
        {
            (void)deltaTime;
        }
        // 창 안에서 부른다. `ImGui::Begin` 과 `End` 는 에디터가 부르므로 여기서
        // 다시 열지 않는다.
        virtual void OnDraw() = 0;
        // 창의 메뉴바 안에서 부른다. 에디터가 `BeginMenuBar` 를 열어 두었으므로
        // 여기서 다시 열지 않는다. `HasMenuBar` 가 참일 때만 불린다.
        virtual void OnMenuBar() {}
        virtual Bool HasMenuBar() const
        {
            return false;
        }
        virtual EditorDock GetPreferredDock() const
        {
            return EditorDock::Center;
        }

        // 닫은 패널은 그리지 않지만 파기하지도 않는다 - 다시 열면 그대로 이어진다.
        Bool IsOpen() const
        {
            return m_open;
        }
        void SetOpen(Bool open)
        {
            m_open = open;
        }

        // **이 패널을 앞으로 가져와 달라**(D-178, 기존 `GameView->Focus()`). 탭으로 겹쳐 있으면
        // 열려 있어도 보이지 않는다 - 재생을 눌렀는데 게임 뷰가 뒤에 있으면 아무 일도 일어나지
        // 않은 것처럼 보인다. 닫혀 있으면 함께 연다. 에디터가 다음 프레임에 한 번 쓰고 내린다.
        void RequestFocus()
        {
            m_open = true;
            m_focusRequested = true;
        }
        Bool TakeFocusRequest()
        {
            const Bool requested = m_focusRequested;
            m_focusRequested = false;
            return requested;
        }

        // 이 프레임에 이 패널(또는 그 안의 자식 창)이 키보드 포커스를 가졌는가. 에디터가 창을 열 때마다 적는다 -
        // 그리지 않은 프레임(닫힘·다른 탭에 가림)은 거짓이다. 게임 뷰가 이것으로 게임 입력을 켠다(D-214).
        Bool IsFocused() const
        {
            return m_focused;
        }
        void SetFocused(Bool focused)
        {
            m_focused = focused;
        }
        // 지난 프레임에 내용이 그려졌는가 - 닫혀 있거나 다른 탭 뒤에 가려졌으면 거짓이다. 에디터가 창을 열 때마다 적는다.
        Bool IsVisible() const
        {
            return m_visible;
        }
        void SetVisible(Bool visible)
        {
            m_visible = visible;
        }

    protected:
        explicit EditorPanel(EditorPanelKind kind)
            : m_kind(kind)
        {
        }

    private:
        friend class EditorApplication;

        EditorPanelKind m_kind;
        Uuid m_id;
        const char* m_dockArea = MainDockArea;
        // 메인 도크가 아닌 도크의 패널을 그 도크 공간에 붙였는가. 처음 그릴 때 한 번 붙이고 그 뒤로는 사람이 옮긴 자리를 지킨다.
        Bool m_placed = false;
        Bool m_open = true;
        Bool m_focusRequested = false;
        Bool m_focused = false;
        Bool m_visible = false;
    };

    // **종류마다 하나인 패널**이다(D-284). 다시 만들면 있던 것이 앞으로 오고, 닫으면 숨는다.
    class UniquePanel : public EditorPanel
    {
    protected:
        UniquePanel()
            : EditorPanel(EditorPanelKind::Unique)
        {
        }
    };

    // **만들 때마다 새로 서는 패널**이다(D-284). 닫으면 사라지고, 다음 실행에 되살리지 않는다.
    class InstancePanel : public EditorPanel
    {
    protected:
        InstancePanel()
            : EditorPanel(EditorPanelKind::Instance)
        {
        }
    };
}
