#pragma once

#include <JBro/Platform/Input.h>
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
        // **디바이스가 먼저 사라졌을 때 부른다.** 아무것도 해제하지 않고 잊는다 -
        // 디바이스가 죽으면 그 위의 리소스도 함께 죽었고, 죽은 디바이스로
        // 해제를 부르면 그 자리에서 터진다. 호스트는 렌더가 실패하면 프레임
        // 안에서 디바이스까지 놓아 버리므로, 이 경우가 실제로 일어난다.
        void AbandonDevice();
        bool IsInitialized() const;

        // 플랫폼이 모은 입력을 넘긴다. **`BeginFrame` 앞에서 부른다** -
        // ImGui 는 `NewFrame` 에서 이번 프레임의 입력 상태를 굳히므로, 그 뒤에
        // 넣은 것은 한 프레임 늦게 반영된다.
        bool PushInput(JArrayView<InputEvent> events);

        // 이번 프레임의 입력을 ImGui 가 가져갔는가. **게임에 입력을 넘길지
        // 판단하는 자리다** - 에디터의 텍스트 필드에 타자를 치는 중에
        // 게임 스크립트가 같은 키를 받으면 안 된다.
        bool WantsMouse() const;
        bool WantsKeyboard() const;

        // 화면 크기와 경과 시간을 준다. 창이 없는 테스트에서도 부를 수 있다.
        bool BeginFrame(const Extent2D& displaySize, float deltaTime);
        // ImGui 를 마무리하고 텍스처 요청을 처리한다. **RHI 프레임 밖에서 부른다.**
        bool EndFrame();
        // 마지막 EndFrame 이 만든 드로우 리스트를 제출한다. 렌더 패스 안에서 부른다.
        bool Draw(IRHICommandContext& commands);

        // 텍스처 핸들을 ImGui 가 쓰는 값으로 접는다. **에디터가 게임 뷰를
        // `ImGui::Image` 로 붙이려면 이것이 필요하다** - 그 값이 다시 이 백엔드로
        // 돌아와 핸들로 펴지므로, 접는 방법을 한 군데서 정해야 한다.
        // `ImTextureID` 를 헤더에 노출하지 않으려고 부호 없는 64비트로 돌려준다.
        static std::uint64_t ToTextureId(TextureHandle handle);

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
