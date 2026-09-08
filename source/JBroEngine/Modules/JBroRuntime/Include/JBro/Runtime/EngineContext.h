#pragma once

namespace JBro
{
    class AssetManager;
    class IPlatform;
    class IRHIModule;
    class Renderer;

    struct EngineContext
    {
        IPlatform* Platform = nullptr;
        IRHIModule* RHI = nullptr;
        JBro::Renderer* Renderer = nullptr;
        AssetManager* Assets = nullptr;
    };
}
