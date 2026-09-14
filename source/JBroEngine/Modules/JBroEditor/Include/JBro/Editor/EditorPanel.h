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

        // 탭에 보이는 이름이자 ImGui 가 창을 식별하는 값이다.
        // **살아 있는 동안 바뀌지 않아야 한다** - 바뀌면 ImGui 가 다른 창으로 보고
        // 도킹 자리와 크기를 잃는다.
        virtual const char* GetTitle() const = 0;

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
