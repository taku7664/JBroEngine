#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Core/Core.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Runtime/ScriptModule.h>

namespace JBro
{
    class AssetSystem;
    class NetworkHost;
    class TaskManager;
    namespace System
    {
        // 오디오 시스템은 `JBroAudio` 의 것이다. 이 헤더를 쓰는 모듈이 오디오 헤더를 보지 않게 이름만 안다.
        class AudioSystem;
        class InputSystem;
        class TimeSystem;
        class DebugDrawSystem;
    }
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
        // 참이면 화면 레이어만 그린다(UI 보기, D-237). 가운데·크기는 기준 픽셀이다. 거짓이면 월드 레이어만 그린다.
        bool screenSpace = false;
        // 스크립트의 디버그 선을 이 뷰에 그릴지다(D-243). 캔버스 뷰는 기본으로 그린다.
        bool debugDraw = true;
        // 캔버스 뷰에서 **들어가 있는 오브젝트**다(D-157, D-252). 있으면 장면 위에 반투명 흰 막을 덮고 이 오브젝트와
        // 그 자손만 막 위에 다시 그린다 - 무엇을 고치는 중인지가 화면에서 보인다. 없으면 막이 없다. 2D 만 읽는다.
        InstanceId focusObject = InvalidInstanceId;
        // **고른 오브젝트들이다**(D-276). 있으면 그 스프라이트만 `outlineMask` 에 한 번 더 그리고, 뷰 위에 그 실제 픽셀의
        // 둘레를 노란 2 픽셀 선으로 덧그린다(기존 `COutlineRenderer2D`). 배열은 부르는 쪽이 이 프레임의 그리기가 끝날 때까지 든다.
        // 두 텍스처는 `target` 과 같은 크기·포맷이고 `RenderTarget | Sampled` 다. 하나라도 비면 외곽선이 없다. 2D 만 읽는다.
        const InstanceId* selection = nullptr;
        std::uint32_t selectionCount = 0;
        TextureHandle outlineMask;
        TextureHandle outlineScratch;

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

    // **레이어 썸네일 하나**(D-287, 기존 `ImEditor::RenderLayerThumbnails`). 그 레이어만 켠 게임 화면의 축소판이다 - 게임 카메라·패럴랙스는 그대로이고,
    // 블렌드·불투명도는 무시하며(아래 레이어가 없다) 바탕은 `clearColor` 로 불투명하게 지운다(투명하면 ImGui 의 곧은 알파 블렌드에서 어두워진다).
    // 화면 레이어는 그 맞춤 방식의 화면 뷰다. 게임 카메라가 없는 월드 레이어는 바탕만이다.
    struct LayerThumbnailDesc
    {
        TextureHandle target;
        Extent2D extent;
        LayerId layer = InvalidLayerId;
        float clearColor[4] = {0.08f, 0.09f, 0.11f, 1.0f};
    };

    struct FrameworkContext
    {
        JMemoryContext memory;
        AssetSystem* assets = nullptr;
        Renderer* renderer = nullptr;
        // 호스트가 소유하는 시계(D-242). **없으면 `Initialize` 가 거절한다.** 프레임 델타·고정 스텝 수·멈춤·한 프레임 진행이 모두
        // 여기서 온다 - 프레임워크는 누산기를 들지 않는다(두 프레임워크가 따로 들어 3D 만 멈춤을 무시했다, time-plan T4).
        System::TimeSystem* time = nullptr;
        // 호스트가 소유하는 디버그 선 저장소(D-243). 있으면 렌더 브리지가 뷰마다 선을 사각형으로 그린다. 없으면(시험) 그리지 않는다.
        const System::DebugDrawSystem* debugDraw = nullptr;
        // 호스트가 소유하는 네트워크(D-122). 있으면 프레임워크가 캔버스를 묶고 복제 풀과 수신·송신 시스템을 세운다.
        // 없으면(테스트의 가짜, 네트워크를 끈 호스트) 아무것도 세우지 않는다.
        NetworkHost* network = nullptr;
        // 호스트가 소유하는 오디오 시스템(D-197). 있으면 프레임워크가 소스·리스너 시스템을 세운다. 없으면(오디오를 끈
        // 호스트) 세우지 않는다 - 소스 컴포넌트는 그대로 읽히고 저장된다.
        System::AudioSystem* audio = nullptr;
        // 호스트가 소유하는 태스크 관리자(D-209). 있으면 폰트의 미리 뜨기가 워커에서 돈다. 없으면(테스트의 가짜) 메인 스레드에서 한 번에 뜬다.
        TaskManager* tasks = nullptr;

        // 호스트가 소유하는 게임 입력(D-214). 프레임워크는 스크립트의 레이어 체인을 이것에 내려보낸다.
        // 없으면(테스트의 가짜) 체인을 돌리지 않고, 스크립트의 폴링은 빈 입력을 본다.
        System::InputSystem* input = nullptr;
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
        // 이번 프레임의 화면 기준이다(D-237): 프로젝트의 기준 해상도와 게임이 그려지는 크기. 호스트가 `Update` 앞에서 부른다 - 화면 레이어의 앵커가
        // 그 프레임의 크기로 잰다(기존 엔진은 그린 뒤에 알려 첫 프레임이 0 이었다). 화면 레이어가 없는 프레임워크는 무시한다.
        virtual void SetScreenSpace(const ScreenSpaceFrame& frame)
        {
            (void)frame;
        }
        // 한 프레임을 돈다. 델타는 인자가 아니라 `FrameworkContext::time` 에서 읽는다 - 호스트가 이미 `BeginFrame` 을 불렀다(D-242).
        virtual void Update() = 0;
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
        // 같은 프레임에 **레이어 하나의 썸네일**을 제출한다(D-287). `RenderEditorView` 처럼 `Render` 뒤에서만 부른다.
        virtual RenderResult RenderLayerThumbnail(const LayerThumbnailDesc& thumbnail)
        {
            (void)thumbnail;
            return RenderResult::NothingToSubmit;
        }
        virtual void Shutdown() = 0;
        // 캔버스의 컴포넌트가 든 에셋 아이디(`xxxId`)를 이번 실행의 핸들(`xxx`)로 푼다(asset-plan §2.6, D-115). 캔버스를
        // 읽은 뒤와 편집 뒤에 호스트·에디터가 부른다. 앞서 잡은 것은 놓고, 참조 수가 0 이 된 에셋은 내린다.
        // 캔버스나 에셋 시스템이 없는 프레임워크(테스트의 가짜)는 아무것도 하지 않는다.
        virtual void BindCanvasAssets()
        {
        }
        // 해석 패스가 볼 에셋 아이디를 모은다(D-236). 싣지 않는다 - 워커 로드가 캔버스를 열 때 무엇을 읽을지 알려고 쓴다.
        // 캔버스나 에셋 시스템이 없는 프레임워크는 아무것도 더하지 않는다.
        virtual void CollectCanvasAssetIds(Array<AssetId>& ids)
        {
            (void)ids;
        }
    };
}
