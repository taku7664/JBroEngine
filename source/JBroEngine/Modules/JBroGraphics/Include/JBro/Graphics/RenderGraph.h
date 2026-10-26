#pragma once

#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    // **렌더 그래프**(D-322 (1), D-339, postprocess-plan §2.2·§3.6). 패스마다 읽고 쓰는 텍스처를 선언하면 그래프가 실행할 패스·텍스처의 수명과
    // 공유·배리어를 낸다. `Modules/JBroGraphics` 안에서만 쓴다 - 게임 스크립트와 프레임워크는 그래프를 보지 않는다(D-33).
    //
    // 한 프레임의 쓰임은 다음과 같다.
    //   1. `Reset` 뒤에 텍스처를 들이고(`ImportTexture`·`ImportBackBuffer`) 만들고(`CreateTexture`), 패스를 **실행할 순서대로** 더한다.
    //      패스의 선언(첨부·읽기·복사·조건·자료)은 막 더한 패스에만 붙는다.
    //   2. `Compile` 이 풀에서 실제 텍스처를 고르고, 돌지 않을 패스를 가리고, 패스마다 배리어를 낸다.
    //   3. `Execute` 가 살아남은 패스를 차례로 기록한다.
    //
    // **실행 순서는 더한 순서다.** 패스는 앞에서 쓴 것만 읽을 수 있으므로 더한 순서가 곧 의존의 위상 순서다 - 그래프는 순서를 바꾸지 않고
    // 의존(읽기·`Load`)을 따라 결과에 닿지 않는 패스를 버린다. 들인 텍스처에 쓰는 패스가 결과다.
    //
    // **프레임 경로에 할당이 없다.** 저장소는 `Initialize` 가 상한만큼 한 번 잡고 `Reset` 이 비운다. 상한을 넘는 선언은 거절되고
    // 그 프레임의 `Compile` 이 실패한다.

    struct RenderGraphTexture
    {
        static constexpr UInt32 Invalid = 0xFFFFFFFFu;
        UInt32 index = Invalid;

        constexpr Bool IsValid() const noexcept
        {
            return index != Invalid;
        }
    };

    struct RenderGraphPass
    {
        static constexpr UInt32 Invalid = 0xFFFFFFFFu;
        UInt32 index = Invalid;

        constexpr Bool IsValid() const noexcept
        {
            return index != Invalid;
        }
    };

    // 그래프가 만드는 텍스처의 모양이다. 셋이 모두 같은 텍스처만 서로 나눠 쓴다.
    struct TransientTextureDesc
    {
        Extent2D extent;
        TextureFormat format = TextureFormat::Unknown;
        TextureUsage usage = TextureUsage::None;
    };

    enum class RenderGraphPassKind : std::uint8_t
    {
        // `Execute` 가 첨부로 렌더 패스를 열고 실행 함수를 부른 뒤 닫는다.
        Raster,
        // 렌더 패스 밖에서 실행 함수가 `CopyTexture` 를 부른다.
        Copy,
        // 렌더 패스 밖에서 실행 함수가 `Dispatch` 를 부른다.
        Compute
    };

    // 패스가 돌지 않은 까닭이다. `Compile` 뒤에 `GetPassSkip` 으로 읽는다.
    enum class RenderGraphSkip : std::uint8_t
    {
        None,
        // 꼭 있어야 하는 텍스처(첨부·읽기·`RunIfAvailable`)가 이 프레임에 없다. 그 모양은 다음 프레임을 열 때 생긴다.
        MissingTexture,
        // `RunIfMissing` 이 걸린 대신 그리기다. 텍스처가 있어 본래의 패스가 돈다.
        TexturePresent,
        // 꼭 읽어야 하는 텍스처를 앞에서 쓴 패스가 돌지 않았다.
        InputNotWritten,
        // 결과에 닿지 않는다 - 이 패스가 쓴 것을 아무도 읽지 않는다.
        Unused
    };

    // 그래프 텍스처가 어디서 왔나다(살펴보기).
    enum class RenderGraphTextureKind : std::uint8_t
    {
        // 그래프가 만들어 풀에서 고른 것이다.
        Transient,
        // 밖에서 들인 것이다(뷰의 타깃·외곽선 마스크 같은 에디터 텍스처).
        Imported,
        BackBuffer
    };

    // 패스가 텍스처를 어떻게 쓰나다.
    enum class RenderGraphAccess : std::uint8_t
    {
        Color,
        Depth,
        Read,
        CopySource,
        CopyDestination,
        Storage
    };

    // 패스의 접근 하나다(살펴보기).
    struct RenderGraphAccessInfo
    {
        RenderGraphTexture texture;
        RenderGraphAccess access = RenderGraphAccess::Read;
        // 색·깊이 첨부에서만 뜻이 있다.
        LoadOperation load = LoadOperation::Load;
        Bool optional = false;
    };

    class RenderGraph;

    // 실행 함수가 받는 것이다. 그래프 텍스처를 실제 텍스처로 푼다.
    class RenderGraphContext
    {
    public:
        IRHICommandContext& GetCommands() const noexcept
        {
            return *m_commands;
        }
        // 실제 텍스처다. 이 프레임에 없는 텍스처면 빈 핸들이다 - 선택 깊이 첨부(`SetDepthAttachment` 의 `optional`)가 달렸는지 이것으로 본다.
        TextureHandle GetTexture(RenderGraphTexture texture) const noexcept;
        // 읽을 수 있는가 - 들인 텍스처이거나, 앞에서 돈 패스가 쓴 텍스처다. 선택 읽기(`Read` 의 `optional`)는 이것으로 고른다.
        Bool HasTexture(RenderGraphTexture texture) const noexcept;

    private:
        friend class RenderGraph;
        const RenderGraph* m_graph = nullptr;
        IRHICommandContext* m_commands = nullptr;
    };

    // 패스를 기록하는 함수다. `data` 는 `SetPassData` 로 붙인 자료다. 거짓이면 프레임을 버린다.
    // 함수 포인터 + 자료 포인터다 - `std::function` 은 프레임 경로에 할당을 들인다(D-322 (1)).
    using RenderGraphExecute = Bool (*)(RenderGraphContext& context, const void* data);

    // **그래프가 만드는 텍스처를 담는 풀**이다. 장치는 프레임 안에서 자원을 만들지 않으므로, `Compile` 이 이번 프레임에 없던 모양을 바라고
    // 다음 프레임을 열기 전의 `Prepare` 가 만든다. 오래 쓰지 않은 것은 놓는다. 메인 스레드 전용이다.
    class RenderTargetPool
    {
    public:
        static constexpr UInt32 Capacity = 12;
        static constexpr UInt32 IdleFrames = 300;

        RenderTargetPool() = default;
        RenderTargetPool(const RenderTargetPool&) = delete;
        RenderTargetPool& operator=(const RenderTargetPool&) = delete;
        ~RenderTargetPool();

        void Initialize(IRHIDevice& device);
        void Shutdown();
        // 프레임을 열기 전에 부른다. 오래 쓰지 않은 것을 놓고, 지난 `Compile` 이 바란 모양을 만든다.
        void Prepare();
        // 이 모양이 지금 하나는 있게 한다. 프레임 밖에서 부른다. 만들지 못하면 거짓이다.
        Bool Ensure(const TransientTextureDesc& desc);
        UInt32 GetTextureCount() const noexcept;
        // 이 모양으로 바라는 수다(다음 `Prepare` 가 더 만들 수). 시험이 본다.
        UInt32 GetWantedCount(const TransientTextureDesc& desc) const noexcept;
        // ── 살펴보기(렌더 그래프 창) ──
        // 칸 하나다. 빈 칸이면 거짓이다.
        Bool GetSlot(UInt32 index, TransientTextureDesc& desc, TextureHandle& texture, UInt32& idleFrames) const noexcept;
        UInt32 GetWantCount() const noexcept;
        Bool GetWant(UInt32 index, TransientTextureDesc& desc, UInt32& count) const noexcept;

    private:
        friend class RenderGraph;
        struct Slot
        {
            TextureHandle texture;
            TransientTextureDesc desc;
            UInt32 idleFrames = 0;
        };
        struct Want
        {
            TransientTextureDesc desc;
            UInt32 count = 0;
        };
        Bool CreateInSlot(const TransientTextureDesc& desc);
        void AddWant(const TransientTextureDesc& desc, UInt32 count);

        IRHIDevice* m_device = nullptr;
        Slot m_slots[Capacity];
        Want m_wants[Capacity];
        UInt32 m_wantCount = 0;
    };

    // 저장소의 상한이다. `Initialize` 가 이만큼 한 번 잡는다.
    struct RenderGraphLimits
    {
        UInt32 maxPasses = 0;
        UInt32 maxTextures = 0;
        // 모든 패스의 첨부·읽기·복사를 합한 수다. 패스마다의 배리어도 이 안에 든다.
        UInt32 maxAccesses = 0;
        // 패스 자료(`SetPassData`)를 담는 바이트다.
        std::size_t dataBytes = 0;
    };

    class RenderGraph
    {
    public:
        static constexpr UInt32 MaxColorAttachments = 4;

        RenderGraph() = default;
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        Bool Initialize(const RenderGraphLimits& limits);
        void Shutdown();
        // 프레임마다 처음에 부른다. 저장소의 크기는 그대로 두고 비운다.
        void Reset();

        // **밖에서 가져온 텍스처**다. 같은 핸들은 같은 그래프 텍스처다. 여기에 쓰는 패스가 그래프의 결과이고, 상태는 RHI 가 맞춘다
        // (그래프는 들인 텍스처에 배리어를 걸지 않는다 - 쓰임을 모른다).
        // 이름은 진단용이고 정적 문자열이어야 한다(복사하지 않는다). 같은 핸들을 다시 들이면 처음 이름이 남는다.
        RenderGraphTexture ImportTexture(TextureHandle texture, const char* name = nullptr);
        // 스왑체인의 백버퍼다. 들인 텍스처와 같고, 배리어를 받지 않는다는 것을 이름으로 남긴다.
        RenderGraphTexture ImportBackBuffer(TextureHandle backBuffer, const char* name = nullptr);
        // **그래프가 만드는 텍스처**다. 실제 텍스처는 `Compile` 이 풀에서 고르고, 수명이 겹치지 않는 같은 모양끼리 하나를 나눠 쓴다.
        RenderGraphTexture CreateTexture(const TransientTextureDesc& desc, const char* name = nullptr);

        // 패스를 더한다. 이름은 진단용이고 정적 문자열이어야 한다(복사하지 않는다). `group` 은 부르는 쪽이 정하는 묶음 번호다 -
        // 렌더러는 뷰 번호를 넣고, 렌더 그래프 창이 그것으로 패스를 묶는다.
        RenderGraphPass AddPass(RenderGraphPassKind kind, const char* name, RenderGraphExecute execute, UInt32 group = 0);
        // 래스터 패스의 색 첨부다. `Load` 는 앞의 내용을 읽는 것으로 친다(그 앞의 쓰기가 필요하다).
        Bool SetColorAttachment(RenderGraphPass pass, RenderGraphTexture texture, LoadOperation load, const ClearColor& clear = {});
        // 래스터 패스의 깊이 첨부다. `optional` 이면 그 텍스처가 이 프레임에 없어도 패스는 깊이 없이 돈다.
        Bool SetDepthAttachment(RenderGraphPass pass, RenderGraphTexture texture, LoadOperation load, StoreOperation store,
            Bool optional);
        // 셰이더가 읽는다. `optional` 이면 없거나 쓰이지 않았어도 패스는 돌고, 실행 함수가 `HasTexture` 로 고른다.
        Bool Read(RenderGraphPass pass, RenderGraphTexture texture, Bool optional = false);
        // 복사 패스의 원본과 대상이다.
        Bool ReadCopySource(RenderGraphPass pass, RenderGraphTexture texture);
        Bool WriteCopyDestination(RenderGraphPass pass, RenderGraphTexture texture);
        // 컴퓨트 패스가 읽고 쓴다.
        Bool ReadWriteStorage(RenderGraphPass pass, RenderGraphTexture texture);
        // 이 텍스처가 이 프레임에 있을 때만 돈다(접근하지 않는 텍스처에 거는 조건이다).
        Bool RunIfAvailable(RenderGraphPass pass, RenderGraphTexture texture);
        // 이 텍스처가 이 프레임에 **없을 때만** 돈다 - 본래 패스의 대신 그리기다.
        Bool RunIfMissing(RenderGraphPass pass, RenderGraphTexture texture);
        // 실행 함수가 받을 자료를 그래프의 저장소에 복사한다.
        template <typename T>
        Bool SetPassData(RenderGraphPass pass, const T& data)
        {
            static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                "pass data is copied into the frame arena and never destroyed");
            return CopyPassData(pass, &data, sizeof(T), alignof(T));
        }

        // 실제 텍스처를 고르고, 돌지 않을 패스를 가리고, 배리어를 낸다. 없던 모양은 풀에 바람으로 남긴다.
        // 선언이 상한을 넘었던 프레임이면 거짓이다.
        Bool Compile(RenderTargetPool& pool);
        // 살아남은 패스를 기록한다. 실행 함수나 RHI 가 거절하면 그 자리에서 거짓이다.
        Bool Execute(IRHICommandContext& commands);

        // ── 살펴보기(시험·진단) ──
        UInt32 GetPassCount() const noexcept;
        UInt32 GetTextureCount() const noexcept;
        const char* GetPassName(RenderGraphPass pass) const noexcept;
        RenderGraphSkip GetPassSkip(RenderGraphPass pass) const noexcept;
        // `Compile` 이 고른 실제 텍스처다. 없으면 빈 핸들이다.
        TextureHandle GetCompiledTexture(RenderGraphTexture texture) const noexcept;
        // 그 패스 앞에서 걸 배리어다.
        JArrayView<TextureBarrier> GetPassBarriers(RenderGraphPass pass) const noexcept;
        // 이 프레임을 컴파일했는가. 짓다가 멈춘 프레임은 거짓이다.
        Bool IsCompiled() const noexcept;
        RenderGraphPassKind GetPassKind(RenderGraphPass pass) const noexcept;
        UInt32 GetPassGroup(RenderGraphPass pass) const noexcept;
        UInt32 GetPassAccessCount(RenderGraphPass pass) const noexcept;
        RenderGraphAccessInfo GetPassAccess(RenderGraphPass pass, UInt32 index) const noexcept;
        // `RunIfAvailable`·`RunIfMissing` 의 텍스처다. 조건이 없으면 무효다.
        RenderGraphTexture GetPassCondition(RenderGraphPass pass, Bool& runIfMissing) const noexcept;
        const char* GetTextureName(RenderGraphTexture texture) const noexcept;
        RenderGraphTextureKind GetTextureKind(RenderGraphTexture texture) const noexcept;
        // 그래프가 만든 텍스처의 모양이다. 들인 텍스처는 모양을 모른다(빈 모양).
        TransientTextureDesc GetTextureDesc(RenderGraphTexture texture) const noexcept;
        // 그 텍스처를 쓰는 첫·끝 패스다(조건 포함). 아무도 쓰지 않으면 거짓이다.
        Bool GetTextureLifetime(RenderGraphTexture texture, UInt32& firstPass, UInt32& lastPass) const noexcept;

    private:
        friend class RenderGraphContext;

        using TextureKind = RenderGraphTextureKind;
        using AccessRole = RenderGraphAccess;
        struct TextureEntry
        {
            TransientTextureDesc desc;
            TextureHandle texture;
            const char* name = nullptr;
            TextureKind kind = TextureKind::Transient;
            UInt32 firstPass = RenderGraphPass::Invalid;
            UInt32 lastPass = RenderGraphPass::Invalid;
            // 이 프레임에 실제 텍스처가 있는가.
            Bool available = false;
            // 할당이 이미 이 텍스처를 보았는가.
            Bool allocated = false;
            // 컴파일·실행 중에 앞의 패스가 썼는가.
            Bool written = false;
            // 뒤의 쓰인 패스가 읽는가(쓸모없는 패스 가리기).
            Bool readLater = false;
        };
        struct AccessEntry
        {
            ClearColor clear;
            UInt32 texture = RenderGraphTexture::Invalid;
            AccessRole role = AccessRole::Read;
            LoadOperation load = LoadOperation::Load;
            StoreOperation store = StoreOperation::Store;
            Bool optional = false;
        };
        struct PassEntry
        {
            const char* name = nullptr;
            RenderGraphExecute execute = nullptr;
            const void* data = nullptr;
            UInt32 firstAccess = 0;
            UInt32 accessCount = 0;
            UInt32 firstBarrier = 0;
            UInt32 barrierCount = 0;
            UInt32 condition = RenderGraphTexture::Invalid;
            UInt32 group = 0;
            RenderGraphPassKind kind = RenderGraphPassKind::Raster;
            Bool runIfMissing = false;
            RenderGraphSkip skip = RenderGraphSkip::None;
        };

        Bool AddAccess(RenderGraphPass pass, const AccessEntry& access);
        Bool IsLastPass(RenderGraphPass pass) const noexcept;
        Bool CopyPassData(RenderGraphPass pass, const void* data, std::size_t size, std::size_t alignment);
        RenderGraphTexture Import(TextureHandle texture, TextureKind kind, const char* name);
        void Allocate(RenderTargetPool& pool);
        void CullPasses();
        void BuildBarriers();
        static Bool Reads(const AccessEntry& access) noexcept;
        static Bool Writes(const AccessEntry& access) noexcept;

        Array<TextureEntry> m_textures;
        Array<PassEntry> m_passes;
        Array<AccessEntry> m_accesses;
        Array<TextureBarrier> m_barriers;
        Array<std::byte> m_data;
        std::size_t m_dataUsed = 0;
        UInt32 m_maxPasses = 0;
        UInt32 m_maxTextures = 0;
        UInt32 m_maxAccesses = 0;
        // 상한을 넘은 선언이 있었다. 그 프레임의 `Compile` 은 실패한다.
        Bool m_overflow = false;
        Bool m_compiled = false;
    };
}
