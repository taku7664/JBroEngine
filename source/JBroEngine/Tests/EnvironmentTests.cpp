#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Canvas/Environment.h>
#include <JBro/Graphics/EnvironmentMap.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **캔버스 환경과 하늘 맵 굽기**(D-447, lighting3d-plan §6). 장치 없이 보는 것: 등장방형 방향과 자리의 왕복, SH 9 계수(고른 하늘은 어느 쪽에서나 같고,
// 위만 밝은 하늘은 위가 밝다), 미리 거른 밉(고른 하늘은 거칠기와 상관없이 같고, 거친 단계는 밝은 점을 퍼뜨린다), `.hdr` 디코드, `.jcanvas` 의 `Environment`.
// 렌더러가 그 값으로 그리는 것은 `MeshPixel` 의 `TestEnvironmentLightsMeshes` 가 본다.

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float a, JBro::Float b, JBro::Float tolerance)
    {
        return std::fabs((a - b).Get()) <= tolerance.Get();
    }

    float HalfToFloat(std::uint16_t half)
    {
        const std::uint32_t exponent = (half >> 10) & 0x1Fu;
        const std::uint32_t mantissa = half & 0x3FFu;
        if (exponent == 0)
        {
            return std::ldexp(static_cast<float>(mantissa), -24);
        }
        return std::ldexp(static_cast<float>(mantissa + 1024u), static_cast<int>(exponent) - 25);
    }

    JBro::Float TexelChannel(const JBro::EnvironmentMapLevel& level, JBro::UInt32 x, JBro::UInt32 y, JBro::UInt32 channel)
    {
        std::uint16_t half = 0;
        std::memcpy(&half, level.rgba16f.Data() + ((static_cast<std::size_t>(y.Get()) * level.width.Get() + x.Get()) * 4u + channel.Get()) * 2u, 2);
        return HalfToFloat(half);
    }

    // 텍셀마다 `paint(u, v)` 의 빛인 그림이다.
    template <typename TPaint>
    JBro::Array<JBro::Float> MakeMap(JBro::UInt32 width, JBro::UInt32 height, TPaint paint)
    {
        JBro::Array<JBro::Float> rgb;
        rgb.Resize(static_cast<std::size_t>(width.Get()) * height.Get() * 3u);
        for (JBro::UInt32 y = 0; y < height; ++y)
        {
            for (JBro::UInt32 x = 0; x < width; ++x)
            {
                const JBro::Float u = (static_cast<JBro::Float>(x.Get()) + 0.5f) / static_cast<JBro::Float>(width.Get());
                const JBro::Float v = (static_cast<JBro::Float>(y.Get()) + 0.5f) / static_cast<JBro::Float>(height.Get());
                JBro::Float value[3];
                paint(u, v, value);
                for (JBro::UInt32 channel = 0; channel < 3; ++channel)
                {
                    rgb[(static_cast<std::size_t>(y.Get()) * width.Get() + x.Get()) * 3u + channel.Get()] = value[channel.Get()];
                }
            }
        }
        return rgb;
    }

    void TestDirectionsAndPlacesRoundTrip()
    {
        const float directions[][3] = {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.6f, 0.8f}, {-0.48f, -0.6f, 0.64f}};
        for (const auto& direction : directions)
        {
            JBro::Float u = 0.0f;
            JBro::Float v = 0.0f;
            JBro::EnvironmentUv(direction[0], direction[1], direction[2], u, v);
            JBro::Float x = 0.0f;
            JBro::Float y = 0.0f;
            JBro::Float z = 0.0f;
            JBro::EnvironmentDirection(u, v, x, y, z);
            Check(Near(x, direction[0], 0.0005f) && Near(y, direction[1], 0.0005f) && Near(z, direction[2], 0.0005f),
                "a direction goes to its place in the sky map and back");
        }
        JBro::Float u = 0.0f;
        JBro::Float v = 0.0f;
        JBro::EnvironmentUv(0.0f, 0.0f, -1.0f, u, v);
        Check(Near(u, 0.5f, 0.0001f) && Near(v, 0.5f, 0.0001f), "-Z on the horizon is the middle of the picture (the shader agrees)");
        JBro::EnvironmentUv(1.0f, 0.0f, 0.0f, u, v);
        Check(Near(u, 0.75f, 0.0001f), "+X is three quarters across");
        JBro::EnvironmentUv(0.0f, 1.0f, 0.0f, u, v);
        Check(Near(v, 0.0f, 0.0001f), "straight up is the top row");
    }

    void TestAnEvenSkyIsTheSameEverywhere()
    {
        const JBro::Array<JBro::Float> rgb = MakeMap(64, 32, [](JBro::Float, JBro::Float, JBro::Float (&out)[3]) {
            out[0] = 0.5f;
            out[1] = 0.25f;
            out[2] = 1.0f;
        });
        JBro::EnvironmentMapBuild build;
        Check(JBro::BuildEnvironmentMap(rgb.Data(), 64, 32, build), "an even sky bakes");
        Check(build.levels.Size() == JBro::EnvironmentMapLevels.Get() && build.levels[0].width == 256 && build.levels[0].height == 128
                && build.levels[5].width == 8 && build.levels[5].height == 4,
            "the bake has six levels from 256 x 128 down to 8 x 4, whatever the picture's size");
        const float sides[][3] = {{0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.6f, 0.0f, 0.8f}};
        for (const auto& side : sides)
        {
            JBro::Float light[3];
            JBro::EvaluateEnvironmentSh(build.sh, side[0], side[1], side[2], light);
            Check(Near(light[0], 0.5f, 0.01f) && Near(light[1], 0.25f, 0.01f) && Near(light[2], 1.0f, 0.01f),
                "an even sky lights a white surface with its own colour from every side");
        }
        for (std::size_t level = 0; level < build.levels.Size(); ++level)
        {
            const JBro::EnvironmentMapLevel& at = build.levels[level];
            const JBro::UInt32 x = at.width / 3;
            const JBro::UInt32 y = at.height / 4;
            Check(Near(TexelChannel(at, x, y, 0), 0.5f, 0.01f) && Near(TexelChannel(at, x, y, 2), 1.0f, 0.01f) && Near(TexelChannel(at, x, y, 3), 1.0f, 0.001f),
                "prefiltering an even sky leaves it even at every roughness");
        }
    }

    void TestALitUpperHalfLightsFromAbove()
    {
        const JBro::Array<JBro::Float> rgb = MakeMap(128, 64, [](JBro::Float, JBro::Float v, JBro::Float (&out)[3]) {
            const JBro::Float value = v < 0.5f ? JBro::Float(1.0f) : JBro::Float(0.0f);
            out[0] = value;
            out[1] = value;
            out[2] = value;
        });
        JBro::Float sh[9][3];
        JBro::ProjectEnvironmentSh(rgb.Data(), 128, 64, sh);
        JBro::Float up[3];
        JBro::Float down[3];
        JBro::Float side[3];
        JBro::EvaluateEnvironmentSh(sh, 0.0f, 1.0f, 0.0f, up);
        JBro::EvaluateEnvironmentSh(sh, 0.0f, -1.0f, 0.0f, down);
        JBro::EvaluateEnvironmentSh(sh, 1.0f, 0.0f, 0.0f, side);
        std::cout << "  half sky: up " << up[0].Get() << ", side " << side[0].Get() << ", down " << down[0].Get() << std::endl;
        // 정확한 값은 1·0.5·0 이다. SH 9 계수(띠 2 까지)로 끊어 생기는 차는 0.1 안이다.
        Check(Near(up[0], 1.0f, 0.1f) && Near(side[0], 0.5f, 0.05f) && Near(down[0], 0.0f, 0.1f),
            "a sky lit only above lights a floor fully, a wall half, a ceiling not at all");
    }

    void TestRoughLevelsSpreadABrightSpot()
    {
        // 지평선 +X 쪽의 밝은 점 하나다.
        const JBro::Array<JBro::Float> rgb = MakeMap(128, 64, [](JBro::Float u, JBro::Float v, JBro::Float (&out)[3]) {
            const JBro::Bool spot = std::fabs((u - 0.75f).Get()) < 0.03f && std::fabs((v - 0.5f).Get()) < 0.05f;
            out[0] = spot ? JBro::Float(50.0f) : JBro::Float(0.0f);
            out[1] = out[0];
            out[2] = out[0];
        });
        JBro::EnvironmentMapBuild build;
        Check(JBro::BuildEnvironmentMap(rgb.Data(), 128, 64, build), "a spot sky bakes");
        const auto at = [&](std::size_t level, JBro::Float u, JBro::Float v) {
            const JBro::EnvironmentMapLevel& image = build.levels[level];
            const JBro::UInt32 x = static_cast<JBro::UInt32>(static_cast<std::uint32_t>((u * static_cast<JBro::Float>(image.width.Get())).Get()));
            const JBro::UInt32 y = static_cast<JBro::UInt32>(static_cast<std::uint32_t>((v * static_cast<JBro::Float>(image.height.Get())).Get()));
            return TexelChannel(image, x, y, 0);
        };
        const JBro::Float mirror = at(0, 0.75f, 0.5f);
        const JBro::Float rough = at(5, 0.75f, 0.5f);
        const JBro::Float roughAway = at(5, 0.25f, 0.5f);
        const JBro::Float glossyBeside = at(1, 0.70f, 0.5f);
        const JBro::Float mirrorBeside = at(0, 0.70f, 0.5f);
        std::cout << "  spot: mirror " << mirror.Get() << ", rough " << rough.Get() << ", rough opposite " << roughAway.Get()
                  << ", beside mirror " << mirrorBeside.Get() << " glossy " << glossyBeside.Get() << std::endl;
        Check(mirror > 10.0f, "the mirror level keeps the spot bright");
        Check(rough > 0.0f && rough < mirror * 0.5f, "the roughest level spreads it - dimmer where it was");
        Check(glossyBeside > mirrorBeside, "a little roughness already bleeds it beside the spot");
        Check(roughAway < 0.01f, "but nothing reaches the far side of the sky");
    }

    // 잘린 파일의 쓰레기(무한·NaN·음수)는 0 으로 굽는다 - 한 텍셀이 SH 전체를 무한으로 만들지 않는다(픽셀 시험에서 잘린 파일이 그랬다).
    void TestBrokenTexelsBakeAsBlack()
    {
        JBro::Array<JBro::Float> rgb = MakeMap(32, 16, [](JBro::Float, JBro::Float, JBro::Float (&out)[3]) {
            out[0] = 0.5f;
            out[1] = 0.5f;
            out[2] = 0.5f;
        });
        rgb[0] = std::numeric_limits<float>::infinity();
        rgb[4] = std::numeric_limits<float>::quiet_NaN();
        rgb[8] = -1.0e30f;
        JBro::EnvironmentMapBuild build;
        Check(JBro::BuildEnvironmentMap(rgb.Data(), 32, 16, build), "a sky with broken texels still bakes");
        JBro::Float light[3];
        JBro::EvaluateEnvironmentSh(build.sh, 0.0f, 1.0f, 0.0f, light);
        Check(std::isfinite(light[0].Get()) && Near(light[0], 0.5f, 0.05f) && Near(light[1], 0.5f, 0.05f),
            "infinite, NaN and negative texels count as black, not as poison");
    }

    // RGBE 한 텍셀이다(Radiance 의 형식).
    void PushRgbe(std::string& bytes, float r, float g, float b)
    {
        const float largest = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (largest < 1e-32f)
        {
            bytes.append(4, '\0');
            return;
        }
        int exponent = 0;
        const float mantissa = std::frexp(largest, &exponent);
        const float scale = mantissa * 256.0f / largest;
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(r * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(g * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(b * scale)));
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(exponent + 128)));
    }

    void TestHdrPicturesDecode()
    {
        // 폭 4 는 8 보다 작아 RLE 없이 그대로 적힌다.
        std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
        const float texels[8][3] = {{0.0f, 0.0f, 4.0f}, {1.0f, 0.5f, 0.25f}, {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f, 8.0f},
            {0.1f, 0.2f, 0.3f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
        for (const auto& texel : texels)
        {
            PushRgbe(file, texel[0], texel[1], texel[2]);
        }
        JBro::DecodedHdrImage image;
        Check(JBro::DecodeHdrImage({reinterpret_cast<const std::byte*>(file.data()), static_cast<JBro::UInt32>(file.size())}, image),
            "a Radiance .hdr picture decodes");
        Check(image.width == 4 && image.height == 2 && image.rgb.Size() == 24, "four by two texels of three channels");
        Check(Near(image.rgb[2], 4.0f, 0.02f) && Near(image.rgb[3], 1.0f, 0.01f) && Near(image.rgb[4], 0.5f, 0.01f) && Near(image.rgb[9], 8.0f, 0.04f)
                && Near(image.rgb[15], 2.0f, 0.01f) && Near(image.rgb[19], 3.0f, 0.02f),
            "the values are linear light above one, not a tone-mapped 8-bit picture");
        const char* notHdr = "\x89PNG\r\n\x1a\n";
        Check(false == JBro::DecodeHdrImage({reinterpret_cast<const std::byte*>(notHdr), 8}, image), "a picture that is not .hdr is not a sky");
        Check(JBro::AssetTypeRules::DetectTypeFromPath("sky/sunset.hdr") == JBro::AssetType::EnvironmentMap, "a .hdr file is a sky map");
    }

    void TestTheCanvasFileKeepsTheEnvironment()
    {
        JBro::Canvas plain(JBro::CreateDefaultAllocator());
        JBro::String text;
        JBro::CanvasFileError error;
        Check(JBro::WriteCanvasText(plain, text, error), "a canvas with the default environment writes");
        Check(std::strstr(text.c_str(), "Environment") == nullptr, "the default environment is not written - the file is what it was before environments");

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::CanvasEnvironment environment;
        environment.ambientSource = JBro::EnvironmentAmbientSource::Gradient;
        environment.skyColor = JBro::Color{0.5f, 0.6f, 0.9f, 1.0f};
        environment.groundColor = JBro::Color{0.2f, 0.1f, 0.05f, 1.0f};
        environment.ambientIntensity = 1.5f;
        environment.skyId = JBro::Uuid::Generate();
        environment.skyIntensity = 2.0f;
        environment.skyRotation = 90.0f;
        environment.reflectionIntensity = 0.5f;
        canvas.SetEnvironment(environment);
        Check(JBro::WriteCanvasText(canvas, text, error), "a canvas with an environment writes");
        Check(std::strstr(text.c_str(), "AmbientSource: Gradient") != nullptr, "the source is written by name");
        JBro::Canvas read(JBro::CreateDefaultAllocator());
        Check(JBro::ReadCanvasText(read, text.c_str(), text.size(), error), "and reads back");
        Check(read.GetEnvironment().SameSettings(environment), "every environment value comes back");

        std::string broken(text.c_str());
        const std::size_t at = broken.find("Gradient");
        broken.replace(at, 8, "Fog");
        JBro::Canvas refused(JBro::CreateDefaultAllocator());
        Check(false == JBro::ReadCanvasText(refused, broken.c_str(), broken.size(), error), "a source this version does not know is refused, not silently changed");

        // 다시 묶기 전 핸들은 같은 하늘이면 지킨다. 다른 하늘이면 비운다.
        environment.sky = JBro::AssetHandle{3, 7};
        canvas.SetEnvironment(environment);
        JBro::CanvasEnvironment brighter = canvas.GetEnvironment();
        brighter.sky = {};
        brighter.skyIntensity = 3.0f;
        canvas.SetEnvironment(brighter);
        Check(canvas.GetEnvironment().sky.generation == 7, "changing a value keeps the bound sky");
        brighter.skyId = JBro::Uuid::Generate();
        canvas.SetEnvironment(brighter);
        Check(canvas.GetEnvironment().sky.generation == 0, "choosing another sky drops the old handle until the canvas is bound again");
        canvas.Clear();
        Check(canvas.GetEnvironment().IsDefault(), "clearing the canvas resets its environment");
    }
}

JBro::Int32 RunEnvironmentTests()
{
    TestDirectionsAndPlacesRoundTrip();
    TestAnEvenSkyIsTheSameEverywhere();
    TestALitUpperHalfLightsFromAbove();
    TestRoughLevelsSpreadABrightSpot();
    TestBrokenTexelsBakeAsBlack();
    TestHdrPicturesDecode();
    TestTheCanvasFileKeepsTheEnvironment();
    std::cout << "Environment tests passed.\n";
    return 0;
}
