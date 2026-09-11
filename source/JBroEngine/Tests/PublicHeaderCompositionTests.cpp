#include <JBro/ScriptAPI.h>
#include <JBro/Framework2D/Framework2D.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool IsWhite(const JBro::Color& color)
    {
        return color.R == 1.0f
            && color.G == 1.0f
            && color.B == 1.0f
            && color.A == 1.0f;
    }

    void TestCanonicalColorComposition()
    {
        static_assert(std::is_same_v<
            decltype(JBro::Component::SpriteRenderer2D{}.tint), JBro::Color>);

        const JBro::Color coreDefault;
        const JBro::Component::SpriteRenderer2D componentDefault;
        const JBro::RenderCamera2D cameraPacketDefault;
        const JBro::SpriteRenderItem spritePacketDefault;

        Check(coreDefault.R == 0.0f
            && coreDefault.G == 0.0f
            && coreDefault.B == 0.0f
            && coreDefault.A == 1.0f,
            "the canonical Core color must keep its opaque-black default");
        Check(IsWhite(componentDefault.tint),
            "SpriteRenderer2D must preserve the former Framework2D white tint default");
        Check(IsWhite(cameraPacketDefault.clearColor),
            "RenderCamera2D must preserve the former Framework2D white clear default");
        Check(IsWhite(spritePacketDefault.tint),
            "SpriteRenderItem must preserve the former Framework2D white tint default");
    }
}

int RunPublicHeaderCompositionTests()
{
    TestCanonicalColorComposition();
    std::cout << "Public header composition tests passed.\n";
    return 0;
}
