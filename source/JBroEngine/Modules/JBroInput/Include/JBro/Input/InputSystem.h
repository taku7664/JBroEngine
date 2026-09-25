#pragma once

#include <JBro/InputTypes/InputHandler.h>
#include <JBro/InputTypes/Internal/SystemContext.h>
#include <JBro/InputTypes/ServiceContext.h>
#include <JBro/InputTypes/System/IInputSystem.h>
#include <JBro/Platform/Input.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro::System
{
    // 게임 입력을 접고 레이어 체인에 내려보낸다(D-201). 엔진이 소유한다(ProjectRule §7).
    //
    // **폴링하지 않는다.** 플랫폼이 모은 이벤트(D-62)를 프레임마다 한 번 접는다 - 기존 엔진은
    // `GetAsyncKeyState` 로 긁어서 한 프레임 안에 눌렀다 뗀 키를 잃었다.
    // 메인 스레드 전용이다.
    class InputSystem final : public IInputSystem
    {
    public:
        InputSystem();
        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        // 이번 프레임을 연다. 지난 프레임의 눌림·뗌 수, 글자, 이동, 휠을 비우고 이벤트를 순서대로 접는다.
        // 입력을 받지 않는 프레임(에디터에서 게임 뷰가 포커스를 갖지 않을 때)도 빈 목록으로 불러야 한다 -
        // 그러지 않으면 지난 프레임의 `IsPressed` 가 남는다. 남은 입력(폴링이 보는 것)도 여기서 이번 프레임 전체로 돌아간다.
        void BeginFrame(JArrayView<InputEvent> events, const InputSurfaceMapping& mapping = {});

        const InputFrame& GetFrame() const;

        // 레이어 체인이 다 돈 뒤 남은 것이다. 체인이 돌지 않은 프레임은 이번 프레임 전체다.
        const InputView& GetResidualView() const noexcept override;

        // 스크립트 DLL 과 이 모듈 사본이 받을 값이다. 주소가 호스트의 수명 동안 바뀌지 않는다 -
        // 확장 블록이 이 주소를 가리킨다.
        const InputSystemContext& GetSystemContext() const;
        const InputServiceContext& GetServiceContext() const;

        // 레이어 체인이다(D-201). 누구를 어떤 차례로 부를지는 부르는 쪽(`ScriptSystem`)이 정하고, 여기는 소비를 나른다.
        //   BeginDispatch() → 켜진 핸들러마다 Deliver() → EndDispatch()
        // `Deliver` 가 참이면 그 핸들러가 `Block` 한 것이고, 부르는 쪽은 거기서 멈춘다. 멈추지 않아도 아래는 빈 입력만 본다.
        void BeginDispatch();
        bool Deliver(IInputHandler& handler);
        // 체인을 닫는다. 폴링(`Service::InputService`)이 보는 남은 입력이 여기서 정해진다.
        void EndDispatch();

        // 프로젝트가 정한 레이어 순서다. 앞이 먼저 받는다. 비어 있으면 기본 순서(Modal, UI, Game, World, Debug)다.
        void SetLayerOrder(JArrayView<NameId> layers);
        // 레이어의 순위다. 작을수록 먼저다. 없는 레이어는 맨 아래(레이어 수)이고 이름마다 한 번 경고한다 -
        // `text` 는 그 경고에만 쓴다. 체인을 세울 때(콜드 경로)만 부른다.
        std::uint32_t GetLayerPriority(NameId layer, const char* text);
        // 레이어 순서가 바뀔 때마다 오른다. 체인을 들고 있는 쪽이 이 값으로 다시 줄 세울지 안다.
        std::uint64_t GetLayerRevision() const;

    private:
        void Fold(const InputEvent& event, const InputSurfaceMapping& mapping);
        void ReleaseAll();

        InputFrame m_frame;
        InputView m_residual;
        // 체인을 따라 내려가는 뷰다. `m_residual` 과 따로 두어, 체인이 도는 동안의 폴링이 반쯤 소비된 것을 보지 않게 한다.
        InputView m_dispatch;
        Array<NameId> m_layers;
        Table<NameId, std::uint8_t> m_warnedLayers;
        std::uint64_t m_layerRevision = 1;
        InputSystemContext m_systemContext;
        InputServiceContext m_serviceContext;
    };
}
