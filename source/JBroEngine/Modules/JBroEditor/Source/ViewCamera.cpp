#include <JBro/Editor/ViewCamera.h>

#include <algorithm>
#include <cmath>

namespace JBro::ViewCamera
{
    Vector2 ToScreen(const Camera& camera, Vector2 content)
    {
        return Vector2{camera.origin.x.Get() + (content.x.Get() - camera.pan.x.Get()) * camera.zoom.Get(),
            camera.origin.y.Get() + (content.y.Get() - camera.pan.y.Get()) * camera.zoom.Get()};
    }

    Vector2 ToContent(const Camera& camera, Vector2 screen)
    {
        const float zoom = camera.zoom.Get() <= 0.0f ? 1.0f : camera.zoom.Get();
        return Vector2{camera.pan.x.Get() + (screen.x.Get() - camera.origin.x.Get()) / zoom,
            camera.pan.y.Get() + (screen.y.Get() - camera.origin.y.Get()) / zoom};
    }

    void Set(Camera& camera, Vector2 pan, Float zoom)
    {
        camera.pan = pan;
        camera.zoom = zoom;
        camera.goalPan = pan;
        camera.goalZoom = zoom;
    }

    void Follow(Camera& camera, Float deltaSeconds)
    {
        const float zoomLeft = camera.goalZoom.Get() - camera.zoom.Get();
        const float panLeftX = camera.goalPan.x.Get() - camera.pan.x.Get();
        const float panLeftY = camera.goalPan.y.Get() - camera.pan.y.Get();
        // 붙이는 문턱은 캔버스 뷰처럼 보이는 크기에 비례한다. 줌은 그 줌의 몫, 팬은 화면 픽셀로 잰다.
        const float zoomSnap = camera.goalZoom.Get() * SnapFraction;
        const float panSnapPixels = 0.25f;
        if (std::fabs(zoomLeft) <= zoomSnap && std::fabs(panLeftX) * camera.goalZoom.Get() <= panSnapPixels
            && std::fabs(panLeftY) * camera.goalZoom.Get() <= panSnapPixels)
        {
            camera.zoom = camera.goalZoom;
            camera.pan = camera.goalPan;
            return;
        }
        const float delta = std::clamp(deltaSeconds.Get(), 0.001f, 0.1f);
        const float step = 1.0f - std::exp(-SmoothSpeed * delta);
        // **줌이 아니라 보이는 크기(1/줌)를 따라간다** - 캔버스 뷰가 직교 크기를 따라가는 것과 같다.
        // 휠이 붙잡은 점이 화면 자리 s 에 있으려면 pan = anchor - (s - origin) x 크기 다. pan 과 크기를 같은 몫으로
        // 옮기면 그 식이 가는 내내 지켜져, 따라가는 도중에도 마우스 아래 점이 움직이지 않는다. 줌을 옮기면 pan 과
        // 어긋나 화면이 지금 자리에서 커졌다가 옆으로 미끄러진다.
        const float scale = 1.0f / camera.zoom.Get();
        const float goalScale = 1.0f / camera.goalZoom.Get();
        camera.zoom = 1.0f / (scale + (goalScale - scale) * step);
        camera.pan = Vector2{camera.pan.x.Get() + panLeftX * step, camera.pan.y.Get() + panLeftY * step};
    }

    void ZoomAt(Camera& camera, Vector2 screen, Float zoom, Float minZoom, Float maxZoom)
    {
        Camera goal = camera;
        goal.pan = camera.goalPan;
        goal.zoom = camera.goalZoom;
        const Vector2 anchor = ToContent(goal, screen);
        camera.goalZoom = std::clamp(zoom.Get(), minZoom.Get(), maxZoom.Get());
        camera.goalPan = Vector2{anchor.x.Get() - (screen.x.Get() - camera.origin.x.Get()) / camera.goalZoom.Get(),
            anchor.y.Get() - (screen.y.Get() - camera.origin.y.Get()) / camera.goalZoom.Get()};
    }

    void PanByPixels(Camera& camera, Vector2 delta)
    {
        camera.goalPan = Vector2{camera.goalPan.x.Get() - delta.x.Get() / camera.goalZoom.Get(),
            camera.goalPan.y.Get() - delta.y.Get() / camera.goalZoom.Get()};
    }

    void Fit(Camera& camera, const Rect& content, Vector2 viewSize, Float margin, Float minZoom, Float maxZoom, Bool immediate)
    {
        const float width = content.max.x.Get() - content.min.x.Get() + margin.Get() * 2.0f;
        const float height = content.max.y.Get() - content.min.y.Get() + margin.Get() * 2.0f;
        float zoom = 1.0f;
        if (viewSize.x.Get() > 0.0f && viewSize.y.Get() > 0.0f && width > 0.0f && height > 0.0f)
        {
            zoom = std::min(viewSize.x.Get() / width, viewSize.y.Get() / height);
        }
        camera.goalZoom = std::clamp(zoom, minZoom.Get(), maxZoom.Get());
        const float centerX = (content.min.x.Get() + content.max.x.Get()) * 0.5f;
        const float centerY = (content.min.y.Get() + content.max.y.Get()) * 0.5f;
        camera.goalPan = Vector2{centerX - viewSize.x.Get() * 0.5f / camera.goalZoom.Get(), centerY - viewSize.y.Get() * 0.5f / camera.goalZoom.Get()};
        if (immediate)
        {
            Set(camera, camera.goalPan, camera.goalZoom);
        }
    }
}
