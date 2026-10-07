#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    class EditorApplication;
    class NodeGraphPanel;

    // **노드 인스펙터**다(D-309). 노드 그래프 도크의 오른쪽에 선 고유 패널이고, 마지막으로 포커스를 가진 그래프 창의 고른 노드를
    // 보인다 - 인스펙터가 캔버스의 고른 오브젝트를 보이는 것과 같은 모양이다(사용자 지시: "우측 사이드바는 인스펙터처럼 따로 나눠").
    //
    //   Multiply                    ← 노드 종류 이름(번역하지 않는다)
    //   A        연결됨
    //   B        [1.00][1.00][1.00][1.00]
    //   ── 미리 보기 ──             ← 샘플 도메인 `Math` 의 결과
    //
    // 고치는 것은 그 그래프 창의 커맨드다. 이 창에 포커스가 있어도 실행 취소·저장은 그 그래프 창의 것이다.
    // 그래프에서 노드를 두 번 누르면 이 창이 앞으로 온다.
    class NodeGraphInspectorPanel final : public UniquePanel
    {
    public:
        static constexpr const char* TypeName = "NodeGraphInspector";

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        EditorDock GetPreferredDock() const override;
        Bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;

    private:
        void DrawInputs(NodeGraphPanel& graph);
        void DrawPreview(NodeGraphPanel& graph);

        EditorApplication* m_editor = nullptr;
    };
}
