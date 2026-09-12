#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // Render()의 세 가지 결말이다. "제출할 것이 없다"는 상태이지 실패가 아니다(D-49).
    enum class RenderResult : std::uint8_t
    {
        // 뷰와 패킷을 모두 넘겼다. 호스트는 프레임을 닫고 제시한다.
        Submitted,
        // 그릴 것이 없다. 호스트는 프레임을 버리고 계속 돌린다.
        NothingToSubmit,
        // 제출 도중 실패했다. 호스트는 프레임을 버리고 오류로 올린다.
        Failed
    };

    struct FrameworkContext
    {
        JMemoryContext memory;
        AssetSystem* assets = nullptr;
        Renderer* renderer = nullptr;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
    };

    class IFramework
    {
    public:
        virtual ~IFramework() = default;

        virtual bool Initialize(const FrameworkContext& context) = 0;
        // Host activates only its successfully opened project, never standalone previews.
        // Hooks must not throw; unbinding precedes destruction of borrowed systems.
        virtual bool BindScriptContexts() noexcept = 0;
        virtual void UnbindScriptContexts() noexcept = 0;
        // 스크립트 DLL 이 요구하는, 이 프레임워크만의 컨텍스트 블록이다(D-37).
        // 호스트는 BindScriptContexts 뒤에 이것을 읽어 DLL 로 넘긴다. 돌려준 배열과
        // 그것이 가리키는 데이터는 프로젝트가 닫힐 때까지 살아 있어야 한다.
        virtual JArrayView<ScriptContextBlock> GetScriptContextBlocks() const noexcept
        {
            return {};
        }
        virtual void Update(float deltaTime) = 0;
        // Host opens/closes the Renderer frame. Framework submits its views and packets only.
        virtual RenderResult Render() = 0;
        virtual void Shutdown() = 0;
    };
}
