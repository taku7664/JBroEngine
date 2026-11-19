#include <JBro/Graphics/PostProcessProfile.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **후처리 프로필의 본문**(D-344, `.jpost`). 덮어쓴 필드만 적히고 나머지는 기본값이며, 모르는 종류는 읽은 그대로 되쓰고, 깨진 YAML 은 거절하고,
// 모르는 필드·읽히지 않는 값은 경고로 버린다. 덮어쓰기를 끄면 그 필드가 기본값으로 돌아간다.

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

    JBro::Bool Parse(const char* text, JBro::PostProcessProfileDocument& document, JBro::String* error = nullptr,
        JBro::Array<JBro::String>* warnings = nullptr)
    {
        return JBro::ParsePostProcessProfile(text, std::strlen(text), document, error, warnings);
    }

    void TestOnlyOverriddenFieldsAreWritten()
    {
        JBro::PostProcessProfileDocument document;
        JBro::PostProcessProfileEntry bloom = JBro::MakePostProcessProfileEntry(JBro::PostProcessEffects::Bloom);
        Check(bloom.typeName == "Bloom" && bloom.overrides == 0 && bloom.block.Size() == sizeof(JBro::BloomEffect), "a new entry is the defaults");
        auto* values = reinterpret_cast<JBro::BloomEffect*>(bloom.block.Data());
        values->intensity = 2.5f;
        values->tint = {1.0f, 0.5f, 0.25f, 1.0f};
        // intensity(1)·tint(3) 을 덮어쓴다.
        bloom.overrides = (JBro::UInt64(1) << 1) | (JBro::UInt64(1) << 3);
        document.entries.Add(bloom);
        JBro::PostProcessProfileEntry tonemap = JBro::MakePostProcessProfileEntry(JBro::PostProcessEffects::Tonemap);
        tonemap.enabled = false;
        document.entries.Add(tonemap);
        const JBro::String text = JBro::WritePostProcessProfile(document);
        Check(text.find("intensity: 2.5") != JBro::String::npos, "the overridden intensity is written");
        Check(text.find("threshold") == JBro::String::npos && text.find("scatter") == JBro::String::npos,
            "fields left at their defaults are not written");
        Check(text.find("Type: Tonemap") != JBro::String::npos && text.find("Enabled: false") != JBro::String::npos, "the order and switch are kept");

        JBro::PostProcessProfileDocument read;
        JBro::String error;
        Check(Parse(text.c_str(), read, &error), "the written profile reads back");
        Check(read.entries.Size() == 2 && read.entries[0].type == JBro::PostProcessEffects::Bloom && read.entries[1].enabled == false,
            "the entries come back in order");
        const auto* back = reinterpret_cast<const JBro::BloomEffect*>(read.entries[0].block.Data());
        Check(back->intensity == 2.5f && back->tint.G == 0.5f && back->threshold == 0.9f, "values and defaults come back");
        Check(read.entries[0].overrides == bloom.overrides, "and so do the overrides");
        Check(JBro::WritePostProcessProfile(read) == text, "writing what was read gives the same text");
    }

    void TestTurningAnOverrideOffRestoresTheDefault()
    {
        JBro::PostProcessProfileEntry exposure = JBro::MakePostProcessProfileEntry(JBro::PostProcessEffects::Exposure);
        reinterpret_cast<JBro::ExposureEffect*>(exposure.block.Data())->exposure = 3.0f;
        Check(JBro::SetPostProcessOverride(exposure, 0, true) && exposure.overrides == 1, "the override turns on");
        Check(reinterpret_cast<JBro::ExposureEffect*>(exposure.block.Data())->exposure == 3.0f, "turning it on keeps the value");
        Check(JBro::SetPostProcessOverride(exposure, 0, false) && exposure.overrides == 0, "the override turns off");
        Check(reinterpret_cast<JBro::ExposureEffect*>(exposure.block.Data())->exposure == 0.0f, "turning it off restores the default");
        Check(false == JBro::SetPostProcessOverride(exposure, 5, true), "a field past the table is refused");
        // ColorLookup 의 렌더러 핸들(필드 3)은 숨긴 필드라 프로필에 적을 수 없다.
        Check(false == JBro::IsPostProcessFieldEditable(JBro::PostProcessEffects::ColorLookup, 3), "hidden fields cannot be overridden");
        Check(JBro::IsPostProcessFieldEditable(JBro::PostProcessEffects::ColorLookup, 0), "the LUT asset can");
    }

    void TestUnknownThingsAreKeptOrDropped()
    {
        const char* text =
            "Effects:\n"
            "  - Type: SomeoneElsesEffect\n"
            "    Enabled: true\n"
            "    Parameters:\n"
            "      strength: 0.75\n"
            "  - Type: Exposure\n"
            "    Parameters:\n"
            "      exposure: 1.5\n"
            "      oldName: 3\n"
            "  - Type: Tonemap\n"
            "    Parameters:\n"
            "      curve: NoSuchCurve\n"
            "  - Enabled: true\n";
        JBro::PostProcessProfileDocument document;
        JBro::String error;
        JBro::Array<JBro::String> warnings;
        Check(Parse(text, document, &error, &warnings), "a profile with unknown parts still reads");
        Check(document.entries.Size() == 3, "an entry without a type is skipped");
        Check(document.entries[0].type == JBro::PostProcessEffectRegistry::NotFound && document.entries[0].unknownKeys.Size() == 1,
            "an unknown effect keeps its parameters");
        Check(reinterpret_cast<const JBro::ExposureEffect*>(document.entries[1].block.Data())->exposure == 1.5f, "a known field reads");
        Check(document.entries[2].overrides == 0, "an unreadable value is not an override");
        Check(warnings.Size() == 3, "the unknown field, the bad value and the typeless entry are warned about");
        const JBro::String written = JBro::WritePostProcessProfile(document);
        Check(written.find("Type: SomeoneElsesEffect") != JBro::String::npos && written.find("strength: 0.75") != JBro::String::npos,
            "the unknown effect is written back as it was read");
        Check(written.find("oldName") == JBro::String::npos, "the unknown field is dropped");

        JBro::PostProcessProfileDocument broken;
        Check(false == Parse("Effects:\n  - Type: \"unclosed\n", broken, &error) && false == error.empty(), "broken YAML is refused");
        JBro::PostProcessProfileDocument empty;
        Check(Parse("", empty) && empty.entries.IsEmpty(), "an empty file is an empty profile");
    }
}

JBro::Int32 RunPostProcessProfileTests()
{
    TestOnlyOverriddenFieldsAreWritten();
    TestTurningAnOverrideOffRestoresTheDefault();
    TestUnknownThingsAreKeptOrDropped();
    std::cout << "Post-process profile tests passed.\n";
    return 0;
}
