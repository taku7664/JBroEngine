# TODO

## Goal
Web build 에서 sprite 색상이 파랗게 보이는 RGBA/BGRA 채널 오류를 수정하고, 모바일 플랫폼 추가 계획을 문서화한다.

## Confirmed Facts
- RHI public texture format 은 현재 `ERHITextureFormat::RGBA8` 하나다.
- WebGPU `CreateTexture2D()` 는 `RHITexture2DDesc::Format` 을 보지 않고 항상 `WGPUTextureFormat_BGRA8Unorm` 으로 texture 를 만든다.
- Web screenshot 에서 캐릭터의 노란/살색 계열이 파랗게 보인다.
- Web swapchain 은 BGRA8 surface 를 사용할 수 있지만, asset texture upload 는 RGBA8 payload 와 맞아야 한다.

## Assumptions
- sprite/image payload 는 RGBA8 순서다. 이 가정은 RHI enum 이름과 stb 계열 image load 관례, Web 에서 빨강/파랑이 뒤집힌 증상으로 뒷받침된다.
- 이번 수정은 WebGPU texture upload 포맷만 고치고, RHI 포맷 enum 확장은 별도 과제로 둔다.
- 모바일 계획은 구현이 아니라 플랫폼 추가 전 설계/작업 분류 문서다.

## Success Criteria
- WebGPU asset texture 는 RGBA8 로 생성된다.
- Web build 가 다시 성공한다.
- 브라우저 WebGPU 오류 오버레이가 표시되지 않는다.
- 모바일 플랫폼 추가 계획 문서가 상세 단계/계약/리스크/검증 항목을 포함한다.

## Plan
- [x] WebGPU texture format mapping 을 추가한다.
- [x] Web package 를 재빌드하고 HTTP/브라우저 상태를 확인한다.
- [x] `Debug_Game|x64` 와 diff check 를 실행한다.
- [x] 모바일 플랫폼 추가 계획 문서를 작성한다.
- [x] `tasks/BuildPipelineDesign.md` 에 Web texture format 주의점을 갱신한다.

## Verification
- [x] `BuildWeb.ps1 -Project ... -Configuration Release -OutputRoot ... -Clean`
- [x] Web package HTTP response check
- [x] 브라우저 오류 오버레이 확인
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
### Read / Thought / Counterexample / Fix
- 코드를 읽었고: `ERHITextureFormat` 은 현재 `RGBA8` 하나인데 WebGPU `CreateTexture2D()` 는 항상 `BGRA8Unorm` texture 를 생성하고 있었다.
- 어떻게 생각했고: screenshot 의 파란 sprite 는 clear color 문제가 아니라 RGBA payload 를 BGRA texture 로 해석한 red/blue channel swap 증상과 일치한다.
- 어떤 반례를 찾았고: WebGPU swapchain 은 `BGRA8Unorm` 이 맞을 수 있으므로 surface format 까지 RGBA 로 바꾸면 안 된다.
- 어떻게 고쳤다: swapchain format 은 유지하고, asset texture 생성만 `RHITexture2DDesc::Format` 을 `WGPUTextureFormat_RGBA8Unorm` 으로 매핑하게 했다.

### Changed
- `Engine/Core/RHI/WebGPU/WebGPURHIDevice.cpp`
  - `ERHITextureFormat::RGBA8` -> `WGPUTextureFormat_RGBA8Unorm` 매핑 추가.
  - asset texture 생성 시 하드코딩된 `BGRA8Unorm` 제거.
- `tasks/BuildPipelineDesign.md`
  - WebGPU swapchain format 과 asset texture format 을 분리해야 한다는 주의점 추가.
- `tasks/MobilePlatformPlan.md`
  - Android/iOS 모바일 플랫폼 추가 계획 작성.

### Verified
- 실제 프로젝트 `C:\Users\박주형\Desktop\Project\Project.Jproject` Web Release 빌드 성공.
- `http://127.0.0.1:8123/` 에서 Web package 핵심 파일 HEAD 응답 200 확인.
- 새 브라우저 탭에서 오류 오버레이 `display: none`, 오류 텍스트 비어 있음을 확인.
- `Debug_Game|x64` 빌드 성공.
- `git diff --check` 통과. CRLF 경고만 존재.

### Not Verified
- WebGPU canvas 픽셀 색상 자동 판독/스크린샷은 도구 한계가 있어 육안 최종 확인이 필요하다.
