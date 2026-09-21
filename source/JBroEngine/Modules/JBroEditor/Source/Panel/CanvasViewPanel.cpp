#include "CanvasViewPanel.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
        const float halfHeight = m_orthographicSize;
        const float halfWidth = rect.height > 0.0f
            ? halfHeight * rect.width / rect.height
            : halfHeight;
        // 월드는 위가 +y, 화면은 아래가 +y 다.
        screenX = rect.left + (worldX - m_centerX) / halfWidth * rect.width * 0.5f + rect.width * 0.5f;
        screenY = rect.top - (worldY - m_centerY) / halfHeight * rect.height * 0.5f + rect.height * 0.5f;
    }

    void CanvasViewPanel::ScreenToWorld(const ViewRect& rect, float screenX, float screenY,
        float& worldX, float& worldY) const
    {
        const float halfHeight = m_orthographicSize;
        const float halfWidth = rect.height > 0.0f
            ? halfHeight * rect.width / rect.height
            : halfHeight;
        if (rect.width <= 0.0f || rect.height <= 0.0f)
        {
            worldX = m_centerX;
            worldY = m_centerY;
            return;
        }
        worldX = m_centerX + (screenX - rect.left - rect.width * 0.5f) / (rect.width * 0.5f) * halfWidth;
        worldY = m_centerY - (screenY - rect.top - rect.height * 0.5f) / (rect.height * 0.5f) * halfHeight;
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
        ImGui::InvisibleButton("##canvas", available,
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
            }
            DrawSelectionOutlines(rect);
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
        DrawContextMenu();
    }

    void CanvasViewPanel::DrawToolBar()
    {
        Widget::GizmoModeBar(m_gizmoMode,
            Loc::TextOr(LocKeys::GizmoTranslate, "Move"),
            Loc::TextOr(LocKeys::GizmoRotate, "Rotate"),
            Loc::TextOr(LocKeys::GizmoScale, "Scale"), true);
        // 기즈모 모드와 보기 단추는 **다른 무리**다. 사이를 띄우고 줄을 그어 가른다 -
        // 붙여 두면 `크기` 와 `격자` 가 한 낱말처럼 읽힌다.
        Widget::ToolBarSeparator();
        if (ImGui::Button(Loc::TextOr(LocKeys::CanvasViewGrid, "Grid")))
        {
            m_showGrid = false == m_showGrid;
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::CanvasViewGridTooltip, "show or hide the grid"));
        if (false == Is3D())
        {
            // 3D 에는 그릴 콜라이더가 없다. 누를 수 없는 단추를 두면 무엇이 되는 것인지 흐려진다.
            ImGui::SameLine(0.0f, 6.0f);
            if (ImGui::Button(Loc::TextOr(LocKeys::CanvasViewColliders, "Colliders")))
            {
                m_showColliders = false == m_showColliders;
            }
            Widget::HoveredTooltip(
                Loc::TextOr(LocKeys::CanvasViewCollidersTooltip, "show or hide collider shapes"));
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(Loc::TextOr(LocKeys::CanvasViewFrame, "Frame")))
        {
            FrameSelection();
        }
        Widget::HoveredTooltip(
            Loc::TextOr(LocKeys::CanvasViewFrameTooltip, "fit the view to the selection"));
    }

    void CanvasViewPanel::HandleCameraInput(const ViewRect& rect, bool hovered)
    {
        const ImGuiIO& io = ImGui::GetIO();

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
                    const float halfHeight = m_orthographicSize;
                    const float halfWidth = rect.height > 0.0f
                        ? halfHeight * rect.width / rect.height
                        : halfHeight;
                    if (rect.width > 0.0f && rect.height > 0.0f)
                    {
                        m_centerX -= delta.x / (rect.width * 0.5f) * halfWidth;
                        m_centerY += delta.y / (rect.height * 0.5f) * halfHeight;
                    }
                }
            }
            else
            {
                m_panning = false;
            }
        }
        else if (hovered
            && (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
                || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            m_panning = true;
            m_panMoved = false;
        }

        if (false == hovered || io.MouseWheel == 0.0f)
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
        const float worldPerPixel = (m_orthographicSize * 2.0f) / rect.height;
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

        const float startX = std::floor(minX / step) * step;
        // 지난 숫자의 오른쪽 끝. 겹치면 건너뛴다 - 겹친 숫자는 둘 다 못 읽는다.
        float lastLabelEnd = -1.0e9f;
        for (float x = startX; x <= maxX; x += step)
        {
            float screenX = 0.0f;
            float unused = 0.0f;
            WorldToScreen(rect, x, 0.0f, screenX, unused);
            // 열 칸마다 한 줄은 진하게. 눈금을 세지 않아도 배율이 읽힌다.
            const bool tenth = std::fabs(std::fmod(x / step, 10.0f)) < 0.001f;
            draw->AddLine(ImVec2(screenX, rect.top), ImVec2(screenX, rect.top + rect.height),
                tenth ? strong : line);

            char text[32] = {};
            std::snprintf(text, sizeof(text), "%.4g", x);
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
        const float startY = std::floor(minY / step) * step;
        float lastLabelY = -1.0e9f;
        for (float y = startY; y <= maxY; y += step)
        {
            float screenY = 0.0f;
            float unused = 0.0f;
            WorldToScreen(rect, 0.0f, y, unused, screenY);
            const bool tenth = std::fabs(std::fmod(y / step, 10.0f)) < 0.001f;
            draw->AddLine(ImVec2(rect.left, screenY), ImVec2(rect.left + rect.width, screenY),
                tenth ? strong : line);

            if (std::fabs(screenY - lastLabelY) < labelSize * 2.2f)
            {
                continue;
            }
            lastLabelY = screenY;
            char text[32] = {};
            std::snprintf(text, sizeof(text), "%.4g", y);
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
        const Vec2 center = transform->worldValid ? transform->worldPosition : transform->position;
        const Vec2 scale = transform->worldValid ? transform->worldScale : transform->scale;

        float halfWidth = EmptyObjectHalfSize;
        float halfHeight = EmptyObjectHalfSize;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if (Component::SpriteRenderer2D* sprite =
                canvas->FindComponentRaw<Component::SpriteRenderer2D>(&mutableObject))
        {
            // `sizeMode` 가 `FromSprite` 면 실제 크기는 에셋이 정하고 추출 단계에서 풀린다.
            // 에디터는 그 결과를 볼 길이 아직 없어 선언된 `size` 를 쓴다 - 스프라이트를 건
            // 오브젝트를 집을 수는 있고, 칸이 정확하지 않을 뿐이다.
            halfWidth = std::fabs(sprite->size.x * scale.x) * 0.5f;
            halfHeight = std::fabs(sprite->size.y * scale.y) * 0.5f;
            offsetX = (0.5f - sprite->pivot.x) * sprite->size.x * scale.x;
            offsetY = (0.5f - sprite->pivot.y) * sprite->size.y * scale.y;
        }
        if (halfWidth < 0.001f)
        {
            halfWidth = EmptyObjectHalfSize;
        }
        if (halfHeight < 0.001f)
        {
            halfHeight = EmptyObjectHalfSize;
        }
        minX = center.x + offsetX - halfWidth;
        maxX = center.x + offsetX + halfWidth;
        minY = center.y + offsetY - halfHeight;
        maxY = center.y + offsetY + halfHeight;
        return true;
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

        canvas->ForEachObject([&](GameObject& object)
        {
            Component::Collider2D* collider =
                canvas->FindComponentRaw<Component::Collider2D>(&object);
            if (collider == nullptr || false == collider->IsEnabled())
            {
                return;
            }
            Component::Transform2D* transform =
                canvas->FindComponentRaw<Component::Transform2D>(&object);
            if (transform == nullptr)
            {
                return;
            }
            const Vec2 center =
                transform->worldValid ? transform->worldPosition : transform->position;
            const Vec2 scale = transform->worldValid ? transform->worldScale : transform->scale;
            const float angle =
                transform->worldValid ? transform->worldRotation : transform->rotation;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            // 콜라이더의 `offset` 은 오브젝트의 로컬 좌표다. 돌고 커진 뒤에 얹힌다.
            const float offsetX = collider->offset.x * scale.x;
            const float offsetY = collider->offset.y * scale.y;
            const Vec2 origin{
                center.x + offsetX * cosine - offsetY * sine,
                center.y + offsetX * sine + offsetY * cosine};

            const bool isSelected = m_editor->IsSelected(&object);
            const ImU32 color = collider->isTrigger
                ? trigger
                : (isSelected ? selected : normal);
            const float thickness = isSelected ? 2.0f : 1.0f;

            if (collider->shape == Component::ColliderShape2D::Circle)
            {
                // 원은 한 축으로만 커져도 원으로 남는다(물리가 그렇게 다룬다).
                // 그러니 **더 큰 쪽**으로 잰다 - 작은 쪽으로 재면 그림보다 작은 원이 되어
                // 실제로 부딪히는 자리를 가린다.
                const float scaleX = std::fabs(scale.x);
                const float scaleY = std::fabs(scale.y);
                const float radius = collider->radius * (scaleX > scaleY ? scaleX : scaleY);
                float screenX = 0.0f;
                float screenY = 0.0f;
                float edgeX = 0.0f;
                float edgeY = 0.0f;
                WorldToScreen(rect, origin.x, origin.y, screenX, screenY);
                WorldToScreen(rect, origin.x + radius, origin.y, edgeX, edgeY);
                draw->AddCircle(ImVec2(screenX, screenY), edgeX - screenX, color, 48, thickness);
                return;
            }

            // 상자는 **돌면 기울어진다.** 외접 사각형으로 그리면 돌려 놓은 오브젝트의
            // 충돌 칸이 실제보다 커 보인다.
            const float halfWidth = collider->size.x * std::fabs(scale.x) * 0.5f;
            const float halfHeight = collider->size.y * std::fabs(scale.y) * 0.5f;
            const float cornerX[4] = {-halfWidth, halfWidth, halfWidth, -halfWidth};
            const float cornerY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
            ImVec2 points[4];
            for (int index = 0; index < 4; ++index)
            {
                const float worldX = origin.x + cornerX[index] * cosine - cornerY[index] * sine;
                const float worldY = origin.y + cornerX[index] * sine + cornerY[index] * cosine;
                float screenX = 0.0f;
                float screenY = 0.0f;
                WorldToScreen(rect, worldX, worldY, screenX, screenY);
                points[index] = ImVec2(screenX, screenY);
            }
            draw->AddPolyline(points, 4, color, ImDrawFlags_Closed, thickness);
        });
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
            draw->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, 0.0f, 0, 1.5f);
        }
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

    void CanvasViewPanel::HandlePicking(const ViewRect& rect, bool hovered)
    {
        // 기즈모를 잡고 있는 중이면 고르지 않는다. 손잡이를 놓는 것이 빈 곳을 누른 것이
        // 되면 끌 때마다 선택이 풀린다.
        // 손잡이 위에서 놓은 것은 고르기가 아니다. 끌지 않고 눌렀다 뗀 것도 마찬가지다 -
        // 기즈모를 건드릴 때마다 선택이 바뀌면 여럿 골라 놓고 옮길 수 없다.
        if (false == hovered || m_gizmoState.dragging || m_editing.IsActive()
            || m_boxSelecting || m_gizmoState.hovered != GizmoAxis::None)
        {
            return;
        }
        if (false == ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            || Widget::MouseWasDragged(ImGuiMouseButton_Left))
        {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        GameObject* picked = PickAt(rect, io.MousePos.x, io.MousePos.y);
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
        if (m_gizmoState.dragging || m_editing.IsActive() || m_panning)
        {
            m_boxSelecting = false;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (false == m_boxSelecting)
        {
            // **임계값을 넘어야 시작한다.** 넘기 전에 시작하면 그냥 클릭한 것도 빈 상자가
            // 되어, 무언가를 고르려던 손짓이 선택을 푸는 손짓이 된다.
            const bool startable = hovered
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
            hit.Add(&object);
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

    void CanvasViewPanel::DrawContextMenu()
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
        if (false == ImGui::BeginPopupContextItem("##CanvasViewMenu"))
        {
            return;
        }
        // 계층의 빈자리와 **같은 한 벌**이다(D-132). 두 화면의 메뉴가 갈라지지 않는다.
        EditorActions::DrawBackgroundMenu(*m_editor);
        ImGui::EndPopup();
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
        const float halfHeight = m_orthographicSize;
        const float halfWidth = rect.height > 0.0f
            ? halfHeight * rect.width / rect.height
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
            view, projection, rect.left, rect.top, rect.width, rect.height, camera);
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
        if (false == hovered || m_gizmoState.dragging || m_editing.IsActive()
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
        canvas->ForEachObject([&](GameObject& object)
        {
            Component::Transform3D* transform =
                canvas->FindComponentRaw<Component::Transform3D>(&object);
            if (transform == nullptr)
            {
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
