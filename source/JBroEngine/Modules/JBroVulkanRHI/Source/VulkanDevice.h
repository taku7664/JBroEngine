#pragma once

#include <JBro/RHI/RHI.h>

#include "VulkanLoader.h"

#include <cstdint>

namespace JBro::Internal
{
    constexpr std::uint32_t MaxColorAttachments = 8;
    constexpr std::uint32_t MaxBoundTextures = 8;
    constexpr std::uint32_t MaxBoundSamplers = 4;
    constexpr std::uint32_t MaxVertexSlots = 8;
    // 셰이더의 t#·s# 가 SPIR-V 에서 앉는 자리다(`-fvk-t-shift 8 0 -fvk-s-shift 16 0`). 0..7 은 비워 둔다.
    constexpr std::uint32_t TextureBindingBase = 8;
    constexpr std::uint32_t SamplerBindingBase = 16;
    // 프레임 슬롯 수의 상한이다. 스왑체인이 `maxFramesInFlight` 로 이 안에서 고른다.
    constexpr std::uint32_t MaxFramesInFlight = 3;
    constexpr std::uint32_t MaxSwapchainImages = 8;

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

        bool BeginRenderPass(const RenderPassDesc& desc) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& viewport) override;
        void SetScissor(const ScissorRect& scissor) override;
        bool SetGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        bool SetVertexBuffer(
            std::uint32_t slot,
            BufferHandle buffer,
            std::uint32_t stride,
            std::size_t offset) override;
        bool SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset) override;
        bool SetGraphicsConstants(JArrayView<std::byte> data) override;
        bool SetTexture(std::uint32_t slot, TextureHandle texture) override;
        bool SetSampler(std::uint32_t slot, SamplerHandle sampler) override;
        bool DrawIndexedInstanced(
            std::uint32_t indexCount,
            std::uint32_t instanceCount,
            std::uint32_t firstIndex,
            std::int32_t baseVertex,
            std::uint32_t firstInstance) override;

        bool IsRenderPassActive() const
        {
            return m_renderPassActive;
        }

    private:
        bool BindPendingDescriptors();

        VulkanDevice* m_device = nullptr;
        VkCommandBuffer m_commands = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VulkanPipelineState* m_activePipeline = nullptr;
        // 패스가 끝나면 셰이더가 읽을 수 있게 돌려 놓을 텍스처들이다(Sampled 로 만든 색 첨부).
        TextureHandle m_sampledAtEnd[MaxColorAttachments];
        std::uint32_t m_sampledAtEndCount = 0;
        VkImageView m_pendingTextures[MaxBoundTextures] = {};
        VkSampler m_pendingSamplers[MaxBoundSamplers] = {};
        // 이 프레임에 쓴 set 들이다. 같은 묶음(레이아웃·텍스처·샘플러)이 다시 오면 할당하지 않고 그 set 을 다시
        // 건다(D-110). 프레임마다 풀이 비워지므로 함께 비운다.
        static constexpr std::uint32_t CachedSets = 16;
        struct CachedSet
        {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VkImageView views[MaxBoundTextures] = {};
            VkSampler samplers[MaxBoundSamplers] = {};
            VkDescriptorSet set = VK_NULL_HANDLE;
        };
        CachedSet m_cachedSets[CachedSets] = {};
        std::uint32_t m_cachedSetCount = 0;
        std::uint32_t m_cachedSetCursor = 0;
        bool FindCachedSet(VkDescriptorSet& set) const;
        void RememberSet(VkDescriptorSet set);
        bool m_descriptorsDirty = false;
        bool m_renderPassActive = false;
        bool m_pipelineActive = false;
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
        std::uint32_t imageCount = 0;
        std::uint32_t currentImage = 0;
        // 제시 직전의 백버퍼 사본이다. 제시한 이미지는 되읽을 수 없으므로(D3D11 과 같은 사정, D-107)
        // 되읽기 계약은 이 사본으로 지킨다.
        VkImage presentedCopy = VK_NULL_HANDLE;
        VkDeviceMemory presentedCopyMemory = VK_NULL_HANDLE;
        VkImageLayout presentedCopyLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::uint32_t generation = 1;
        std::uint32_t backBufferGeneration = 1;
        bool occupied = false;
    };

    struct VulkanBufferState
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        BufferDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct VulkanTextureState
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkFormat format = VK_FORMAT_UNDEFINED;
        TextureDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct VulkanSamplerState
    {
        VkSampler sampler = VK_NULL_HANDLE;
        SamplerDesc desc;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    struct VulkanPipelineState
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        // 텍스처나 샘플러를 하나라도 읽는 파이프라인만 갖는다. 없으면 set 을 묶지 않는다.
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkShaderStageFlags pushConstantStages = 0;
        std::uint32_t pushConstantBytes = 0;
        std::uint32_t sampledTextureCount = 0;
        std::uint32_t samplerCount = 0;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    // 프레임마다 도는 것들이다. 슬롯의 펜스가 끝나야 그 슬롯의 명령 버퍼와 풀을 다시 쓴다.
    struct VulkanFrameSlot
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::uint64_t serial = 0;
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
        std::uint64_t handle = 0;
        std::uint64_t serial = 0;
    };

    class VulkanDevice final : public IRHIDevice
    {
    public:
        VulkanDevice() = default;
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        bool Initialize(const RHIDeviceCreateInfo& createInfo);
        void Shutdown();

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle buffer) override;
        bool WriteBuffer(BufferHandle buffer, std::size_t offset, JArrayView<std::byte> data) override;
        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle texture) override;
        bool WriteTexture(TextureHandle texture, std::uint32_t mipLevel, JArrayView<std::byte> data) override;
        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle sampler) override;
        GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle swapchain) override;
        bool ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent) override;
        BeginFrameResult BeginFrame(SwapchainHandle swapchain) override;
        FrameStatus EndFrame(const FrameContext& frame) override;
        void AbortFrame(const FrameContext& frame) override;
        FrameStatus GetStatus() const override;
        void WaitIdle() override;
        bool ReadTexture(
            TextureHandle texture,
            std::byte* destination,
            std::size_t destinationSize,
            TextureReadback& result) override;
        std::uint32_t GetFramesInFlight() const override;
        std::uint32_t GetValidationErrorCount() const override;

        // 컨텍스트가 쓰는 해석 함수들. 백버퍼 핸들은 스왑체인의 이번 이미지로 풀린다.
        struct AttachmentView
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkImageLayout* layout = nullptr;
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            VkExtent2D extent = {};
            bool sampled = false;
        };
        bool ResolveAttachment(TextureHandle texture, AttachmentView& view);
        bool ResolveSampledTexture(TextureHandle texture, VkImageView& view);
        bool ResolveBuffer(BufferHandle buffer, VkBuffer& native, BufferDesc& desc);
        bool ResolveSampler(SamplerHandle sampler, VkSampler& native);
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
        static constexpr std::uint32_t MaxSwapchains = 8;
        static constexpr std::uint32_t BackBufferTextureBase = 1;
        static constexpr std::uint32_t MaxBuffers = 1024;
        static constexpr std::uint32_t MaxTextures = 512;
        static constexpr std::uint32_t MaxGraphicsPipelines = 256;
        static constexpr std::uint32_t MaxSamplers = 64;
        static constexpr std::uint32_t MaxRetired = 4096;
        static constexpr std::uint32_t TextureResourceBase = BackBufferTextureBase + MaxSwapchains;

        bool CreateInstance(bool validation);
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();
        bool CreateFrameSlots();
        void DestroyFrameSlots();
        VulkanSwapchainState* FindSwapchain(SwapchainHandle swapchain);
        bool BuildSwapchain(VulkanSwapchainState& state, VkSwapchainKHR old);
        void ReleaseSwapchainImages(VulkanSwapchainState& state);
        bool AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
            VkMemoryPropertyFlags fallback, VkDeviceMemory& memory, bool& hostVisible);
        bool CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory);
        bool CreateStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped);
        // 프레임 밖의 한 번짜리 명령이다. 끝날 때까지 기다린다(로드·되읽기 경로).
        bool BeginOneShot();
        bool EndOneShot();
        void Retire(VulkanRetiredObject::Kind kind, std::uint64_t handle);
        void FlushRetired(std::uint64_t completedSerial);
        void DestroyRetired(const VulkanRetiredObject& object);
        bool ResolveReadableImage(TextureHandle texture, VkImage& image, VkImageLayout*& layout, TextureDesc& desc);
        FrameStatus SubmitAndPresent(bool present);
        void MarkDeviceLost();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties m_memoryProperties = {};
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_queue = VK_NULL_HANDLE;
        std::uint32_t m_queueFamily = 0;
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
        std::uint32_t m_retiredCount = 0;
        std::uint64_t m_frameSerial = 0;
        std::uint64_t m_completedSerial = 0;
        std::uint32_t m_slotCount = 1;
        std::uint32_t m_slot = 0;
        std::uint32_t m_activeSwapchainIndex = 0;
        std::uint32_t m_validationErrors = 0;
        FrameStatus m_status = FrameStatus::InvalidState;
        bool m_frameActive = false;
        bool m_oneShotActive = false;
    };

    // 소스 파일들이 함께 쓰는 변환.
    VkFormat ToVulkanFormat(TextureFormat format);
    std::uint32_t VulkanPixelSize(TextureFormat format);
    std::uint32_t VulkanNextGeneration(std::uint32_t generation);
}
