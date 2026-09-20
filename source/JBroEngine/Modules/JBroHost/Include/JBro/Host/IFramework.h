#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    class AssetSystem;
    class NetworkHost;
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
        // 호스트가 소유하는 네트워크(D-122). 있으면 프레임워크가 캔버스를 묶고 복제 풀과 수신·송신 시스템을 세운다.
        // 없으면(테스트의 가짜, 네트워크를 끈 호스트) 아무것도 세우지 않는다.
        NetworkHost* network = nullptr;
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
        // 캔버스의 컴포넌트가 든 에셋 아이디(`xxxId`)를 이번 실행의 핸들(`xxx`)로 푼다(asset-plan §2.6, D-115). 캔버스를
        // 읽은 뒤와 편집 뒤에 호스트·에디터가 부른다. 앞서 잡은 것은 놓고, 참조 수가 0 이 된 에셋은 내린다.
        // 캔버스나 에셋 시스템이 없는 프레임워크(테스트의 가짜)는 아무것도 하지 않는다.
        virtual void BindCanvasAssets()
        {
        }
    };
}
