#include <JBro/Graphics/PostProcess.h>

#include <JBro/Graphics/Renderer.h>

#include <cmath>
#include <cstring>

namespace JBro
{
    namespace
    {
        // 상수 블록은 float4 하나다(BuiltinPostProcess.hlsl 의 b1).
        struct PostConstants
        {
            Float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        };

        JArrayView<std::byte> AsBytes(const PostConstants& constants)
        {
            return {reinterpret_cast<const std::byte*>(&constants), static_cast<JBro::UInt32>(sizeof(constants))};
        }

        Bool AddExposurePasses(PostProcessBuilder& builder, const void* parameters)
        {
            const ExposureEffect& effect = *static_cast<const ExposureEffect*>(parameters);
            // 장면은 화면 값이다. 빛을 2^EV 배 하는 것은 화면 값을 2^(EV / 2.2) 배 하는 것이다(셰이더 머리말).
            PostConstants constants;
            constants.params[0] = static_cast<JBro::Float>(std::exp2(static_cast<double>(effect.exposure.Get()) / 2.2));
            return builder.AddFullscreenPass("Exposure", PostProcessProgram::Exposure, AsBytes(constants));
        }

        Bool AddTonemapPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const TonemapEffect& effect = *static_cast<const TonemapEffect*>(parameters);
            PostConstants constants;
            constants.params[0] = static_cast<JBro::Float>(static_cast<JBro::UInt32>(effect.curve));
            return builder.AddFullscreenPass("Tonemap", PostProcessProgram::Tonemap, AsBytes(constants));
        }

        // 상수 블록이 float4 넷인 패스(블룸·그레이딩, D-343)다.
        struct WideConstants
        {
            Float params[4][4] = {};
        };

        JArrayView<std::byte> AsBytes(const WideConstants& constants)
        {
            return {reinterpret_cast<const std::byte*>(&constants), static_cast<JBro::UInt32>(sizeof(constants))};
        }

        Float Inverse(UInt32 size)
        {
            return 1.0f / static_cast<JBro::Float>(size.Get() == 0 ? 1u : size.Get());
        }

        // **블룸**(D-343). 절반 크기에서 시작하는 밉 체인이다 - 거르며 줄이기 → 줄이기 → 가장 작은 것부터 키우며 섞기 → 장면에 더하기.
        // 밉은 효과가 만든 텍스처라 패스마다 그 전체를 쓴다. 장면의 뷰포트 밖은 다른 뷰이거나 비었으므로 거를 때 뷰포트 안으로 좌표를 묶는다.
        constexpr UInt32 MaxBloomMips = 8;
        const char* const BloomDownNames[MaxBloomMips] = {
            "BloomDown0", "BloomDown1", "BloomDown2", "BloomDown3", "BloomDown4", "BloomDown5", "BloomDown6", "BloomDown7"};
        const char* const BloomUpNames[MaxBloomMips] = {
            "BloomUp0", "BloomUp1", "BloomUp2", "BloomUp3", "BloomUp4", "BloomUp5", "BloomUp6", "BloomUp7"};

        Bool AddBloomPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const BloomEffect& effect = *static_cast<const BloomEffect*>(parameters);
            const Extent2D full = builder.GetExtent();
            Extent2D sizes[MaxBloomMips];
            RenderGraphTexture down[MaxBloomMips];
            const UInt32 wanted = effect.iterations < 1 ? UInt32(1)
                : (effect.iterations > 8 ? MaxBloomMips : static_cast<JBro::UInt32>(static_cast<std::uint32_t>(effect.iterations.Get())));
            Extent2D mip = {full.width > 1 ? (full.width + 1) / 2 : UInt32(1), full.height > 1 ? (full.height + 1) / 2 : UInt32(1)};
            UInt32 count = 0;
            while (count < wanted)
            {
                sizes[count.Get()] = mip;
                down[count.Get()] = builder.CreateTexture(mip, PostProcessHdrFormat, BloomDownNames[count.Get()]);
                ++count;
                if (mip.width <= 2 || mip.height <= 2)
                {
                    break;
                }
                mip = {mip.width / 2, mip.height / 2};
            }

            // 거르며 줄이기: 뷰포트 안 텍셀의 가운데까지만 읽는다.
            const Viewport viewport = builder.GetViewport();
            const Float fullWidth = static_cast<JBro::Float>(full.width);
            const Float fullHeight = static_cast<JBro::Float>(full.height);
            WideConstants prefilter;
            prefilter.params[0][0] = effect.threshold;
            prefilter.params[0][1] = effect.threshold * 0.5f;
            prefilter.params[0][2] = effect.clamp;
            prefilter.params[1][0] = (viewport.x + 0.5f) / fullWidth;
            prefilter.params[1][1] = (viewport.y + 0.5f) / fullHeight;
            prefilter.params[1][2] = (viewport.x + viewport.width - 0.5f) / fullWidth;
            prefilter.params[1][3] = (viewport.y + viewport.height - 0.5f) / fullHeight;
            prefilter.params[2][0] = Inverse(sizes[0].width);
            prefilter.params[2][1] = Inverse(sizes[0].height);
            PostProcessPassDesc pass;
            pass.name = "BloomPrefilter";
            pass.program = PostProcessProgram::BloomPrefilter;
            pass.output = down[0];
            pass.outputExtent = sizes[0];
            pass.outputFormat = PostProcessHdrFormat;
            pass.constants = AsBytes(prefilter);
            if (false == builder.AddPass(pass))
            {
                return false;
            }
            for (UInt32 at = 1; at < count; ++at)
            {
                WideConstants constants;
                constants.params[0][0] = Inverse(sizes[(at - 1).Get()].width);
                constants.params[0][1] = Inverse(sizes[(at - 1).Get()].height);
                constants.params[2][0] = Inverse(sizes[at.Get()].width);
                constants.params[2][1] = Inverse(sizes[at.Get()].height);
                PostProcessPassDesc downsample;
                downsample.name = "BloomDownsample";
                downsample.program = PostProcessProgram::BloomDownsample;
                downsample.inputs[0] = down[(at - 1).Get()];
                downsample.inputCount = 1;
                downsample.output = down[at.Get()];
                downsample.outputExtent = sizes[at.Get()];
                downsample.outputFormat = PostProcessHdrFormat;
                downsample.constants = AsBytes(constants);
                if (false == builder.AddPass(downsample))
                {
                    return false;
                }
            }
            // 키우며 섞기: 가장 작은 밉부터 한 칸 큰 밉과 섞어 올라온다.
            RenderGraphTexture blurred = down[(count - 1).Get()];
            for (UInt32 level = count - 1; level > 0; --level)
            {
                const UInt32 at = level - 1;
                const RenderGraphTexture up = builder.CreateTexture(sizes[at.Get()], PostProcessHdrFormat, BloomUpNames[at.Get()]);
                WideConstants constants;
                constants.params[0][0] = Inverse(sizes[level.Get()].width);
                constants.params[0][1] = Inverse(sizes[level.Get()].height);
                constants.params[0][2] = effect.scatter;
                constants.params[2][0] = Inverse(sizes[at.Get()].width);
                constants.params[2][1] = Inverse(sizes[at.Get()].height);
                PostProcessPassDesc upsample;
                upsample.name = "BloomUpsample";
                upsample.program = PostProcessProgram::BloomUpsample;
                upsample.inputs[0] = down[at.Get()];
                upsample.inputs[1] = blurred;
                upsample.inputCount = 2;
                upsample.output = up;
                upsample.outputExtent = sizes[at.Get()];
                upsample.outputFormat = PostProcessHdrFormat;
                upsample.constants = AsBytes(constants);
                if (false == builder.AddPass(upsample))
                {
                    return false;
                }
                blurred = up;
            }
            WideConstants composite;
            composite.params[0][0] = effect.intensity;
            composite.params[1][0] = effect.tint.R;
            composite.params[1][1] = effect.tint.G;
            composite.params[1][2] = effect.tint.B;
            composite.params[2][0] = 1.0f / fullWidth;
            composite.params[2][1] = 1.0f / fullHeight;
            PostProcessPassDesc add;
            add.name = "BloomComposite";
            add.program = PostProcessProgram::BloomComposite;
            add.inputs[0] = builder.GetInput();
            add.inputs[1] = blurred;
            add.inputCount = 2;
            add.constants = AsBytes(composite);
            return builder.AddPass(add);
        }

        Bool AddColorAdjustmentsPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const ColorAdjustmentsEffect& effect = *static_cast<const ColorAdjustmentsEffect*>(parameters);
            // -100..100 은 0..2 배이고, 색조는 한 바퀴의 몫이다.
            WideConstants constants;
            constants.params[0][0] = (effect.contrast + 100.0f) / 100.0f;
            constants.params[0][1] = (effect.saturation + 100.0f) / 100.0f;
            constants.params[0][2] = effect.hueShift / 360.0f;
            constants.params[1][0] = effect.colorFilter.R;
            constants.params[1][1] = effect.colorFilter.G;
            constants.params[1][2] = effect.colorFilter.B;
            return builder.AddFullscreenPass("ColorAdjustments", PostProcessProgram::ColorAdjust, AsBytes(constants));
        }

        Bool AddColorLookupPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const ColorLookupEffect& effect = *static_cast<const ColorLookupEffect*>(parameters);
            UInt32 width = 0;
            UInt32 height = 0;
            UInt32 depth = 0;
            // LUT 가 아직 없으면(에셋을 고르지 않았거나 싣지 못했다) 그대로 옮긴다 - 효과는 출력을 써야 한다.
            if (effect.lutTexture.generation == 0 || false == builder.GetTextureSize(effect.lutTexture, width, height, depth) || depth < 2)
            {
                return builder.AddFullscreenPass("ColorLookup", PostProcessProgram::Copy, {});
            }
            WideConstants constants;
            constants.params[0][0] = effect.contribution;
            constants.params[0][1] = static_cast<JBro::Float>(width.Get());
            constants.params[1][0] = effect.lutDomainMin.x;
            constants.params[1][1] = effect.lutDomainMin.y;
            constants.params[1][2] = effect.lutDomainMin.z;
            constants.params[2][0] = effect.lutDomainMax.x;
            constants.params[2][1] = effect.lutDomainMax.y;
            constants.params[2][2] = effect.lutDomainMax.z;
            PostProcessPassDesc pass;
            pass.name = "ColorLookup";
            pass.program = PostProcessProgram::ColorLookup;
            pass.externalTexture = effect.lutTexture;
            pass.constants = AsBytes(constants);
            return builder.AddPass(pass);
        }

        const ExposureEffect DefaultExposure = {};
        const TonemapEffect DefaultTonemap = {};
        const BloomEffect DefaultBloom = {};
        const ColorAdjustmentsEffect DefaultColorAdjustments = {};
        const ColorLookupEffect DefaultColorLookup = {};

        struct RegistryStorage
        {
            PostProcessEffectType types[PostProcessEffectRegistry::Capacity];
            UInt32 count = 0;
        };

        Bool IsValidType(const PostProcessEffectType& type)
        {
            if (type.name == nullptr || type.name[0] == '\0' || type.addPasses == nullptr)
            {
                return false;
            }
            // 블록이 있으면 기본값도 있어야 하고, 없으면 둘 다 없다.
            return (type.blockSize == 0) == (type.defaults == nullptr);
        }

        UInt32 FindIn(const RegistryStorage& storage, const char* name)
        {
            if (name == nullptr)
            {
                return PostProcessEffectRegistry::NotFound;
            }
            for (UInt32 at = 0; at < storage.count; ++at)
            {
                if (std::strcmp(storage.types[at.Get()].name, name) == 0)
                {
                    return at;
                }
            }
            return PostProcessEffectRegistry::NotFound;
        }

        UInt32 AddTo(RegistryStorage& storage, const PostProcessEffectType& type)
        {
            if (storage.count >= PostProcessEffectRegistry::Capacity || false == IsValidType(type)
                || FindIn(storage, type.name) != PostProcessEffectRegistry::NotFound)
            {
                return PostProcessEffectRegistry::NotFound;
            }
            const UInt32 id = storage.count;
            storage.types[id.Get()] = type;
            ++storage.count;
            return id;
        }

        // 표는 처음 열릴 때 빌트인을 정해진 번호로 들인다(`PostProcessEffects`). 정적 초기화 차례에 기대지 않는다.
        RegistryStorage& Storage()
        {
            static RegistryStorage storage = [] {
                RegistryStorage built;
                PostProcessEffectType exposure;
                exposure.name = "Exposure";
                exposure.stage = PostProcessStage::BeforeTonemap;
                exposure.needs = PostProcessNeeds::Hdr;
                exposure.parameters = &GetPropertyTable<ExposureEffect>();
                exposure.blockSize = static_cast<JBro::UInt32>(sizeof(ExposureEffect));
                exposure.defaults = &DefaultExposure;
                exposure.addPasses = &AddExposurePasses;
                AddTo(built, exposure);

                PostProcessEffectType tonemap;
                tonemap.name = "Tonemap";
                tonemap.stage = PostProcessStage::Tonemap;
                tonemap.needs = PostProcessNeeds::Hdr;
                tonemap.parameters = &GetPropertyTable<TonemapEffect>();
                tonemap.blockSize = static_cast<JBro::UInt32>(sizeof(TonemapEffect));
                tonemap.defaults = &DefaultTonemap;
                tonemap.addPasses = &AddTonemapPasses;
                AddTo(built, tonemap);

                PostProcessEffectType bloom;
                bloom.name = "Bloom";
                bloom.stage = PostProcessStage::BeforeTonemap;
                bloom.needs = PostProcessNeeds::Hdr;
                bloom.parameters = &GetPropertyTable<BloomEffect>();
                bloom.blockSize = static_cast<JBro::UInt32>(sizeof(BloomEffect));
                bloom.defaults = &DefaultBloom;
                bloom.addPasses = &AddBloomPasses;
                AddTo(built, bloom);

                PostProcessEffectType adjustments;
                adjustments.name = "ColorAdjustments";
                adjustments.stage = PostProcessStage::AfterTonemap;
                adjustments.parameters = &GetPropertyTable<ColorAdjustmentsEffect>();
                adjustments.blockSize = static_cast<JBro::UInt32>(sizeof(ColorAdjustmentsEffect));
                adjustments.defaults = &DefaultColorAdjustments;
                adjustments.addPasses = &AddColorAdjustmentsPasses;
                AddTo(built, adjustments);

                PostProcessEffectType lookup;
                lookup.name = "ColorLookup";
                lookup.stage = PostProcessStage::AfterTonemap;
                lookup.parameters = &GetPropertyTable<ColorLookupEffect>();
                lookup.blockSize = static_cast<JBro::UInt32>(sizeof(ColorLookupEffect));
                lookup.defaults = &DefaultColorLookup;
                lookup.addPasses = &AddColorLookupPasses;
                AddTo(built, lookup);
                return built;
            }();
            return storage;
        }
    }

    UInt32 PostProcessEffectRegistry::Register(const PostProcessEffectType& type)
    {
        return AddTo(Storage(), type);
    }

    const PostProcessEffectType* PostProcessEffectRegistry::Get(UInt32 id)
    {
        RegistryStorage& storage = Storage();
        return id < storage.count ? &storage.types[id.Get()] : nullptr;
    }

    UInt32 PostProcessEffectRegistry::Find(const char* name)
    {
        return FindIn(Storage(), name);
    }

    UInt32 PostProcessEffectRegistry::GetCount()
    {
        return Storage().count;
    }

    Bool PostProcessBuilder::AddFullscreenPass(const char* name, PostProcessProgram program, JArrayView<std::byte> constants)
    {
        PostProcessPassDesc desc;
        desc.name = name;
        desc.program = program;
        desc.constants = constants;
        return AddPass(desc);
    }

    Bool PostProcessBuilder::AddPass(const PostProcessPassDesc& desc)
    {
        if (m_renderer == nullptr)
        {
            return false;
        }
        // 셈만 하는 차례에는 그래프를 건드리지 않는다.
        if (m_counting)
        {
            return desc.name != nullptr && desc.constants.size <= MaxConstantBytes && desc.inputCount <= PostProcessPassDesc::MaxInputs;
        }
        return m_renderer->AddPostProcessPass(*this, desc);
    }

    RenderGraphTexture PostProcessBuilder::CreateTexture(const Extent2D& extent, TextureFormat format, const char* name)
    {
        const TransientTextureDesc desc = {extent, format, TextureUsage::RenderTarget | TextureUsage::Sampled};
        if (m_counting)
        {
            for (UInt32 at = 0; at < *m_countSize; ++at)
            {
                TextureCount& entry = m_counts[at.Get()];
                if (entry.desc.extent.width == extent.width && entry.desc.extent.height == extent.height && entry.desc.format == format)
                {
                    ++entry.count;
                    return RenderGraphTexture{0};
                }
            }
            if (*m_countSize >= MaxCountedShapes)
            {
                m_countOverflow = true;
                return RenderGraphTexture{0};
            }
            m_counts[m_countSize->Get()] = {desc, 1};
            ++*m_countSize;
            return RenderGraphTexture{0};
        }
        if (m_renderer == nullptr || extent.width == 0 || extent.height == 0)
        {
            return {};
        }
        return m_renderer->m_graph.CreateTexture(desc, name);
    }

    Bool PostProcessBuilder::GetTextureSize(AssetHandle texture, UInt32& width, UInt32& height, UInt32& depth) const
    {
        return m_renderer != nullptr && m_renderer->GetRegisteredTextureSize(texture, width, height, depth);
    }
}
