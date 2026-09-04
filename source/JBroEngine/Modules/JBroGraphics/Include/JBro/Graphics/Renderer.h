#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>

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
        Extent2D surfaceExtent{1280, 720};
        TextureFormat backBufferFormat = TextureFormat::BGRA8Unorm;
        PresentMode presentMode = PresentMode::VSync;
        std::uint8_t backBufferCount = 3;
        std::uint8_t maxFramesInFlight = 2;
        std::uint32_t maxViews = 8;
        std::uint32_t maxSpriteSubmissions = 65536;
        std::uint32_t maxMeshSubmissions = 16384;
        bool validation = false;
    };

    struct CameraParams
    {
        Matrix4x4 view;
        Matrix4x4 projection;
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        Viewport viewport;
        AssetHandle postProcessProfile;
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

    struct RendererFrameStats
    {
        std::uint32_t viewCount = 0;
        std::uint32_t spriteCount = 0;
        std::uint32_t meshCount = 0;
        std::uint32_t droppedViewCount = 0;
        std::uint32_t droppedSpriteCount = 0;
        std::uint32_t droppedMeshCount = 0;
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

        FrameStatus BeginFrame();
        bool BeginView(const CameraParams& camera);
        bool SubmitSprite(const SpriteSubmit& item);
        bool SubmitSprites(JArrayView<SpriteSubmit> items);
        bool SubmitMesh(const MeshSubmit& item);
        bool SubmitMeshes(JArrayView<MeshSubmit> items);
        bool EndView();
        FrameStatus EndFrame();

        bool ResizeSurface(const Extent2D& extent);
        RendererFrameStats GetLastFrameStats() const;

        bool IsDeviceLost() const;

    private:
        struct ViewPacket
        {
            CameraParams camera;
            std::uint32_t spriteOffset = 0;
            std::uint32_t spriteCount = 0;
            std::uint32_t meshOffset = 0;
            std::uint32_t meshCount = 0;
        };

        static constexpr std::uint32_t InvalidViewIndex = 0xFFFFFFFFu;

        bool RecordViews();
        void ResetSubmissionStorage();

        RendererConfig m_config;
        IRHIModule* m_rhi = nullptr;
        IRHIDevice* m_device = nullptr;
        SwapchainHandle m_swapchain;
        FrameContext m_frame;
        Array<ViewPacket> m_views;
        Array<SpriteSubmit> m_sprites;
        Array<MeshSubmit> m_meshes;
        RendererFrameStats m_currentStats;
        RendererFrameStats m_lastStats;
        std::uint32_t m_activeView = InvalidViewIndex;
        bool m_frameActive = false;
    };
}
