#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>

#include <cstddef>

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

    // 2D 스프라이트의 월드 변환이다. 열 벡터 규약의 2x3 아핀 여섯 값과 깊이 하나를 담는다(D-54).
    //   x' = linear[0]*x + linear[1]*y + translation[0]
    //   y' = linear[2]*x + linear[3]*y + translation[1]
    // 4x4 로 넘길 때 사라지던 것은 항상 같던 z 행과 w 행뿐이다.
    struct SpriteTransform2D
    {
        float linear[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        float translation[2] = {0.0f, 0.0f};
        float depth = 0.0f;
    };

    // 정렬과 레이어 합성은 프레임워크가 제출 전에 끝낸다.
    // 렌더러는 받은 순서대로 그린다(D-53).
    struct SpriteSubmit
    {
        SpriteTransform2D world;
        AssetHandle sprite;
        AssetHandle material;
        float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
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
        ~Renderer();
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
        void AbortFrame();

        bool ResizeSurface(const Extent2D& extent);
        RendererFrameStats GetLastFrameStats() const;

        bool IsDeviceLost() const;
        bool IsInitialized() const;
        Extent2D GetSurfaceExtent() const;
        std::uint32_t GetSpriteSubmissionLimit() const;

    private:
        struct ViewPacket
        {
            CameraParams camera;
            std::uint32_t spriteOffset = 0;
            std::uint32_t spriteCount = 0;
            std::uint32_t meshOffset = 0;
            std::uint32_t meshCount = 0;
        };

        // 이 멤버 순서가 정점 속성 오프셋이고 BuiltinSprite.hlsl 의 ABI 다.
        // 크기나 순서를 바꾸면 셰이더도 함께 다시 만든다(Shaders/Compile.ps1).
        struct GpuSpriteInstance
        {
            SpriteTransform2D world;
            float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        };

        static_assert(sizeof(SpriteTransform2D) == 28,
            "sprite transform layout is part of the shader ABI");
        static_assert(sizeof(GpuSpriteInstance) == 44,
            "sprite instance stride is part of the shader ABI");
        static_assert(offsetof(GpuSpriteInstance, world) == 0,
            "instance attribute 1 and 2 read the transform from offset 0");
        static_assert(offsetof(GpuSpriteInstance, tint) == 28,
            "instance attribute 3 reads the tint from offset 28");

        static constexpr std::uint32_t InvalidViewIndex = 0xFFFFFFFFu;
        static constexpr std::uint32_t MaxFrameSlots = 3;

        bool CreateBuiltinSpriteResources();
        void DestroyBuiltinSpriteResources();
        bool UploadSpriteInstances();
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
        Array<GpuSpriteInstance> m_gpuSpriteInstances;
        BufferHandle m_spriteVertexBuffer;
        BufferHandle m_spriteIndexBuffer;
        BufferHandle m_spriteInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_spritePipeline;
        RendererFrameStats m_currentStats;
        RendererFrameStats m_lastStats;
        std::uint32_t m_activeView = InvalidViewIndex;
        bool m_frameActive = false;
    };
}
