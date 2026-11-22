#include <JBro/Framework3DSystem/Rendering/EnvironmentLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Renderer.h>

#include <JBro/Types/Int.h>

namespace JBro
{
    void EnvironmentLibrary::Initialize(Renderer* renderer, const AssetSystem* assets)
    {
        m_renderer = renderer;
        m_assets = assets;
    }

    void EnvironmentLibrary::Shutdown()
    {
        Release();
        m_renderer = nullptr;
        m_assets = nullptr;
    }

    void EnvironmentLibrary::Release()
    {
        if (m_renderer != nullptr && m_uploaded.generation != 0)
        {
            m_renderer->UnregisterTexture(m_uploaded);
        }
        m_uploaded = {};
        m_source = {};
        m_sourceGeneration = 0;
        for (auto& coefficient : m_sh)
        {
            coefficient[0] = 0.0f;
            coefficient[1] = 0.0f;
            coefficient[2] = 0.0f;
        }
    }

    void EnvironmentLibrary::Resolve(const CanvasEnvironment& environment, Environment3D& result)
    {
        result = Environment3D{};
        result.ambient = static_cast<EnvironmentAmbient>(static_cast<std::uint8_t>(environment.ambientSource));
        const Float ambient = environment.ambientIntensity < 0.0f ? Float(0.0f) : environment.ambientIntensity;
        const Color* colors[4] = {&environment.ambientColor, &environment.skyColor, &environment.equatorColor, &environment.groundColor};
        Float* targets[4] = {result.ambientColor, result.skyColor, result.equatorColor, result.groundColor};
        for (Int32 at = 0; at < 4; ++at)
        {
            targets[at.Get()][0] = colors[at.Get()]->R * ambient;
            targets[at.Get()][1] = colors[at.Get()]->G * ambient;
            targets[at.Get()][2] = colors[at.Get()]->B * ambient;
        }
        const Float sky = environment.skyIntensity < 0.0f ? Float(0.0f) : environment.skyIntensity;
        result.skyIntensity = sky;
        result.skyRotation = environment.skyRotation * (3.14159265f / 180.0f);
        result.reflectionIntensity = environment.reflectionIntensity < 0.0f ? Float(0.0f) : environment.reflectionIntensity;

        // 하늘 맵을 굽는다. 하늘이 바뀌었거나 같은 하늘이 다시 읽혔으면(자료 세대) 다시 굽는다. 굽지 못하면 하늘 없이 그린다.
        const EnvironmentMapData* data = m_assets != nullptr && environment.sky.generation != 0 ? m_assets->GetEnvironmentMap(environment.sky) : nullptr;        if (data == nullptr)
        {
            if (m_uploaded.generation != 0)
            {
                Release();
            }
            return;
        }
        const Bool same = m_uploaded.generation != 0 && m_source.index == environment.sky.index && m_source.generation == environment.sky.generation
            && m_sourceGeneration == data->dataGeneration;
        if (false == same && m_renderer != nullptr)
        {
            Release();
            EnvironmentMapBuild build;
            if (BuildEnvironmentMap(data->rgb.Data(), data->width, data->height, build))
            {
                m_uploaded = m_renderer->RegisterEnvironmentMap(build);
                ++m_buildCount;
                if (m_uploaded.generation != 0)
                {
                    m_source = environment.sky;
                    m_sourceGeneration = data->dataGeneration;
                    for (Int32 at = 0; at < 9; ++at)
                    {
                        for (Int32 channel = 0; channel < 3; ++channel)
                        {
                            m_sh[at.Get()][channel.Get()] = build.sh[at.Get()][channel.Get()];
                        }
                    }
                }
            }
        }
        if (m_uploaded.generation == 0)
        {
            return;
        }
        result.map = m_uploaded;        // 확산광은 하늘의 세기와 환경광 세기를 함께 받는다(배경과 반사는 하늘의 세기만).
        for (Int32 at = 0; at < 9; ++at)
        {
            for (Int32 channel = 0; channel < 3; ++channel)
            {
                result.sh[at.Get()][channel.Get()] = m_sh[at.Get()][channel.Get()] * sky * ambient;
            }
        }
    }
}
