#pragma once

#include <JBro/RHI/RHI.h>

#include "VulkanLoader.h"

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Internal
{
    constexpr UInt32 MaxColorAttachments = 8;
    constexpr UInt32 MaxBoundTextures = 8;
    constexpr UInt32 MaxBoundSamplers = 4;
    constexpr UInt32 MaxVertexSlots = 8;
    // 셰이더의 t#·s# 가 SPIR-V 에서 앉는 자리다(`-fvk-t-shift 8 0 -fvk-s-shift 16 0`). 0..7 은 비워 둔다.
    constexpr UInt32 TextureBindingBase = 8;
    constexpr UInt32 SamplerBindingBase = 16;
    // 프레임 슬롯 수의 상한이다. 스왑체인이 `maxFramesInFlight` 로 이 안에서 고른다.
    constexpr UInt32 MaxFramesInFlight = 3;
    constexpr UInt32 MaxSwapchainImages = 8;

    class VulkanDevice;
    struct VulkanPipelineState;
    struct VulkanTextureState;

    // 이번 프레임의 명령 버퍼를 `IRHICommandContext` 로 감싼다. 배리어는 렌더 패스 밖에서만 넣을 수 있으므로
    // 첨부의 레이아웃 전이는 `BeginRenderPass` 앞과 `EndRenderPass` 뒤에 몰아 넣는다.
    class VulkanCommandContext final : public IRHICommandContext
    {
    public:
        void Bind(VulkanDevice* device);
        // 프레임마다 부른다. 슬롯의 명령 버퍼와 디스크립터 풀을 받는다.
        void BeginFrame(VkCommandBuffer commands, VkDescriptorPool descriptorPool);
        void Reset();

        Bool BeginRenderPass(const RenderPassDesc& desc) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& viewport) override;
        void SetScissor(const ScissorRect& scissor) override;
        Bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        Bool SetVertexBuffer(
            UInt32 slot,
            BufferHandle buffer,
            UInt32 stride,
            std::size_t offset) override;
        Bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) override;
        Bool SetGraphicsConstants(JArrayView<std::byte> data) override;
        Bool SetTexture(UInt32 slot, TextureHandle texture) override;
        Bool SetSampler(UInt32 slot, SamplerHandle sampler) override;
        Bool DrawIndexedInstanced(
            UInt32 indexCount,
            UInt32 instanceCount,
            UInt32 firstIndex,
            Int32 baseVertex,
            UInt32 firstInstance) override;
        Bool CopyTexture(TextureHandle source, TextureHandle destination) override;

        Bool IsRenderPassActive() const
        {
            return m_renderPassActive;
        }

    private:
        Bool BindPendingDescriptors();

        VulkanDevice* m_device = nullptr;
        VkCommandBuffer m_commands = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VulkanPipelineState* m_activePipeline = nullptr;
        // 패스가 끝나면 셰이더가 읽을 수 있게 돌려 놓을 텍스처들이다(Sampled 로 만든 색 첨부).
        TextureHandle m_sampledAtEnd[MaxColorAttachments];
        UInt32 m_sampledAtEndCount = 0;
        VkImageView m_pendingTextures[MaxBoundTextures] = {};
        VkSampler m_pendingSamplers[MaxBoundSamplers] = {};
        // 이 프레임에 쓴 set 들이다. 같은 묶음(레이아웃·텍스처·샘플러)이 다시 오면 할당하지 않고 그 set 을 다시
        // 건다(D-110). 프레임마다 풀이 비워지므로 함께 비운다.
        static constexpr UInt32 CachedSets = 16;
        struct CachedSet
        {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VkImageView views[MaxBoundTextures] = {};
            VkSampler samplers[MaxBoundSamplers] = {};
            VkDescriptorSet set = VK_NULL_HANDLE;
        };
        CachedSet m_cachedSets[CachedSets] = {};
        UInt32 m_cachedSetCount = 0;
        UInt32 m_cachedSetCursor = 0;
        Bool FindCachedSet(VkDescriptorSet& set) const;
        void RememberSet(VkDescriptorSet set);
        Bool m_descriptorsDirty = false;
        Bool m_renderPassActive = false;
        Bool m_pipelineActive = false;
    };

    struct VulkanSwapchainState
    {
        SwapchainDesc desc;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        // 스왑체인 이미지의 실제 크기다. 계약의 크기(`desc.extent`)보다 클 수 있다 - 그 안의 왼쪽 위만 쓴다.
        VkExtent2D imageExtent = {};
        VkImage images[MaxSwapchainImages] = {};
        VkImageView views[MaxSwapchainImages] = {};
        VkImageLayout layouts[MaxSwapchainImages] = {};
        // 이미지마다 하나다. 제시가 어느 이미지를 기다릴지 이미지 번호로 정해지므로 슬롯이 아니라 이미지에 붙인다.
        VkSemaphore renderFinished[MaxSwapchainImages] = {};
        UInt32 imageCount = 0;
        UInt32 currentImage = 0;
        // 제시 직전의 백버퍼 사본이다. 제시한 이미지는 되읽을 수 없으므로(D3D11 과 같은 사정, D-107)
        // 되읽기 계약은 이 사본으로 지킨다.
        VkImage presentedCopy = VK_NULL_HANDLE;
        VkDeviceMemory presentedCopyMemory = VK_NULL_HANDLE;
        VkImageLayout presentedCopyLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        UInt32 generation = 1;
        UInt32 backBufferGeneration = 1;
        Bool occupied = false;
    };

    struct VulkanBufferState
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        BufferDesc desc;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct VulkanTextureState
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkFormat format = VK_FORMAT_UNDEFINED;
        TextureDesc desc;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct VulkanSamplerState
    {
        VkSampler sampler = VK_NULL_HANDLE;
        SamplerDesc desc;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    struct VulkanPipelineState
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        // 텍스처나 샘플러를 하나라도 읽는 파이프라인만 갖는다. 없으면 set 을 묶지 않는다.
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkShaderStageFlags pushConstantStages = 0;
        UInt32 pushConstantBytes = 0;
        UInt32 sampledTextureCount = 0;
        UInt32 samplerCount = 0;
        UInt32 generation = 1;
        Bool occupied = false;
    };

    // 프레임마다 도는 것들이다. 슬롯의 펜스가 끝나야 그 슬롯의 명령 버퍼와 풀을 다시 쓴다.
    struct VulkanFrameSlot
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        UInt64 serial = 0;
    };

    // 파기가 미뤄진 객체다. 아직 GPU 에 떠 있을 수 있는 프레임이 쓰던 것은 그 프레임의 펜스가 끝난 뒤 지운다.
    struct VulkanRetiredObject
    {
        enum class Kind : std::uint8_t
        {
            Buffer,
            Image,
            ImageView,
            Sampler,
            Pipeline,
            PipelineLayout,
            DescriptorSetLayout,
            Memory
        };
        Kind kind = Kind::Buffer;
        UInt64 handle = 0;
        UInt64 serial = 0;
    };

    class VulkanDevice final : public IRHIDevice
    {
    public:
        VulkanDevice() = default;
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        Bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        Bool WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        Bool WriteTexture(TextureHandle texture, UInt32 mipLevel, JArrayView<std::byte> data) override;
        Bool WriteTextureRegion(TextureHandle texture, UInt32 mipLevel, UInt32 x, UInt32 y,
            UInt32 width, UInt32 height, JArrayView<std::byte> data, UInt32 rowPitch) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        Bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;
        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;
        Bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        UInt32 GetFramesInFlight() const override;
        UInt32 GetValidationErrorCount() const override;

        // 컨텍스트가 쓰는 해석 함수들. 백버퍼 핸들은 스왑체인의 이번 이미지로 풀린다.
        struct AttachmentView
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkImageLayout* layout = nullptr;
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            VkExtent2D extent = {};
            Bool sampled = false;
        };
        Bool ResolveAttachment(TextureHandle texture, AttachmentView& view);
        Bool ResolveSampledTexture(TextureHandle texture, VkImageView& view);
        Bool ResolveBuffer(BufferHandle buffer, VkBuffer& native, BufferDesc& desc);
        Bool ResolveSampler(SamplerHandle sampler, VkSampler& native);
        VulkanPipelineState* ResolvePipeline(GraphicsPipelineHandle pipeline);
        void CountValidationError();
        VkDevice GetNativeDevice() const
        {
            return m_device;
        }

        // 두 소스 파일이 함께 쓰는 레이아웃 전이. 스테이지·접근은 레이아웃에서 보수적으로 고른다.
        static void TransitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
            VkImageLayout from, VkImageLayout to);

    private:
        static constexpr UInt32 MaxSwapchains = 8;
        static constexpr UInt32 BackBufferTextureBase = 1;
        static constexpr UInt32 MaxBuffers = 1024;
        static constexpr UInt32 MaxTextures = 512;
        static constexpr UInt32 MaxGraphicsPipelines = 256;
        static constexpr UInt32 MaxSamplers = 64;
        static constexpr UInt32 MaxRetired = 4096;
        static constexpr UInt32 TextureResourceBase = BackBufferTextureBase + MaxSwapchains;

        Bool CreateInstance(Bool validation);
        Bool PickPhysicalDevice();
        Bool CreateLogicalDevice();
        Bool CreateFrameSlots();
        void DestroyFrameSlots();
        VulkanSwapchainState* FindSwapchain(SwapchainHandle swapchain);
        Bool BuildSwapchain(VulkanSwapchainState& state, VkSwapchainKHR old);
        void ReleaseSwapchainImages(VulkanSwapchainState& state);
        Bool AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
            VkMemoryPropertyFlags fallback, VkDeviceMemory& memory, Bool& hostVisible);
        Bool CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory);
        Bool CreateStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped);
        // 프레임 밖의 한 번짜리 명령이다. 끝날 때까지 기다린다(로드·되읽기 경로).
        Bool BeginOneShot();
        Bool EndOneShot();
        void Retire(VulkanRetiredObject::Kind kind, UInt64 handle);
        void FlushRetired(UInt64 completedSerial);
        void DestroyRetired(const VulkanRetiredObject& object);
        Bool ResolveReadableImage(TextureHandle texture, VkImage& image, VkImageLayout*& layout, TextureDesc& desc);
        FrameStatus SubmitAndPresent(Bool present);
        void MarkDeviceLost();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties m_memoryProperties = {};
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_queue = VK_NULL_HANDLE;
        UInt32 m_queueFamily = 0;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_oneShotCommands = VK_NULL_HANDLE;
        VkFence m_oneShotFence = VK_NULL_HANDLE;
        VulkanCommandContext m_commandContext;
        VulkanFrameSlot m_slots[MaxFramesInFlight];
        VulkanSwapchainState m_swapchains[MaxSwapchains];
        VulkanBufferState m_buffers[MaxBuffers];
        VulkanTextureState m_textures[MaxTextures];
        VulkanPipelineState m_graphicsPipelines[MaxGraphicsPipelines];
        VulkanSamplerState m_samplers[MaxSamplers];
        VulkanRetiredObject m_retired[MaxRetired];
        UInt32 m_retiredCount = 0;
        UInt64 m_frameSerial = 0;
        UInt64 m_completedSerial = 0;
        UInt32 m_slotCount = 1;
        UInt32 m_slot = 0;
        UInt32 m_activeSwapchainIndex = 0;
        UInt32 m_validationErrors = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        Bool m_frameActive = false;
        Bool m_oneShotActive = false;
    };

    // 소스 파일들이 함께 쓰는 변환.
    VkFormat ToVulkanFormat(TextureFormat format);
    UInt32 VulkanPixelSize(TextureFormat format);
    UInt32 VulkanNextGeneration(UInt32 generation);
}
