# TODO

## Goal

모바일 플랫폼을 Android/iOS 공통 계층과 플랫폼별 계층으로 정리하고, 모바일 공통 목표 RHI를 Vulkan으로 잡아 기반 틀을 추가한다.

## Assumptions

- `Mobile`은 Android/iOS 공통 런타임 계층이고, Android/iOS는 entry/surface/signing/RHI에서 분리한다.
- Vulkan을 모바일 native 렌더러의 1차 방향으로 둔다.
- iOS는 native Vulkan이 직접 제공되지 않으므로 추후 MoltenVK 같은 Vulkan-to-Metal 계층이 필요하다.
- 이번 작업은 Vulkan device/swapchain/resource의 실제 렌더링 완성이 아니라, RHI 선택/컴파일/확장 지점이 생기는 기반 작업이다.
- Windows/Web 기존 빌드는 동작을 유지한다.

## Success Criteria

- 문서에 Mobile 공통 계층과 Android/iOS 분리 기준이 명확히 기록된다.
- `ERHIApi::Vulkan`과 `CVulkanRHIDevice` 기본 클래스가 추가된다.
- 모바일 플랫폼 compile 경로에서 Vulkan RHI를 선택하도록 연결된다.
- Vulkan resource/swapchain/command context/pipeline 기본 객체가 추가된다.
- 모바일 platform entry가 surface/focus/pause/resume/exit 이벤트를 주입할 API가 생긴다.
- Windows `Debug_Game|x64` 빌드가 깨지지 않는다.

## Plan

- [x] 모바일 공통/플랫폼별 분리 기준 문서 갱신
- [x] Vulkan RHI 최소 device 클래스 추가
- [x] Engine RHI 선택 경로에 mobile/Vulkan 연결
- [x] Engine project/filter에 Vulkan 파일 연결
- [x] Vulkan buffer/texture/sampler/program/pipeline/swapchain/command context 파일 추가
- [x] 모바일 platform event 주입 API 추가

## Verification

- [x] `Debug_Game|x64` 빌드
- [x] 변경 범위 `git diff --check`
- [x] Diff review

## Review

### Changed

- 코드를 읽었고: RHI enum은 `D3D11/WebGPU/WebGL2/None`까지만 있고, mobile compile 경로는 RHI가 `EmptyRHIDevice`로 빠졌다.
- 어떻게 생각했고: `Mobile` 하나로 플랫폼을 합치더라도 renderer backend는 명확한 공통 목표가 필요하며, Vulkan으로 통일하면 Android/iOS 모두 같은 RHI 계약을 바라볼 수 있다고 판단했다.
- 어떤 반례를 찾았고: iOS는 Vulkan을 OS 기본 API로 직접 제공하지 않으므로, Vulkan 통합 방향은 MoltenVK 같은 Vulkan-to-Metal 계층 없이는 실제 iOS에서 성립하지 않는다.
- 어떻게 고쳤다: `ERHIApi::Vulkan`, `CVulkanRHIDevice` 최소 backend, mobile compile 시 Vulkan RHI 선택 경로, VS project/filter 연결을 추가하고 문서에 iOS MoltenVK 전제를 명시했다.
- 코드를 읽었고: `CMobilePlatform`은 OS entry에서 surface/focus/lifecycle 이벤트를 주입할 API가 없었다.
- 어떻게 생각했고: Android Activity/iOS AppDelegate가 붙어도 엔진에 native surface와 pause/resume을 전달할 수 없으면 플랫폼 구현이 막힌다고 봤다.
- 어떤 반례를 찾았고: 앱 pause 중에도 focused=true로 tick/render가 계속 돌거나, surface recreate 후 RHI resize가 갱신되지 않는 반례가 있었다.
- 어떻게 고쳤다: `RequestExit`, `SetFocus`, `SetNativeSurfaceHandle`, `ResizeSurface`, `NotifyPause`, `NotifyResume`을 추가했다.
- Vulkan RHI 객체 계층을 `Buffer/Texture/Sampler/Program/Pipeline/Swapchain/CommandContext`로 분리했다.
- Vulkan shader program은 문자열 GLSL 임의 컴파일이 아니라 `ERHIProgramLanguage::SPIRV`와 `RHIProgramDesc::SourceSize` 기반으로 받도록 계약을 명확히 했다.

### Verified

- `Debug_Game|x64` MSBuild 성공.
- 변경 범위 `git diff --check` 통과. LF/CRLF 변환 경고만 있음.
- 전체 `git diff --check` 는 빌드 산출물 `Build/Debug_Editor/Localization/ko-KR.yaml` 의 trailing whitespace 로 실패했다.

### Not Verified

- Android/iOS native Vulkan SDK, MoltenVK, surface/swapchain 생성은 아직 구현 전이므로 검증하지 않았다.
- 실제 모바일 기기/시뮬레이터 실행은 아직 패키징 단계가 없어 검증하지 않았다.
- SPIR-V shader cook, descriptor set binding, texture upload path는 아직 실제 렌더 결과로 검증하지 않았다.
- 전체 `git diff --check` 는 빌드 산출물의 기존/생성 whitespace 문제로 통과하지 못했다.

### Risks

- iOS Vulkan 방향은 MoltenVK 도입과 라이선스/배포/링크 방식 결정이 필요하다.
- 현재 Vulkan backend는 Windows 빌드에서 컴파일 게이트만 통과했다. Android/iOS NDK/MoltenVK 환경에서 추가 컴파일 검증이 필요하다.
- Vulkan descriptor set/texture upload는 다음 단계에서 완성해야 SpriteRenderer2D가 실제 텍스처를 샘플링할 수 있다.
