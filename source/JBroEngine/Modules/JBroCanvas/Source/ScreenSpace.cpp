#include <JBro/Canvas/ScreenSpace.h>

#include <cmath>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    namespace
    {
        Bool Usable(Float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }
    }

    Bool GetScreenArea(const ScreenSpaceFrame& frame, ScreenArea& area)
    {
        if (false == Usable(frame.targetWidth) || false == Usable(frame.targetHeight))
        {
            return false;
        }
        if (Usable(frame.areaWidth) && Usable(frame.areaHeight) && std::isfinite(frame.areaX) && std::isfinite(frame.areaY))
        {
            area.x = frame.areaX;
            area.y = frame.areaY;
            area.width = frame.areaWidth;
            area.height = frame.areaHeight;
            return true;
        }
        area.x = 0.0f;
        area.y = 0.0f;
        area.width = frame.targetWidth;
        area.height = frame.targetHeight;
        return true;
    }

    Bool GetSafeScreenArea(const ScreenSpaceFrame& frame, ScreenArea& area)
    {
        ScreenArea drawn;
        if (false == GetScreenArea(frame, drawn))
        {
            return false;
        }
        if (false == frame.safeArea.IsAny())
        {
            area = drawn;
            return true;
        }
        // 띠가 그려지는 영역보다 두꺼우면 남는 것이 없다. 음수 크기로 뒤집는 대신 0 으로 둔다 -
        // 받는 쪽이 폭으로 나누는 자리가 있고, 뒤집힌 크기는 거기서 부호가 뒤바뀐 배치를 만든다.
        const Float width = drawn.width - frame.safeArea.left - frame.safeArea.right;
        const Float height = drawn.height - frame.safeArea.top - frame.safeArea.bottom;
        area.x = drawn.x + frame.safeArea.left;
        area.y = drawn.y + frame.safeArea.top;
        area.width = width > 0.0f ? width : Float(0.0f);
        area.height = height > 0.0f ? height : Float(0.0f);
        return true;
    }

    Bool ComputePixelPerfectArea(const ScreenSpaceFrame& frame, ScreenArea& area, Float& scale)
    {
        if (false == Usable(frame.referenceWidth) || false == Usable(frame.referenceHeight)
            || false == Usable(frame.targetWidth) || false == Usable(frame.targetHeight))
        {
            return false;
        }
        const Float fitX = frame.targetWidth / frame.referenceWidth;
        const Float fitY = frame.targetHeight / frame.referenceHeight;
        const Float fit = fitX < fitY ? fitX : fitY;
        const Float whole = std::floor(fit);
        // 정수 배율이 1 이상이면 그것이다. 대상이 기준보다 작으면 들어가는 만큼 줄인다.
        const Float used = whole >= 1.0f ? whole : fit;
        ScreenArea result;
        result.width = frame.referenceWidth * used;
        result.height = frame.referenceHeight * used;
        // 곱셈의 반올림으로 대상을 한 올 넘지 않게 한다 - 뷰포트가 대상 밖으로 나가면 렌더러가 프레임을 거절한다.
        if (result.width > frame.targetWidth)
        {
            result.width = frame.targetWidth;
        }
        if (result.height > frame.targetHeight)
        {
            result.height = frame.targetHeight;
        }
        result.x = std::floor((frame.targetWidth - result.width) * 0.5f);
        result.y = std::floor((frame.targetHeight - result.height) * 0.5f);
        area = result;
        scale = used;
        return true;
    }

    Bool ComputeScreenExtent(ScreenScaleMode mode, const ScreenSpaceFrame& frame, ScreenExtent& extent)
    {
        ScreenArea area;
        if (false == Usable(frame.referenceWidth) || false == Usable(frame.referenceHeight)
            || false == GetScreenArea(frame, area))
        {
            return false;
        }
        const Float aspect = area.width / area.height;
        ScreenExtent result;
        switch (mode)
        {
        case ScreenScaleMode::FixedWidth:
            result.halfWidth = frame.referenceWidth * 0.5f;
            result.halfHeight = result.halfWidth / aspect;
            break;
        case ScreenScaleMode::Contain:
        {
            // 대상 1 픽셀에 기준 몇 픽셀이 드는가 - 기준 사각형이 다 들어가도록 두 축 가운데 작은 배율을 쓴다.
            const Float scaleX = area.width / frame.referenceWidth;
            const Float scaleY = area.height / frame.referenceHeight;
            const Float scale = scaleX < scaleY ? scaleX : scaleY;
            result.halfWidth = area.width * 0.5f / scale;
            result.halfHeight = area.height * 0.5f / scale;
            break;
        }
        case ScreenScaleMode::ConstantPixel:
            result.halfWidth = area.width * 0.5f;
            result.halfHeight = area.height * 0.5f;
            break;
        case ScreenScaleMode::FixedHeight:
        default:
            result.halfHeight = frame.referenceHeight * 0.5f;
            result.halfWidth = result.halfHeight * aspect;
            break;
        }
        extent = result;
        return true;
    }

    void ComputeAnchorPoint(const ScreenExtent& extent, Float anchorX, Float anchorY, Float& x, Float& y)
    {
        x = -extent.halfWidth + 2.0f * extent.halfWidth * anchorX;
        y = -extent.halfHeight + 2.0f * extent.halfHeight * anchorY;
    }

    Bool ScreenPixelToNormalized(const ScreenSpaceFrame& frame, Float pixelX, Float pixelY, Float& x, Float& y)
    {
        ScreenArea area;
        if (false == GetScreenArea(frame, area))
        {
            return false;
        }
        // 그려지는 사각형 밖(레터박스 띠)은 -1..1 밖으로 나온다. 누를 것이 없으니 부르는 쪽이 거기서 걸러진다.
        x = (pixelX - area.x) / area.width * 2.0f - 1.0f;
        y = 1.0f - (pixelY - area.y) / area.height * 2.0f;
        return true;
    }

    Bool NormalizedToScreenPixel(const ScreenSpaceFrame& frame, Float x, Float y, Float& pixelX, Float& pixelY)
    {
        ScreenArea area;
        if (false == GetScreenArea(frame, area))
        {
            return false;
        }
        pixelX = area.x + (x + 1.0f) * 0.5f * area.width;
        pixelY = area.y + (1.0f - y) * 0.5f * area.height;
        return true;
    }

    Bool ScreenPixelToLayer(ScreenScaleMode mode, const ScreenSpaceFrame& frame, Float pixelX, Float pixelY, Float& x, Float& y)
    {
        ScreenExtent extent;
        Float nx = 0.0f;
        Float ny = 0.0f;
        if (false == ComputeScreenExtent(mode, frame, extent) || false == ScreenPixelToNormalized(frame, pixelX, pixelY, nx, ny))
        {
            return false;
        }
        x = nx * extent.halfWidth;
        y = ny * extent.halfHeight;
        return true;
    }

    Bool LayerToScreenPixel(ScreenScaleMode mode, const ScreenSpaceFrame& frame, Float x, Float y, Float& pixelX, Float& pixelY)
    {
        ScreenExtent extent;
        if (false == ComputeScreenExtent(mode, frame, extent))
        {
            return false;
        }
        return NormalizedToScreenPixel(frame, x / extent.halfWidth, y / extent.halfHeight, pixelX, pixelY);
    }

    const char* LayerSpaceName(LayerSpace space)
    {
        return space == LayerSpace::Screen ? "Screen" : "World";
    }

    Bool ParseLayerSpace(const char* name, LayerSpace& space)
    {
        if (name == nullptr)
        {
            return false;
        }
        if (std::strcmp(name, "World") == 0)
        {
            space = LayerSpace::World;
            return true;
        }
        if (std::strcmp(name, "Screen") == 0)
        {
            space = LayerSpace::Screen;
            return true;
        }
        return false;
    }

    const char* ScreenScaleModeName(ScreenScaleMode mode)
    {
        switch (mode)
        {
        case ScreenScaleMode::FixedWidth:
            return "FixedWidth";
        case ScreenScaleMode::Contain:
            return "Contain";
        case ScreenScaleMode::ConstantPixel:
            return "ConstantPixel";
        case ScreenScaleMode::FixedHeight:
        default:
            return "FixedHeight";
        }
    }

    Bool ParseScreenScaleMode(const char* name, ScreenScaleMode& mode)
    {
        if (name == nullptr)
        {
            return false;
        }
        const ScreenScaleMode modes[] = { ScreenScaleMode::FixedHeight, ScreenScaleMode::FixedWidth, ScreenScaleMode::Contain,
            ScreenScaleMode::ConstantPixel };
        for (const ScreenScaleMode candidate : modes)
        {
            if (std::strcmp(name, ScreenScaleModeName(candidate)) == 0)
            {
                mode = candidate;
                return true;
            }
        }
        return false;
    }
}
