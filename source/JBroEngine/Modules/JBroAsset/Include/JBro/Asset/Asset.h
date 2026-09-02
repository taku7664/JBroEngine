#pragma once

#include <JBro/Core/Core.h>

namespace JBro::Engine
{
    struct AssetId
    {
        std::uint64_t value = 0;
    };

    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    struct AssetMetadata
    {
        AssetId id;
        JStringView type;
        JStringView sourcePath;
    };

    class AssetRegistry
    {
    public:
        void Register(const AssetMetadata& metadata);
        void Unregister(AssetId id);
        const AssetMetadata* Find(AssetId id) const;
    };

    class AssetManager final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

        AssetHandle Load(AssetId id);
        void Unload(AssetHandle handle);
        bool IsLoaded(AssetHandle handle) const;
    };
}
