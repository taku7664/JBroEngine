#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/RHI/RHI.h>

namespace JBro
{
    struct Matrix4x4
    {
        float values[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
    };

    struct RendererConfig
    {
        GraphicsApi api = GraphicsApi::D3D12;
        SurfaceHandle surface;
        bool validation = false;
    };

    struct CameraParams
    {
        Matrix4x4 view;
        Matrix4x4 projection;
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct SpriteSubmit
    {
        Matrix4x4 world;
        AssetHandle sprite;
        AssetHandle material;
        float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        std::int32_t renderOrder = 0;
    };

    struct MeshSubmit
    {
        Matrix4x4 world;
        AssetHandle mesh;
        AssetHandle material;
    };

    class Renderer final
    {
    public:
        Renderer() = default;
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        bool Initialize(IRHIModule& rhi, const RendererConfig& config);
        void Shutdown();

        void BeginFrame();
        void SetCamera(const CameraParams& camera);
        void SubmitSprite(const SpriteSubmit& item);
        void SubmitMesh(const MeshSubmit& item);
        void EndFrame();

        bool IsDeviceLost() const;

    private:
        IRHIModule* m_rhi = nullptr;
        IRHIDevice* m_device = nullptr;
    };
}
