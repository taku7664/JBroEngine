#include "SimulationViewPanel.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework3DSystem/System/Camera3DSystem.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <cstdio>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    const char* SimulationViewPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체가 여기 달려 있다.
        return TypeName;
    }

    const char* SimulationViewPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelSimulation, "Simulation");
    }

    Bool SimulationViewPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void SimulationViewPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }

        // 그림 붙이기는 위젯이 한다(D-413) - 바탕·레터박스·자리 잡기. **입력은 받지 않는다** -
        // 시뮬레이션 뷰에서는 고르지도 끌지도 않는다(D-131).
        const Widget::ViewportRect rect =
            Widget::ViewportImage(m_editor->GetSimulationViewTexture(), m_editor->GetSimulationViewExtent());
        if (false == rect.hasPanel)
        {
            return;
        }
        // 이 프레임에 게임 화면을 붙였다. 붙이지 않은 프레임(닫힘·다른 탭에 가림)에는
        // 게임을 그리지 않는다(D-63).
        m_editor->RequestSimulationView();
        // 게임 입력(D-214). 포커스가 여기 있으면 다음 프레임부터 게임이 키를 받고, 마우스는 이 그림 사각형 기준의 게임 픽셀이다.
        m_editor->ReportSimulationView(IsFocused(), rect.imageLeft, rect.imageTop, rect.imageWidth, rect.imageHeight);

        DrawStatusOverlay(rect);
    }

    void SimulationViewPanel::DrawStatusOverlay(const Widget::ViewportRect& rect) const
    {
        // 기존 엔진의 시뮬레이션 뷰와 같은 자리, 같은 내용이다. 그림이 안 나올 때 **왜 안 나오는지**를
        // 말하지 않으면 고장과 구분되지 않는다.
        const Bool playing = m_editor->IsSimulationPlaying() && false == m_editor->IsSimulationPaused();
        const char* text = nullptr;
        // 글자 앞의 아이콘이 상태를 먼저 말한다(D-278) - 재생·멈춤·경고가 글자를 읽기 전에 갈린다.
        const char* icon = Icons::Warning;
        ImU32 color = IM_COL32(210, 216, 224, 255);
        if (m_editor->GetCanvas() == nullptr)
        {
            text = Loc::TextOr(LocKeys::SimulationViewNoCanvas, "no canvas is open");
        }
        else if (false == rect.hasImage || false == m_editor->DidGameSubmitLastFrame())
        {
            // **그림 자리가 있어도 게임이 낸 것이 없으면 카메라가 없는 것이다**(D-178).
            // 텍스처가 있는지만 보면 카메라 없는 검은 화면을 "실행 중" 이라고 말한다 -
            // 기존 시뮬레이션 뷰는 그 둘을 갈랐다.
            // 카메라가 있는데 값이 잘못되어 건너뛴 것이면 그렇게 말한다(D-239). "카메라 없음" 이라고 하면 붙어 있는 카메라를 찾아 헤맨다.
            const Bool unusable = m_editor->GetUnusableGameCameraCount() > 0;
            text = unusable
                ? Loc::TextOr(LocKeys::SimulationViewCameraUnusable, "the Camera2D values cannot be drawn")
                : Loc::TextOr(LocKeys::SimulationViewNoCamera, "there is no camera");
            icon = unusable ? Icons::Warning : Icons::NoCamera;
        }
        else if (m_editor->IsAtScriptBreak())
        {
            // 스크립트가 중단점에 섰다(D-330). 그냥 일시정지와 가른다 - 어느 줄에 섰는지가 이 화면을 보는 까닭이다.
            const CodeLocation& location = m_editor->GetScriptBreakLocation();
            const char* file = location.file.c_str();
            for (const char* at = file; *at != '\0'; ++at)
            {
                if (*at == '/' || *at == '\\')
                {
                    file = at + 1;
                }
            }
            std::snprintf(m_breakStatus, sizeof(m_breakStatus), Loc::TextOr(LocKeys::SimulationViewScriptBreak, "Stopped at %s:%u"),
                location.file.empty() ? "?" : file, static_cast<unsigned int>(location.line.Value));
            text = m_breakStatus;
            icon = Icons::Pause;
            color = IM_COL32(255, 200, 90, 255);
        }
        else if (playing)
        {
            text = Loc::TextOr(LocKeys::SimulationViewPlaying, "Playing");
            icon = Icons::Play;
            color = IM_COL32(100, 230, 120, 255);
        }
        else
        {
            text = Loc::TextOr(LocKeys::SimulationViewStopped, "Stopped");
            icon = Icons::Stop;
        }
        Widget::ViewportStatusLine(rect, 0, icon, color, text);

        // **어느 카메라로 그리고 있는지 애매하면 말한다**(D-187, D-438, 기존 캔버스 인스펙터의 `camera_ambiguous` 경고).
        // 켜진 뷰포트가 카메라를 자동으로 고르는데(첫 켜진 카메라) 켜진 카메라가 여럿이면, 그중 무엇인지는 캔버스 차례가 정한다. 2D 와 3D 가 같다.
        if (Canvas* canvas = m_editor->GetCanvas())
        {
            Bool anyAuto = false;
            for (std::size_t index = 0; index < canvas->GetViewportCount(); ++index)
            {
                const CanvasViewport* viewport = canvas->GetViewportAt(index);
                anyAuto = anyAuto || (viewport->active && false == viewport->HasCamera());
            }
            const UInt32 active = m_editor->GetFrameworkKind() == FrameworkKind::Framework3D
                ? System::Camera3DSystem::CountActiveCameras(*canvas)
                : System::Camera2DSystem::CountActiveCameras(*canvas);
            if (anyAuto && active > 1)
            {
                Widget::ViewportStatusLine(rect, 1, Icons::Warning, IM_COL32(255, 200, 90, 255),
                    Loc::TextOr(LocKeys::SimulationViewCameraAmbiguous,
                        "a viewport picks its camera automatically and several cameras are active, so the first one is used"));
            }
        }
    }
}
