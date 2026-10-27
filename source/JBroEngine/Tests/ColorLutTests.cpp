#include <JBro/Asset/ColorLutFile.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Asset/AssetTypeRules.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **`.cube` LUT 읽기**(D-343). 받는 키·주석·정의역·칸 수와, 거절하는 모양(1D LUT·모자라거나 넘치는 칸·뒤집힌 정의역·숫자가 아닌 줄)과,
// 반정밀 실수 텍셀로 옮기기를 본다. 렌더러가 그 텍셀로 그리는 것은 `PostProcessPixel` 이 본다.

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

    JBro::Bool Parse(const char* text, JBro::ColorLut& lut, JBro::String* error = nullptr)
    {
        return JBro::ParseColorLut(text, std::strlen(text), lut, error);
    }

    void TestAWellFormedCubeReads()
    {
        const char* text =
            "\xEF\xBB\xBF# made by hand\n"
            "TITLE \"Warm\"\n"
            "LUT_3D_SIZE 2\r\n"
            "DOMAIN_MIN 0 0 0\n"
            "DOMAIN_MAX 1 2 1   # wider green\n"
            "LUT_3D_INPUT_RANGE 0 1\n"
            "\n"
            "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
            "0 0 1\n1 0 1\n0 1 1\n1.0 1.0 1.0\n";
        JBro::ColorLut lut;
        JBro::String error;
        Check(Parse(text, lut, &error), "a well formed cube reads");
        Check(lut.size == 2 && lut.rgb.Size() == 24, "it holds size cubed rows of three");
        Check(lut.title == "Warm", "the title is read without its quotes");
        Check(lut.domainMax[1] == 2.0f && lut.domainMin[0] == 0.0f, "the domain is read");
        Check(lut.rgb[3] == 1.0f && lut.rgb[4] == 0.0f, "red runs fastest - the second row is pure red");
        Check(lut.rgb[12] == 0.0f && lut.rgb[14] == 1.0f, "blue runs slowest - the fifth row is pure blue");
    }

    void TestBrokenCubesAreRefused()
    {
        struct Case
        {
            const char* text;
            const char* what;
        };
        const Case cases[] = {
            {"LUT_1D_SIZE 4\n0 0 0\n", "a 1D LUT is refused"},
            {"0 0 0\nLUT_3D_SIZE 2\n", "a row before the size is refused"},
            {"LUT_3D_SIZE 1\n0 0 0\n", "a size under two is refused"},
            {"LUT_3D_SIZE 2.5\n", "a size that is not whole is refused"},
            {"LUT_3D_SIZE 2\n0 0 0\n1 0 0\n", "too few rows are refused"},
            {"LUT_3D_SIZE 2\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n", "too many rows are refused"},
            {"LUT_3D_SIZE 2\n0 0 x\n", "a row that is not numbers is refused"},
            {"LUT_3D_SIZE 2\n0 0\n", "a row of two numbers is refused"},
            {"DOMAIN_MIN 1 1 1\nDOMAIN_MAX 0 1 1\nLUT_3D_SIZE 2\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n",
                "an upside down domain is refused"},
            {"", "an empty file is refused"},
        };
        for (const Case& item : cases)
        {
            JBro::ColorLut lut;
            JBro::String error;
            Check(false == Parse(item.text, lut, &error) && false == error.empty(), item.what);
        }
        JBro::ColorLut lut;
        JBro::String error;
        Parse("LUT_3D_SIZE 2\n0 0 0\nnope\n", lut, &error);
        Check(error.find("line 3") != JBro::String::npos, "the error names the line");
    }

    void TestTexelsAreHalfFloats()
    {
        JBro::ColorLut lut;
        lut.size = 1;
        lut.rgb.Add(1.0f);
        lut.rgb.Add(0.5f);
        lut.rgb.Add(-2.0f);
        JBro::Array<std::byte> texels;
        JBro::PackColorLutRgba16(lut, texels);
        Check(texels.Size() == 8, "one cell is eight bytes");
        std::uint16_t half[4] = {};
        std::memcpy(half, texels.Data(), sizeof(half));
        Check(half[0] == 0x3C00u && half[1] == 0x3800u && half[2] == 0xC000u && half[3] == 0x3C00u,
            "1, 0.5 and -2 become their half floats and alpha is 1");
        lut.rgb[0] = 1.0e9f;
        lut.rgb[1] = 1.0e-9f;
        lut.rgb[2] = 0.333333f;
        JBro::PackColorLutRgba16(lut, texels);
        std::memcpy(half, texels.Data(), sizeof(half));
        Check(half[0] == 0x7C00u && half[1] == 0x0000u && half[2] == 0x3555u, "out of range is infinity, tiny is zero, a third rounds to nearest");
    }

    void TestTheExtensionsAreAssetTypes()
    {
        Check(JBro::AssetTypeRules::DetectTypeFromPath("grade.cube") == JBro::AssetType::ColorLut, "a .cube file is a colour LUT");
        Check(JBro::AssetTypeRules::DetectTypeFromPath("night.jpost") == JBro::AssetType::PostProcessProfile, "a .jpost file is a post-process profile");
        Check(JBro::AssetTypeRules::TypeOfIdFieldName("colorLutId") == JBro::AssetType::ColorLut, "colorLutId picks colour LUTs");
        Check(JBro::AssetTypeRules::TypeOfIdFieldName("postProcessProfileId") == JBro::AssetType::PostProcessProfile,
            "postProcessProfileId picks post-process profiles");
    }
}

JBro::Int32 RunColorLutTests()
{
    TestAWellFormedCubeReads();
    TestBrokenCubesAreRefused();
    TestTexelsAreHalfFloats();
    TestTheExtensionsAreAssetTypes();
    std::cout << "Colour LUT tests passed.\n";
    return 0;
}
