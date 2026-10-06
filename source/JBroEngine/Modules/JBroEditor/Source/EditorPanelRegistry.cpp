#include <JBro/Editor/EditorPanelRegistry.h>

#include <JBro/Editor/LocalizationKeys.h>

#include "Panel/AssetBrowserPanel.h"
#include "Panel/CanvasViewPanel.h"
#include "Panel/EditorSettingsPanel.h"
#include "Panel/GameViewPanel.h"
#include "Panel/HierarchyPanel.h"
#include "Panel/InspectorPanel.h"
#include "Panel/LogPanel.h"
#include "Panel/ProfilerPanel.h"
#include "Panel/ProjectSettingsPanel.h"
#include "Panel/ShortcutPanel.h"
#include "Panel/SpriteViewerPanel.h"
#include "Panel/StatsPanel.h"

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Bool IsEmptyName(const char* name)
        {
            return name == nullptr || name[0] == '\0';
        }
    }

    EditorPanelRegistry::EditorPanelRegistry()
    {
        EditorDockAreaInfo main;
        main.name = MainDockArea;
        main.titleKey = LocKeys::DockMain;
        main.fallbackTitle = "Main";
        m_areas.Add(main);
    }

    EditorPanelRegistry& EditorPanelRegistry::Get()
    {
        static EditorPanelRegistry registry;
        return registry;
    }

    Bool EditorPanelRegistry::RegisterDockArea(const EditorDockAreaInfo& info)
    {
        if (IsEmptyName(info.name) || FindDockArea(info.name) != nullptr)
        {
            return false;
        }
        m_areas.Add(info);
        return true;
    }

    const EditorDockAreaInfo* EditorPanelRegistry::FindDockArea(const char* name) const
    {
        if (IsEmptyName(name))
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_areas.Size(); ++index)
        {
            if (std::strcmp(m_areas[index].name, name) == 0)
            {
                return &m_areas[index];
            }
        }
        return nullptr;
    }

    UInt32 EditorPanelRegistry::GetDockAreaCount() const
    {
        return static_cast<std::uint32_t>(m_areas.Size());
    }

    const EditorDockAreaInfo& EditorPanelRegistry::GetDockAreaAt(UInt32 index) const
    {
        return m_areas[index];
    }

    Bool EditorPanelRegistry::Register(const EditorPanelTypeInfo& info)
    {
        if (IsEmptyName(info.name) || info.Create == nullptr || Find(info.name) != nullptr
            || FindDockArea(info.dockArea) == nullptr)
        {
            return false;
        }
        m_types.Add(info);
        return true;
    }

    const EditorPanelTypeInfo* EditorPanelRegistry::Find(const char* name) const
    {
        if (IsEmptyName(name))
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_types.Size(); ++index)
        {
            if (std::strcmp(m_types[index].name, name) == 0)
            {
                return &m_types[index];
            }
        }
        return nullptr;
    }

    UInt32 EditorPanelRegistry::GetCount() const
    {
        return static_cast<std::uint32_t>(m_types.Size());
    }

    const EditorPanelTypeInfo& EditorPanelRegistry::GetAt(UInt32 index) const
    {
        return m_types[index];
    }

    void RegisterBuiltinEditorPanelTypes()
    {
        static Bool registered = false;
        if (registered)
        {
            return;
        }
        registered = true;
        // **첫 번째가 가운데를 갖는다 - 편집 화면이 거기여야 한다.** 게임 뷰도 같은 칸에
        // 탭으로 들어가지만, 처음 보이는 것은 만드는 화면이다(D-130).
        RegisterEditorPanelType<CanvasViewPanel>(true);
        RegisterEditorPanelType<GameViewPanel>(true);
        RegisterEditorPanelType<HierarchyPanel>(true);
        RegisterEditorPanelType<InspectorPanel>(true);
        RegisterEditorPanelType<AssetBrowserPanel>(true);
        RegisterEditorPanelType<StatsPanel>(true);
        RegisterEditorPanelType<LogPanel>(true);
        RegisterEditorPanelType<ProjectSettingsPanel>(true);
        RegisterEditorPanelType<ProfilerPanel>(true);
        RegisterEditorPanelType<ShortcutPanel>(true);
        RegisterEditorPanelType<EditorSettingsPanel>(true);

        // **파일을 여는 창은 뿌리에 따로 선 도크에 속한다**(D-155·D-284, 기존 `CSpriteViewerDockWindow`). 메인 도크와 나란히
        // 뿌리에 탭으로 서고, 그림마다 비고유 패널이 그 안에 열린다.
        EditorDockAreaInfo viewer;
        viewer.name = SpriteViewerPanel::DockAreaName;
        viewer.titleKey = LocKeys::SpriteViewerTitle;
        viewer.fallbackTitle = "Sprite Viewer";
        EditorPanelRegistry::Get().RegisterDockArea(viewer);
        RegisterEditorPanelType<SpriteViewerPanel>(false, SpriteViewerPanel::DockAreaName);
    }
}
