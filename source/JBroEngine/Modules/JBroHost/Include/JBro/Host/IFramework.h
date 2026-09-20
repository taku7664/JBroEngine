#pragma once

#include <JBro/Core/Core.h>
#include <JBro/RHI/RHI.h>
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

    // **캔버스 뷰**가 쓰는 뷰다(D-130). 게임의 카메라가 아니라 **편집 카메라**로 같은 장면을
    // 한 번 더, 에디터가 잡아 둔 텍스처에 그린다.
    //
    // 기존 엔진에서 유니티의 씬 뷰 노릇을 하던 것이 캔버스 뷰이고, 게임 뷰는 시뮬레이션이
    // 보는 화면이었다. 둘은 **같은 프레임에 함께 보여야** 하므로 뷰마다 타깃이 따로 있어야 한다.
    struct EditorViewDesc
    {
        // 그릴 곳. 비어 있으면 아무것도 하지 않는다.
        TextureHandle target;
        // 그 텍스처의 크기. 0 이면 아무것도 하지 않는다.
        Extent2D extent;
        // 화면 한가운데가 보는 월드 좌표.
        float centerX = 0.0f;
        float centerY = 0.0f;
        // 화면 **세로 절반**이 담는 월드 길이다. 게임 카메라의 `orthographicSize` 와 같은 뜻이라
        // 두 화면의 배율을 같은 수로 견줄 수 있다.
        float orthographicSize = 5.0f;
        float clearColor[4] = {0.13f, 0.14f, 0.17f, 1.0f};

        // ── 3D 만 쓰는 값 ────────────────────────────────────────────
        //
        // 3D 의 편집 카메라는 **바라보는 점 둘레를 도는** 카메라다(궤도 카메라). 2D 처럼
        // 평면을 밀고 당기는 것으로는 뒤를 볼 수 없다. 2D 는 이 값들을 읽지 않는다.
        float centerZ = 0.0f;
        // 바라보는 점에서 떨어진 거리. 휠이 이것을 바꾼다.
        float distance = 12.0f;
        // 그 점 둘레의 각(도). 가로 회전과 세로 회전이다.
        float yawDegrees = 40.0f;
        float pitchDegrees = -25.0f;
        // 세로 화각(도). 직교가 아니라 원근이다 - 3D 는 깊이가 보여야 한다.
        float verticalFieldOfView = 60.0f;
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
        // **게임을 돌릴 것인가**(D-131). 거짓이면 스크립트·물리·네트워크는 서고,
        // 트랜스폼과 추출은 그대로 돈다 - 편집 중에도 화면은 나와야 하기 때문이다.
        // 게임 실행은 늘 참이고 에디터만 이것을 끈다. 프로젝트를 열 때 한 번 더 적용된다.
        virtual void SetSimulationEnabled(bool enabled)
        {
            (void)enabled;
        }
        // Host opens/closes the Renderer frame. Framework submits its views and packets only.
        virtual RenderResult Render() = 0;
        // 같은 프레임에 **편집 카메라로 한 번 더** 제출한다(D-130). `Render` 바로 뒤에서만
        // 부른다 - 그 프레임에 모아 둔 그릴 것을 그대로 다시 쓰기 때문이다.
        // 캔버스가 없거나 차원이 이 뷰를 모르면 `NothingToSubmit` 이다.
        virtual RenderResult RenderEditorView(const EditorViewDesc& view)
        {
            (void)view;
            return RenderResult::NothingToSubmit;
        }
        virtual void Shutdown() = 0;
        // 캔버스의 컴포넌트가 든 에셋 아이디(`xxxId`)를 이번 실행의 핸들(`xxx`)로 푼다(asset-plan §2.6, D-115). 캔버스를
        // 읽은 뒤와 편집 뒤에 호스트·에디터가 부른다. 앞서 잡은 것은 놓고, 참조 수가 0 이 된 에셋은 내린다.
        // 캔버스나 에셋 시스템이 없는 프레임워크(테스트의 가짜)는 아무것도 하지 않는다.
        virtual void BindCanvasAssets()
        {
        }
    };
}
