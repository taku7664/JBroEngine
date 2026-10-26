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

        const ExposureEffect DefaultExposure = {};
        const TonemapEffect DefaultTonemap = {};

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
        return m_renderer != nullptr && m_renderer->AddPostProcessPass(*this, name, program, constants);
    }
}
