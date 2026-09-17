#include <JBro/Editor/EditorUI.h>

#include <JBro/Editor/EditorTheme.h>

#include <imgui.h>

#include "EditorUIPS.generated.h"
#include "EditorUIVS.generated.h"

namespace JBro::Sm5
{
    using BYTE = unsigned char;
#include "EditorUIPS_SM5.generated.h"
#include "EditorUIVS_SM5.generated.h"
}

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
            return static_cast<ImTextureID>(EditorUI::ToTextureId(handle));
        }

        TextureHandle FromTextureId(ImTextureID id)
        {
            const std::uint64_t packed = static_cast<std::uint64_t>(id);
            TextureHandle handle;
            handle.index = static_cast<std::uint32_t>(packed & 0xffffffffull);
            handle.generation = static_cast<std::uint32_t>(packed >> 32);
            return handle;
        }

        // `Key` 를 ImGui 의 것으로 옮긴다. 값이 줄줄이 이어지는 구간은 더해서 넘기고,
        // 나머지만 적는다 - 110개를 한 줄씩 적으면 한 줄 틀린 것을 아무도 못 찾는다.
        ImGuiKey ToImGuiKey(Key key)
        {
            const auto value = static_cast<std::uint16_t>(key);
            const auto offsetFrom = [value](Key first) {
                return static_cast<int>(value) - static_cast<int>(first);
            };

            if (key >= Key::Digit0 && key <= Key::Digit9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_0 + offsetFrom(Key::Digit0));
            }
            if (key >= Key::A && key <= Key::Z)
            {
                return static_cast<ImGuiKey>(ImGuiKey_A + offsetFrom(Key::A));
            }
            if (key >= Key::F1 && key <= Key::F12)
            {
                return static_cast<ImGuiKey>(ImGuiKey_F1 + offsetFrom(Key::F1));
            }
            if (key >= Key::Keypad0 && key <= Key::Keypad9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + offsetFrom(Key::Keypad0));
            }

            switch (key)
            {
            case Key::Tab: return ImGuiKey_Tab;
            case Key::Left: return ImGuiKey_LeftArrow;
            case Key::Right: return ImGuiKey_RightArrow;
            case Key::Up: return ImGuiKey_UpArrow;
            case Key::Down: return ImGuiKey_DownArrow;
            case Key::PageUp: return ImGuiKey_PageUp;
            case Key::PageDown: return ImGuiKey_PageDown;
            case Key::Home: return ImGuiKey_Home;
            case Key::End: return ImGuiKey_End;
            case Key::Insert: return ImGuiKey_Insert;
            case Key::Delete: return ImGuiKey_Delete;
            case Key::Backspace: return ImGuiKey_Backspace;
            case Key::Space: return ImGuiKey_Space;
            case Key::Enter: return ImGuiKey_Enter;
            case Key::Escape: return ImGuiKey_Escape;
            case Key::LeftControl: return ImGuiKey_LeftCtrl;
            case Key::LeftShift: return ImGuiKey_LeftShift;
            case Key::LeftAlt: return ImGuiKey_LeftAlt;
            case Key::LeftSuper: return ImGuiKey_LeftSuper;
            case Key::RightControl: return ImGuiKey_RightCtrl;
            case Key::RightShift: return ImGuiKey_RightShift;
            case Key::RightAlt: return ImGuiKey_RightAlt;
            case Key::RightSuper: return ImGuiKey_RightSuper;
            case Key::Menu: return ImGuiKey_Menu;
            case Key::Apostrophe: return ImGuiKey_Apostrophe;
            case Key::Comma: return ImGuiKey_Comma;
            case Key::Minus: return ImGuiKey_Minus;
            case Key::Period: return ImGuiKey_Period;
            case Key::Slash: return ImGuiKey_Slash;
            case Key::Semicolon: return ImGuiKey_Semicolon;
            case Key::Equal: return ImGuiKey_Equal;
            case Key::LeftBracket: return ImGuiKey_LeftBracket;
            case Key::Backslash: return ImGuiKey_Backslash;
            case Key::RightBracket: return ImGuiKey_RightBracket;
            case Key::GraveAccent: return ImGuiKey_GraveAccent;
            case Key::CapsLock: return ImGuiKey_CapsLock;
            case Key::ScrollLock: return ImGuiKey_ScrollLock;
            case Key::NumLock: return ImGuiKey_NumLock;
            case Key::PrintScreen: return ImGuiKey_PrintScreen;
            case Key::Pause: return ImGuiKey_Pause;
            case Key::KeypadDecimal: return ImGuiKey_KeypadDecimal;
            case Key::KeypadDivide: return ImGuiKey_KeypadDivide;
            case Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
            case Key::KeypadSubtract: return ImGuiKey_KeypadSubtract;
            case Key::KeypadAdd: return ImGuiKey_KeypadAdd;
            case Key::KeypadEnter: return ImGuiKey_KeypadEnter;
            default: return ImGuiKey_None;
            }
        }

        int ToImGuiMouseButton(MouseButton button)
        {
            switch (button)
            {
            case MouseButton::Left: return 0;
            case MouseButton::Right: return 1;
            case MouseButton::Middle: return 2;
            case MouseButton::Extra1: return 3;
            case MouseButton::Extra2: return 4;
            default: return -1;
            }
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

    bool EditorUI::Initialize(IRHIDevice& device, TextureFormat backBufferFormat, GraphicsApi api)
    {
        if (m_initialized)
        {
            return false;
        }
        m_device = &device;
        // **슬롯 수는 RHI 에게 묻는다.** 짐작해서 하나로 두면 지난 프레임이
        // 아직 읽는 정점 버퍼를 이번 프레임이 덮어쓴다.
        m_frameSlots = device.GetFramesInFlight();
        if (m_frameSlots == 0)
        {
            m_frameSlots = 1;
        }
        if (m_frameSlots > MaxFrameSlots)
        {
            m_frameSlots = MaxFrameSlots;
        }

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
        // 패널을 서로 붙이고 탭으로 묶는다. 에디터는 창이 여럿이라 이것 없이는
        // 패널마다 떠다니는 상자가 된다.
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // 생김새는 기존 엔진에서 그대로 가져온다(D-73). **색과 글꼴을 여기서
        // 정하는 이유는 컨텍스트마다 한 벌이기 때문이다** - 패널이 저마다
        // 색을 밀어 넣기 시작하면 어디서 온 색인지 알 수 없게 된다.
        EditorTheme::Apply();
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
        // D3D11 은 DXBC 를 읽는다(D-107). 나머지는 DXIL 이다.
        if (api == GraphicsApi::D3D11)
        {
            pipelineDesc.vertexShader = {Sm5::JBroEditorUIVS_SM5, sizeof(Sm5::JBroEditorUIVS_SM5)};
            pipelineDesc.pixelShader = {Sm5::JBroEditorUIPS_SM5, sizeof(Sm5::JBroEditorUIPS_SM5)};
        }
        else
        {
            pipelineDesc.vertexShader = {JBroEditorUIVS, sizeof(JBroEditorUIVS)};
            pipelineDesc.pixelShader = {JBroEditorUIPS, sizeof(JBroEditorUIPS)};
        }
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
            for (std::uint32_t slot = 0; slot < MaxFrameSlots; ++slot)
            {
                m_device->DestroyBuffer(m_vertices[slot]);
                m_device->DestroyBuffer(m_indices[slot]);
            }
        }
        m_pipeline = {};
        m_sampler = {};
        for (std::uint32_t slot = 0; slot < MaxFrameSlots; ++slot)
        {
            m_vertices[slot] = {};
            m_indices[slot] = {};
            m_vertexCapacity[slot] = 0;
            m_indexCapacity[slot] = 0;
        }
        m_frameSlots = 1;
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

    void EditorUI::AbandonDevice()
    {
        if (m_context != nullptr)
        {
            ImGui::DestroyContext(static_cast<ImGuiContext*>(m_context));
            m_context = nullptr;
        }
        // GPU 리소스는 디바이스와 함께 사라졌다. 핸들만 버린다.
        m_device = nullptr;
        m_pipeline = {};
        m_sampler = {};
        for (std::uint32_t slot = 0; slot < MaxFrameSlots; ++slot)
        {
            m_vertices[slot] = {};
            m_indices[slot] = {};
            m_vertexCapacity[slot] = 0;
            m_indexCapacity[slot] = 0;
        }
        m_frameSlots = 1;
        m_lastDrawCount = 0;
        m_frameOpen = false;
        m_initialized = false;
    }

    std::uint64_t EditorUI::ToTextureId(TextureHandle handle)
    {
        return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
    }

    bool EditorUI::PushInput(JArrayView<InputEvent> events)
    {
        if (false == m_initialized)
        {
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        ImGuiIO& io = ImGui::GetIO();

        for (std::uint32_t index = 0; index < events.size; ++index)
        {
            const InputEvent& event = events.data[index];
            switch (event.kind)
            {
            case InputEventKind::KeyDown:
            case InputEventKind::KeyUp:
            {
                const bool down = event.kind == InputEventKind::KeyDown;
                // **조합키는 키 자체와 따로 알려 줘야 한다.** ImGui 는 Ctrl+C 를
                // 판단할 때 이 상태를 보지, 좌우 Ctrl 키의 눌림을 보지 않는다.
                io.AddKeyEvent(ImGuiMod_Ctrl, (event.modifiers & KeyModifierControl) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (event.modifiers & KeyModifierShift) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (event.modifiers & KeyModifierAlt) != 0);
                io.AddKeyEvent(ImGuiMod_Super, (event.modifiers & KeyModifierSuper) != 0);
                const ImGuiKey key = ToImGuiKey(event.key);
                if (key != ImGuiKey_None)
                {
                    io.AddKeyEvent(key, down);
                }
                break;
            }

            case InputEventKind::Text:
                io.AddInputCharacter(event.codePoint);
                break;

            case InputEventKind::MouseMove:
                io.AddMousePosEvent(event.x, event.y);
                break;

            case InputEventKind::MouseButtonDown:
            case InputEventKind::MouseButtonUp:
            {
                const int button = ToImGuiMouseButton(event.button);
                if (button >= 0)
                {
                    io.AddMouseButtonEvent(
                        button, event.kind == InputEventKind::MouseButtonDown);
                }
                break;
            }

            case InputEventKind::MouseWheel:
                io.AddMouseWheelEvent(event.x, event.y);
                break;

            case InputEventKind::FocusGained:
            case InputEventKind::FocusLost:
                // 창을 떠날 때 눌려 있던 키가 눌린 채로 남지 않게 한다.
                io.AddFocusEvent(event.kind == InputEventKind::FocusGained);
                break;
            }
        }
        return true;
    }

    bool EditorUI::WantsMouse() const
    {
        if (false == m_initialized)
        {
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool EditorUI::WantsKeyboard() const
    {
        if (false == m_initialized)
        {
            return false;
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_context));
        return ImGui::GetIO().WantCaptureKeyboard;
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
        // 정점·인덱스 버퍼의 **자리만** 여기서 잡는다. `CreateBuffer` 는
        // `CreateTexture` 와 같은 이유로 프레임 안에서 거절하므로, 그리는
        // 자리에서는 이미 잡혀 있어야 한다. 채우는 것은 슬롯을 아는 `Draw` 다.
        return ReserveBuffers();
    }

    bool EditorUI::UploadDrawData(std::uint32_t slot)
    {
        const ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->TotalVtxCount == 0)
        {
            // 그릴 것이 없다. 실패가 아니다.
            return true;
        }

        // 자리는 EndFrame 이 이미 잡아 두었다. 여기서는 채우기만 한다 -
        // 프레임 안이라 버퍼를 새로 만들 수 없다.
        const std::size_t vertexBytes =
            static_cast<std::size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
        const std::size_t indexBytes =
            static_cast<std::size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
        if (vertexBytes > m_vertexCapacity[slot] || indexBytes > m_indexCapacity[slot])
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
            if (false == m_device->WriteBuffer(m_vertices[slot], vertexOffset,
                    {reinterpret_cast<const std::byte*>(list->VtxBuffer.Data),
                        static_cast<std::uint32_t>(listVertexBytes)})
                || false == m_device->WriteBuffer(m_indices[slot], indexOffset,
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

    bool EditorUI::EnsureBuffers(
        std::uint32_t slot, std::size_t vertexBytes, std::size_t indexBytes)
    {
        // 프레임마다 다시 만들지 않는다. 늘어날 때만 새로 잡는다 —
        // UI 정점 수는 프레임마다 출렁이므로 딱 맞게 잡으면 매번 다시 만들게 된다.
        if (vertexBytes > m_vertexCapacity[slot])
        {
            m_device->DestroyBuffer(m_vertices[slot]);
            const std::size_t capacity = vertexBytes + vertexBytes / 2;
            BufferDesc desc;
            desc.size = capacity;
            desc.usage = BufferUsage::Vertex;
            desc.memory = MemoryType::Upload;
            m_vertices[slot] = m_device->CreateBuffer(desc);
            if (false == m_vertices[slot].IsValid())
            {
                m_vertexCapacity[slot] = 0;
                return false;
            }
            m_vertexCapacity[slot] = capacity;
        }
        if (indexBytes > m_indexCapacity[slot])
        {
            m_device->DestroyBuffer(m_indices[slot]);
            const std::size_t capacity = indexBytes + indexBytes / 2;
            BufferDesc desc;
            desc.size = capacity;
            desc.usage = BufferUsage::Index;
            desc.memory = MemoryType::Upload;
            m_indices[slot] = m_device->CreateBuffer(desc);
            if (false == m_indices[slot].IsValid())
            {
                m_indexCapacity[slot] = 0;
                return false;
            }
            m_indexCapacity[slot] = capacity;
        }
        return true;
    }

    bool EditorUI::ReserveBuffers()
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
        for (std::uint32_t slot = 0; slot < m_frameSlots; ++slot)
        {
            if (false == EnsureBuffers(slot, vertexBytes, indexBytes))
            {
                return false;
            }
        }
        return true;
    }

    bool EditorUI::Draw(IRHICommandContext& commands, std::uint32_t frameSlot)
    {
        m_lastDrawCount = 0;
        if (frameSlot >= m_frameSlots)
        {
            // RHI 가 말한 것보다 큰 슬롯이다. 그냥 두면 배열 밖을 읽는다.
            //
            // **이 줄은 뮤테이션으로 죽지 않는다.** 지우면 배열 밖을 읽는데, 거기서
            // 나온 쓰레기 핸들은 아래 `IsValid()` 에서 걸려 어차피 거짓이 나온다.
            // 관측되는 결과가 같다는 것이지 같은 코드라는 뜻은 아니다 - 한쪽은
            // 정의되지 않은 동작이다.
            return false;
        }
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

        // **이 슬롯의 버퍼에 지금 채운다.** 이 슬롯을 쓰는 지난 프레임은 이미
        // 끝났다고 RHI 가 보장하므로, 여기서 덮어써도 읽는 중인 것을 건드리지
        // 않는다. 커맨드 리스트는 아직 제출되지 않았으니 순서도 맞다.
        if (false == UploadDrawData(frameSlot))
        {
            return false;
        }
        if (false == m_vertices[frameSlot].IsValid()
            || false == m_indices[frameSlot].IsValid())
        {
            return false;
        }

        if (false == commands.SetGraphicsPipeline(m_pipeline))
        {
            return false;
        }
        if (false == commands.SetVertexBuffer(
                0, m_vertices[frameSlot], sizeof(ImDrawVert), 0)
            || false == commands.SetIndexBuffer(
                m_indices[frameSlot], IndexFormat::UInt16, 0))
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
                // **화면 밖으로 나간 쪽을 0 으로 붙인다.** ImGui 는 잘라내기 사각형을
                // 화면 밖까지 태연히 내보낸다.
                //
                // **D3D12 는 이것을 고쳐 주지 않아도 된다** - 음수 시저가 유효하기
                // 때문이다(뷰포트 경계 하한이 -32768 이다). 검증 레이어를 켜고 음수
                // 시저를 넘겨 봤지만 한 마디도 하지 않았다. 그래서 이 두 줄은 D3D12
                // 에서는 지워도 아무 차이가 없고, 뮤테이션 테스트가 죽지 않는다.
                //
                // 남기는 이유는 Vulkan 이다. `VkRect2D` 의 오프셋은 부호 있는 정수지만
                // 시저에서는 0 이상이어야 한다고 규격이 못박는다. 그 백엔드가 생기면
                // 이 규칙은 RHI 로 내려가야 한다 - 렌더 타깃 크기를 아는 것은 거기다.
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
