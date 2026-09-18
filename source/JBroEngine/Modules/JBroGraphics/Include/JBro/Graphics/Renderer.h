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

    // 이번 프레임의 뷰를 어디에 그릴지다. 비워 두면 스왑체인 백버퍼다.
    //
    // **렌더러의 모드가 아니라 인자다.** 에디터는 게임 화면을 자기 패널 안에 붙여야 하므로
    // 같은 렌더러를 텍스처로 한 번 부르고, 게임 실행은 백버퍼로 부른다. 그 차이가 전부이고
    // 두 개의 렌더 경로가 되어서는 안 된다.
    struct FrameTarget
    {
        // 비어 있으면 백버퍼다.
        TextureHandle texture;
        // 텍스처를 줄 때는 그 크기도 줘야 한다. 뷰포트가 타깃 안에 있는지 재는 기준이
        // 그것이고, 백버퍼일 때와 달리 렌더러가 알 길이 없다.
        Extent2D extent;
        // 거짓이면 이 프레임의 뷰를 기록하지 않는다. 게임 뷰 렌더는 **매 프레임 opt-in**
        // 이다(D-63) - 에디터의 게임 뷰 패널이 그려지지 않는 프레임에는 텍스처를 건드리지
        // 않고, 다시 보일 때 마지막 그림에서 이어진다. 제출된 뷰는 `skippedViewCount` 로 센다.
        bool recordViews = true;
    };

    // 뷰를 모두 기록한 뒤, 프레임을 닫기 전에 불린다. 에디터 UI 가 백버퍼에
    // 그리는 자리다.
    //
    // **렌더러가 프레임과 커맨드 컨텍스트를 쥔 채로 불러들인다.** 그것들을 밖으로
    // 꺼내 주면 프레임을 누가 여닫는지가 둘로 갈린다. false 를 돌려주면 프레임을
    // 버린다 - UI 가 반쯤 그려진 화면을 내보내지 않는다.
    // `frameSlot` 은 이 프레임이 쓰는 슬롯이다. 매 프레임 덮어쓰는 자원을
    // 가진 쪽은 이것으로 갈라 써야 한다 - 그 슬롯의 지난 프레임이 끝났다는 것은
    // 이미 보장되어 있고, 하나로 두면 아직 읽는 중인 것을 덮어쓴다.
    using FrameOverlay = bool (*)(
        IRHICommandContext& commands,
        TextureHandle backBuffer,
        std::uint32_t frameSlot,
        void* user);

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
        float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // 렌더러가 GPU 에 올리는 메시 정점이다. 위치와 법선만 있다 - 재질이 생기면 UV 가 붙는다
    // (framework3d-plan §2.4). 셰이더 ABI 라 크기와 자리를 아래에서 단언한다.
    struct MeshVertex
    {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float normal[3] = {0.0f, 0.0f, 1.0f};
    };

    struct RendererFrameStats
    {
        std::uint32_t viewCount = 0;
        std::uint32_t spriteCount = 0;
        std::uint32_t meshCount = 0;
        std::uint32_t droppedViewCount = 0;
        // 타깃이 뷰 기록을 끈 프레임에 제출된 뷰다. 버린 것이 아니라 그리지 않기로 한 것이다.
        std::uint32_t skippedViewCount = 0;
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

        // 타깃을 비우면 백버퍼에 그린다.
        FrameStatus BeginFrame(const FrameTarget& target = {});
        bool BeginView(const CameraParams& camera);
        bool SubmitSprite(const SpriteSubmit& item);
        bool SubmitSprites(JArrayView<SpriteSubmit> items);
        bool SubmitMesh(const MeshSubmit& item);
        bool SubmitMeshes(JArrayView<MeshSubmit> items);

        // 메시 지오메트리를 GPU 에 올리고 `MeshSubmit::mesh` 에 넣을 핸들을 준다. 프레임 밖에서만
        // 부른다. 빈 배열·너무 큰 배열·프레임 안이면 빈 핸들이다.
        // **핸들 모양이 `AssetHandle` 인 것은 `MeshSubmit` 이 그 타입이기 때문이다.** 발급자가 렌더러라는
        // 것은 `MeshLibrary`(Framework3DSystem)만 안다 - `AssetSystem` 이 실제로 로드하게 되면 그쪽이
        // 발급한다(`[가정]`, framework3d-plan §2.3).
        AssetHandle RegisterMesh(JArrayView<MeshVertex> vertices, JArrayView<std::uint32_t> indices);
        void UnregisterMesh(AssetHandle mesh);
        std::uint32_t GetMeshCount() const;
        bool EndView();
        FrameStatus EndFrame();
        void AbortFrame();

        bool ResizeSurface(const Extent2D& extent);
        RendererFrameStats GetLastFrameStats() const;
        // 마지막으로 기록된 프레임의 첫 뷰 카메라다. 에디터의 기즈모가 화면과 월드를 잇는 데 쓴다(D-109) -
        // UI 는 엔진 프레임보다 먼저 만들어지므로 한 프레임 전의 카메라다. 뷰를 기록한 프레임이 아직 없으면 거짓이다.
        bool GetLastViewCamera(CameraParams& camera) const;

        // 프레임을 닫기 전에 부를 것을 건다. **프레임 밖에서만 바꾼다** -
        // 프레임 중간에 바뀌면 이미 기록한 것과 어긋난다. nullptr 이면 뗀다.
        bool SetFrameOverlay(FrameOverlay overlay, void* user);
        bool HasFrameOverlay() const;

        // 렌더러가 만든 디바이스다. **리소스를 만들고 지우는 데만 쓴다** -
        // 에디터가 게임 뷰 텍스처와 자기 UI 파이프라인을 만들려면 이것이 필요하다.
        // 프레임을 여닫는 것은 여전히 렌더러의 일이다.
        IRHIDevice* GetDevice() const;

        bool IsDeviceLost() const;
        bool IsInitialized() const;
        // 창(스왑체인)의 크기다. 프레임이 텍스처로 가는 동안에도 이것은 창이다.
        Extent2D GetSurfaceExtent() const;
        // **이번 프레임이 실제로 그려지는 크기다.** 타깃을 준 프레임은 그
        // 텍스처의 크기이고, 아니면 창 크기다.
        //
        // 카메라는 창이 아니라 이것으로 화면을 잡아야 한다. 창으로 잡으면
        // 에디터에서 게임 화면이 에디터 창 모양을 따라가고, 게임 뷰가 창보다
        // 작으면 뷰포트가 타깃 밖으로 나가 프레임이 통째로 거절된다.
        Extent2D GetFrameExtent() const;
        // 백버퍼 포맷이다. 백버퍼에 얹혀 그리는 파이프라인은 이것과 같아야
        // 만들어진다 - 렌더 타깃 포맷은 파이프라인을 만들 때 굳는다.
        TextureFormat GetBackBufferFormat() const;
        std::uint32_t GetSpriteSubmissionLimit() const;
        // 마지막으로 제시한 백버퍼를 CPU 로 읽는다. **진단과 테스트 경로다** —
        // GPU 를 기다리므로 프레임 안에서 부를 수 없고 매 프레임 경로도 아니다.
        bool ReadBackBuffer(std::byte* destination, std::size_t destinationSize, TextureReadback& result);

    private:
        struct ViewPacket
        {
            CameraParams camera;
            std::uint32_t spriteOffset = 0;
            std::uint32_t spriteCount = 0;
            std::uint32_t meshOffset = 0;
            std::uint32_t meshCount = 0;
            // 이 뷰의 메시 드로우 묶음(`m_meshRuns`)이 시작하는 자리와 개수. 업로드가 채운다.
            std::uint32_t runOffset = 0;
            std::uint32_t runCount = 0;
        };

        // 같은 메시를 그리는 인스턴스들의 연속 구간이다(D-110). 업로드가 뷰 안에서 메시별로 모아 놓으므로
        // 드로우 하나가 구간 하나다 - 메시마다 드로우를 내던 것에서 메시 **종류**마다 드로우를 내는 것으로.
        struct MeshRun
        {
            AssetHandle mesh;
            std::uint32_t firstInstance = 0;
            std::uint32_t instanceCount = 0;
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
        static_assert(offsetof(SpriteTransform2D, translation) == 16,
            "instance attribute 2 reads the translation and depth from offset 16");
        static_assert(offsetof(GpuSpriteInstance, tint) == 28,
            "instance attribute 3 reads the tint from offset 28");

        // 메시 인스턴스 하나. 월드 4x4(행 넷)와 tint. `BuiltinMesh.hlsl` 의 ATTRIBUTE2..6 이 이것을 읽는다.
        struct GpuMeshInstance
        {
            Matrix4x4 world;
            float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        };
        static_assert(sizeof(MeshVertex) == 24, "mesh vertex stride is part of the shader ABI");
        static_assert(offsetof(MeshVertex, normal) == 12, "attribute 1 reads the normal from offset 12");
        static_assert(sizeof(GpuMeshInstance) == 80, "mesh instance stride is part of the shader ABI");
        static_assert(offsetof(GpuMeshInstance, tint) == 64, "attribute 6 reads the tint from offset 64");

        // 올라간 메시 하나. 핸들의 index 가 이 배열의 자리고 generation 이 재사용을 가른다.
        struct MeshResource
        {
            BufferHandle vertexBuffer;
            BufferHandle indexBuffer;
            std::uint32_t indexCount = 0;
            std::uint32_t generation = 1;
            bool occupied = false;
        };

        // 깊이 텍스처 하나. 타깃 크기마다 하나씩 두고 크기가 바뀌면 다시 만든다.
        struct DepthTarget
        {
            TextureHandle texture;
            Extent2D extent;
        };

        static constexpr std::uint32_t InvalidViewIndex = 0xFFFFFFFFu;
        static constexpr std::uint32_t MaxFrameSlots = 3;

        bool CreateBuiltinSpriteResources();
        void DestroyBuiltinSpriteResources();
        bool UploadSpriteInstances();
        bool CreateBuiltinMeshResources();
        void DestroyBuiltinMeshResources();
        bool UploadMeshInstances();
        void DestroyMeshResources();
        // `extent` 크기의 깊이 텍스처를 준다. 백버퍼용과 프레임 타깃용을 따로 든다.
        bool AcquireDepthTarget(const Extent2D& extent, bool forTexture, TextureHandle& depth);
        void DestroyDepthTargets();
        const MeshResource* FindMesh(AssetHandle mesh) const;
        bool RecordViews();
        void ResetSubmissionStorage();

        RendererConfig m_config;
        IRHIModule* m_rhi = nullptr;
        IRHIDevice* m_device = nullptr;
        SwapchainHandle m_swapchain;
        FrameContext m_frame;
        FrameTarget m_frameTarget;
        FrameOverlay m_frameOverlay = nullptr;
        void* m_frameOverlayUser = nullptr;
        Array<ViewPacket> m_views;
        Array<SpriteSubmit> m_sprites;
        Array<MeshSubmit> m_meshes;
        // 인스턴스 배열은 초기화 때 상한 크기로 한 번 잡고 프레임마다 앞에서부터 채운다 - `Resize` 는 매 프레임
        // 값 초기화(memset)를 하고, 그 비용이 자료를 옮기는 것보다 컸다(D-110 리뷰).
        Array<GpuSpriteInstance> m_gpuSpriteInstances;
        std::size_t m_gpuSpriteCount = 0;
        std::size_t m_gpuMeshCount = 0;
        BufferHandle m_spriteVertexBuffer;
        BufferHandle m_spriteIndexBuffer;
        BufferHandle m_spriteInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_spritePipeline;
        // 깊이가 달린 패스(메시가 있는 뷰) 위에 스프라이트를 얹을 때 쓰는 쌍둥이다. 포맷만 같고 깊이는 보지도
        // 쓰지도 않는다 - 파이프라인의 깊이 포맷은 패스의 첨부와 같아야 하기 때문에 둘이 필요하다.
        GraphicsPipelineHandle m_spriteOverDepthPipeline;
        Array<MeshResource> m_meshResources;
        Array<GpuMeshInstance> m_gpuMeshInstances;
        Array<MeshRun> m_meshRuns;
        // 뷰마다 메시 슬롯별 개수를 세는 작업 배열. 크기는 등록된 메시 슬롯 수다.
        Array<std::uint32_t> m_meshHistogram;
        BufferHandle m_meshInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_meshPipeline;
        DepthTarget m_depthTargets[2];
        RendererFrameStats m_currentStats;
        RendererFrameStats m_lastStats;
        CameraParams m_lastViewCamera;
        bool m_hasLastViewCamera = false;
        // EndFrame 이 프레임 컨텍스트를 비우므로 읽기 경로를 위해 따로 기억한다.
        TextureHandle m_lastPresentedBackBuffer;
        std::uint32_t m_activeView = InvalidViewIndex;
        bool m_frameActive = false;
    };
}
