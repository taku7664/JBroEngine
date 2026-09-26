#include <JBro/Canvas/ScreenSpace.h>

#include <cmath>
#include <cstring>

namespace JBro
{
    namespace
    {
        bool Usable(float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }
    }

    bool ComputeScreenExtent(ScreenScaleMode mode, const ScreenSpaceFrame& frame, ScreenExtent& extent)
    {
        if (false == Usable(frame.referenceWidth) || false == Usable(frame.referenceHeight)
            || false == Usable(frame.targetWidth) || false == Usable(frame.targetHeight))
        {
            return false;
        }
        const float aspect = frame.targetWidth / frame.targetHeight;
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
            const float scaleX = frame.targetWidth / frame.referenceWidth;
            const float scaleY = frame.targetHeight / frame.referenceHeight;
            const float scale = scaleX < scaleY ? scaleX : scaleY;
            result.halfWidth = frame.targetWidth * 0.5f / scale;
            result.halfHeight = frame.targetHeight * 0.5f / scale;
            break;
        }
        case ScreenScaleMode::ConstantPixel:
            result.halfWidth = frame.targetWidth * 0.5f;
            result.halfHeight = frame.targetHeight * 0.5f;
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

    void ComputeAnchorPoint(const ScreenExtent& extent, float anchorX, float anchorY, float& x, float& y)
    {
        x = -extent.halfWidth + 2.0f * extent.halfWidth * anchorX;
        y = -extent.halfHeight + 2.0f * extent.halfHeight * anchorY;
    }

    const char* LayerSpaceName(LayerSpace space)
    {
        return space == LayerSpace::Screen ? "Screen" : "World";
    }

    bool ParseLayerSpace(const char* name, LayerSpace& space)
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

    bool ParseScreenScaleMode(const char* name, ScreenScaleMode& mode)
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
