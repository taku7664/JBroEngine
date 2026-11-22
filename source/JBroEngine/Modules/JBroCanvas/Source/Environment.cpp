#include <JBro/Canvas/Environment.h>

namespace JBro
{
    namespace
    {
        Bool SameColor(const Color& left, const Color& right)
        {
            return left.R == right.R && left.G == right.G && left.B == right.B && left.A == right.A;
        }
    }

    Bool CanvasEnvironment::IsDefault() const
    {
        return SameSettings(CanvasEnvironment{});
    }

    Bool CanvasEnvironment::SameSettings(const CanvasEnvironment& other) const
    {
        return ambientSource == other.ambientSource && SameColor(ambientColor, other.ambientColor) && SameColor(skyColor, other.skyColor)
            && SameColor(equatorColor, other.equatorColor) && SameColor(groundColor, other.groundColor) && ambientIntensity == other.ambientIntensity
            && skyId == other.skyId && skyIntensity == other.skyIntensity && skyRotation == other.skyRotation
            && reflectionIntensity == other.reflectionIntensity;
    }
}
