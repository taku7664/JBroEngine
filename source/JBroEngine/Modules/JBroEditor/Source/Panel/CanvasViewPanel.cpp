#include "CanvasViewPanel.h"

#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Asset/Asset.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/System/Text3DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace JBro
{
    namespace
    {
        // 화면 세로 절반이 담는 월드 길이의 한계다. 아래로는 한 화면이 2 cm, 위로는
        // 한 화면이 2 km 쯤 된다. 끝을 두지 않으면 휠 한 번에 0 이나 무한으로 간다.
        constexpr float MinOrthographicSize = 0.01f;
        constexpr float MaxOrthographicSize = 1000.0f;
        // 휠 한 칸의 배율. 1.1 은 열 칸에 약 2.6 배라 손에 붙는다.
        constexpr float ZoomStep = 1.1f;
        // 트랜스폼만 있는(그림이 없는) 오브젝트가 화면에서 차지하는 크기다. 이것이 없으면
        // 빈 오브젝트를 클릭으로 고를 수 없다.
        constexpr float EmptyObjectHalfSize = 0.25f;
        // 격자의 칸이 화면에서 이보다 촘촘해지면 한 단계 굵은 칸으로 넘어간다.
        constexpr float MinGridPixels = 8.0f;
        // 3D 궤도 카메라의 한계. 세로 각은 수직을 넘지 않는다 - 넘으면 화면이 뒤집힌다.
        constexpr float MinPitchDegrees = -89.0f;
        constexpr float MaxPitchDegrees = 89.0f;
        constexpr float MinDistance = 0.1f;
        constexpr float MaxDistance = 5000.0f;
        // 끈 픽셀 하나가 도는 각(도).
        constexpr float OrbitDegreesPerPixel = 0.4f;

        // 이 배율에서 쓸 격자 간격(월드 단위). 1·2·5·10·20·50 … 으로 올라간다.
        float ChooseGridStep(float worldPerPixel)
        {
            float step = 0.001f;
            while (step / worldPerPixel < MinGridPixels)
            {
                // 1 → 2 → 5 → 10 의 되풀이다. 열 배마다 같은 모양이 돌아온다.
                const float decade = std::pow(10.0f, std::floor(std::log10(step) + 0.5f));
                const float ratio = step / decade;
                if (ratio < 1.5f)
                {
                    step = decade * 2.0f;
                }
                else if (ratio < 3.5f)
                {
                    step = decade * 5.0f;
                }
                else
                {
                    step = decade * 10.0f;
                }
                if (step > 1.0e6f)
                {
                    break;
                }
            }
            return step;
        }
    }

    const char* CanvasViewPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체와 도킹 자리가 여기 달려 있다.
        return "CanvasView";
    }

    const char* CanvasViewPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelCanvasView, "Canvas");
    }

    bool CanvasViewPanel::Is3D() const
    {
        return m_editor != nullptr && m_editor->GetFrameworkKind() == FrameworkKind::Framework3D;
    }

    bool CanvasViewPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // **보던 자리에서 이어 본다**(D-146). 프로젝트 파일에 적힌 값이 있으면 그것으로 시작한다.
        float centerX = 0.0f;
        float centerY = 0.0f;
        float size = 0.0f;
        editor.GetSessionCamera(centerX, centerY, size);
        if (size > 0.0f)
        {
            // `size` 가 0 이면 적힌 적이 없다는 뜻이다 - 화면 세로 절반이 담는 월드 길이라
            // 0 일 수 없고, 0 을 그대로 쓰면 아무것도 보이지 않는 배율이 된다.
            SetCamera(centerX, centerY, size);
        }
        // 폴리곤 포인트 편집을 콜라이더의 우클릭 메뉴에서도 켠다. 콜라이더가 여럿이면 누른 것을 고친다.
        editor.GetComponentMenus().Register(MakeStableTypeId(Component::Collider2D::StaticTypeName()),
            &CanvasViewPanel::DrawEditPointsItem, this, this);
        // **기즈모 모드 단축키는 이 패널에 포커스가 있을 때만 돈다**(D-228). 기본 조합은 기존 기즈모와 같은 W·E·R 이다.
        // 조합키 없는 글자라 글자 칸에 타자를 치는 중에는 돌지 않는다(`whileTyping` 기본 거짓).
        struct Row
        {
            const char* id;
            const char* labelKey;
            ImGuiKey key;
            GizmoMode mode;
        };
        const Row rows[] = {
            {"canvas_view.gizmo_translate", LocKeys::GizmoTranslate, ImGuiKey_W, GizmoMode::Translate},
            {"canvas_view.gizmo_rotate", LocKeys::GizmoRotate, ImGuiKey_E, GizmoMode::Rotate},
            {"canvas_view.gizmo_scale", LocKeys::GizmoScale, ImGuiKey_R, GizmoMode::Scale},
        };
        for (std::size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index)
        {
            EditorShortcutDesc desc;
            desc.id = rows[index].id;
            desc.labelKey = rows[index].labelKey;
            desc.categoryKey = LocKeys::PanelCanvasView;
            desc.scope = GetTitle();
            desc.primary.key = rows[index].key;
            desc.handler = MakeOwnerPtr<GizmoModeShortcut>(*this, rows[index].mode);
            m_shortcuts[index] = editor.GetShortcuts().Register(std::move(desc));
        }
        return true;
    }

    void CanvasViewPanel::OnDestroy()
    {
        if (m_editor != nullptr)
        {
            m_editor->GetComponentMenus().Unregister(this);
            for (ShortcutHandle& handle : m_shortcuts)
            {
                m_editor->GetShortcuts().Unregister(handle);
                handle = InvalidShortcutHandle;
            }
        }
    }

    CanvasViewPanel::GizmoModeShortcut::GizmoModeShortcut(CanvasViewPanel& panel, GizmoMode mode)
        : m_panel(panel), m_mode(mode)
    {
    }

    bool CanvasViewPanel::GizmoModeShortcut::Execute(EditorApplication& editor)
    {
        (void)editor;
        m_panel.m_gizmoMode = m_mode;
        return true;
    }

    bool CanvasViewPanel::DrawEditPointsItem(const ComponentMenuContext& context)
    {
        CanvasViewPanel* panel = static_cast<CanvasViewPanel*>(context.user);
        const auto* collider = static_cast<const Component::Collider2D*>(context.component);
        // 폴리곤이 아니면 회색이다. 숨기면 이 기능이 있는지 알 수 없다(D-181).
        const bool polygon = collider != nullptr && PolygonEditModel::EditsPoints(*collider);
        if (Widget::MenuItem(Loc::TextOr(LocKeys::CanvasViewEditPoints, "Edit Points"), nullptr, polygon,
                Loc::TextOr(LocKeys::CanvasViewEditPointsNotPolygon, "shape must be Polygon")))
        {
            panel->m_editCollider = true;
            panel->m_pointTarget = context.address;
        }
        // 값도 슬롯도 바꾸지 않는다. 편집 도구를 켤 뿐이다.
        return true;
    }

    void CanvasViewPanel::SetCamera(float centerX, float centerY, float orthographicSize)
    {
        if (false == std::isfinite(centerX) || false == std::isfinite(centerY)
            || false == std::isfinite(orthographicSize) || orthographicSize <= 0.0f)
        {
            return;
        }
        m_centerX = centerX;
        m_centerY = centerY;
        m_orthographicSize = std::clamp(orthographicSize, MinOrthographicSize, MaxOrthographicSize);
    }

    void CanvasViewPanel::WorldToScreen(const ViewRect& rect, float worldX, float worldY,
        float& screenX, float& screenY) const
    {
        // **엔진이 그린 화면의 크기로 센다**(D-150). 패널 크기로 세면 월드 원점이 패널
        // 한가운데라고 여기게 되는데, 실제로는 텍스처 한가운데다.
        const float drawWidth = rect.drawWidth > 0.0f ? rect.drawWidth : rect.width;
        const float drawHeight = rect.drawHeight > 0.0f ? rect.drawHeight : rect.height;
        const float halfHeight = m_orthographicSize;
        const float halfWidth = drawHeight > 0.0f
            ? halfHeight * drawWidth / drawHeight
            : halfHeight;
        // 월드는 위가 +y, 화면은 아래가 +y 다.
        screenX = rect.left + (worldX - m_centerX) / halfWidth * drawWidth * 0.5f + drawWidth * 0.5f;
        screenY = rect.top - (worldY - m_centerY) / halfHeight * drawHeight * 0.5f + drawHeight * 0.5f;
    }

    void CanvasViewPanel::ScreenToWorld(const ViewRect& rect, float screenX, float screenY,
        float& worldX, float& worldY) const
    {
        // `WorldToScreen` 의 거꾸로다. 같은 크기로 세지 않으면 누른 자리와 잡히는 자리가 갈린다.
        const float drawWidth = rect.drawWidth > 0.0f ? rect.drawWidth : rect.width;
        const float drawHeight = rect.drawHeight > 0.0f ? rect.drawHeight : rect.height;
        const float halfHeight = m_orthographicSize;
        const float halfWidth = drawHeight > 0.0f
            ? halfHeight * drawWidth / drawHeight
            : halfHeight;
        if (drawWidth <= 0.0f || drawHeight <= 0.0f)
        {
            worldX = m_centerX;
            worldY = m_centerY;
            return;
        }
        worldX = m_centerX
            + (screenX - rect.left - drawWidth * 0.5f) / (drawWidth * 0.5f) * halfWidth;
        worldY = m_centerY
            - (screenY - rect.top - drawHeight * 0.5f) / (drawHeight * 0.5f) * halfHeight;
    }

    void CanvasViewPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        DrawToolBar();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x <= 1.0f || available.y <= 1.0f)
        {
            return;
        }

        // **패널 크기가 곧 편집 화면의 크기다.** 게임 화면과 반대다 - 게임은 해상도가
        // 정해진 그림이라 늘려 붙이지만, 편집 화면은 이 자리가 화면이다.
        const Extent2D wanted{
            static_cast<std::uint32_t>(available.x),
            static_cast<std::uint32_t>(available.y)};
        if (Is3D())
        {
            m_editor->RequestCanvasView3D(wanted, m_centerX, m_centerY, m_centerZ,
                m_distance, m_yawDegrees, m_pitchDegrees);
        }
        else
        {
            m_editor->RequestCanvasView(wanted, m_centerX, m_centerY, m_orthographicSize);
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ViewRect rect;
        rect.left = origin.x;
        rect.top = origin.y;
        rect.width = available.x;
        rect.height = available.y;
        // 엔진이 그린 화면의 크기다(D-150). 아직 텍스처가 없으면 패널 크기로 둔다 -
        // 첫 프레임에는 그림도 없어 어긋날 것이 없다.
        const Extent2D drawn = m_editor->GetCanvasViewExtent();
        rect.drawWidth = drawn.width != 0 ? static_cast<float>(drawn.width) : available.x;
        rect.drawHeight = drawn.height != 0 ? static_cast<float>(drawn.height) : available.y;
        m_lastRect = rect;
        m_hasLastRect = false == Is3D();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const TextureHandle texture = m_editor->GetCanvasViewTexture();
        const Extent2D extent = m_editor->GetCanvasViewExtent();
        if (texture.IsValid() && extent.width != 0 && extent.height != 0)
        {
            // 텍스처는 요청보다 크다(64 의 배수로 올려 잡았다). 왼쪽 위에서 패널 크기만큼만
            // 잘라 쓴다 - 늘려 붙이면 같은 장면이 미세하게 찌그러진다.
            const float u = available.x / static_cast<float>(extent.width);
            const float v = available.y / static_cast<float>(extent.height);
            draw->AddImage(
                static_cast<ImTextureID>(EditorUI::ToTextureId(texture)),
                ImVec2(rect.left, rect.top),
                ImVec2(rect.left + rect.width, rect.top + rect.height),
                ImVec2(0.0f, 0.0f), ImVec2(u, v));
        }
        else
        {
            draw->AddRectFilled(
                ImVec2(rect.left, rect.top),
                ImVec2(rect.left + rect.width, rect.top + rect.height),
                IM_COL32(20, 21, 26, 255));
        }

        // 입력을 받는 자리다. 그림 위 어디를 눌러도 이 창이 받는다.
        //
        // **뒤에 오는 것에 자리를 내준다.** 기즈모 손잡이는 이 단추 위에 그려지는데,
        // 겹침을 허락하지 않으면 손잡이를 눌러도 이 단추가 먼저 잡아 끌기가 시작되지 않는다.
        ImGui::SetNextItemAllowOverlap();
        Widget::HitArea("##canvas", available,
            ImGuiButtonFlags_MouseButtonLeft
                | ImGuiButtonFlags_MouseButtonRight
                | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();

        draw->PushClipRect(
            ImVec2(rect.left, rect.top),
            ImVec2(rect.left + rect.width, rect.top + rect.height), true);
        // **격자와 선택 테두리는 차원마다 다른 길로 간다.** 2D 는 화면과 월드가 나눗셈
        // 하나로 이어지지만, 3D 는 기즈모와 **같은 투영**을 거쳐야 한다(D-140).
        if (false == Is3D())
        {
            if (m_showGrid)
            {
                DrawGrid(rect);
            }
            if (m_showColliders)
            {
                DrawColliders(rect);
                DrawJoints(rect);
            }
            DrawPolygonEditor(rect);
            DrawSelectionOutlines(rect);
            DrawOverlay(rect);
        }
        else
        {
            if (m_showGrid)
            {
                DrawGrid3D(rect);
            }
            DrawSelectionMarkers3D(rect);
        }
        draw->PopClipRect();

        HandleCameraInput(rect, hovered);
        DrawGizmo(rect);
        if (Is3D())
        {
            HandlePicking3D(rect, hovered);
        }
        else
        {
            HandleBoxSelect(rect, hovered);
            HandlePicking(rect, hovered);
        }
        DrawContextMenu(rect);
    }

    void CanvasViewPanel::DrawPreviewLocale()
    {
        // **미리 볼 언어**(D-226). 프로젝트에 게임 언어가 있을 때만 보인다. 고르면 엔진의 로케일이 바뀌어 `textKey` 텍스트가
        // 그 언어의 표로 다시 그려진다 - 저장하지 않는 보기 설정이다.
        const Array<String>& locales = m_editor->GetProjectFile().locales;
        if (locales.IsEmpty())
        {
            return;
        }
        constexpr std::size_t MaxLocales = 32;
        const char* names[MaxLocales] = {};
        const String current = m_editor->GetPreviewLocale();
        int chosen = -1;
        const std::size_t count = std::min(locales.Size(), MaxLocales);
        for (std::size_t index = 0; index < count; ++index)
        {
            names[index] = locales[index].c_str();
            if (locales[index] == current)
            {
                chosen = static_cast<int>(index);
            }
        }
        Widget::ToolBarSeparator();
        if (Widget::FilterCombo("##previewLocale", ArrayView<const char* const>(names, count), chosen)
                .ShowFilter(false)
                .EmptyText(Loc::TextOr(LocKeys::CanvasViewPreviewLocale, "Language"))
                .Width(96.0f)
                .Draw()
            && chosen >= 0)
        {
            m_editor->SetPreviewLocale(names[chosen]);
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::CanvasViewPreviewLocaleTooltip,
            "preview texts with a textKey in this language"));
    }

    void CanvasViewPanel::DrawToolBar()
    {
        Widget::GizmoModeBar(m_gizmoMode,
            Loc::TextOr(LocKeys::GizmoTranslate, "Move"),
            Loc::TextOr(LocKeys::GizmoRotate, "Rotate"),
            Loc::TextOr(LocKeys::GizmoScale, "Scale"));
        // **로컬·월드**(D-171, 기존 기즈모의 `L`/`W`). 크기 모드에서는 쓰지 않으므로 잠근다 -
        // 눌러도 아무 일이 없으면 고장과 구분되지 않는다.
        ImGui::SameLine(0.0f, 6.0f);
        {
            const bool scaling = m_gizmoMode == GizmoMode::Scale;
            if (scaling)
            {
                ImGui::BeginDisabled();
            }
            const bool world = m_gizmoSpace == GizmoSpace::World;
            if (Widget::Button(world
                    ? Loc::TextOr(LocKeys::GizmoSpaceWorld, "World")
                    : Loc::TextOr(LocKeys::GizmoSpaceLocal, "Local")))
            {
                m_gizmoSpace = world ? GizmoSpace::Local : GizmoSpace::World;
            }
            if (scaling)
            {
                ImGui::EndDisabled();
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::GizmoSpaceTooltip,
                "put the handles on the object's axes or on the world's; scaling always uses the object's"));
        }
        // 기즈모 모드와 보기 단추는 **다른 무리**다. 사이를 띄우고 줄을 그어 가른다 -
        // 붙여 두면 `크기` 와 `격자` 가 한 낱말처럼 읽힌다.
        Widget::ToolBarSeparator();
        if (Widget::Button(Loc::TextOr(LocKeys::CanvasViewGrid, "Grid")))
        {
            m_showGrid = false == m_showGrid;
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::CanvasViewGridTooltip, "show or hide the grid"));
        if (false == Is3D())
        {
            // 3D 에는 그릴 콜라이더가 없다. 누를 수 없는 단추를 두면 무엇이 되는 것인지 흐려진다.
            ImGui::SameLine(0.0f, 6.0f);
            if (Widget::Button(Loc::TextOr(LocKeys::CanvasViewColliders, "Colliders")))
            {
                m_showColliders = false == m_showColliders;
            }
            Widget::HoveredTooltip(
                Loc::TextOr(LocKeys::CanvasViewCollidersTooltip, "show or hide collider shapes"));
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::Button(Loc::TextOr(LocKeys::CanvasViewFrame, "Frame")))
        {
            FrameSelection();
        }
        Widget::HoveredTooltip(
            Loc::TextOr(LocKeys::CanvasViewFrameTooltip, "fit the view to the selection"));
        DrawPreviewLocale();
        if (false == Is3D())
        {
            // **눈금을 픽셀로도 읽는다**(D-184, 기존 `단위: Unit`/`단위: Pixel` 토글).
            // 3D 에는 픽셀로 읽을 자가 없다 - 원근에서는 한 유닛이 거리마다 다른 픽셀이다.
            ImGui::SameLine(0.0f, 6.0f);
            if (Widget::Button(m_rulerInPixels
                    ? Loc::TextOr(LocKeys::CanvasViewUnitPixel, "Pixel")
                    : Loc::TextOr(LocKeys::CanvasViewUnitWorld, "Unit")))
            {
                m_rulerInPixels = false == m_rulerInPixels;
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::CanvasViewUnitTooltip,
                "read the ruler in world units or in pixels"));
            // 보기 단추가 아니라 **고치는 도구**다. 무리를 가르고 맨 끝에 둔다 - 앞의 단추들 자리를 밀지 않는다.
            Widget::ToolBarSeparator();
            if (Widget::Button(Loc::TextOr(LocKeys::CanvasViewEditCollider, "Edit Collider")))
            {
                m_editCollider = false == m_editCollider;
                // 끄면 메뉴로 고른 콜라이더도 잊는다. 다시 켜면 첫 폴리곤부터다.
                m_pointTarget = {};
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::CanvasViewEditColliderTooltip,
                "edit the selected object's polygon collider"));
        }
    }

    void CanvasViewPanel::HandleCameraInput(const ViewRect& rect, bool hovered)
    {
        const ImGuiIO& io = ImGui::GetIO();
        // **입력 자리의 hover 로는 잴 수 없다**(D-179). 기즈모 손잡이가 그것을 가리므로,
        // 오브젝트의 한가운데에 마우스를 두면 **휠이 먹지 않고 화면도 끌리지 않았다**.
        (void)hovered;
        const bool pointerHere = PointerInView(rect);

        // **끌던 것은 마우스가 밖으로 나가도 이어진다.** 화면 가장자리까지 옮기려면
        // 그 바깥으로 나가게 되는데, 거기서 멈추면 끌기가 끊어진다.
        if (m_panning)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)
                || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f)
                {
                    m_panMoved = true;
                }
                if (Is3D())
                {
                    // **3D 는 돈다.** 평면을 밀고 당기는 것으로는 뒤를 볼 수 없다.
                    m_yawDegrees -= delta.x * OrbitDegreesPerPixel;
                    m_pitchDegrees = std::clamp(
                        m_pitchDegrees - delta.y * OrbitDegreesPerPixel,
                        MinPitchDegrees, MaxPitchDegrees);
                }
                else
                {
                    // 끈 픽셀을 월드 길이로 바꾼다. **그린 화면의 크기로 센다**(D-150) -
                    // 다른 크기로 세면 끈 만큼 움직이지 않아 그림이 손을 따라오지 않는다.
                    const float drawWidth = rect.drawWidth > 0.0f ? rect.drawWidth : rect.width;
                    const float drawHeight = rect.drawHeight > 0.0f ? rect.drawHeight : rect.height;
                    const float halfHeight = m_orthographicSize;
                    const float halfWidth = drawHeight > 0.0f
                        ? halfHeight * drawWidth / drawHeight
                        : halfHeight;
                    if (drawWidth > 0.0f && drawHeight > 0.0f)
                    {
                        m_centerX -= delta.x / (drawWidth * 0.5f) * halfWidth;
                        m_centerY += delta.y / (drawHeight * 0.5f) * halfHeight;
                    }
                }
            }
            else
            {
                m_panning = false;
            }
        }
        else if (pointerHere
            && (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
                || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            m_panning = true;
            m_panMoved = false;
        }

        if (false == pointerHere || io.MouseWheel == 0.0f)
        {
            return;
        }
        if (Is3D())
        {
            // 3D 의 줌은 **가까이 가는 것**이다. 바라보는 점은 그대로 두고 거리만 준다.
            m_distance = std::clamp(
                m_distance * std::pow(ZoomStep, -io.MouseWheel), MinDistance, MaxDistance);
            return;
        }
        // **마우스 아래의 월드 점을 붙잡고 줌한다.** 가운데를 기준으로 줌하면 보던 것이
        // 화면 밖으로 밀려나 다시 찾아가야 한다.
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        ScreenToWorld(rect, io.MousePos.x, io.MousePos.y, anchorX, anchorY);
        const float factor = std::pow(ZoomStep, -io.MouseWheel);
        const float next = std::clamp(
            m_orthographicSize * factor, MinOrthographicSize, MaxOrthographicSize);
        const float applied = next / m_orthographicSize;
        m_orthographicSize = next;
        m_centerX = anchorX + (m_centerX - anchorX) * applied;
        m_centerY = anchorY + (m_centerY - anchorY) * applied;
    }

    void CanvasViewPanel::DrawGrid(const ViewRect& rect)
    {
        if (rect.width <= 0.0f || rect.height <= 0.0f)
        {
            return;
        }
        const float drawHeight = rect.drawHeight > 0.0f ? rect.drawHeight : rect.height;
        const float worldPerPixel = (m_orthographicSize * 2.0f) / drawHeight;
        const float step = ChooseGridStep(worldPerPixel);
        if (false == std::isfinite(step) || step <= 0.0f)
        {
            return;
        }

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        ScreenToWorld(rect, rect.left, rect.top + rect.height, minX, minY);
        ScreenToWorld(rect, rect.left + rect.width, rect.top, maxX, maxY);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 line = IM_COL32(255, 255, 255, 18);
        const ImU32 strong = IM_COL32(255, 255, 255, 38);
        const ImU32 axisX = IM_COL32(220, 90, 90, 160);
        const ImU32 axisY = IM_COL32(110, 200, 110, 160);

        // 칸이 너무 많으면 그리지 않는다. 화면 가득한 선은 정보가 아니라 잡음이고,
        // 드로우 목록만 불린다.
        const float columns = (maxX - minX) / step;
        const float rows = (maxY - minY) / step;
        if (columns > 4096.0f || rows > 4096.0f)
        {
            return;
        }

        // **눈금에 숫자를 붙인다**(D-144). 선만 있으면 몇 번째 칸인지 세어야 하고, 배율이
        // 바뀌면 그 셈이 다시 시작된다. 기존 엔진의 캔버스 뷰도 이 숫자를 적었다.
        const ImU32 labelColor = IM_COL32(185, 195, 210, 210);
        ImFont* labelFont = ImGui::GetFont();
        const float labelSize = ImGui::GetFontSize() * 0.8f;
        // 숫자는 테두리 안쪽에 붙인다. X 는 아래, Y 는 왼쪽 - 기존과 같은 자리다.
        const float labelBottom = rect.top + rect.height - labelSize - 2.0f;
        const float labelLeft = rect.left + 3.0f;

        // **픽셀로 읽을 때는 에셋 PPU 의 기본값을 곱한다**(D-184). 유닛으로 읽을 때는 1 이다 -
        // 곱하는 값만 달라지고 자리를 잡는 셈은 그대로라, 두 모드가 따로 어긋날 자리가 없다.
        const float rulerScale = m_rulerInPixels ? DefaultPixelsPerUnit : 1.0f;

        // **선마다 번호로 센다**(D-162). `x += step` 으로 더해 가면 오차가 쌓여 0 이어야 할 선이
        // `-2.98e-08` 로 적혔다(실제 에디터에서 그랬다). 번호에 간격을 곱하면 0 은 정확히 0 이다.
        const long long firstX = static_cast<long long>(std::floor(minX / step));
        const long long lastX = static_cast<long long>(std::floor(maxX / step));
        // 지난 숫자의 오른쪽 끝. 겹치면 건너뛴다 - 겹친 숫자는 둘 다 못 읽는다.
        float lastLabelEnd = -1.0e9f;
        for (long long index = firstX; index <= lastX; ++index)
        {
            const float x = static_cast<float>(index) * step;
            float screenX = 0.0f;
            float unused = 0.0f;
            WorldToScreen(rect, x, 0.0f, screenX, unused);
            // 열 칸마다 한 줄은 진하게. 눈금을 세지 않아도 배율이 읽힌다.
            const bool tenth = index % 10 == 0;
            draw->AddLine(ImVec2(screenX, rect.top), ImVec2(screenX, rect.top + rect.height),
                tenth ? strong : line);

            char text[32] = {};
            std::snprintf(text, sizeof(text), "%.4g", x * rulerScale);
            const ImVec2 extent = labelFont->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, text);
            const float textLeft = screenX - extent.x * 0.5f;
            // 넉넉히 띄운다. 닿을 듯 말 듯 붙은 숫자는 읽는 데 눈이 더 든다.
            if (textLeft < lastLabelEnd + 24.0f)
            {
                continue;
            }
            lastLabelEnd = textLeft + extent.x;
            draw->AddText(labelFont, labelSize, ImVec2(textLeft, labelBottom), labelColor, text);
        }
        const long long firstY = static_cast<long long>(std::floor(minY / step));
        const long long lastY = static_cast<long long>(std::floor(maxY / step));
        float lastLabelY = -1.0e9f;
        for (long long index = firstY; index <= lastY; ++index)
        {
            const float y = static_cast<float>(index) * step;
            float screenY = 0.0f;
            float unused = 0.0f;
            WorldToScreen(rect, 0.0f, y, unused, screenY);
            const bool tenth = index % 10 == 0;
            draw->AddLine(ImVec2(rect.left, screenY), ImVec2(rect.left + rect.width, screenY),
                tenth ? strong : line);

            // 아래쪽 X 숫자 줄과 겹치는 자리는 건너뛴다. 왼쪽 아래 구석에서 두 숫자가 포개져 읽히지 않았다.
            if (std::fabs(screenY - lastLabelY) < labelSize * 2.2f
                || screenY + labelSize * 0.5f > labelBottom - 2.0f)
            {
                continue;
            }
            lastLabelY = screenY;
            char text[32] = {};
            std::snprintf(text, sizeof(text), "%.4g", y * rulerScale);
            draw->AddText(labelFont, labelSize,
                ImVec2(labelLeft, screenY - labelSize * 0.5f), labelColor, text);
        }

        // 원점의 두 축. 어디가 (0,0) 인지 화면에서 바로 보여야 한다.
        float originX = 0.0f;
        float originY = 0.0f;
        WorldToScreen(rect, 0.0f, 0.0f, originX, originY);
        draw->AddLine(ImVec2(rect.left, originY), ImVec2(rect.left + rect.width, originY), axisX, 1.5f);
        draw->AddLine(ImVec2(originX, rect.top), ImVec2(originX, rect.top + rect.height), axisY, 1.5f);
    }

    bool CanvasViewPanel::GetWorldBounds(const GameObject& object,
        float& minX, float& minY, float& maxX, float& maxY) const
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        GameObject& mutableObject = const_cast<GameObject&>(object);
        Component::Transform2D* transform =
            canvas->FindComponentRaw<Component::Transform2D>(&mutableObject);
        if (transform == nullptr)
        {
            return false;
        }
        // 월드 캐시가 아직 안 섰으면 로컬을 그대로 쓴다. 부모가 있으면 어긋나지만,
        // 짐작으로 행렬을 쌓는 것보다 낫다 - 한 프레임 뒤에 제자리로 온다.
        const Matrix3x2 world = transform->worldValid
            ? transform->world
            : MakeTransformMatrix2D(transform->position, transform->rotation, transform->scale);
        const Vec2 center{ world.m31, world.m32 };

        // 오브젝트 로컬(유닛)의 사각형을 월드로 옮겨 감싼다. **회전도 따른다** - 예전에는 크기만 곱해, 돌린 스프라이트와
        // 글자의 모서리를 눌러도 잡히지 않았다(text-plan §7). 결과는 돌린 사각형을 감싸는 축 정렬 사각형이다.
        bool hasBox = false;
        const auto enclose = [&](float localMinX, float localMinY, float localMaxX, float localMaxY) {
            const float xs[2] = { localMinX, localMaxX };
            const float ys[2] = { localMinY, localMaxY };
            for (const float x : xs)
            {
                for (const float y : ys)
                {
                    const float wx = x * world.m11 + y * world.m21 + world.m31;
                    const float wy = x * world.m12 + y * world.m22 + world.m32;
                    if (false == hasBox)
                    {
                        minX = wx;
                        maxX = wx;
                        minY = wy;
                        maxY = wy;
                        hasBox = true;
                    }
                    else
                    {
                        minX = std::min(minX, wx);
                        maxX = std::max(maxX, wx);
                        minY = std::min(minY, wy);
                        maxY = std::max(maxY, wy);
                    }
                }
            }
        };

        if (Component::SpriteRenderer2D* sprite =
                canvas->FindComponentRaw<Component::SpriteRenderer2D>(&mutableObject))
        {
            // **에셋이 정한 크기를 에셋에게 묻는다**(D-148). `sizeMode` 가 `FromSprite` 면
            // 실제 크기는 스프라이트의 칸 픽셀을 그 에셋의 PPU 로 나눈 값이고, 그리는 쪽도
            // 그렇게 푼다(D-117). 선언된 `size` 를 대신 쓰면 집는 칸이 그림과 어긋나,
            // 눈에 보이는 그림의 가장자리를 눌러도 잡히지 않는다.
            float widthUnits = sprite->size.x;
            float heightUnits = sprite->size.y;
            float pivotX = sprite->pivot.x;
            float pivotY = sprite->pivot.y;
            const AssetSystem* assets = m_editor->GetAssetSystem();
            const SpriteData* data =
                assets != nullptr ? assets->GetSprite(sprite->sprite) : nullptr;
            if (data != nullptr && false == data->frames.IsEmpty()
                && data->options.pixelsPerUnit > 0.0f)
            {
                // 칸 번호가 넘치면 마지막 칸이다. 그리는 쪽(`SpriteLibrary::Resolve`)과 같다.
                const std::size_t frameIndex =
                    sprite->frameIndex < data->frames.Size()
                        ? sprite->frameIndex
                        : data->frames.Size() - 1;
                const SpriteFrame& frame = data->frames[frameIndex];
                if (sprite->sizeMode == Component::SpriteSizeMode::FromSprite)
                {
                    widthUnits = static_cast<float>(frame.width) / data->options.pixelsPerUnit;
                    heightUnits = static_cast<float>(frame.height) / data->options.pixelsPerUnit;
                }
                if (sprite->pivotMode == Component::SpritePivotMode::FromSprite)
                {
                    pivotX = frame.pivotX;
                    pivotY = frame.pivotY;
                }
            }
            if (std::fabs(widthUnits) > 0.001f && std::fabs(heightUnits) > 0.001f)
            {
                enclose(-pivotX * widthUnits, -pivotY * heightUnits,
                    (1.0f - pivotX) * widthUnits, (1.0f - pivotY) * heightUnits);
            }
        }

        // **텍스트는 그린 블록의 사각형이다**(text-plan §4.6). 크기는 레이아웃이 정하므로 컴포넌트 필드로는 알 수 없고,
        // 마지막으로 레이아웃한 시스템에게 묻는다. 여러 개가 붙었거나 스프라이트와 함께면 모두를 감싼다 - 기존 엔진은
        // 스프라이트가 있으면 텍스트를 보지 않아 스프라이트 밖으로 나온 글자를 눌러도 잡히지 않았다.
        // 폰트가 없어 레이아웃이 없는 텍스트와 빈 글자는 빈 오브젝트의 작은 상자로 남는다.
        if (System::Text2DSystem* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>())
        {
            canvas->FindComponentsRaw<Component::Text2D>(&mutableObject, m_textScratch);
            for (Component::Text2D* text : m_textScratch)
            {
                float localMinX = 0.0f;
                float localMinY = 0.0f;
                float localMaxX = 0.0f;
                float localMaxY = 0.0f;
                if (false == texts->GetLocalBounds(text->GetInstanceId(), localMinX, localMinY, localMaxX, localMaxY))
                {
                    continue;
                }
                if (localMaxX - localMinX < 0.001f || localMaxY - localMinY < 0.001f)
                {
                    continue;
                }
                enclose(localMinX, localMinY, localMaxX, localMaxY);
            }
            m_textScratch.Clear();
        }

        // 그릴 것이 없으면(빈 오브젝트, 폰트 없는 텍스트) 월드 원점 둘레의 작은 상자다. 크기·회전과 무관하게 같은 크기로 잡힌다.
        if (false == hasBox || maxX - minX < 0.001f || maxY - minY < 0.001f)
        {
            minX = center.x - EmptyObjectHalfSize;
            maxX = center.x + EmptyObjectHalfSize;
            minY = center.y - EmptyObjectHalfSize;
            maxY = center.y + EmptyObjectHalfSize;
        }
        return true;
    }

    void CanvasViewPanel::DrawJoints(const ViewRect& rect)
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        // **조인트는 앵커와 상대 앵커를 잇는 선이다**(D-233). 경첩은 핀을 원으로, 거리 조인트는 두 앵커를 선으로 잇는다.
        // 상대가 없으면 상대 앵커는 월드의 점이다. 자동 설정은 재생이 처음 이을 때 적으므로 그 전에는 적힌 값대로 보인다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 color = IM_COL32(255, 140, 220, 210);
        const auto poseOf = [&](GameObject* object, PolygonPose& pose) {
            Component::Transform2D* transform =
                object != nullptr ? canvas->FindComponentRaw<Component::Transform2D>(object) : nullptr;
            if (transform == nullptr)
            {
                return false;
            }
            pose.center = transform->worldValid ? transform->worldPosition : transform->position;
            pose.scale = transform->worldValid ? transform->worldScale : transform->scale;
            const float angle = transform->worldValid ? transform->worldRotation : transform->rotation;
            pose.cosine = std::cos(angle);
            pose.sine = std::sin(angle);
            return true;
        };
        const auto connectedPoint = [&](const GameObjectHandle& connected, Vec2 anchor) {
            PolygonPose other;
            if (poseOf(Internal::GameObjectHandleAccess::Resolve(connected), other))
            {
                return LocalToScreen(rect, other, {}, anchor);
            }
            Vec2 screen;
            WorldToScreen(rect, anchor.x, anchor.y, screen.x, screen.y);
            return screen;
        };
        canvas->ForEachObject([&](GameObject& object) {
            if (object.IsEditorHidden())
            {
                return;
            }
            PolygonPose pose;
            if (false == poseOf(&object, pose))
            {
                return;
            }
            const float thickness = m_editor->IsSelected(&object) ? 2.0f : 1.0f;
            canvas->FindComponentsRaw<Component::DistanceJoint2D>(&object, m_distanceJointScratch);
            for (Component::DistanceJoint2D* joint : m_distanceJointScratch)
            {
                if (joint == nullptr || false == joint->IsEnabled())
                {
                    continue;
                }
                const Vec2 a = LocalToScreen(rect, pose, {}, joint->anchor);
                const Vec2 b = connectedPoint(joint->connectedObject, joint->connectedAnchor);
                draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, thickness);
                draw->AddCircleFilled(ImVec2(a.x, a.y), 3.0f, color);
                draw->AddCircleFilled(ImVec2(b.x, b.y), 3.0f, color);
            }
            canvas->FindComponentsRaw<Component::HingeJoint2D>(&object, m_hingeJointScratch);
            for (Component::HingeJoint2D* joint : m_hingeJointScratch)
            {
                if (joint == nullptr || false == joint->IsEnabled())
                {
                    continue;
                }
                const Vec2 pin = LocalToScreen(rect, pose, {}, joint->anchor);
                draw->AddCircle(ImVec2(pin.x, pin.y), 6.0f, color, 16, thickness);
                // 자동이 아니면 상대 쪽 핀도 그린다 - 두 핀이 떨어져 있으면 재생할 때 그 사이를 당겨 붙인다.
                if (false == joint->autoConnectedAnchor)
                {
                    const Vec2 other = connectedPoint(joint->connectedObject, joint->connectedAnchor);
                    draw->AddLine(ImVec2(pin.x, pin.y), ImVec2(other.x, other.y), color, thickness);
                    draw->AddCircle(ImVec2(other.x, other.y), 3.0f, color, 12, thickness);
                }
            }
        });
    }

    void CanvasViewPanel::DrawColliders(const ViewRect& rect)
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        // **물리는 눈에 보이지 않는다.** 충돌 칸이 그림과 어긋나 있어도 화면에는 아무 표시가
        // 없어서, 부딪혀 보고 나서야 안다. 기존 엔진의 캔버스 뷰도 여기서 이것을 그렸다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 normal = IM_COL32(80, 255, 140, 179);
        const ImU32 selected = IM_COL32(80, 180, 255, 220);
        // 트리거는 **막는 것이 아니라 알리는 것**이라 색을 달리한다. 같은 색으로 두면
        // 왜 통과하는지 화면에서 알 수 없다.
        const ImU32 trigger = IM_COL32(255, 210, 90, 179);
        // 물리가 받지 않는 외곽선(자기 교차·넓이 없음)이다. 그려 두지 않으면 왜 부딪히지 않는지 모른다.
        const ImU32 rejected = IM_COL32(255, 80, 80, 230);
        // 오목한 폴리곤을 물리가 나눈 볼록 조각. 고른 것만 옅게 그린다 - 조각 사이 이음매가 어디인지 보인다.
        const ImU32 pieceColor = IM_COL32(80, 180, 255, 80);

        canvas->ForEachObject([&](GameObject& object)
        {
            // 캔버스 뷰에서 감춘 오브젝트는 그리지도 집지도 않는다(D-163, 기존 `EditorHidden`).
            if (object.IsEditorHidden())
            {
                return;
            }
            Component::Transform2D* transform =
                canvas->FindComponentRaw<Component::Transform2D>(&object);
            if (transform == nullptr)
            {
                return;
            }
            // 콜라이더는 한 오브젝트에 여럿 붙는다(`Multiple`). 물리가 모두 쓰므로 모두 그린다.
            canvas->FindComponentsRaw<Component::Collider2D>(&object, m_colliderScratch);
            if (m_colliderScratch.IsEmpty())
            {
                return;
            }
            PolygonPose pose;
            pose.center = transform->worldValid ? transform->worldPosition : transform->position;
            pose.scale = transform->worldValid ? transform->worldScale : transform->scale;
            const float angle = transform->worldValid ? transform->worldRotation : transform->rotation;
            pose.cosine = std::cos(angle);
            pose.sine = std::sin(angle);
            const bool isSelected = m_editor->IsSelected(&object);
            const float thickness = isSelected ? 2.0f : 1.0f;

            for (Component::Collider2D* collider : m_colliderScratch)
            {
                if (collider == nullptr || false == collider->IsEnabled())
                {
                    continue;
                }
                const ImU32 color = collider->isTrigger
                    ? trigger
                    : (isSelected ? selected : normal);

                if (collider->shape == Component::ColliderShape2D::Circle)
                {
                    // 원은 한 축으로만 커져도 원으로 남는다(물리가 그렇게 다룬다).
                    // 그러니 **더 큰 쪽**으로 잰다 - 작은 쪽으로 재면 그림보다 작은 원이 되어
                    // 실제로 부딪히는 자리를 가린다.
                    const float scaleX = std::fabs(pose.scale.x);
                    const float scaleY = std::fabs(pose.scale.y);
                    const float radius = collider->radius * (scaleX > scaleY ? scaleX : scaleY);
                    const Vec2 middle = LocalToScreen(rect, pose, collider->offset, {});
                    float edgeX = 0.0f;
                    float edgeY = 0.0f;
                    float centerX = 0.0f;
                    float centerY = 0.0f;
                    ScreenToWorld(rect, middle.x, middle.y, centerX, centerY);
                    WorldToScreen(rect, centerX + radius, centerY, edgeX, edgeY);
                    draw->AddCircle(ImVec2(middle.x, middle.y), edgeX - middle.x, color, 48, thickness);
                    continue;
                }

                if (collider->shape == Component::ColliderShape2D::Capsule)
                {
                    // 물리와 같은 함수로 잰다: 크기를 곱한 `size` 상자에 꼭 맞는 알약이다. 상자로 그리면 둥근 끝 옆의
                    // 빈 곳이 부딪히는 자리처럼 보인다.
                    const Physics2D::ConvexPolygon capsule = Physics2D::MakeCapsuleInBox(
                        { collider->offset.x * pose.scale.x, collider->offset.y * pose.scale.y },
                        { collider->size.x * 0.5f * pose.scale.x, collider->size.y * 0.5f * pose.scale.y });
                    const Vec2 a = capsule.points[0];
                    const Vec2 b = capsule.points[1];
                    const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                    const Vec2 axis = length > 0.0f ? Vec2{ (b.x - a.x) / length, (b.y - a.y) / length } : Vec2{ 1.0f, 0.0f };
                    const Vec2 side{ -axis.y, axis.x };
                    constexpr float HalfTurn = 3.14159265f;
                    constexpr int ArcSegments = 16;
                    m_screenScratch.Clear();
                    // b 쪽 반원(옆 -side 에서 축 방향을 지나 +side), 이어서 a 쪽 반원. 두 반원 사이의 곧은 변은 닫는 선이다.
                    for (int end = 0; end < 2; ++end)
                    {
                        const Vec2 cap = end == 0 ? b : a;
                        const float start = end == 0 ? -0.5f * HalfTurn : 0.5f * HalfTurn;
                        for (int k = 0; k <= ArcSegments; ++k)
                        {
                            const float turn = start + HalfTurn * static_cast<float>(k) / static_cast<float>(ArcSegments);
                            const float c = std::cos(turn) * capsule.radius;
                            const float s = std::sin(turn) * capsule.radius;
                            const Vec2 local{ cap.x + axis.x * c + side.x * s, cap.y + axis.y * c + side.y * s };
                            // 캡슐은 크기와 offset 을 이미 곱한 바디 로컬이다. 돌리고 옮기기만 한다.
                            const float worldX = pose.center.x + local.x * pose.cosine - local.y * pose.sine;
                            const float worldY = pose.center.y + local.x * pose.sine + local.y * pose.cosine;
                            Vec2 screen;
                            WorldToScreen(rect, worldX, worldY, screen.x, screen.y);
                            m_screenScratch.Add(screen);
                        }
                    }
                    const std::size_t count = m_screenScratch.Size();
                    for (std::size_t index = 0; index < count; ++index)
                    {
                        const Vec2 from = m_screenScratch[index];
                        const Vec2 to = m_screenScratch[(index + 1) % count];
                        draw->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, thickness);
                    }
                    continue;
                }

                // 상자는 **돌면 기울어진다.** 외접 사각형으로 그리면 돌려 놓은 오브젝트의 충돌 칸이 실제보다
                // 커 보인다. 폴리곤은 꼭짓점을 그대로 그리고, 꼭짓점이 없으면 물리처럼 `size` 상자다.
                if (PolygonEditModel::EditsPoints(*collider))
                {
                    PolygonEditModel::SeedPoints(*collider, m_outlineScratch);
                }
                else
                {
                    Component::Collider2D box = *collider;
                    box.points.Clear();
                    PolygonEditModel::SeedPoints(box, m_outlineScratch);
                }
                m_screenScratch.Clear();
                for (const Vec2& point : m_outlineScratch)
                {
                    m_screenScratch.Add(LocalToScreen(rect, pose, collider->offset, point));
                }
                ImU32 outlineColor = color;
                if (collider->shape == Component::ColliderShape2D::Polygon)
                {
                    const PieceCache& pieces = PiecesFor(*collider, pose.scale);
                    if (pieces.error != Physics2D::PolygonError::None)
                    {
                        outlineColor = rejected;
                    }
                    else if (isSelected && pieces.pieces.Size() > 1)
                    {
                        for (const Physics2D::ConvexPolygon& convex : pieces.pieces)
                        {
                            ImVec2 corners[Physics2D::MaxPolygonVertices];
                            for (std::uint32_t k = 0; k < convex.count; ++k)
                            {
                                // 조각은 크기와 offset 을 이미 곱한 바디 로컬이다. 돌리고 옮기기만 한다.
                                const Vec2 local = convex.points[k];
                                const float worldX = pose.center.x + local.x * pose.cosine - local.y * pose.sine;
                                const float worldY = pose.center.y + local.x * pose.sine + local.y * pose.cosine;
                                float screenX = 0.0f;
                                float screenY = 0.0f;
                                WorldToScreen(rect, worldX, worldY, screenX, screenY);
                                corners[k] = ImVec2(screenX, screenY);
                            }
                            draw->AddPolyline(corners, static_cast<int>(convex.count), pieceColor,
                                ImDrawFlags_Closed, 1.0f);
                        }
                    }
                }
                // 변마다 선 하나다. 꼭짓점 수에 상한을 두지 않는다 - 편집으로 얼마든지 는다. 열린 체인은 끝과 처음을 잇지 않는다.
                const std::size_t count = m_screenScratch.Size();
                const std::size_t edges = PolygonEditModel::IsClosedOutline(*collider) ? count : (count > 0 ? count - 1 : 0);
                for (std::size_t index = 0; count >= 2 && index < edges; ++index)
                {
                    const Vec2 a = m_screenScratch[index];
                    const Vec2 b = m_screenScratch[(index + 1) % count];
                    draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), outlineColor, thickness);
                }
            }
        });
    }

    Vec2 CanvasViewPanel::LocalToScreen(const ViewRect& rect, const PolygonPose& pose, Vec2 offset, Vec2 local) const
    {
        // 콜라이더의 점은 오브젝트 로컬이다. offset 을 더하고, 커지고, 돌고, 옮겨진다(물리와 같은 순서).
        const float x = (local.x + offset.x) * pose.scale.x;
        const float y = (local.y + offset.y) * pose.scale.y;
        const float worldX = pose.center.x + x * pose.cosine - y * pose.sine;
        const float worldY = pose.center.y + x * pose.sine + y * pose.cosine;
        Vec2 screen;
        WorldToScreen(rect, worldX, worldY, screen.x, screen.y);
        return screen;
    }

    Vec2 CanvasViewPanel::ScreenToLocal(const ViewRect& rect, const PolygonPose& pose, Vec2 offset, Vec2 screen) const
    {
        float worldX = 0.0f;
        float worldY = 0.0f;
        ScreenToWorld(rect, screen.x, screen.y, worldX, worldY);
        const float dx = worldX - pose.center.x;
        const float dy = worldY - pose.center.y;
        const float x = dx * pose.cosine + dy * pose.sine;
        const float y = -dx * pose.sine + dy * pose.cosine;
        // 크기가 0 인 축은 되돌릴 수 없다. 그 축은 움직이지 않은 것으로 둔다.
        const float localX = pose.scale.x != 0.0f ? x / pose.scale.x : 0.0f;
        const float localY = pose.scale.y != 0.0f ? y / pose.scale.y : 0.0f;
        return { localX - offset.x, localY - offset.y };
    }

    bool CanvasViewPanel::ProjectWorldToScreen(float worldX, float worldY, float& screenX, float& screenY) const
    {
        if (false == m_hasLastRect)
        {
            return false;
        }
        WorldToScreen(m_lastRect, worldX, worldY, screenX, screenY);
        return true;
    }

    const CanvasViewPanel::PieceCache& CanvasViewPanel::PiecesFor(const Component::Collider2D& collider, Vec2 scale)
    {
        // 지문: 꼭짓점·offset·크기·size. 물리의 어댑터와 같은 판단을 하되 그 코드를 끌어오지 않는다 - 에디터는
        // 시스템이 돌지 않는 편집 중에도 그린다.
        std::uint64_t signature = 14695981039346656037ull;
        const auto mix = [&signature](float value)
        {
            const float normalized = value == 0.0f ? 0.0f : value;
            std::uint32_t bits = 0;
            std::memcpy(&bits, &normalized, sizeof(bits));
            signature ^= bits;
            signature *= 1099511628211ull;
        };
        mix(static_cast<float>(collider.points.Size()));
        for (const Vec2& point : collider.points)
        {
            mix(point.x);
            mix(point.y);
        }
        mix(collider.offset.x);
        mix(collider.offset.y);
        mix(collider.size.x);
        mix(collider.size.y);
        mix(scale.x);
        mix(scale.y);

        const InstanceId id = collider.GetInstanceId();
        PieceCache* cache = m_pieceCache.Find(id);
        if (cache == nullptr)
        {
            m_pieceCache.TryAdd(id, PieceCache{});
            cache = m_pieceCache.Find(id);
            cache->signature = signature + 1;
        }
        if (cache->signature != signature)
        {
            cache->signature = signature;
            PolygonEditModel::SeedPoints(collider, m_outlineScratch);
            for (Vec2& point : m_outlineScratch)
            {
                point = { (point.x + collider.offset.x) * scale.x, (point.y + collider.offset.y) * scale.y };
            }
            cache->error = Physics2D::DecomposePolygon(m_outlineScratch.View(), cache->pieces);
        }
        return *cache;
    }

    bool CanvasViewPanel::FindPolygonTarget(PolygonTarget& target)
    {
        target = {};
        Canvas* canvas = m_editor->GetCanvas();
        GameObject* object = m_editor->GetSelectedObject();
        if (false == m_editCollider || canvas == nullptr || object == nullptr || object->IsEditorHidden())
        {
            return false;
        }
        Component::Transform2D* transform = canvas->FindComponentRaw<Component::Transform2D>(object);
        if (transform == nullptr)
        {
            return false;
        }
        // 우클릭 메뉴의 "포인트 편집" 으로 고른 것이 먼저다(D-220). 고른 오브젝트가 바뀌었거나, 그 콜라이더가
        // 사라졌거나 꺼졌거나 폴리곤이 아니게 됐으면 잊고 아래의 첫째로 돌아간다.
        if (m_pointTarget.objectId != InvalidEditorObjectId)
        {
            Component::Collider2D* chosen = nullptr;
            if (m_editor->GetObjectIds().Resolve(m_pointTarget.objectId) == object)
            {
                chosen = static_cast<Component::Collider2D*>(ResolveComponent(m_editor->GetObjectIds(), m_pointTarget));
            }
            if (chosen != nullptr && chosen->IsEnabled() && PolygonEditModel::EditsPoints(*chosen))
            {
                target.collider = chosen;
            }
            else
            {
                m_pointTarget = {};
            }
        }
        if (target.collider == nullptr)
        {
            // 한 오브젝트에 폴리곤이 여럿이면 첫째다(기존 엔진은 한 오브젝트에 폴리곤 하나였다).
            canvas->FindComponentsRaw<Component::Collider2D>(object, m_colliderScratch);
            for (Component::Collider2D* collider : m_colliderScratch)
            {
                if (collider != nullptr && collider->IsEnabled()
                    && PolygonEditModel::EditsPoints(*collider))
                {
                    target.collider = collider;
                    break;
                }
            }
        }
        if (target.collider == nullptr
            || false == MakeComponentAddress(m_editor->GetObjectIds(), *object, *target.collider, target.address))
        {
            target.collider = nullptr;
            return false;
        }
        target.object = object;
        target.pose.center = transform->worldValid ? transform->worldPosition : transform->position;
        target.pose.scale = transform->worldValid ? transform->worldScale : transform->scale;
        const float angle = transform->worldValid ? transform->worldRotation : transform->rotation;
        target.pose.cosine = std::cos(angle);
        target.pose.sine = std::sin(angle);
        return true;
    }

    void CanvasViewPanel::CommitPoints(const ComponentAddress& address, const String& before, const Array<Vec2>& after)
    {
        ComponentBase* component = ResolveComponent(m_editor->GetObjectIds(), address);
        SetPropertyCommand::Path path;
        if (component == nullptr || false == SetPropertyCommand::MakeFieldPath(address.typeId, "points", path))
        {
            return;
        }
        // 새 값을 글자로 뜬다: 한 번 써서 읽고, 쓰기 전 값으로 되돌린다. 쓰는 것은 커맨드의 몫이다(§11.3).
        Component::Collider2D* collider = static_cast<Component::Collider2D*>(component);
        collider->points = after;
        String text;
        const bool read = SetPropertyCommand::ReadValue(*component, address.typeId, path, text);
        SetPropertyCommand::ApplyValue(*component, address.typeId, path, before);
        if (false == read || text == before)
        {
            return;
        }
        m_editor->GetCommands().Execute(
            MakeOwnerPtr<SetPropertyCommand>(m_editor->GetObjectIds(), address, path, before, text));
    }

    void CanvasViewPanel::DrawPolygonEditor(const ViewRect& rect)
    {
        const ImGuiIO& io = ImGui::GetIO();
        PolygonTarget target;
        const bool hasTarget = FindPolygonTarget(target);
        if (m_vertexDragging && (false == hasTarget || false == target.address.Equals(m_dragAddress)))
        {
            // 끌던 콜라이더가 사라졌거나 다른 것을 골랐다. 남은 것이 있으면 끌기 전 값으로 되돌린다.
            if (ComponentBase* component = ResolveComponent(m_editor->GetObjectIds(), m_dragAddress))
            {
                SetPropertyCommand::Path path;
                if (SetPropertyCommand::MakeFieldPath(m_dragAddress.typeId, "points", path))
                {
                    SetPropertyCommand::ApplyValue(*component, m_dragAddress.typeId, path, m_dragBefore);
                }
            }
            m_vertexDragging = false;
        }
        if (false == hasTarget)
        {
            m_polygonHover = {};
            return;
        }
        Component::Collider2D& collider = *target.collider;
        const Vec2 mouse{ io.MousePos.x, io.MousePos.y };

        if (m_vertexDragging)
        {
            // 끄는 동안은 콜라이더에 바로 쓴다(미리 보기) - 물리 그림과 조각도 따라온다. 놓을 때 되돌리고 커맨드로 쓴다.
            if (m_dragVertex < m_dragPoints.Size())
            {
                m_dragPoints[m_dragVertex] = ScreenToLocal(rect, target.pose, collider.offset, mouse);
            }
            collider.points = m_dragPoints;
            char id[32];
            std::snprintf(id, sizeof(id), "##vertex_%u", m_dragVertex);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                Widget::OverlayHandle(id, true, true, false);
            }
            else
            {
                Widget::OverlayHandle(id, false, false, false);
                m_vertexDragging = false;
                const Array<Vec2> after = m_dragPoints;
                CommitPoints(m_dragAddress, m_dragBefore, after);
            }
        }

        PolygonEditModel::SeedPoints(collider, m_outlineScratch);
        m_screenScratch.Clear();
        for (const Vec2& point : m_outlineScratch)
        {
            m_screenScratch.Add(LocalToScreen(rect, target.pose, collider.offset, point));
        }

        m_polygonHover = {};
        if (false == m_vertexDragging && PointerInView(rect) && false == ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            m_polygonHover = PolygonEditModel::Pick(m_screenScratch.View(), mouse, PolygonEditModel::IsClosedOutline(collider));
        }
        if (false == ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_vertexPressed = false;
        }

        if (m_polygonHover.kind == PolygonEditModel::HitKind::Vertex)
        {
            char id[32];
            std::snprintf(id, sizeof(id), "##vertex_%u", m_polygonHover.index);
            const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            Widget::OverlayHandle(id, true, pressed, pressed);
            if (pressed)
            {
                SetPropertyCommand::Path path;
                if (SetPropertyCommand::MakeFieldPath(target.address.typeId, "points", path)
                    && SetPropertyCommand::ReadValue(collider, target.address.typeId, path, m_dragBefore))
                {
                    m_dragPoints = m_outlineScratch;
                    m_dragVertex = m_polygonHover.index;
                    m_dragAddress = target.address;
                    m_vertexDragging = true;
                    m_vertexPressed = true;
                }
            }
        }
        else if (m_polygonHover.kind == PolygonEditModel::HitKind::Edge)
        {
            const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            Widget::OverlayHandle("##edge_insert", true, false, pressed);
            if (pressed)
            {
                // 변을 누르면 그 자리에 버텍스가 생긴다. 커맨드 하나다.
                SetPropertyCommand::Path path;
                String before;
                if (SetPropertyCommand::MakeFieldPath(target.address.typeId, "points", path)
                    && SetPropertyCommand::ReadValue(collider, target.address.typeId, path, before))
                {
                    Array<Vec2> after = m_outlineScratch;
                    PolygonEditModel::InsertOnEdge(after, m_polygonHover.index,
                        ScreenToLocal(rect, target.pose, collider.offset, m_polygonHover.point));
                    CommitPoints(target.address, before, after);
                }
                m_vertexPressed = true;
            }
        }

        // 그리기: 편집 중인 외곽선은 파랗고 굵게, 버텍스마다 점, 가리킨 것은 희게, 변 위에는 더하기 점.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 edgeColor = IM_COL32(80, 180, 255, 235);
        const ImU32 handleColor = IM_COL32(80, 180, 255, 235);
        const ImU32 hotColor = IM_COL32(255, 255, 255, 255);
        const ImU32 insertColor = IM_COL32(140, 230, 100, 230);
        const ImU32 shadow = IM_COL32(0, 0, 0, 120);
        constexpr float HandleRadius = 3.5f;
        const std::size_t count = m_screenScratch.Size();
        const std::size_t edges = PolygonEditModel::IsClosedOutline(collider) ? count : (count > 0 ? count - 1 : 0);
        for (std::size_t index = 0; count >= 2 && index < edges; ++index)
        {
            const Vec2 a = m_screenScratch[index];
            const Vec2 b = m_screenScratch[(index + 1) % count];
            draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), edgeColor, 2.0f);
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const Vec2 p = m_screenScratch[index];
            const bool hot = (m_vertexDragging && index == m_dragVertex)
                || (m_polygonHover.kind == PolygonEditModel::HitKind::Vertex && index == m_polygonHover.index);
            draw->AddCircleFilled(ImVec2(p.x + 1.0f, p.y + 1.0f), HandleRadius, shadow);
            draw->AddCircleFilled(ImVec2(p.x, p.y), HandleRadius, hot ? hotColor : handleColor);
        }
        if (m_polygonHover.kind == PolygonEditModel::HitKind::Edge)
        {
            const Vec2 p = m_polygonHover.point;
            draw->AddCircleFilled(ImVec2(p.x + 1.0f, p.y + 1.0f), HandleRadius, shadow);
            draw->AddCircleFilled(ImVec2(p.x, p.y), HandleRadius, insertColor);
        }
    }

    bool CanvasViewPanel::DrawVertexMenu(const ViewRect& rect)
    {
        // 버텍스 위에서 우클릭하면 그 버텍스의 메뉴다. 캔버스의 오브젝트 메뉴 대신 연다.
        if (PointerInView(rect) && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && false == m_panMoved)
        {
            PolygonTarget target;
            if (FindPolygonTarget(target))
            {
                PolygonEditModel::SeedPoints(*target.collider, m_outlineScratch);
                m_screenScratch.Clear();
                for (const Vec2& point : m_outlineScratch)
                {
                    m_screenScratch.Add(LocalToScreen(rect, target.pose, target.collider->offset, point));
                }
                const ImGuiIO& io = ImGui::GetIO();
                const PolygonEditModel::Hit hit = PolygonEditModel::Pick(m_screenScratch.View(),
                    { io.MousePos.x, io.MousePos.y }, PolygonEditModel::IsClosedOutline(*target.collider));
                if (hit.kind == PolygonEditModel::HitKind::Vertex)
                {
                    m_menuAddress = target.address;
                    m_menuVertex = hit.index;
                    Widget::OpenContextMenu("##ColliderVertexMenu");
                }
            }
        }
        if (false == Widget::BeginOpenedContextMenu("##ColliderVertexMenu"))
        {
            return false;
        }
        ComponentBase* component = ResolveComponent(m_editor->GetObjectIds(), m_menuAddress);
        Component::Collider2D* collider = static_cast<Component::Collider2D*>(component);
        if (collider != nullptr)
        {
            PolygonEditModel::SeedPoints(*collider, m_outlineScratch);
            const std::uint32_t minimum = PolygonEditModel::MinPointCount(*collider);
            const bool removable = m_outlineScratch.Size() > minimum;
            const char* why = minimum < PolygonEditModel::MinVertexCount
                ? Loc::TextOr(LocKeys::CanvasViewPointDeleteMinChain, "a chain needs at least two points")
                : Loc::TextOr(LocKeys::CanvasViewPointDeleteMin, "a polygon needs at least three points");
            if (Widget::MenuItem(Loc::TextOr(LocKeys::CanvasViewPointDelete, "Delete Point"), nullptr, removable, why))
            {
                SetPropertyCommand::Path path;
                String before;
                if (SetPropertyCommand::MakeFieldPath(m_menuAddress.typeId, "points", path)
                    && SetPropertyCommand::ReadValue(*collider, m_menuAddress.typeId, path, before))
                {
                    Array<Vec2> after = m_outlineScratch;
                    if (PolygonEditModel::RemoveVertex(after, m_menuVertex, minimum))
                    {
                        CommitPoints(m_menuAddress, before, after);
                    }
                }
            }
        }
        Widget::EndContextMenu();
        return true;
    }


    void CanvasViewPanel::DrawSelectionOutlines(const ViewRect& rect)
    {
        const Array<GameObject*> selected = m_editor->GetSelectedObjects();
        if (selected.Size() == 0)
        {
            return;
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        GameObject* primary = m_editor->GetSelectedObject();
        for (std::size_t index = 0; index < selected.Size(); ++index)
        {
            GameObject* object = selected[index];
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            if (object == nullptr || false == GetWorldBounds(*object, minX, minY, maxX, maxY))
            {
                continue;
            }
            float x0 = 0.0f;
            float y0 = 0.0f;
            float x1 = 0.0f;
            float y1 = 0.0f;
            WorldToScreen(rect, minX, maxY, x0, y0);
            WorldToScreen(rect, maxX, minY, x1, y1);
            // **주된 것은 더 밝다.** 여럿 골랐을 때 어느 것이 인스펙터에 있는지가
            // 화면에서 보여야 한다.
            const ImU32 color = object == primary
                ? IM_COL32(255, 168, 64, 255)
                : IM_COL32(255, 168, 64, 140);
            // **그림의 모양을 두를 수 있으면 그렇게 한다**(D-149). 사각형만 두르면 그림이
            // 칸의 한 귀퉁이에만 있을 때 빈자리까지 테두리가 둘러쳐진다.
            if (DrawSpriteContour(rect, *object, color))
            {
                continue;
            }
            draw->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, 0.0f, 0, 1.5f);
        }
    }

    bool CanvasViewPanel::DrawSpriteContour(
        const ViewRect& rect, GameObject& object, ImU32 color)
    {
        Canvas* canvas = m_editor->GetCanvas();
        const AssetSystem* assets = m_editor->GetAssetSystem();
        if (canvas == nullptr || assets == nullptr)
        {
            return false;
        }
        Component::SpriteRenderer2D* sprite =
            canvas->FindComponentRaw<Component::SpriteRenderer2D>(&object);
        Component::Transform2D* transform =
            canvas->FindComponentRaw<Component::Transform2D>(&object);
        if (sprite == nullptr || transform == nullptr)
        {
            return false;
        }
        const SpriteData* data = assets->GetSprite(sprite->sprite);
        if (data == nullptr || data->frames.IsEmpty() || data->options.pixelsPerUnit <= 0.0f)
        {
            return false;
        }
        const std::size_t frameIndex = sprite->frameIndex < data->frames.Size()
            ? sprite->frameIndex
            : data->frames.Size() - 1;
        const SpriteFrame& frame = data->frames[frameIndex];
        // 모양을 가진 쪽은 텍스처다. 스프라이트는 그 텍스처의 어느 칸인지를 안다.
        const Array<EditorSpriteContours::Segment>* segments =
            m_editor->GetSpriteContour(data->texture, frame);
        if (segments == nullptr || segments->IsEmpty())
        {
            return false;
        }

        // 칸 안의 비율을 월드로 편다. 크기와 피벗은 `GetWorldBounds` 와 같은 셈이다 -
        // 둘이 갈리면 두른 선과 집는 칸이 서로 다른 자리를 가리킨다.
        float widthUnits = static_cast<float>(frame.width) / data->options.pixelsPerUnit;
        float heightUnits = static_cast<float>(frame.height) / data->options.pixelsPerUnit;
        float pivotX = frame.pivotX;
        float pivotY = frame.pivotY;
        if (sprite->sizeMode == Component::SpriteSizeMode::Custom)
        {
            widthUnits = sprite->size.x;
            heightUnits = sprite->size.y;
        }
        if (sprite->pivotMode == Component::SpritePivotMode::Custom)
        {
            pivotX = sprite->pivot.x;
            pivotY = sprite->pivot.y;
        }
        const Vec2 center = transform->worldValid ? transform->worldPosition : transform->position;
        const Vec2 scale = transform->worldValid ? transform->worldScale : transform->scale;
        const float angle = transform->worldValid ? transform->worldRotation : transform->rotation;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float worldWidth = widthUnits * scale.x;
        const float worldHeight = heightUnits * scale.y;

        // 칸 좌표의 y 는 **아래로** 간다(그림의 왼쪽 위가 원점). 월드의 y 는 위로 가므로 뒤집는다.
        const auto toScreen = [&](float u, float v, float& screenX, float& screenY) {
            const float localX = (u - pivotX) * worldWidth;
            const float localY = (pivotY - v) * worldHeight;
            const float worldX = center.x + localX * cosine - localY * sine;
            const float worldY = center.y + localX * sine + localY * cosine;
            WorldToScreen(rect, worldX, worldY, screenX, screenY);
        };

        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index < segments->Size(); ++index)
        {
            const EditorSpriteContours::Segment& segment = (*segments)[index];
            float x0 = 0.0f;
            float y0 = 0.0f;
            float x1 = 0.0f;
            float y1 = 0.0f;
            toScreen(segment.x0, segment.y0, x0, y0);
            toScreen(segment.x1, segment.y1, x1, y1);
            draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
        }
        return true;
    }

    GameObject* CanvasViewPanel::PickAt(
        const ViewRect& rect, float screenX, float screenY) const
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return nullptr;
        }
        float worldX = 0.0f;
        float worldY = 0.0f;
        ScreenToWorld(rect, screenX, screenY, worldX, worldY);

        // **작은 것이 이긴다.** 큰 배경 위에 놓인 작은 오브젝트를 집을 수 있어야 한다.
        GameObject* best = nullptr;
        float bestArea = 0.0f;
        canvas->ForEachObject([&](GameObject& object)
        {
            // 캔버스 뷰에서 감춘 오브젝트는 그리지도 집지도 않는다(D-163, 기존 `EditorHidden`).
            if (object.IsEditorHidden())
            {
                return;
            }
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            if (false == GetWorldBounds(object, minX, minY, maxX, maxY))
            {
                return;
            }
            if (worldX < minX || worldX > maxX || worldY < minY || worldY > maxY)
            {
                return;
            }
            const float area = (maxX - minX) * (maxY - minY);
            if (best == nullptr || area < bestArea)
            {
                best = &object;
                bestArea = area;
            }
        });
        return best;
    }

    bool CanvasViewPanel::PointerInView(const ViewRect& rect) const
    {
        // **입력 자리의 hover 로는 잴 수 없다**(D-179, D-170 과 같은 까닭). 그것은 기즈모를
        // 그리기 **전에** 재는 값이라, 손잡이가 앞 프레임부터 hover 를 쥐고 있으면 거짓이다 -
        // 고른 오브젝트의 한가운데에는 늘 손잡이가 있다. 창 위에 마우스가 있고 그 자리가
        // 뷰 안이면 그것으로 충분하다.
        // **무언가를 끌고 지나가는 중이면 아니다.** 에셋을 끌어 인스펙터로 가져가다 이 화면을
        // 지나면, 그것이 사각 선택의 시작이 되어 꾸러미를 집어삼킨다(테스트가 그것을 잡았다).
        if (ImGui::GetDragDropPayload() != nullptr)
        {
            return false;
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool inside = mouse.x >= rect.left && mouse.x < rect.left + rect.width
            && mouse.y >= rect.top && mouse.y < rect.top + rect.height;
        return inside
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    }

    GameObject* CanvasViewPanel::GetFocus() const
    {
        return m_focus != 0 ? m_editor->GetObjectIds().Resolve(m_focus) : nullptr;
    }

    GameObject* CanvasViewPanel::MapToLevel(GameObject* hit) const
    {
        GameObject* focus = GetFocus();
        for (GameObject* at = hit; at != nullptr; at = at->GetParent())
        {
            if (focus != nullptr && at == focus)
            {
                // 들어간 오브젝트 자신의 몸을 눌렀다. 자식이 아니어도 그것은 고를 수 있어야 한다.
                return focus;
            }
            GameObject* parent = at->GetParent();
            if (focus == nullptr ? parent == nullptr : parent == focus)
            {
                return at;
            }
        }
        return nullptr;
    }

    void CanvasViewPanel::DrawOverlay(const ViewRect& rect)
    {
        // **화면 왼쪽 위에 지금 상태를 적는다**(D-172, 기존 캔버스 뷰의 텍스트 오버레이).
        // 고른 것과 편집 카메라의 자리는 화면만 보고는 알 수 없다 - 배율을 얼마나 당겨 두었는지,
        // 여럿 골랐는지 하나 골랐는지가 인스펙터를 봐야 드러났다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float y = rect.top + 8.0f;
        const float x = rect.left + 8.0f;
        const auto line = [&](const char* text, ImU32 color)
        {
            const ImVec2 extent = ImGui::CalcTextSize(text);
            draw->AddRectFilled(ImVec2(x - 4.0f, y - 2.0f),
                ImVec2(x + extent.x + 4.0f, y + extent.y + 2.0f), IM_COL32(20, 21, 26, 190), 3.0f);
            draw->AddText(ImVec2(x, y), color, text);
            y += extent.y + 4.0f;
        };

        char text[256] = {};
        // 고른 것을 적는 규칙은 에디터가 안다(D-172). 상태를 아는 쪽이 그 글도 낸다 -
        // 화면마다 다시 쓰면 이름 없는 것과 여럿 고른 것을 저마다 다르게 적는다.
        m_editor->DescribeSelection(text, sizeof(text));
        line(text, IM_COL32(210, 216, 224, 255));

        if (Is3D())
        {
            std::snprintf(text, sizeof(text),
                Loc::TextOr(LocKeys::CanvasViewCamera3DFormat, "camera yaw %.0f pitch %.0f distance %.1f"),
                m_yawDegrees, m_pitchDegrees, m_distance);
        }
        else
        {
            // **-0.00 은 0 이다.** 부호 있는 0 이 화면에 나오면 어딘가 틀린 것처럼 읽힌다
            // (격자 라벨도 같은 이유로 고쳤다, D-162).
            const auto tidy = [](float value)
            {
                return value > -0.005f && value < 0.005f ? 0.0f : value;
            };
            std::snprintf(text, sizeof(text),
                Loc::TextOr(LocKeys::CanvasViewCameraFormat, "camera (%.2f, %.2f) size %.2f"),
                tidy(m_centerX), tidy(m_centerY), m_orthographicSize);
        }
        line(text, IM_COL32(150, 158, 170, 255));

        // 들어가 있으면 그 사실과 나오는 법을 적는다. 모르면 왜 부모가 안 잡히는지 알 수 없다.
        if (GameObject* focus = GetFocus())
        {
            const char* name = focus->GetTag();
            std::snprintf(text, sizeof(text),
                Loc::TextOr(LocKeys::CanvasViewInsideFormat,
                    "inside %s - double-click empty space to leave"),
                name != nullptr && name[0] != '\0' ? name : "?");
            line(text, IM_COL32(255, 220, 120, 230));
        }
    }

    void CanvasViewPanel::HandlePicking(const ViewRect& rect, bool hovered)
    {
        // 기즈모를 잡고 있는 중이면 고르지 않는다. 손잡이를 놓는 것이 빈 곳을 누른 것이
        // 되면 끌 때마다 선택이 풀린다.
        // 손잡이 위에서 놓은 것은 고르기가 아니다. 끌지 않고 눌렀다 뗀 것도 마찬가지다 -
        // 기즈모를 건드릴 때마다 선택이 바뀌면 여럿 골라 놓고 옮길 수 없다.
        // **손잡이 위에서 두 번 눌러도 들어간다**(D-179). `hovered` 로 재면 오브젝트의
        // 한가운데 - 늘 기즈모가 있는 자리 - 에서는 두 번 누르기가 없는 일이 된다.
        const bool pointerHere = PointerInView(rect);
        if (pointerHere && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_doubleClick = true;
        }
        if (false == pointerHere || m_gizmoState.dragging || m_editing.IsActive()
            || m_boxSelecting || m_vertexDragging || m_vertexPressed)
        {
            m_vertexPressed = m_vertexPressed && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            return;
        }
        // 버텍스 손잡이나 변 위에서 누른 것은 고르기가 아니다 - 누른 순간 이미 끌기나 버텍스 더하기가 되었다.
        if (m_polygonHover.kind != PolygonEditModel::HitKind::None)
        {
            return;
        }
        // **손잡이 위에서 한 번 누른 것은 고르기가 아니다.** 기즈모를 건드릴 때마다 선택이
        // 바뀌면 여럿 골라 놓고 옮길 수 없다. 두 번 누르기는 들어가기이므로 통과시킨다 -
        // 그러지 않으면 고른 오브젝트의 한가운데로는 영영 들어갈 수 없다.
        if (m_gizmoState.hovered != GizmoAxis::None && false == m_doubleClick)
        {
            return;
        }
        if (false == ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            || Widget::MouseWasDragged(ImGuiMouseButton_Left))
        {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        GameObject* picked = MapToLevel(PickAt(rect, io.MousePos.x, io.MousePos.y));
        if (m_doubleClick)
        {
            m_doubleClick = false;
            GameObject* focus = GetFocus();
            if (picked != nullptr && picked != focus)
            {
                // **두 번 누르면 그 안으로 들어간다.** 그 뒤로는 이것의 직계 자식이 고르는 단위다.
                m_focus = m_editor->GetObjectIds().Track(picked);
                m_editor->SetSelectedObject(picked);
            }
            else if (picked == nullptr && focus != nullptr)
            {
                // 빈 곳을 두 번 누르면 **한 층 나온다.** 나온 오브젝트를 골라 두어 어디서 나왔는지 보인다.
                GameObject* parent = focus->GetParent();
                m_focus = parent != nullptr ? m_editor->GetObjectIds().Track(parent) : 0;
                m_editor->SetSelectedObject(focus);
            }
            return;
        }
        if (picked == nullptr)
        {
            if (false == io.KeyCtrl && false == io.KeyShift)
            {
                m_editor->ClearSelection();
            }
            return;
        }
        // 계층과 같은 손놀림이다: Ctrl·Shift 는 하나씩 붙였다 뗐다, 맨 클릭은 통째로.
        if (io.KeyCtrl || io.KeyShift)
        {
            if (m_editor->IsSelected(picked))
            {
                m_editor->RemoveFromSelection(picked);
            }
            else
            {
                m_editor->AddToSelection(picked);
            }
        }
        else
        {
            m_editor->SetSelectedObject(picked);
        }
    }

    void CanvasViewPanel::HandleBoxSelect(const ViewRect& rect, bool hovered)
    {
        // 기즈모를 잡고 있으면 상자를 시작하지 않는다. 손잡이를 끄는 것이 곧 상자가 되면
        // 옮길 때마다 선택이 통째로 바뀐다.
        if (m_gizmoState.dragging || m_editing.IsActive() || m_panning || m_vertexDragging || m_vertexPressed)
        {
            m_boxSelecting = false;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (false == m_boxSelecting)
        {
            // **임계값을 넘어야 시작한다.** 넘기 전에 시작하면 그냥 클릭한 것도 빈 상자가
            // 되어, 무언가를 고르려던 손짓이 선택을 푸는 손짓이 된다.
            const bool startable = PointerInView(rect)
                && m_gizmoState.hovered == GizmoAxis::None
                && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                && Widget::MouseWasDragged(ImGuiMouseButton_Left);
            if (false == startable)
            {
                return;
            }
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            m_boxStart = ImVec2(io.MousePos.x - delta.x, io.MousePos.y - delta.y);
            // 누르기 시작한 자리에 무언가 있었으면 그것을 끌려던 것이다 - 상자가 아니다.
            if (PickAt(rect, m_boxStart.x, m_boxStart.y) != nullptr)
            {
                return;
            }
            m_boxSelecting = true;
        }

        const ImVec2 current = io.MousePos;
        const float left = (std::min)(m_boxStart.x, current.x);
        const float right = (std::max)(m_boxStart.x, current.x);
        const float top = (std::min)(m_boxStart.y, current.y);
        const float bottom = (std::max)(m_boxStart.y, current.y);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
            IM_COL32(120, 180, 255, 40));
        draw->AddRect(ImVec2(left, top), ImVec2(right, bottom),
            IM_COL32(120, 180, 255, 200));

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            return;
        }
        m_boxSelecting = false;

        // ── 놓았다. 상자에 **닿은** 것을 모은다. ─────────────────────────
        //
        // 완전히 들어온 것만 고르는 쪽도 있지만, 큰 배경을 걸치기만 해도 잡히는 쪽이
        // 2D 편집에서는 손에 붙는다. 기존 엔진도 겹침 기준이다.
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        float worldLeft = 0.0f;
        float worldTop = 0.0f;
        float worldRight = 0.0f;
        float worldBottom = 0.0f;
        ScreenToWorld(rect, left, bottom, worldLeft, worldBottom);
        ScreenToWorld(rect, right, top, worldRight, worldTop);

        Array<GameObject*> hit;
        canvas->ForEachObject([&](GameObject& object)
        {
            // 캔버스 뷰에서 감춘 오브젝트는 그리지도 집지도 않는다(D-163, 기존 `EditorHidden`).
            if (object.IsEditorHidden())
            {
                return;
            }
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            if (false == GetWorldBounds(object, minX, minY, maxX, maxY))
            {
                return;
            }
            if (maxX < worldLeft || minX > worldRight
                || maxY < worldBottom || minY > worldTop)
            {
                return;
            }
            // 지금 층의 오브젝트로 올린다(D-157). 한 부모의 조각 여럿이 걸려도 부모는 한 번만 든다.
            GameObject* level = MapToLevel(&object);
            if (level == nullptr)
            {
                return;
            }
            for (std::size_t index = 0; index < hit.Size(); ++index)
            {
                if (hit[index] == level)
                {
                    return;
                }
            }
            hit.Add(level);
        });

        // Ctrl·Shift 는 더한다. 맨 끌기는 통째로 바꾼다 - 계층·클릭과 같은 손놀림이다.
        if (false == io.KeyCtrl && false == io.KeyShift)
        {
            m_editor->ClearSelection();
        }
        for (std::size_t index = 0; index < hit.Size(); ++index)
        {
            m_editor->AddToSelection(hit[index]);
        }
    }

    void CanvasViewPanel::DrawContextMenu(const ViewRect& rect)
    {
        // 화면을 옮기려고 오른쪽 단추를 끌었으면 메뉴를 열지 않는다.
        if (m_panMoved)
        {
            if (false == ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                m_panMoved = false;
            }
            return;
        }
        if (m_editor->GetCanvas() == nullptr)
        {
            return;
        }
        if (DrawVertexMenu(rect))
        {
            return;
        }
        // **메뉴를 여는 때를 우리가 정한다**(D-170). ImGui 의 창 메뉴는 그 자리에 위젯이 있으면
        // 열지 않는데, 고른 오브젝트 위에는 늘 기즈모 손잡이가 있다 - 그래서 오브젝트의
        // 한가운데를 우클릭하면 아무 메뉴도 열리지 않았다.
        //
        // 입력 자리(`##canvas`)의 hover 도 쓰지 못한다. 그것은 기즈모를 그리기 **전에** 재는데,
        // 손잡이가 앞 프레임부터 hover 를 쥐고 있으면 거짓이다 - 같은 자리가 또 막힌다.
        // 창 위에 마우스가 있고 그 자리가 뷰 안이면 그것으로 충분하다.
        if (PointerInView(rect) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            Widget::OpenContextMenu("##CanvasViewMenu");
        }
        if (false == Widget::BeginOpenedContextMenu("##CanvasViewMenu"))
        {
            return;
        }
        // **누른 자리에 만든다**(D-168, 기존 `spawnWorldPos`). 원점에 만들면 화면 밖에
        // 생기기도 해서, 만든 것을 찾으러 화면을 끌어야 했다. 메뉴가 열릴 때의 자리를
        // 묻는다 - 그 뒤에 마우스가 항목 위로 움직이기 때문이다.
        const ImVec2 opened = ImGui::GetMousePosOnOpeningCurrentPopup();
        ObjectPlacement placement;
        if (false == Is3D())
        {
            ScreenToWorld(rect, opened.x, opened.y, placement.position[0], placement.position[1]);
            placement.hasPosition = true;
        }
        // **누른 자리에 오브젝트가 있으면 그것의 메뉴다**(D-170, 기존 캔버스 뷰도 같다).
        // 빈 곳이면 빈자리 메뉴다. 둘 다 계층과 **같은 한 벌**이다(D-132) -
        // 두 화면의 메뉴가 갈라지지 않는다.
        GameObject* under = Is3D()
            ? nullptr
            : MapToLevel(PickAt(rect, opened.x, opened.y));
        if (under != nullptr)
        {
            EditorActions::DrawObjectMenu(*m_editor, *under, placement);
        }
        else
        {
            EditorActions::DrawBackgroundMenu(*m_editor, placement);
        }
        Widget::EndContextMenu();
    }

    void CanvasViewPanel::FrameSelection()
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool any = false;
        auto include = [&](const GameObject& object)
        {
            float x0 = 0.0f;
            float y0 = 0.0f;
            float x1 = 0.0f;
            float y1 = 0.0f;
            if (false == GetWorldBounds(object, x0, y0, x1, y1))
            {
                return;
            }
            if (false == any)
            {
                minX = x0;
                minY = y0;
                maxX = x1;
                maxY = y1;
                any = true;
                return;
            }
            minX = (std::min)(minX, x0);
            minY = (std::min)(minY, y0);
            maxX = (std::max)(maxX, x1);
            maxY = (std::max)(maxY, y1);
        };

        const Array<GameObject*> selected = m_editor->GetSelectedObjects();
        if (selected.Size() != 0)
        {
            for (std::size_t index = 0; index < selected.Size(); ++index)
            {
                if (const GameObject* object = selected[index])
                {
                    include(*object);
                }
            }
        }
        else
        {
            // 고른 것이 없으면 캔버스 전체를 담는다. 길을 잃었을 때 돌아오는 단추다.
            canvas->ForEachObject([&](GameObject& object) { include(object); });
        }
        if (false == any)
        {
            m_centerX = 0.0f;
            m_centerY = 0.0f;
            m_orthographicSize = 5.0f;
            return;
        }
        m_centerX = (minX + maxX) * 0.5f;
        m_centerY = (minY + maxY) * 0.5f;
        // 가장자리에 붙지 않게 조금 넓게 잡는다.
        const float halfHeight = (maxY - minY) * 0.5f * 1.2f;
        const float halfWidth = (maxX - minX) * 0.5f * 1.2f;
        m_orthographicSize = std::clamp(
            (std::max)(halfHeight, halfWidth * 0.75f),
            MinOrthographicSize, MaxOrthographicSize);
    }

    bool CanvasViewPanel::MakeGizmoCamera(const ViewRect& rect, GizmoCamera& camera) const
    {
        if (Is3D())
        {
            // **렌더러가 이번 프레임에 쓴 편집 카메라를 그대로 쓴다**(D-140). 여기서 같은
            // 궤도 행렬을 한 번 더 세우면 둘로 갈려, 한쪽만 고쳐졌을 때 손잡이가 그림과
            // 다른 자리에 선다. UI 가 엔진 프레임보다 먼저 만들어지므로 한 프레임 전의 것이다.
            Renderer* renderer = m_editor->GetRenderer();
            CameraParams drawn;
            if (renderer == nullptr || false == renderer->GetLastEditorViewCamera(drawn))
            {
                return false;
            }
            // 그 카메라의 뷰포트는 편집 화면 텍스처의 픽셀이다. 텍스처는 패널보다 크게
            // 잡혀 있고(64 의 배수) 패널 크기만큼만 잘라 붙였으므로, NDC 가 앉는 자리는
            // **텍스처 크기**의 사각형이다 - 그 왼쪽 위가 그림의 왼쪽 위와 같다.
            const Extent2D extent = m_editor->GetCanvasViewExtent();
            if (extent.width == 0 || extent.height == 0)
            {
                return false;
            }
            return GizmoModel::MakeCamera(drawn.view, drawn.projection,
                rect.left, rect.top,
                static_cast<float>(extent.width), static_cast<float>(extent.height), camera);
        }

        // 2D 는 우리가 카메라를 다 안다. 엔진이 캔버스 뷰를 그릴 때 쓰는 것과 같은 식이다.
        // **크기는 그린 화면의 것이다**(D-150) - 3D 가 텍스처 크기를 쓰는 것과 같은 이유다.
        const float drawWidth = rect.drawWidth > 0.0f ? rect.drawWidth : rect.width;
        const float drawHeight = rect.drawHeight > 0.0f ? rect.drawHeight : rect.height;
        const float halfHeight = m_orthographicSize;
        const float halfWidth = drawHeight > 0.0f
            ? halfHeight * drawWidth / drawHeight
            : halfHeight;
        constexpr float NearPlane = -100.0f;
        constexpr float FarPlane = 100.0f;
        constexpr float Depth = FarPlane - NearPlane;
        const Matrix4x4 view{{
            1.0f, 0.0f, 0.0f, -m_centerX,
            0.0f, 1.0f, 0.0f, -m_centerY,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        const Matrix4x4 projection{{
            1.0f / halfWidth, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f / halfHeight, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / Depth, -NearPlane / Depth,
            0.0f, 0.0f, 0.0f, 1.0f}};
        return GizmoModel::MakeCamera(
            view, projection, rect.left, rect.top, drawWidth, drawHeight, camera);
    }

    void CanvasViewPanel::DrawGrid3D(const ViewRect& rect)
    {
        GizmoCamera camera;
        if (false == MakeGizmoCamera(rect, camera))
        {
            return;
        }
        // **바닥은 y=0 평면이다.** 격자를 카메라의 높이에 맞춰 띄우면 무엇이 바닥인지
        // 알 수 없게 되고, 오브젝트를 놓을 때 기준이 사라진다.
        const Vec3 look{m_centerX, m_centerY, m_centerZ};

        // 간격은 2D 와 같은 눈금(1·2·5·10 …)을 쓴다. 다만 배율을 직교 크기가 아니라
        // **바라보는 점에서 한 단위가 몇 픽셀로 보이는지**로 잰다 - 원근에서는 거리가
        // 배율이고, 그 거리는 바라보는 점의 것을 대표로 삼는다.
        float centerScreenX = 0.0f;
        float centerScreenY = 0.0f;
        float unitScreenX = 0.0f;
        float unitScreenY = 0.0f;
        if (false == GizmoModel::Project(camera, look, centerScreenX, centerScreenY)
            || false == GizmoModel::Project(
                camera, Vec3{look.x + 1.0f, look.y, look.z}, unitScreenX, unitScreenY))
        {
            return;
        }
        const float dx = unitScreenX - centerScreenX;
        const float dy = unitScreenY - centerScreenY;
        const float pixelsPerUnit = std::sqrt(dx * dx + dy * dy);
        if (false == std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.001f)
        {
            return;
        }
        const float step = ChooseGridStep(1.0f / pixelsPerUnit);
        if (false == std::isfinite(step) || step <= 0.0f)
        {
            return;
        }

        // **끝이 있는 격자다.** 평면은 지평선까지 이어지지만, 거기까지 선을 그으면
        // 먼 쪽이 한 덩어리로 뭉쳐 잡음이 된다. 바라보는 점 둘레의 칸만 그린다.
        constexpr int HalfLines = 20;
        const float half = step * static_cast<float>(HalfLines);
        const float baseX = std::floor(look.x / step) * step;
        const float baseZ = std::floor(look.z / step) * step;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 line = IM_COL32(255, 255, 255, 18);
        const ImU32 strong = IM_COL32(255, 255, 255, 38);
        const ImU32 axisX = IM_COL32(220, 90, 90, 160);
        const ImU32 axisZ = IM_COL32(90, 130, 220, 160);

        // 한 선을 토막 내어 잇는다. **양 끝만 투영하면 안 된다** - 선이 카메라 평면을
        // 가로지르면 한쪽 끝이 뒤에 있어 투영이 없고, 그러면 선 전체가 사라진다.
        constexpr int Segments = 16;
        auto drawWorldLine = [&](const Vec3& from, const Vec3& to, ImU32 color, float thickness)
        {
            float previousX = 0.0f;
            float previousY = 0.0f;
            bool hasPrevious = false;
            for (int index = 0; index <= Segments; ++index)
            {
                const float t = static_cast<float>(index) / static_cast<float>(Segments);
                const Vec3 point{
                    from.x + (to.x - from.x) * t,
                    from.y + (to.y - from.y) * t,
                    from.z + (to.z - from.z) * t};
                float x = 0.0f;
                float y = 0.0f;
                if (false == GizmoModel::Project(camera, point, x, y))
                {
                    hasPrevious = false;
                    continue;
                }
                if (hasPrevious)
                {
                    draw->AddLine(ImVec2(previousX, previousY), ImVec2(x, y), color, thickness);
                }
                previousX = x;
                previousY = y;
                hasPrevious = true;
            }
        };

        for (int index = -HalfLines; index <= HalfLines; ++index)
        {
            const float offset = step * static_cast<float>(index);
            const float x = baseX + offset;
            const float z = baseZ + offset;
            // 열 칸마다 한 줄은 진하게. 2D 와 같은 규칙이라 배율이 같은 방식으로 읽힌다.
            const bool tenthX = std::fabs(std::fmod(x / step, 10.0f)) < 0.001f;
            const bool tenthZ = std::fabs(std::fmod(z / step, 10.0f)) < 0.001f;
            drawWorldLine(Vec3{x, 0.0f, baseZ - half}, Vec3{x, 0.0f, baseZ + half},
                tenthX ? strong : line, 1.0f);
            drawWorldLine(Vec3{baseX - half, 0.0f, z}, Vec3{baseX + half, 0.0f, z},
                tenthZ ? strong : line, 1.0f);
        }

        // 원점의 두 축. 어디가 (0,0,0) 인지 바닥에서 바로 보여야 한다.
        drawWorldLine(Vec3{baseX - half, 0.0f, 0.0f}, Vec3{baseX + half, 0.0f, 0.0f}, axisX, 1.5f);
        drawWorldLine(Vec3{0.0f, 0.0f, baseZ - half}, Vec3{0.0f, 0.0f, baseZ + half}, axisZ, 1.5f);
    }

    void CanvasViewPanel::DrawSelectionMarkers3D(const ViewRect& rect)
    {
        Canvas* canvas = m_editor->GetCanvas();
        const Array<GameObject*> selected = m_editor->GetSelectedObjects();
        if (canvas == nullptr || selected.Size() == 0)
        {
            return;
        }
        GizmoCamera camera;
        if (false == MakeGizmoCamera(rect, camera))
        {
            return;
        }
        // **점 하나를 찍는다.** 메시의 실제 크기를 에디터가 알 길이 아직 없어서
        // 상자를 두르지 못한다 - 짐작한 크기로 두르면 맞지 않는 테두리가 되고,
        // 맞지 않는 테두리는 없는 것보다 나쁘다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        GameObject* primary = m_editor->GetSelectedObject();
        for (std::size_t index = 0; index < selected.Size(); ++index)
        {
            GameObject* object = selected[index];
            if (object == nullptr)
            {
                continue;
            }
            Component::Transform3D* transform =
                canvas->FindComponentRaw<Component::Transform3D>(object);
            if (transform == nullptr)
            {
                continue;
            }
            const Vec3 at = transform->worldValid ? transform->worldPosition : transform->position;
            float x = 0.0f;
            float y = 0.0f;
            if (false == GizmoModel::Project(camera, at, x, y))
            {
                continue;
            }
            const ImU32 color = object == primary
                ? IM_COL32(255, 168, 64, 255)
                : IM_COL32(255, 168, 64, 140);
            constexpr float Half = 6.0f;
            draw->AddRect(ImVec2(x - Half, y - Half), ImVec2(x + Half, y + Half), color, 0.0f, 0, 1.5f);
        }
    }

    void CanvasViewPanel::HandlePicking3D(const ViewRect& rect, bool hovered)
    {
        if (false == PointerInView(rect) || m_gizmoState.dragging || m_editing.IsActive()
            || m_gizmoState.hovered != GizmoAxis::None)
        {
            return;
        }
        if (false == ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            || Widget::MouseWasDragged(ImGuiMouseButton_Left))
        {
            return;
        }
        Canvas* canvas = m_editor->GetCanvas();
        GizmoCamera camera;
        if (canvas == nullptr || false == MakeGizmoCamera(rect, camera))
        {
            return;
        }

        // **화면에서 가까운 것을 고른다.** 메시의 크기를 모르므로 월드의 광선 교차 대신
        // 오브젝트의 자리를 화면으로 투영해 마우스와의 거리를 잰다 - 이것이 3D 에서
        // "눌러서 고르기" 가 실제로 하는 일에 가장 가깝고, 짐작한 상자보다 덜 틀린다.
        const ImGuiIO& io = ImGui::GetIO();
        constexpr float PickRadius = 18.0f;
        GameObject* best = nullptr;
        float bestDistance = PickRadius * PickRadius;
        // **3D 텍스트는 글자 블록으로 고른다**(D-222). 크기를 아는 유일한 3D 그림이다 - 블록의 네 모서리를 화면으로 투영한 사각형 안을 누르면
        // 그 텍스트다(자리 투영보다 먼저 이긴다). 빌보드의 모서리는 편집 카메라의 오른쪽·위 축으로 편다(그린 것과 같은 카메라).
        System::Text3DSystem* texts = canvas->GetSystems().FindSystem<System::Text3DSystem>();
        Vec3 cameraRight{1.0f, 0.0f, 0.0f};
        Vec3 cameraUp{0.0f, 1.0f, 0.0f};
        CameraParams drawn;
        if (Renderer* renderer = m_editor->GetRenderer(); renderer != nullptr && renderer->GetLastEditorViewCamera(drawn))
        {
            // 뷰 행렬의 왼쪽 위 3x3 은 카메라 회전의 전치다 - 첫 행이 카메라의 오른쪽, 둘째 행이 위다.
            cameraRight = Vec3{drawn.view.values[0], drawn.view.values[1], drawn.view.values[2]};
            cameraUp = Vec3{drawn.view.values[4], drawn.view.values[5], drawn.view.values[6]};
        }
        const auto insideText = [&](GameObject& object, const Component::Transform3D& transform) -> bool {
            if (texts == nullptr)
            {
                return false;
            }
            Component::Text3D* text = canvas->FindComponentRaw<Component::Text3D>(&object);
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            if (text == nullptr || false == text->visible
                || false == texts->GetLocalBounds(text->GetInstanceId(), minX, minY, maxX, maxY))
            {
                return false;
            }
            const Vec3 origin = transform.worldValid ? transform.worldPosition : transform.position;
            const Quaternion rotation = transform.worldValid ? transform.worldRotation : transform.rotation;
            const Vec3 scale = transform.worldValid ? transform.worldScale : transform.scale;
            const bool billboard = text->facing == Component::TextFacing3D::Billboard;
            const Vec3 axisX = billboard ? Scale(cameraRight, scale.x) : Rotate(rotation, Vec3{scale.x, 0.0f, 0.0f});
            const Vec3 axisY = billboard ? Scale(cameraUp, scale.y) : Rotate(rotation, Vec3{0.0f, scale.y, 0.0f});
            float screenMinX = 0.0f;
            float screenMinY = 0.0f;
            float screenMaxX = 0.0f;
            float screenMaxY = 0.0f;
            if (false == GizmoModel::ProjectPlaneRect(camera, origin, axisX, axisY, minX, minY, maxX, maxY, screenMinX,
                    screenMinY, screenMaxX, screenMaxY))
            {
                return false;
            }
            return io.MousePos.x >= screenMinX && io.MousePos.x <= screenMaxX && io.MousePos.y >= screenMinY
                && io.MousePos.y <= screenMaxY;
        };
        canvas->ForEachObject([&](GameObject& object)
        {
            // 캔버스 뷰에서 감춘 오브젝트는 그리지도 집지도 않는다(D-163, 기존 `EditorHidden`).
            if (object.IsEditorHidden())
            {
                return;
            }
            Component::Transform3D* transform =
                canvas->FindComponentRaw<Component::Transform3D>(&object);
            if (transform == nullptr)
            {
                return;
            }
            if (insideText(object, *transform))
            {
                best = &object;
                bestDistance = -1.0f;
                return;
            }
            const Vec3 at = transform->worldValid ? transform->worldPosition : transform->position;
            float x = 0.0f;
            float y = 0.0f;
            if (false == GizmoModel::Project(camera, at, x, y))
            {
                return;
            }
            const float dx = x - io.MousePos.x;
            const float dy = y - io.MousePos.y;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance)
            {
                best = &object;
                bestDistance = distance;
            }
        });

        best = MapToLevel(best);
        if (best == nullptr)
        {
            if (false == io.KeyCtrl && false == io.KeyShift)
            {
                m_editor->ClearSelection();
            }
            return;
        }
        if (io.KeyCtrl || io.KeyShift)
        {
            if (m_editor->IsSelected(best))
            {
                m_editor->RemoveFromSelection(best);
            }
            else
            {
                m_editor->AddToSelection(best);
            }
        }
        else
        {
            m_editor->SetSelectedObject(best);
        }
    }

    void CanvasViewPanel::DrawGizmo(const ViewRect& rect)
    {
        // 콜라이더를 고치는 동안은 기즈모가 없다 - 버텍스 손잡이와 겹친다.
        PolygonTarget polygon;
        if (false == Is3D() && FindPolygonTarget(polygon))
        {
            if (m_gizmoState.dragging || m_editing.IsActive())
            {
                m_editing.Cancel(*m_editor);
                m_gizmoState.dragging = false;
            }
            m_gizmoState.hovered = GizmoAxis::None;
            return;
        }
        GameObject* selected = m_editor->GetSelectedObject();
        GizmoSubject subject;
        const bool hasSubject = selected != nullptr
            && GizmoEditing::ReadSubject(*m_editor, *selected, subject);
        if (false == hasSubject && false == m_gizmoState.dragging)
        {
            return;
        }
        if (m_gizmoState.dragging && false == hasSubject)
        {
            m_editing.Cancel(*m_editor);
            m_gizmoState.dragging = false;
            return;
        }

        GizmoCamera camera;
        if (false == MakeGizmoCamera(rect, camera))
        {
            // 아직 그린 프레임이 없다(3D 의 첫 프레임). 끌던 것이 있으면 놓는다.
            if (m_gizmoState.dragging || m_editing.IsActive())
            {
                m_editing.Cancel(*m_editor);
                m_gizmoState.dragging = false;
            }
            return;
        }

        // **월드 축으로 보면 손잡이의 회전만 지운다**(D-171). 옮기기·돌리기는 월드 델타로 쓰므로
        // (`GizmoEditing::Write`) 주체의 회전은 손잡이가 어느 쪽을 가리키는지만 정한다.
        if (m_gizmoSpace == GizmoSpace::World && m_gizmoMode != GizmoMode::Scale)
        {
            subject.rotation = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
        }
        const GizmoSubject shown = m_gizmoState.dragging ? m_gizmoState.drag.start : subject;
        const Widget::GizmoOutput output =
            Widget::Gizmo(m_gizmoMode, camera, shown, m_gizmoState, true);
        if (output.dragStarted)
        {
            if (false == m_editing.Begin(*m_editor, m_gizmoMode, shown))
            {
                m_gizmoState.dragging = false;
                return;
            }
        }
        if (output.dragging && m_editing.IsActive())
        {
            m_editing.Apply(*m_editor, output.subject);
        }
        if (output.dragEnded && m_editing.IsActive())
        {
            m_editing.Commit(*m_editor);
        }
    }
}
