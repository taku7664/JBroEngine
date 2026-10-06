#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorShortcutManager.h>
#include <JBro/Types/Array.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class ComponentBase;
    class ComponentMenuTable;
    class EditorApplication;
    class EditorPanel;
    class GameObject;

    // 행동이 나오는 메뉴다(D-284). 여럿을 겹쳐 적는다.
    namespace EditorActionMenu
    {
        inline constexpr UInt32 None = 0;
        inline constexpr UInt32 File = 1u << 0;
        inline constexpr UInt32 Edit = 1u << 1;
        inline constexpr UInt32 Simulation = 1u << 2;
        // 오브젝트를 우클릭한 메뉴(계층의 줄·캔버스 뷰).
        inline constexpr UInt32 Object = 1u << 3;
        // 빈자리를 우클릭한 메뉴(계층의 바탕·캔버스 뷰의 빈 곳).
        inline constexpr UInt32 Background = 1u << 4;
        // 컴포넌트의 우클릭 메뉴(오브젝트 메뉴의 타입 하위 메뉴·인스펙터 컴포넌트 머리). `componentType` 과 함께 쓴다.
        inline constexpr UInt32 Component = 1u << 5;
    }

    // 행동 하나가 불릴 때 받는 것이다.
    struct EditorActionContext
    {
        EditorApplication* editor = nullptr;
        // 행동이 패널 종류의 것이면 그 종류의 패널이다 - 단축키면 포커스를 가진 것, 메뉴면 그 종류의 맨 앞. 없으면 널이다.
        EditorPanel* panel = nullptr;
        // 오브젝트 메뉴에서 우클릭한 오브젝트다. 널이면 지금 고른 것이 대상이다(편집 메뉴·단축키).
        GameObject* object = nullptr;
        // 컴포넌트 메뉴의 그 인스턴스다. 그리는 동안만 유효하다 - 대상은 주소로 가리킨다.
        ComponentAddress component;
        ComponentBase* componentPointer = nullptr;
        // 우클릭한 자리다(새로 놓는 오브젝트가 쓴다).
        ObjectPlacement placement;
        // 어느 메뉴에서 불렸는가. 단축키·명령 팔레트면 `None` 이다.
        UInt32 menu = EditorActionMenu::None;
    };

    // **행동 하나다**(D-284). 메뉴 항목·단축키·가이드의 행동이 이 한 줄에서 나온다.
    struct EditorActionInfo
    {
        // 행동 이름(`object.copy`)이다. 사용자 키매핑의 저장 키이고 가이드의 `Do:` 다. 번역하지 않고 바꾸지 않는다.
        const char* name = nullptr;
        // 보이는 이름과 무리(단축키 목록·키매핑 설정의 제목). 둘 다 로컬라이징 키다. `fallbackLabel` 은 키가 없을 때의 글자다.
        const char* labelKey = nullptr;
        const char* fallbackLabel = nullptr;
        const char* categoryKey = nullptr;
        // 오브젝트 메뉴에서만 다르게 부르는 이름(`오브젝트 추가` 가 줄에서는 `자식 오브젝트 추가`). 없으면 위의 이름이다.
        const char* objectMenuLabelKey = nullptr;
        const char* objectMenuFallbackLabel = nullptr;
        const char* icon = nullptr;
        // 패널 종류 이름이다. 널이면 전역이다. 있으면 그 종류의 패널에 포커스가 있을 때만 단축키가 돈다.
        const char* panelType = nullptr;
        // 컴포넌트 메뉴의 행동이면 그 컴포넌트 타입이다.
        ComponentTypeId componentType = InvalidComponentTypeId;
        // 나오는 메뉴(`EditorActionMenu`)다. 가이드가 이것으로 어느 메뉴로 갈지 안다.
        UInt32 menus = EditorActionMenu::None;
        // **에디터가 메뉴의 정해진 자리에 직접 놓는가.** 거짓이면 그 메뉴의 확장 칸(공용 항목 뒤)에 등록한 차례로 선다.
        Bool placedByEditor = false;

        EditorShortcutBinding primary;
        EditorShortcutBinding secondary;
        Bool whileTyping = false;
        Bool duringGame = false;
        Bool blocksGlobal = true;

        // 메뉴에 보이는가. 널이면 늘 보인다. 보이지 않는 것은 회색과 다르다 - 영영 켜지지 않는 자리에만 쓴다.
        Bool (*IsShown)(const EditorActionContext& context) = nullptr;
        // 지금 할 수 있는가와 왜 못 하는가(D-181). 널이면 늘 할 수 있다.
        Bool (*CanExecute)(const EditorActionContext& context) = nullptr;
        const char* (*WhyBlocked)(const EditorActionContext& context) = nullptr;
        Bool (*Execute)(EditorActionContext& context) = nullptr;
        // 하고 나면 메뉴의 그 오브젝트가 더 없을 수 있는가(지우기·붙여넣기·부모 해제). 오브젝트 메뉴가 그 프레임에 그만 그린다.
        Bool mayRemoveObject = false;
    };

    // **에디터 행동 표**다(D-284). 패널 종류 표와 같은 전역 하나이고 에디터가 켜지기 전에 모든 행동이 오른다 - 열린 적 없는
    // 패널의 행동도 단축키 목록·키매핑 설정·가이드에 있다. 오른 차례가 확장 칸의 차례다. 메인 스레드 전용이다.
    class EditorActionRegistry final
    {
    public:
        EditorActionRegistry() = default;
        EditorActionRegistry(const EditorActionRegistry&) = delete;
        EditorActionRegistry& operator=(const EditorActionRegistry&) = delete;

        static EditorActionRegistry& Get();

        // 이름이 비었거나 겹치거나, 할 일이 없거나, 모르는 패널 종류를 말하거나, 컴포넌트 메뉴인데 타입이 없으면 거절한다.
        Bool Register(const EditorActionInfo& info);
        // 그 이름의 행동을 뗀다. 뗐으면 참이다. 이미 단축키 관리자에 오른 줄은 남지만 할 일이 행동을 찾지 못해 아무것도 하지 않는다.
        Bool Unregister(const char* name);
        const EditorActionInfo* Find(const char* name) const;
        UInt32 GetCount() const;
        const EditorActionInfo& GetAt(UInt32 index) const;

    private:
        Array<EditorActionInfo> m_actions;
    };

    // 에디터가 가진 행동을 모두 올린다. 패널 종류 표를 먼저 채운다. 몇 번 불러도 한 번만 올린다.
    void RegisterBuiltinEditorActions();

    // 행동 표를 쓰는 길이다. 메뉴·단축키·가이드가 같은 판정을 거친다.
    namespace EditorActionUi
    {
        // `context.panel` 이 비었으면 그 행동의 패널 종류에서 찾아 채운다(포커스를 가진 것, 없으면 맨 앞).
        void ResolvePanel(const EditorActionInfo& action, EditorActionContext& context);
        Bool CanExecute(const EditorActionInfo& action, const EditorActionContext& context);
        // 할 수 있으면 nullptr 이다.
        const char* WhyBlocked(const EditorActionInfo& action, const EditorActionContext& context);
        // 할 수 없으면 하지 않고 거짓이다.
        Bool Execute(const EditorActionInfo& action, EditorActionContext& context);
        // 보이는 이름이다. 오브젝트 메뉴면 그 메뉴의 이름이다.
        const char* Label(const EditorActionInfo& action, UInt32 menu);
        // 지금 첫 조합을 글자로(사용자가 바꿨으면 바꾼 것). 조합이 없으면 빈 글자다.
        EditorShortcutText Keys(const EditorApplication& editor, const EditorActionInfo& action);

        // 이미 열린 메뉴 안에 그 이름의 행동을 그리고, 눌렸으면 한다. 가이드 포커스 표식도 단다. 보이지 않거나 모르는 이름이면
        // 그리지 않는다. 눌러서 했으면 참이다. `label` 을 주면 표의 이름 대신 쓴다(재생/정지처럼 상태로 바뀌는 이름).
        Bool DrawItem(const char* name, EditorActionContext& context, const char* label = nullptr, const char* icon = nullptr);
        // **확장 칸**이다. 그 메뉴에 나오는 행동 중 에디터가 자리를 정하지 않은 것을 오른 차례로 그린다. 하나라도 그리면 앞에
        // 구분선을 넣는다. 그린 것 중 하나가 오브젝트를 없앴을 수 있으면 거짓이다.
        Bool DrawExtensions(UInt32 menu, EditorActionContext& context);

        // 행동을 모두 단축키 관리자에 올린다(기본 조합이 없는 것도 - 사용자가 키를 줄 수 있다). 에디터가 켜질 때 한 번.
        void RegisterShortcuts(EditorShortcutManager& shortcuts);
        // 컴포넌트 메뉴의 행동을 컴포넌트 메뉴 표에 올린다. 에디터가 켜질 때 한 번.
        void RegisterComponentMenus(ComponentMenuTable& menus);
    }
}
