#pragma once

#include <JBro/RHI/RHI.h>

namespace JBro
{
    class IPlatform;

    // ImGui 를 JBroRHI 위에서 그린다(D-60).
    //
    // `imgui_impl_dx12` 를 쓰지 않는 이유는 그것이 `ID3D12Device` 와 디스크립터 힙을
    // 직접 달라고 하기 때문이다. RHI 가 감춘 것을 도로 꺼내면 추상화에 구멍이 하나 생기고,
    // 그 구멍은 다른 백엔드를 붙일 때마다 다시 뚫어야 한다.
    //
    // **프레임 순서가 계약이다.**
    //
    //     ui.BeginFrame(...)          // ImGui::NewFrame
    //     ... ImGui::Begin/End ...    // 사용자 UI
    //     ui.EndFrame()               // ImGui::Render + 텍스처 요청 처리
    //     device->BeginFrame(...)     // RHI 프레임 시작
    //     ui.Draw(commands)           // 드로우 리스트 제출
    //     device->EndFrame(...)
    //
    // **텍스처 처리가 `EndFrame` 에 있는 것이 핵심이다.** ImGui 는 폰트 아틀라스를
    // 만들거나 고쳐 달라고 요청하는데, `IRHIDevice::WriteTexture` 는 GPU 를 기다리므로
    // 프레임 안에서 부를 수 없다. 그래서 프레임을 열기 **전에** 처리한다.
    class EditorUI
    {
    public:
        EditorUI() = default;
        ~EditorUI();
        EditorUI(const EditorUI&) = delete;
        EditorUI& operator=(const EditorUI&) = delete;

        // 백버퍼 포맷은 파이프라인을 만들 때 필요하다.
        bool Initialize(IRHIDevice& device, TextureFormat backBufferFormat);
        void Shutdown();
        bool IsInitialized() const;

        // 화면 크기와 경과 시간을 준다. 창이 없는 테스트에서도 부를 수 있다.
        bool BeginFrame(const Extent2D& displaySize, float deltaTime);
        // ImGui 를 마무리하고 텍스처 요청을 처리한다. **RHI 프레임 밖에서 부른다.**
        bool EndFrame();
        // 마지막 EndFrame 이 만든 드로우 리스트를 제출한다. 렌더 패스 안에서 부른다.
        bool Draw(IRHICommandContext& commands);

        // 마지막으로 제출한 드로우 수다. 테스트가 붙잡는 손잡이다.
        std::size_t GetLastDrawCount() const;

    private:
        struct TextureSlot
        {
            TextureHandle handle;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
        };

        bool EnsureBuffers(std::size_t vertexBytes, std::size_t indexBytes);
        bool ProcessTextureRequests();
        bool UploadDrawData();

        IRHIDevice* m_device = nullptr;
        GraphicsPipelineHandle m_pipeline;
        SamplerHandle m_sampler;
        BufferHandle m_vertices;
        BufferHandle m_indices;
        std::size_t m_vertexCapacity = 0;
        std::size_t m_indexCapacity = 0;
        std::size_t m_lastDrawCount = 0;
        // ImGui 컨텍스트는 이 객체가 소유한다. 여러 개를 만들 일이 없으므로 숨긴다.
        void* m_context = nullptr;
        bool m_frameOpen = false;
        bool m_initialized = false;
    };
}
