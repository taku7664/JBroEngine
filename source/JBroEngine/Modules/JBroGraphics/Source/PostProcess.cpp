#include <JBro/Graphics/PostProcess.h>

#include <JBro/Graphics/MaterialLibrary.h>
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
            constants.params[0][3] = builder.IsPremultiplied() ? 1.0f : 0.0f;
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
            constants.params[0][2] = builder.IsPremultiplied() ? 1.0f : 0.0f;
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

        // ── 레이어 효과(D-345) ── 반경과 굵기는 대상 픽셀이고 셰이더의 탭 상한(33)까지다.
        constexpr Float MaxLayerReach = 32.0f;

        Float ClampReach(Float value)
        {
            // 유한하지 않은 값(NaN)은 0 이다 - 비교가 둘 다 거짓이라 처음 값이 남는다.
            Float reach = 0.0f;
            if (value > MaxLayerReach)
            {
                reach = MaxLayerReach;
            }
            else if (value > 0.0f)
            {
                reach = value;
            }
            return reach;
        }

        // 가우스의 탭 수와 시그마다. 반경의 절반이 시그마이고 반경을 올린 만큼 탭을 둔다.
        void GaussianTaps(Float radius, Float& taps, Float& sigma)
        {
            const Float reach = ClampReach(radius);
            taps = static_cast<JBro::Float>(std::ceil(static_cast<double>(reach.Get())));
            sigma = reach * 0.5f > 0.5f ? reach * 0.5f : Float(0.5f);
        }

        Float RoundPixel(Float value)
        {
            return static_cast<JBro::Float>(std::lround(static_cast<double>(value.Get())));
        }

        // 효과 안의 중간 텍스처에 쓰는 패스다. 뷰와 같은 크기·입력 포맷이고 전체를 쓴다.
        PostProcessPassDesc WholeTexturePass(PostProcessBuilder& builder, const char* name, PostProcessProgram program)
        {
            PostProcessPassDesc pass;
            pass.name = name;
            pass.program = program;
            pass.outputExtent = builder.GetExtent();
            pass.outputFormat = builder.GetInputFormat();
            pass.output = builder.CreateTexture(pass.outputExtent, pass.outputFormat, name);
            return pass;
        }

        Bool AddBlurPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const BlurEffect& effect = *static_cast<const BlurEffect*>(parameters);
            WideConstants across;
            GaussianTaps(effect.radius, across.params[0][2], across.params[0][3]);
            if (across.params[0][2] == 0.0f)
            {
                return builder.AddFullscreenPass("Blur", PostProcessProgram::Copy, {});
            }
            across.params[0][0] = 1.0f;
            PostProcessPassDesc first = WholeTexturePass(builder, "BlurAcross", PostProcessProgram::LayerBlur);
            first.constants = AsBytes(across);
            if (false == builder.AddPass(first))
            {
                return false;
            }
            WideConstants down = across;
            down.params[0][0] = 0.0f;
            down.params[0][1] = 1.0f;
            PostProcessPassDesc second;
            second.name = "BlurDown";
            second.program = PostProcessProgram::LayerBlur;
            second.inputs[0] = first.output;
            second.inputCount = 1;
            second.constants = AsBytes(down);
            return builder.AddPass(second);
        }

        Bool AddOutlinePasses(PostProcessBuilder& builder, const void* parameters)
        {
            const OutlineEffect& effect = *static_cast<const OutlineEffect*>(parameters);
            const Float width = ClampReach(effect.width);
            if (width == 0.0f || effect.color.A <= 0.0f)
            {
                return builder.AddFullscreenPass("Outline", PostProcessProgram::Copy, {});
            }
            // 끝을 한 픽셀에 걸쳐 흐리므로 굵기보다 한 칸 더 본다.
            const Float reach = static_cast<JBro::Float>(std::ceil(static_cast<double>(width.Get()))) + 1.0f;
            WideConstants distance;
            distance.params[0][0] = reach;
            PostProcessPassDesc first = WholeTexturePass(builder, "OutlineDistance", PostProcessProgram::LayerOutlineDistance);
            first.constants = AsBytes(distance);
            if (false == builder.AddPass(first))
            {
                return false;
            }
            WideConstants ring;
            ring.params[0][0] = width;
            ring.params[0][1] = reach;
            ring.params[1][0] = effect.color.R;
            ring.params[1][1] = effect.color.G;
            ring.params[1][2] = effect.color.B;
            ring.params[1][3] = effect.color.A;
            PostProcessPassDesc second;
            second.name = "OutlineComposite";
            second.program = PostProcessProgram::LayerOutlineComposite;
            second.inputs[0] = first.output;
            second.inputs[1] = builder.GetInput();
            second.inputCount = 2;
            second.constants = AsBytes(ring);
            return builder.AddPass(second);
        }

        Bool AddDropShadowPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const DropShadowEffect& effect = *static_cast<const DropShadowEffect*>(parameters);
            if (effect.color.A <= 0.0f)
            {
                return builder.AddFullscreenPass("DropShadow", PostProcessProgram::Copy, {});
            }
            // 옮김은 화면 픽셀이고 y 는 위가 + 다. 텍스처의 줄은 아래로 늘어나므로 y 를 뒤집는다. 픽셀 단위로 반올림한다.
            WideConstants across;
            across.params[0][0] = RoundPixel(effect.offset.x);
            across.params[0][1] = -RoundPixel(effect.offset.y);
            GaussianTaps(effect.softness, across.params[0][2], across.params[0][3]);
            PostProcessPassDesc first = WholeTexturePass(builder, "ShadowAcross", PostProcessProgram::LayerShadowBlur);
            first.constants = AsBytes(across);
            if (false == builder.AddPass(first))
            {
                return false;
            }
            WideConstants down;
            down.params[0][2] = across.params[0][2];
            down.params[0][3] = across.params[0][3];
            down.params[1][0] = effect.color.R;
            down.params[1][1] = effect.color.G;
            down.params[1][2] = effect.color.B;
            down.params[1][3] = effect.color.A;
            PostProcessPassDesc second;
            second.name = "ShadowComposite";
            second.program = PostProcessProgram::LayerShadowComposite;
            second.inputs[0] = first.output;
            second.inputs[1] = builder.GetInput();
            second.inputCount = 2;
            second.constants = AsBytes(down);
            return builder.AddPass(second);
        }

        // 머티리얼(D-347). 렌더러의 머티리얼이 없으면 패스 서술이 그대로 옮기기로 남는다.
        Bool AddMaterialPasses(PostProcessBuilder& builder, const void* parameters)
        {
            const MaterialEffect& effect = *static_cast<const MaterialEffect*>(parameters);
            PostProcessPassDesc pass;
            pass.name = "Material";
            pass.program = PostProcessProgram::Copy;
            pass.material = effect.rendererMaterial;
            // 볼륨이 같은 셰이더의 두 머티리얼을 섞었으면 섞인 파라미터로 그린다(D-348).
            if (effect.mixed && effect.parameterBytes <= MaxMaterialParameterBytes)
            {
                pass.materialParameters = {effect.parameters, effect.parameterBytes};
            }
            return builder.AddPass(pass);
        }

        // **머티리얼 효과를 섞는다**(D-348). 같은 셰이더면 파라미터 칸을 종류대로 보간한다 - 실수는 잇고, 정수는 이어서 반올림하며, 참거짓은 반에서
        // 바뀐다. 머티리얼(과 그 텍스처)은 반에서 바뀐다. 셰이더가 다르면 블록 전체가 반에서 바뀐다.
        Bool MixMaterial(void* targetBlock, const void* sourceBlock, UInt64 overrides, Float weight)
        {
            MaterialEffect& target = *static_cast<MaterialEffect*>(targetBlock);
            const MaterialEffect& source = *static_cast<const MaterialEffect*>(sourceBlock);
            // 첫 필드(`materialId`)를 덮어쓰지 않은 항목은 섞을 것이 없다.
            if ((overrides & 1u) == 0)
            {
                return true;
            }
            const UInt32 cells = source.parameterBytes / 4;
            const Bool sameShader = false == source.shaderId.IsNull() && target.shaderId == source.shaderId
                && target.parameterBytes == source.parameterBytes && source.parameterBytes <= MaxMaterialParameterBytes
                && std::memcmp(target.cellKinds, source.cellKinds, cells.Get()) == 0;
            if (false == sameShader)
            {
                if (weight >= 0.5f)
                {
                    std::memcpy(&target, &source, sizeof(MaterialEffect));
                }
                return true;
            }
            for (UInt32 cell = 0; cell < cells; ++cell)
            {
                std::byte* to = target.parameters + cell.Get() * 4;
                const std::byte* from = source.parameters + cell.Get() * 4;
                switch (static_cast<MaterialCellKind>(source.cellKinds[cell.Get()]))
                {
                case MaterialCellKind::Float:
                {
                    float a = 0.0f;
                    float b = 0.0f;
                    std::memcpy(&a, to, 4);
                    std::memcpy(&b, from, 4);
                    const float mixed = a + (b - a) * weight.Get();
                    std::memcpy(to, &mixed, 4);
                    break;
                }
                case MaterialCellKind::Int:
                {
                    std::int32_t a = 0;
                    std::int32_t b = 0;
                    std::memcpy(&a, to, 4);
                    std::memcpy(&b, from, 4);
                    const std::int32_t mixed = static_cast<std::int32_t>(std::lround(a + (static_cast<double>(b) - a) * weight.Get()));
                    std::memcpy(to, &mixed, 4);
                    break;
                }
                case MaterialCellKind::UInt:
                {
                    std::uint32_t a = 0;
                    std::uint32_t b = 0;
                    std::memcpy(&a, to, 4);
                    std::memcpy(&b, from, 4);
                    const std::uint32_t mixed = static_cast<std::uint32_t>(std::llround(a + (static_cast<double>(b) - a) * weight.Get()));
                    std::memcpy(to, &mixed, 4);
                    break;
                }
                case MaterialCellKind::Bool:
                    if (weight >= 0.5f)
                    {
                        std::memcpy(to, from, 4);
                    }
                    break;
                case MaterialCellKind::Unused:
                    break;
                }
            }
            target.mixed = true;
            if (weight >= 0.5f)
            {
                target.materialId = source.materialId;
                target.material = source.material;
                target.rendererMaterial = source.rendererMaterial;
            }
            return true;
        }

        const ExposureEffect DefaultExposure = {};
        const TonemapEffect DefaultTonemap = {};
        const BloomEffect DefaultBloom = {};
        const ColorAdjustmentsEffect DefaultColorAdjustments = {};
        const ColorLookupEffect DefaultColorLookup = {};
        const BlurEffect DefaultBlur = {};
        const OutlineEffect DefaultOutline = {};
        const DropShadowEffect DefaultDropShadow = {};
        const MaterialEffect DefaultMaterial = {};

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
                adjustments.onLayer = true;
                adjustments.parameters = &GetPropertyTable<ColorAdjustmentsEffect>();
                adjustments.blockSize = static_cast<JBro::UInt32>(sizeof(ColorAdjustmentsEffect));
                adjustments.defaults = &DefaultColorAdjustments;
                adjustments.addPasses = &AddColorAdjustmentsPasses;
                AddTo(built, adjustments);

                PostProcessEffectType lookup;
                lookup.name = "ColorLookup";
                lookup.stage = PostProcessStage::AfterTonemap;
                lookup.onLayer = true;
                lookup.parameters = &GetPropertyTable<ColorLookupEffect>();
                lookup.blockSize = static_cast<JBro::UInt32>(sizeof(ColorLookupEffect));
                lookup.defaults = &DefaultColorLookup;
                lookup.addPasses = &AddColorLookupPasses;
                AddTo(built, lookup);

                PostProcessEffectType blur;
                blur.name = "Blur";
                blur.stage = PostProcessStage::AfterLayer;
                blur.onLayer = true;
                blur.parameters = &GetPropertyTable<BlurEffect>();
                blur.blockSize = static_cast<JBro::UInt32>(sizeof(BlurEffect));
                blur.defaults = &DefaultBlur;
                blur.addPasses = &AddBlurPasses;
                AddTo(built, blur);

                PostProcessEffectType outline;
                outline.name = "Outline";
                outline.stage = PostProcessStage::AfterLayer;
                outline.onLayer = true;
                outline.parameters = &GetPropertyTable<OutlineEffect>();
                outline.blockSize = static_cast<JBro::UInt32>(sizeof(OutlineEffect));
                outline.defaults = &DefaultOutline;
                outline.addPasses = &AddOutlinePasses;
                AddTo(built, outline);

                PostProcessEffectType shadow;
                shadow.name = "DropShadow";
                shadow.stage = PostProcessStage::AfterLayer;
                shadow.onLayer = true;
                shadow.parameters = &GetPropertyTable<DropShadowEffect>();
                shadow.blockSize = static_cast<JBro::UInt32>(sizeof(DropShadowEffect));
                shadow.defaults = &DefaultDropShadow;
                shadow.addPasses = &AddDropShadowPasses;
                AddTo(built, shadow);

                PostProcessEffectType material;
                material.name = "Material";
                material.stage = PostProcessStage::AfterTonemap;
                material.onLayer = true;
                material.parameters = &GetPropertyTable<MaterialEffect>();
                material.blockSize = static_cast<JBro::UInt32>(sizeof(MaterialEffect));
                material.defaults = &DefaultMaterial;
                material.addPasses = &AddMaterialPasses;
                material.mix = &MixMaterial;
                AddTo(built, material);
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
