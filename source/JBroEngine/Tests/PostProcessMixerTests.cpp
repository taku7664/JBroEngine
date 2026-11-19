#include <JBro/Framework2D/Component/PostProcessVolume2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/PostProcessVolume2DWeight.h>
#include <JBro/Framework3D/Component/PostProcessVolume3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/PostProcessVolume3DWeight.h>
#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Graphics/PostProcessMixer.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **볼륨 섞기와 무게**(D-346, postprocess-plan §3.13). 섞기는 필드의 리플렉션 종류로 정한다 - `Float`·`Color` 는 보간, `Int32` 는 보간해 반올림,
// 열거·에셋은 무게 절반에서 바꾼다. 볼륨은 덮어쓴 필드만 섞고, 기준에 없는 종류는 기본값에서 시작하는 항목을 더한다. 스크립트의 덮어쓰기는
// 맨 끝이다. 무게는 모양 안이면 1, 밖 `blendDistance` 안이면 줄고, 그 밖은 0 이다.

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

    JBro::Bool Near(JBro::Float a, JBro::Float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }

    // 프로필 하나(항목·덮어쓴 필드·블록)다.
    struct Profile
    {
        JBro::PostProcessEntry entries[4];
        JBro::UInt64 overrides[4] = {};
        JBro::UInt32 count = 0;
        alignas(16) std::byte blocks[512] = {};
        JBro::UInt32 used = 0;

        template <typename T>
        T& Add(JBro::UInt32 type, JBro::UInt64 overridden)
        {
            // 블록은 기본값을 바이트로 옮겨 세운다. 효과 블록은 POD 다.
            const T defaults{};
            std::memcpy(blocks + used.Get(), &defaults, sizeof(T));
            T* block = reinterpret_cast<T*>(blocks + used.Get());
            entries[count.Get()] = {type, used, true};
            overrides[count.Get()] = overridden;
            used = static_cast<JBro::UInt32>((static_cast<std::size_t>(used.Get()) + sizeof(T) + 15u) & ~static_cast<std::size_t>(15u));
            ++count;
            return *block;
        }

        JBro::PostProcessStack Stack() const
        {
            return {{entries, count}, {blocks, used}};
        }

        JBro::PostProcessProfileView View() const
        {
            return {Stack(), {overrides, count}};
        }
    };

    template <typename T>
    const T* Find(const JBro::PostProcessStack& stack, JBro::UInt32 type)
    {
        for (JBro::UInt32 at = 0; at < stack.entries.size; ++at)
        {
            if (stack.entries.data[at.Get()].type == type)
            {
                return reinterpret_cast<const T*>(stack.blocks.data + stack.entries.data[at.Get()].blockOffset.Get());
            }
        }
        return nullptr;
    }

    void TestFieldsMixByTheirKind()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(8, 1024);
        Profile base;
        base.Add<JBro::ExposureEffect>(JBro::PostProcessEffects::Exposure, 0).exposure = 1.0f;
        JBro::BloomEffect& baseBloom = base.Add<JBro::BloomEffect>(JBro::PostProcessEffects::Bloom, 0);
        baseBloom.iterations = 2;
        baseBloom.tint = {1.0f, 1.0f, 1.0f, 1.0f};
        base.Add<JBro::TonemapEffect>(JBro::PostProcessEffects::Tonemap, 0).curve = JBro::Tonemapper::None;

        Profile volume;
        volume.Add<JBro::ExposureEffect>(JBro::PostProcessEffects::Exposure, 1).exposure = 3.0f;
        // 블룸: intensity(1)·tint(3)·iterations(5) 를 덮어쓴다. threshold(0) 는 적지 않았다.
        JBro::BloomEffect& bloom = volume.Add<JBro::BloomEffect>(
            JBro::PostProcessEffects::Bloom, (JBro::UInt64(1) << 1) | (JBro::UInt64(1) << 3) | (JBro::UInt64(1) << 5));
        bloom.threshold = 5.0f;
        bloom.intensity = 3.0f;
        bloom.tint = {0.0f, 0.0f, 1.0f, 1.0f};
        bloom.iterations = 7;
        volume.Add<JBro::TonemapEffect>(JBro::PostProcessEffects::Tonemap, 1).curve = JBro::Tonemapper::AgX;

        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.25f);
        JBro::PostProcessStack result = mixer.GetResult();
        Check(result.entries.size == 3, "a volume over the same types adds no entry");
        Check(Near(Find<JBro::ExposureEffect>(result, JBro::PostProcessEffects::Exposure)->exposure, 1.5f), "a Float moves a quarter of the way");
        const JBro::BloomEffect* mixed = Find<JBro::BloomEffect>(result, JBro::PostProcessEffects::Bloom);
        Check(Near(mixed->intensity, 1.5f) && Near(mixed->tint.R, 0.75f) && Near(mixed->tint.B, 1.0f), "a Color mixes channel by channel");
        Check(mixed->iterations == 3, "an Int32 mixes and rounds (2 + 5 / 4)");
        Check(Near(mixed->threshold, 0.9f), "a field the volume did not override keeps the base");
        Check(Find<JBro::TonemapEffect>(result, JBro::PostProcessEffects::Tonemap)->curve == JBro::Tonemapper::None,
            "an enum under half the weight stays");

        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.5f);
        result = mixer.GetResult();
        Check(Find<JBro::TonemapEffect>(result, JBro::PostProcessEffects::Tonemap)->curve == JBro::Tonemapper::AgX, "and switches at half");

        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.0f);
        Check(Near(Find<JBro::ExposureEffect>(mixer.GetResult(), JBro::PostProcessEffects::Exposure)->exposure, 1.0f), "no weight leaves the base");
        mixer.Blend(volume.View(), 3.0f);
        Check(Near(Find<JBro::ExposureEffect>(mixer.GetResult(), JBro::PostProcessEffects::Exposure)->exposure, 3.0f), "a weight above one is one");
    }

    void TestAVolumeAddsWhatTheBaseLacks()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(8, 1024);
        Profile volume;
        volume.Add<JBro::ColorAdjustmentsEffect>(JBro::PostProcessEffects::ColorAdjustments, JBro::UInt64(1) << 3).saturation = -100.0f;
        mixer.Begin({});
        mixer.Blend(volume.View(), 0.5f);
        const JBro::PostProcessStack result = mixer.GetResult();
        Check(result.entries.size == 1, "a type missing from the base is added");
        const auto* adjustments = Find<JBro::ColorAdjustmentsEffect>(result, JBro::PostProcessEffects::ColorAdjustments);
        Check(Near(adjustments->saturation, -50.0f) && Near(adjustments->contrast, 0.0f), "starting from its defaults");

        // 두 볼륨: 앞(우선순위 낮음)이 -100 으로, 뒤가 +100 으로 끝까지 섞으면 뒤가 이긴다.
        Profile later;
        later.Add<JBro::ColorAdjustmentsEffect>(JBro::PostProcessEffects::ColorAdjustments, JBro::UInt64(1) << 3).saturation = 100.0f;
        mixer.Begin({});
        mixer.Blend(volume.View(), 1.0f);
        mixer.Blend(later.View(), 1.0f);
        Check(Near(Find<JBro::ColorAdjustmentsEffect>(mixer.GetResult(), JBro::PostProcessEffects::ColorAdjustments)->saturation, 100.0f),
            "the volume blended last is on top");
    }

    void TestAnAssetSwitchCarriesItsResolvedValues()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(8, 1024);
        Profile base;
        JBro::ColorLookupEffect& before = base.Add<JBro::ColorLookupEffect>(JBro::PostProcessEffects::ColorLookup, 0);
        before.lutTexture = {1, 1};
        Profile volume;
        // colorLutId(0) 만 덮어썼다. 렌더러 텍스처(숨긴 3)는 그 아이디를 따라 바뀐다.
        JBro::ColorLookupEffect& after = volume.Add<JBro::ColorLookupEffect>(JBro::PostProcessEffects::ColorLookup, 1);
        after.colorLutId = JBro::Uuid::Generate();
        after.lutTexture = {2, 1};
        after.lutDomainMax = {2.0f, 2.0f, 2.0f};
        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.4f);
        const auto* kept = Find<JBro::ColorLookupEffect>(mixer.GetResult(), JBro::PostProcessEffects::ColorLookup);
        Check(kept->lutTexture.index == 1 && kept->colorLutId.IsNull(), "under half the weight the base LUT stays with its texture");
        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.6f);
        const auto* switched = Find<JBro::ColorLookupEffect>(mixer.GetResult(), JBro::PostProcessEffects::ColorLookup);
        Check(switched->colorLutId == after.colorLutId && switched->lutTexture.index == 2 && Near(switched->lutDomainMax.x, 2.0f),
            "over half the LUT switches and its texture and domain follow");
    }

    // 머티리얼 효과의 섞기 자료를 세운다: 칸 넷(실수·정수·참거짓·빈 칸).
    void SetMaterialCells(JBro::MaterialEffect& effect, const char* shader, float number, std::int32_t integer, std::uint32_t flag)
    {
        effect.shaderId = JBro::Uuid::FromName(shader);
        effect.parameterBytes = 16;
        effect.cellKinds[0] = static_cast<std::uint8_t>(JBro::MaterialCellKind::Float);
        effect.cellKinds[1] = static_cast<std::uint8_t>(JBro::MaterialCellKind::Int);
        effect.cellKinds[2] = static_cast<std::uint8_t>(JBro::MaterialCellKind::Bool);
        effect.cellKinds[3] = static_cast<std::uint8_t>(JBro::MaterialCellKind::Unused);
        std::memcpy(effect.parameters, &number, 4);
        std::memcpy(effect.parameters + 4, &integer, 4);
        std::memcpy(effect.parameters + 8, &flag, 4);
    }

    struct MaterialCells
    {
        float number = 0.0f;
        std::int32_t integer = 0;
        std::uint32_t flag = 0;
    };

    MaterialCells ReadMaterialCells(const JBro::MaterialEffect& effect)
    {
        MaterialCells cells;
        std::memcpy(&cells.number, effect.parameters, 4);
        std::memcpy(&cells.integer, effect.parameters + 4, 4);
        std::memcpy(&cells.flag, effect.parameters + 8, 4);
        return cells;
    }

    // **같은 셰이더의 두 머티리얼은 파라미터로 섞인다**(D-348). 칸마다 종류대로다 - 실수는 잇고, 정수는 이어서 반올림하고, 참거짓과 머티리얼(렌더러의 것)은
    // 반에서 바뀐다. 섞였으면 `mixed` 가 켜져 렌더러가 섞인 블록을 올린다. 셰이더가 다르면 블록 전체가 반에서 바뀌고 섞이지 않는다.
    void TestMaterialsOfOneShaderMixTheirParameters()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(8, 1024);
        Profile base;
        JBro::MaterialEffect& calm = base.Add<JBro::MaterialEffect>(JBro::PostProcessEffects::Material, 1);
        calm.materialId = JBro::Uuid::FromName("calm material");
        calm.rendererMaterial = {1, 1};
        SetMaterialCells(calm, "fog shader", 0.0f, 2, 0);
        Profile volume;
        JBro::MaterialEffect& storm = volume.Add<JBro::MaterialEffect>(JBro::PostProcessEffects::Material, 1);
        storm.materialId = JBro::Uuid::FromName("storm material");
        storm.rendererMaterial = {2, 1};
        SetMaterialCells(storm, "fog shader", 1.0f, 6, 1);

        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.25f);
        const auto* quarter = Find<JBro::MaterialEffect>(mixer.GetResult(), JBro::PostProcessEffects::Material);
        MaterialCells cells = ReadMaterialCells(*quarter);
        Check(quarter->mixed && Near(cells.number, 0.25f) && cells.integer == 3 && cells.flag == 0,
            "a quarter of the way: the float moves, the int rounds (2 + 4 / 4) and the bool stays");
        Check(quarter->rendererMaterial.index == 1 && quarter->materialId == calm.materialId, "under half the base material draws");

        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.75f);
        const auto* most = Find<JBro::MaterialEffect>(mixer.GetResult(), JBro::PostProcessEffects::Material);
        cells = ReadMaterialCells(*most);
        Check(most->mixed && Near(cells.number, 0.75f) && cells.integer == 5 && cells.flag == 1, "three quarters: the bool has switched too");
        Check(most->rendererMaterial.index == 2 && most->materialId == storm.materialId, "over half the volume's material draws");

        // 다른 셰이더: 섞지 않고 반에서 통째로 바뀐다.
        SetMaterialCells(storm, "other shader", 1.0f, 6, 1);
        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.25f);
        const auto* kept = Find<JBro::MaterialEffect>(mixer.GetResult(), JBro::PostProcessEffects::Material);
        Check(false == kept->mixed && Near(ReadMaterialCells(*kept).number, 0.0f) && kept->rendererMaterial.index == 1,
            "a material of another shader does not mix under half");
        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 0.75f);
        const auto* switched = Find<JBro::MaterialEffect>(mixer.GetResult(), JBro::PostProcessEffects::Material);
        Check(false == switched->mixed && switched->rendererMaterial.index == 2 && switched->shaderId == JBro::Uuid::FromName("other shader"),
            "and switches whole over half");

        // 머티리얼을 덮어쓰지 않은 항목은 섞을 것이 없다.
        volume.overrides[0] = 0;
        SetMaterialCells(storm, "fog shader", 1.0f, 6, 1);
        mixer.Begin(base.Stack());
        mixer.Blend(volume.View(), 1.0f);
        const auto* untouched = Find<JBro::MaterialEffect>(mixer.GetResult(), JBro::PostProcessEffects::Material);
        Check(false == untouched->mixed && untouched->rendererMaterial.index == 1, "an entry that did not override the material mixes nothing");
    }

    void TestScriptOverridesSitOnTop()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(8, 1024);
        const JBro::UInt32 saturation = JBro::PostProcessMixer::FindField("ColorAdjustments", "saturation");
        Check(saturation != JBro::PostProcessMixer::InvalidField, "a field is found by its effect and name");
        Check(JBro::PostProcessMixer::FindField("ColorAdjustments", "nothing") == JBro::PostProcessMixer::InvalidField, "an unknown field is not");
        Check(JBro::PostProcessMixer::FindField("NoSuchEffect", "saturation") == JBro::PostProcessMixer::InvalidField, "nor an unknown effect");
        Check(JBro::PostProcessMixer::FindField("ColorLookup", "lutTexture") == JBro::PostProcessMixer::InvalidField,
            "a resolved value cannot be overridden");
        const JBro::Float grey = -100.0f;
        const JBro::Int32 wrong = 3;
        Check(false == mixer.SetOverride(saturation, JBro::TypeDescriptorOf<JBro::Int32>::Get(), &wrong), "the value must be the field's type");
        Check(mixer.SetOverride(saturation, JBro::TypeDescriptorOf<JBro::Float>::Get(), &grey), "a Float field takes a Float");

        Profile volume;
        volume.Add<JBro::ColorAdjustmentsEffect>(JBro::PostProcessEffects::ColorAdjustments, (JBro::UInt64(1) << 3) | 1).contrast = 50.0f;
        mixer.Begin({});
        mixer.Blend(volume.View(), 1.0f);
        mixer.ApplyOverrides();
        const auto* adjustments = Find<JBro::ColorAdjustmentsEffect>(mixer.GetResult(), JBro::PostProcessEffects::ColorAdjustments);
        Check(Near(adjustments->saturation, -100.0f) && Near(adjustments->contrast, 50.0f), "the script's value is laid over the volume's");

        mixer.ClearOverride(saturation);
        Check(mixer.GetOverrideCount() == 0, "clearing removes the override");
        mixer.Begin({});
        mixer.ApplyOverrides();
        Check(mixer.GetResult().entries.size == 0, "and with nothing left the view has no effects");

        // 자리(32)가 차면 더 받지 않는다. 같은 필드는 갈아 끼운다.
        for (JBro::Int32 at = 0; at < 40; ++at)
        {
            mixer.SetOverride(saturation, JBro::TypeDescriptorOf<JBro::Float>::Get(), &grey);
        }
        Check(mixer.GetOverrideCount() == 1, "the same field is replaced, not added");
        mixer.ClearOverrides();
        Check(mixer.GetOverrideCount() == 0, "clearing all empties the list");
    }

    void TestTheMixerDropsWhatDoesNotFit()
    {
        JBro::PostProcessMixer mixer;
        mixer.Reserve(1, 1024);
        Profile base;
        base.Add<JBro::ExposureEffect>(JBro::PostProcessEffects::Exposure, 0);
        base.Add<JBro::TonemapEffect>(JBro::PostProcessEffects::Tonemap, 0);
        mixer.Begin(base.Stack());
        Check(mixer.GetResult().entries.size == 1 && mixer.GetDroppedCount() == 1, "an entry past the reserved room is dropped and counted");
    }

    void TestA2DVolumeWeighsByDistance()
    {
        JBro::Component::PostProcessVolume2D volume;
        JBro::Component::Transform2D transform;
        transform.worldPosition = {10.0f, 0.0f};
        transform.worldScale = {2.0f, 2.0f};
        volume.weight = 0.8f;
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, nullptr, {1000.0f, 0.0f}), 0.8f), "a global volume is its weight anywhere");

        volume.shape = JBro::Component::PostProcessVolumeShape2D::Box;
        volume.size = {2.0f, 1.0f};
        volume.blendDistance = 4.0f;
        // 크기 2 배라 반폭은 2(x)·1(y) 다.
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {11.5f, 0.5f}), 0.8f), "inside the box is the full weight");
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {14.0f, 0.0f}), 0.8f * 0.5f),
            "two units outside of a four unit blend is half");
        Check(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {17.0f, 0.0f}) == 0.0f, "past the blend distance is nothing");
        Check(JBro::ComputePostProcessVolumeWeight2D(volume, nullptr, {10.0f, 0.0f}) == 0.0f, "a shape without a transform is nothing");
        // 90 도 돌리면 긴 쪽이 세로다.
        transform.worldRotation = 1.5707964f;
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {10.0f, 1.5f}), 0.8f), "the box turns with its object");
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {11.5f, 0.0f}), 0.8f * (1.0f - 0.5f / 4.0f)),
            "and is narrow across");
        volume.blendDistance = 0.0f;
        Check(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {11.5f, 0.0f}) == 0.0f, "without a blend distance the edge is sharp");

        volume.shape = JBro::Component::PostProcessVolumeShape2D::Circle;
        volume.radius = 1.0f;
        volume.blendDistance = 2.0f;
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {11.9f, 0.0f}), 0.8f), "inside the scaled circle is full");
        Check(Near(JBro::ComputePostProcessVolumeWeight2D(volume, &transform, {13.0f, 0.0f}), 0.8f * 0.5f), "a unit outside is half");
    }

    void TestA3DVolumeWeighsByDistance()
    {
        JBro::Component::PostProcessVolume3D volume;
        JBro::Component::Transform3D transform;
        transform.worldPosition = {0.0f, 0.0f, 5.0f};
        volume.shape = JBro::Component::PostProcessVolumeShape3D::Sphere;
        volume.radius = 2.0f;
        volume.blendDistance = 2.0f;
        Check(Near(JBro::ComputePostProcessVolumeWeight3D(volume, &transform, {0.0f, 1.0f, 5.0f}), 1.0f), "inside the sphere is full");
        Check(Near(JBro::ComputePostProcessVolumeWeight3D(volume, &transform, {0.0f, 0.0f, 8.0f}), 0.5f), "a unit outside is half");
        volume.shape = JBro::Component::PostProcessVolumeShape3D::Box;
        volume.size = {4.0f, 2.0f, 2.0f};
        Check(Near(JBro::ComputePostProcessVolumeWeight3D(volume, &transform, {1.9f, 0.0f, 5.0f}), 1.0f), "inside the long side of the box");
        // y 축으로 90 도 돌리면 긴 쪽이 z 다.
        const JBro::Float half = std::sqrt(0.5f);
        transform.worldRotation = {0.0f, half, 0.0f, half};
        Check(Near(JBro::ComputePostProcessVolumeWeight3D(volume, &transform, {1.9f, 0.0f, 5.0f}), 1.0f - 0.9f / 2.0f),
            "turned, the same point is outside the short side");
        Check(Near(JBro::ComputePostProcessVolumeWeight3D(volume, &transform, {0.0f, 0.0f, 6.9f}), 1.0f), "and the long side runs along z");
    }
}

JBro::Int32 RunPostProcessMixerTests()
{
    TestFieldsMixByTheirKind();
    TestAVolumeAddsWhatTheBaseLacks();
    TestAnAssetSwitchCarriesItsResolvedValues();
    TestMaterialsOfOneShaderMixTheirParameters();
    TestScriptOverridesSitOnTop();
    TestTheMixerDropsWhatDoesNotFit();
    TestA2DVolumeWeighsByDistance();
    TestA3DVolumeWeighsByDistance();
    std::cout << "Post-process mixer tests passed.\n";
    return 0;
}
