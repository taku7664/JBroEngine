#include <JBro/Editor/EditorUI.h>

#include <imgui.h>

#include "EditorUIPS.generated.h"
#include "EditorUIVS.generated.h"

#include <cstddef>
#include <cstring>

namespace JBro
{
    namespace
    {
        // ImGui 가 텍스처를 식별하는 값이다. 우리 핸들은 인덱스와 세대 둘이므로
        // 하나로 접어 넣는다 — ImTextureID 는 정수 하나다.
        ImTextureID ToTextureId(TextureHandle handle)
        {
            const std::uint64_t packed =
                (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
            return static_cast<ImTextureID>(packed);
        }

        TextureHandle FromTextureId(ImTextureID id)
        {
            const std::uint64_t packed = static_cast<std::uint64_t>(id);
            TextureHandle handle;
            handle.index = static_cast<std::uint32_t>(packed & 0xffffffffull);
            handle.generation = static_cast<std::uint32_t>(packed >> 32);
            return handle;
        }

        struct alignas(4) UIPushConstants
        {
            float scaleX = 0.0f;
            float scaleY = 0.0f;
            float translateX = 0.0f;
            float translateY = 0.0f;
        };
    }

    EditorUI::~EditorUI()
    {
        Shutdown();
    }

    bool EditorUI::Initialize(IRHIDevice& device, TextureFormat backBufferFormat)
    {
        if (m_initialized)
        {
            return false;
        }
        m_device = &device;

        IMGUI_CHECKVERSION();
        ImGuiContext* context = ImGui::CreateContext();
        if (context == nullptr)
        {
            m_device = nullptr;
            return false;
        }
        m_context = context;
        ImGui::SetCurrentContext(context);

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "JBroRHI";
        // **ini 를 끈다.** 기본값은 현재 작업 디렉터리에 `imgui.ini` 를 쓰고 다음 실행 때
        // 그것을 읽는다. 그러면 창 위치가 지난 실행에 달리고, 테스트는 자기가 지정한
        // 자리에 창이 서지 않는 이유를 알 수 없게 된다. 저장소에 파일도 남는다.
        // 도킹 레이아웃 보존은 나중에 프로젝트 경로에 **명시적으로** 넣는다.
        io.IniFilename = nullptr;
        // 1.92 부터 백엔드가 텍스처를 직접 만들고 지운다. 이것을 켜지 않으면
        // ImGui 가 옛 방식(GetTexDataAsRGBA32)을 기대한다.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        // 인덱스는 16비트다. 우리 SetIndexBuffer 가 그 형식을 받는다.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        SamplerDesc samplerDesc;
        samplerDesc.minFilter = FilterMode::Linear;
        samplerDesc.magFilter = FilterMode::Linear;
        samplerDesc.addressU = AddressMode::ClampToEdge;
        samplerDesc.addressV = AddressMode::ClampToEdge;
        m_sampler = device.CreateSampler(samplerDesc);
        if (false == m_sampler.IsValid())
        {
            Shutdown();
            return false;
        }

        // `ImDrawVert` 를 그대로 읽는다. 정점을 옮겨 담지 않는다.
        static const VertexAttributeDesc attributes[] = {
            {0, static_cast<std::uint32_t>(offsetof(ImDrawVert, pos)), VertexFormat::Float2},
            {1, static_cast<std::uint32_t>(offsetof(ImDrawVert, uv)),  VertexFormat::Float2},
            {2, static_cast<std::uint32_t>(offsetof(ImDrawVert, col)), VertexFormat::UByte4Norm},
        };
        static_assert(sizeof(ImDrawIdx) == 2, "the index buffer is bound as 16 bit");

        VertexBufferLayoutDesc layout;
        layout.stride = sizeof(ImDrawVert);
        layout.attributes = {attributes, 3};
        const TextureFormat colorFormats[] = {backBufferFormat};

        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = {JBroEditorUIVS, sizeof(JBroEditorUIVS)};
        pipelineDesc.pixelShader = {JBroEditorUIPS, sizeof(JBroEditorUIPS)};
        pipelineDesc.vertexBuffers = {&layout, 1};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.blend = BlendMode::Alpha;
        pipelineDesc.cull = CullMode::None;
        pipelineDesc.pushConstantStages = ShaderStage::Vertex;
        pipelineDesc.pushConstantBytes = sizeof(UIPushConstants);
        pipelineDesc.sampledTextureCount = 1;
        pipelineDesc.samplerCount = 1;
        m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
        if (false == m_pipeline.IsValid())
        {
            Shutdown();
            return false;
        }

        m_initialized = true;
        return true;
    }

    void EditorUI::Shutdown()
    {
        if (m_context != nullptr)
        {
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
            // ImGui 가 만든 텍스처를 우리가 쥐고 있다. 컨텍스트를 없애기 전에 돌려준다.
            if (m_device != nullptr)
            {
                for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
                {
                    if (texture != nullptr && texture->GetTexID() != ImTextureID_Invalid)
                    {
                        m_device->DestroyTexture(FromTextureId(texture->GetTexID()));
                        texture->SetTexID(ImTextureID_Invalid);
                        texture->SetStatus(ImTextureStatus_Destroyed);
                    }
                }
            }
            ImGui::DestroyContext(static_cast<ImGuiContext*>(m_context));
            m_context = nullptr;
        }
        if (m_device != nullptr)
        {
            m_device->DestroyGraphicsPipeline(m_pipeline);
            m_device->DestroySampler(m_sampler);
            m_device->DestroyBuffer(m_vertices);
            m_device->DestroyBuffer(m_indices);
        }
        m_pipeline = {};
        m_sampler = {};
        m_vertices = {};
        m_indices = {};
        m_vertexCapacity = 0;
        m_indexCapacity = 0;
        m_lastDrawCount = 0;
        m_device = nullptr;
        m_frameOpen = false;
        m_initialized = false;
    }

    bool EditorUI::IsInitialized() const
    {
        return m_initialized;
    }

    std::size_t EditorUI::GetLastDrawCount() const
    {
        return m_lastDrawCount;
    }

    bool EditorUI::BeginFrame(const Extent2D& displaySize, float deltaTime)
    {
        if (false == m_initialized || m_frameOpen)
        {
            return false;
        }
        if (displaySize.width == 0 || displaySize.height == 0 || deltaTime <= 0.0f)
        {
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(
            static_cast<float>(displaySize.width),
            static_cast<float>(displaySize.height));
        io.DeltaTime = deltaTime;
        ImGui::NewFrame();
        m_frameOpen = true;
        return true;
    }

    bool EditorUI::EndFrame()
    {
        if (false == m_initialized || false == m_frameOpen)
        {
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        ImGui::Render();
        m_frameOpen = false;
        if (false == ProcessTextureRequests())
        {
            return false;
        }
        // 정점·인덱스 버퍼도 여기서 잡는다. `CreateBuffer` 는 `CreateTexture` 와 같은 이유로
        // 프레임 안에서 거절하므로, 그리는 자리에서는 이미 잡혀 있어야 한다.
        return UploadDrawData();
    }

    bool EditorUI::UploadDrawData()
    {
        const ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->TotalVtxCount == 0)
        {
            // 그릴 것이 없다. 실패가 아니다.
            return true;
        }

        const std::size_t vertexBytes =
            static_cast<std::size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
        const std::size_t indexBytes =
            static_cast<std::size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
        if (false == EnsureBuffers(vertexBytes, indexBytes))
        {
            return false;
        }

        // 리스트를 이어 붙여 한 버퍼에 넣는다. 리스트마다 버퍼를 두면
        // 드로우마다 바인딩이 바뀐다.
        std::size_t vertexOffset = 0;
        std::size_t indexOffset = 0;
        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
        {
            const ImDrawList* list = drawData->CmdLists[listIndex];
            const std::size_t listVertexBytes =
                static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
            const std::size_t listIndexBytes =
                static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
            if (false == m_device->WriteBuffer(m_vertices, vertexOffset,
                    {reinterpret_cast<const std::byte*>(list->VtxBuffer.Data),
                        static_cast<std::uint32_t>(listVertexBytes)})
                || false == m_device->WriteBuffer(m_indices, indexOffset,
                    {reinterpret_cast<const std::byte*>(list->IdxBuffer.Data),
                        static_cast<std::uint32_t>(listIndexBytes)}))
            {
                return false;
            }
            vertexOffset += listVertexBytes;
            indexOffset += listIndexBytes;
        }
        return true;
    }

    bool EditorUI::ProcessTextureRequests()
    {
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        for (ImTextureData* texture : platformIO.Textures)
        {
            if (texture == nullptr)
            {
                continue;
            }
            switch (texture->Status)
            {
            case ImTextureStatus_WantCreate:
            case ImTextureStatus_WantUpdates:
            {
                // 부분 갱신을 따로 하지 않고 통째로 다시 올린다. `WriteTexture` 가
                // 면 하나를 받으므로 그것이 맞고, 폰트 아틀라스가 바뀌는 일은 드물다.
                if (texture->Format != ImTextureFormat_RGBA32)
                {
                    return false;
                }
                TextureHandle handle = texture->GetTexID() != ImTextureID_Invalid
                    ? FromTextureId(texture->GetTexID())
                    : TextureHandle{};

                // 크기가 달라졌으면 새로 만든다. 같은 크기면 자리를 그대로 쓴다.
                bool needsCreate = false == handle.IsValid();
                if (handle.IsValid() && texture->Status == ImTextureStatus_WantCreate)
                {
                    needsCreate = true;
                }
                if (needsCreate)
                {
                    if (handle.IsValid())
                    {
                        m_device->DestroyTexture(handle);
                    }
                    TextureDesc desc;
                    desc.extent = {
                        static_cast<std::uint32_t>(texture->Width),
                        static_cast<std::uint32_t>(texture->Height)};
                    desc.format = TextureFormat::RGBA8Unorm;
                    desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
                    handle = m_device->CreateTexture(desc);
                    if (false == handle.IsValid())
                    {
                        return false;
                    }
                }

                const std::size_t size = static_cast<std::size_t>(texture->Width)
                    * static_cast<std::size_t>(texture->Height) * 4;
                if (false == m_device->WriteTexture(handle, 0,
                    {reinterpret_cast<const std::byte*>(texture->GetPixels()),
                        static_cast<std::uint32_t>(size)}))
                {
                    return false;
                }
                texture->SetTexID(ToTextureId(handle));
                texture->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantDestroy:
            {
                if (texture->GetTexID() != ImTextureID_Invalid)
                {
                    m_device->DestroyTexture(FromTextureId(texture->GetTexID()));
                    texture->SetTexID(ImTextureID_Invalid);
                }
                texture->SetStatus(ImTextureStatus_Destroyed);
                break;
            }
            default:
                break;
            }
        }
        return true;
    }

    bool EditorUI::EnsureBuffers(std::size_t vertexBytes, std::size_t indexBytes)
    {
        // 프레임마다 다시 만들지 않는다. 늘어날 때만 새로 잡는다 —
        // UI 정점 수는 프레임마다 출렁이므로 딱 맞게 잡으면 매번 다시 만들게 된다.
        if (vertexBytes > m_vertexCapacity)
        {
            m_device->DestroyBuffer(m_vertices);
            const std::size_t capacity = vertexBytes + vertexBytes / 2;
            BufferDesc desc;
            desc.size = capacity;
            desc.usage = BufferUsage::Vertex;
            desc.memory = MemoryType::Upload;
            m_vertices = m_device->CreateBuffer(desc);
            if (false == m_vertices.IsValid())
            {
                m_vertexCapacity = 0;
                return false;
            }
            m_vertexCapacity = capacity;
        }
        if (indexBytes > m_indexCapacity)
        {
            m_device->DestroyBuffer(m_indices);
            const std::size_t capacity = indexBytes + indexBytes / 2;
            BufferDesc desc;
            desc.size = capacity;
            desc.usage = BufferUsage::Index;
            desc.memory = MemoryType::Upload;
            m_indices = m_device->CreateBuffer(desc);
            if (false == m_indices.IsValid())
            {
                m_indexCapacity = 0;
                return false;
            }
            m_indexCapacity = capacity;
        }
        return true;
    }

    bool EditorUI::Draw(IRHICommandContext& commands)
    {
        m_lastDrawCount = 0;
        if (false == m_initialized || m_frameOpen)
        {
            // 프레임이 아직 열려 있다. EndFrame 을 부르지 않았다는 뜻이고,
            // 그 상태의 드로우 리스트는 지난 프레임 것이다.
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        const ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr)
        {
            return false;
        }
        if (drawData->TotalVtxCount == 0)
        {
            // 그릴 것이 없다. 실패가 아니다.
            return true;
        }

        // 버퍼는 EndFrame 이 이미 채워 두었다.
        if (false == m_vertices.IsValid() || false == m_indices.IsValid())
        {
            return false;
        }

        if (false == commands.SetGraphicsPipeline(m_pipeline))
        {
            return false;
        }
        if (false == commands.SetVertexBuffer(0, m_vertices, sizeof(ImDrawVert), 0)
            || false == commands.SetIndexBuffer(m_indices, IndexFormat::UInt16, 0))
        {
            return false;
        }

        // 화면 좌표를 클립 좌표로. y 는 뒤집는다 — ImGui 는 위가 0 이다.
        UIPushConstants push;
        push.scaleX = 2.0f / drawData->DisplaySize.x;
        push.scaleY = -2.0f / drawData->DisplaySize.y;
        push.translateX = -1.0f - drawData->DisplayPos.x * push.scaleX;
        push.translateY = 1.0f - drawData->DisplayPos.y * push.scaleY;
        if (false == commands.SetGraphicsConstants(
            {reinterpret_cast<const std::byte*>(&push), static_cast<std::uint32_t>(sizeof(push))}))
        {
            return false;
        }

        const ImVec2 clipOffset = drawData->DisplayPos;
        std::uint32_t globalVertexOffset = 0;
        std::uint32_t globalIndexOffset = 0;
        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
        {
            const ImDrawList* list = drawData->CmdLists[listIndex];
            for (int commandIndex = 0; commandIndex < list->CmdBuffer.Size; ++commandIndex)
            {
                const ImDrawCmd& command = list->CmdBuffer[commandIndex];
                if (command.UserCallback != nullptr)
                {
                    // 콜백은 지원하지 않는다. 조용히 건너뛰면 화면이 틀리게 나온다.
                    return false;
                }
                if (command.ElemCount == 0)
                {
                    continue;
                }

                ScissorRect scissor;
                scissor.left = static_cast<std::int32_t>(command.ClipRect.x - clipOffset.x);
                scissor.top = static_cast<std::int32_t>(command.ClipRect.y - clipOffset.y);
                scissor.right = static_cast<std::int32_t>(command.ClipRect.z - clipOffset.x);
                scissor.bottom = static_cast<std::int32_t>(command.ClipRect.w - clipOffset.y);
                if (scissor.right <= scissor.left || scissor.bottom <= scissor.top)
                {
                    continue;
                }
                if (scissor.left < 0)
                {
                    scissor.left = 0;
                }
                if (scissor.top < 0)
                {
                    scissor.top = 0;
                }
                commands.SetScissor(scissor);

                if (false == commands.SetTexture(0, FromTextureId(command.GetTexID()))
                    || false == commands.SetSampler(0, m_sampler))
                {
                    return false;
                }
                if (false == commands.DrawIndexedInstanced(
                    command.ElemCount,
                    1,
                    command.IdxOffset + globalIndexOffset,
                    static_cast<std::int32_t>(command.VtxOffset + globalVertexOffset),
                    0))
                {
                    return false;
                }
                ++m_lastDrawCount;
            }
            globalVertexOffset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
            globalIndexOffset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        }
        return true;
    }
}
