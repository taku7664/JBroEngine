#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

#include <cstdint>
#include <type_traits>

namespace JBro
{
    // 패널 종류 하나다(D-284).
    struct EditorPanelTypeInfo
    {
        // 종류 이름이다. 패널의 `GetTitle()` 과 같아야 하고, 번역하지 않으며, 바꾸지 않는다 - 도킹 자리와 단축키 범위의 키다.
        const char* name = nullptr;
        EditorPanelKind kind = EditorPanelKind::Unique;
        // 소속 도크다. 표에 오른 도크여야 한다.
        const char* dockArea = MainDockArea;
        // 에디터 UI 를 켤 때 만들어 두는가. 고유 도구 창은 참이고, 무엇을 열지 정해야 서는 비고유 패널은 거짓이다.
        bool createWithUi = false;
        OwnerPtr<EditorPanel> (*Create)() = nullptr;
    };

    // 뿌리 도크에 붙는 도크 하나다(D-284). 패널은 도크에만 붙고, 도크만 뿌리에 붙는다.
    struct EditorDockAreaInfo
    {
        const char* name = nullptr;
        // 뿌리의 탭에 보이는 이름이다. 로컬라이징 키와 그 키가 없을 때의 글자다.
        const char* titleKey = nullptr;
        const char* fallbackTitle = nullptr;
    };

    // **에디터 패널 종류 표**다(D-284). `ComponentRegistry` 와 같은 모양이다 - 전역 하나이고, 에디터가 켜지기 전에
    // `RegisterEditorPanelType<T>()` 로 모든 종류를 올린다. 열린 적 없는 패널의 종류도 표에는 있다.
    //
    // 오른 차례가 처음 배치의 차례다 - 같은 칸에 붙는 패널은 먼저 오른 것이 앞 탭이다.
    // 메인 도크(`MainDockArea`)는 처음부터 있다. 메인 스레드 전용이다.
    class EditorPanelRegistry final
    {
    public:
        EditorPanelRegistry();
        EditorPanelRegistry(const EditorPanelRegistry&) = delete;
        EditorPanelRegistry& operator=(const EditorPanelRegistry&) = delete;

        static EditorPanelRegistry& Get();

        // 이름이 비었거나 겹치면 거절한다.
        bool RegisterDockArea(const EditorDockAreaInfo& info);
        const EditorDockAreaInfo* FindDockArea(const char* name) const;
        std::uint32_t GetDockAreaCount() const;
        const EditorDockAreaInfo& GetDockAreaAt(std::uint32_t index) const;

        // 이름이 비었거나 겹치거나, 만드는 함수가 없거나, 모르는 도크를 말하면 거절한다.
        bool Register(const EditorPanelTypeInfo& info);
        const EditorPanelTypeInfo* Find(const char* name) const;
        std::uint32_t GetCount() const;
        const EditorPanelTypeInfo& GetAt(std::uint32_t index) const;

    private:
        Array<EditorPanelTypeInfo> m_types;
        Array<EditorDockAreaInfo> m_areas;
    };

    // 패널 종류 하나를 표에 올린다. 이름은 `T::TypeName`, 고유인지는 `T` 가 상속한 쪽(`UniquePanel`·`InstancePanel`)이 정한다.
    // 만드는 함수가 이 자리에서 만들어진다.
    template <typename T>
    bool RegisterEditorPanelType(bool createWithUi, const char* dockArea = MainDockArea)
    {
        constexpr bool unique = std::is_base_of_v<UniquePanel, T>;
        constexpr bool instance = std::is_base_of_v<InstancePanel, T>;
        static_assert(unique != instance, "a panel type derives from exactly one of UniquePanel and InstancePanel");

        EditorPanelTypeInfo info;
        info.name = T::TypeName;
        info.kind = unique ? EditorPanelKind::Unique : EditorPanelKind::Instance;
        info.dockArea = dockArea;
        info.createWithUi = createWithUi;
        info.Create = []() -> OwnerPtr<EditorPanel>
        {
            return MakeOwnerPtr<T>();
        };
        return EditorPanelRegistry::Get().Register(info);
    }

    // 에디터가 가진 패널 종류를 모두 올린다. 몇 번 불러도 한 번만 올린다. 에디터가 켜질 때 부른다.
    void RegisterBuiltinEditorPanelTypes();
}
