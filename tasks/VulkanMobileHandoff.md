# Vulkan / Mobile Handoff

## 현재 목표

모바일 플랫폼 Android/iOS 를 위한 공통 Vulkan RHI 기반을 세우고, 이후 Android APK / iOS MoltenVK 패키징으로 이어갈 수 있게 한다.

중요한 방향:

- `Mobile` 은 Android/iOS 공통 런타임 계층이다.
- Android/iOS 는 entry, native surface, lifecycle, signing, packaging 에서 분리한다.
- Renderer/RHI 는 모바일 공통 목표를 Vulkan 으로 둔다.
- iOS 는 native Vulkan 을 직접 제공하지 않으므로 MoltenVK 가 필요하다.

## Vulkan SDK 확인 상태

확인일: 2026-06-04

현재 Codex 프로세스에서 확인됨:

- `VULKAN_SDK=C:\VulkanSDK\1.4.350.0`
- `C:\VulkanSDK\1.4.350.0\Include\vulkan\vulkan.h` 존재
- `C:\VulkanSDK\1.4.350.0\Lib\vulkan-1.lib` 존재
- `C:\VulkanSDK\1.4.350.0\Bin\glslangValidator.exe` 존재
- `glslangValidator --version` 정상
- `vulkaninfo --summary` 정상
- GPU: `NVIDIA GeForce RTX 2080`
- Vulkan Instance Version: `1.4.350`

주의:

- `vulkaninfo` 에서 일부 overlay layer API version warning 이 보인다.
- Vulkan SDK 는 `$(VULKAN_SDK)` 환경변수 기반으로 프로젝트에 연결했다. 하드코딩 경로를 쓰지 않는다.

## 이미 구현된 기반

플랫폼:

- `JBRO_PLATFORM_ANDROID`
- `JBRO_PLATFORM_IOS`
- `JBRO_PLATFORM_MOBILE`
- `EPlatformType::Android`
- `EPlatformType::IOS`
- `ERenderSurfaceType::MobileNativeSurface`
- `CMobilePlatform`
- `CMobileRenderSurface`

모바일 platform event 주입 API:

- `CMobilePlatform::RequestExit`
- `CMobilePlatform::SetFocus`
- `CMobilePlatform::SetNativeSurfaceHandle`
- `CMobilePlatform::ResizeSurface`
- `CMobilePlatform::NotifyPause`
- `CMobilePlatform::NotifyResume`

RHI:

- `ERHIApi::Vulkan`
- `ERHIProgramLanguage::SPIRV`
- `RHIProgramDesc::SourceSize`
- `JBRO_RHI_VULKAN`
- `CVulkanRHIDevice`
- `CVulkanSwapchain`
- `CVulkanCommandContext`
- `CVulkanBuffer`
- `CVulkanTexture`
- `CVulkanSampler`
- `CVulkanProgram`
- `CVulkanGraphicsPipeline`

이번 Vulkan 브랜치에서 추가로 완료한 것:

- `Engine.vcxproj`
  - `$(VULKAN_SDK)\Include` 추가.
- `Application.vcxproj`
  - `$(VULKAN_SDK)\Include` 추가.
  - `$(VULKAN_SDK)\Lib` 추가.
  - `vulkan-1.lib` 링크 추가.
- Windows 에서도 Vulkan 코드가 컴파일되도록 `JBRO_RHI_VULKAN` 가드를 도입.
- Windows Vulkan surface 생성 경로 추가.
  - `ERenderSurfaceType::Win32Hwnd`
  - `VK_KHR_win32_surface`
  - `vkCreateWin32SurfaceKHR`
- Vulkan descriptor set 기반 sprite binding 구현.
  - binding 0: uniform buffer
  - binding 1: sampled image
  - binding 2: sampler
- Vulkan command context에 per-frame descriptor pool reset 및 draw 직전 descriptor set update/bind 추가.
- Vulkan texture initial data upload 구현.
  - staging buffer
  - image layout transition
  - buffer-to-image copy
- Vulkan pipeline vertex input mapping 구현.
  - `RHIVertexElementDesc` -> `VkVertexInputAttributeDescription`
  - `ERHIPrimitiveTopology` -> `VkPrimitiveTopology`
- 기본 2D sprite shader용 SPIR-V 내장 헤더 추가.
  - `Engine/Core/Renderer/SpriteVulkanShaders.h`
  - `Forward2DRenderer` 는 Vulkan RHI에서 SPIR-V 바이너리를 사용.
- Vulkan offscreen render target path 추가.
  - `RenderPassDesc.ColorAttachment.Target` 이 있으면 `CVulkanTexture` framebuffer를 사용.
  - swapchain/offscreen 모두 같은 color render pass 계약을 사용.
  - pass 종료 후 swapchain image는 `PRESENT_SRC_KHR`, offscreen image는 `SHADER_READ_ONLY_OPTIMAL` 로 전환.
- Vulkan render target `ERHILoadOp::Load` 보존 경로 추가.
  - render pass 는 `LOAD` 기반으로 고정.
  - `ERHILoadOp::Clear` 는 `vkCmdClearAttachments` 로 명시 clear.
  - swapchain image layout 을 이미지별로 추적.
  - `CVulkanTexture` image layout 상태를 추적.
- Vulkan validation/debug utils 추가.
  - `RHIDesc::EnableDebugLayer` 가 true이고 layer/extension이 존재할 때만 활성화.
  - validation layer가 없는 환경에서는 instance creation을 실패시키지 않음.
  - debug callback은 엔진 `Log` 로 warning/error를 전달.

문서:

- `tasks/MobilePlatformPlan.md` 에 모바일/Vulkan 방향과 남은 작업 기록됨.

## 아직 안 된 것

Vulkan backend 완성:

- descriptor pool sizing / reuse 정책 고도화
- texture layout 상태 추적 고도화
- resize 시 swapchain recreation 안정화
- `vkAcquireNextImageKHR` / `vkQueuePresentKHR` 실패 코드 처리
- shader cook pipeline 자동화
  - 현재 기본 sprite shader 는 내장 SPIR-V 이다.
  - 일반 material/shader asset 은 아직 build/cook 단계에서 SPIR-V 로 변환하지 않는다.
- Android/iOS NDK/Xcode 실제 컴파일 검증

Mobile package:

- Android Gradle template
- Android native entry / JNI / `ANativeWindow` 연결
- Android NDK Vulkan compile/link
- iOS Xcode template
- iOS MoltenVK include/lib/framework 연결
- iOS `CAMetalLayer` / MoltenVK surface 연결

## 다음 작업 순서 추천

1. shader cook 계약을 잡는다.
   - runtime `RHIProgramDesc::Source` 에 SPIR-V binary pointer, `SourceSize` 에 byte size 를 넣는 방향.
   - `glslangValidator` 또는 `glslc` 를 build/cook 단계에서 호출한다.
   - 기본 sprite shader 는 이미 내장 SPIR-V 로 연결됨.
   - material/shader asset 은 아직 cook 대상이 아님.

2. Vulkan render target path 후속 검증.
   - clear/store offscreen path 와 `ERHILoadOp::Load` 보존 경로는 구현됨.
   - 실제 Vulkan runtime smoke 로 validation warning 여부 확인 필요.

3. Android 최소 surface path 를 만든다.
   - Android Activity/JNI 에서 `ANativeWindow*` 를 `CMobilePlatform::SetNativeSurfaceHandle` 로 전달.
   - surface size 변경 시 `ResizeSurface`.
   - pause/resume 시 `NotifyPause` / `NotifyResume`.

4. Android NDK 빌드 설정을 만든다.
   - Vulkan headers/libs 는 Android NDK의 Vulkan을 사용해야 한다.
   - Windows `$(VULKAN_SDK)\Lib\vulkan-1.lib` 를 Android 링크에 재사용하면 안 된다.

5. iOS MoltenVK 연결 방식을 확정한다.
   - iOS 는 native Vulkan 이 없으므로 MoltenVK framework/include/link 계약이 필요.

## 검증 이력

최근 확인:

- `Debug_Game|x64` MSBuild 성공.
- `Release_Game|x64` MSBuild 성공.
- Vulkan render target/debug utils 추가 후 `Debug_Game|x64` MSBuild 성공.
- Vulkan render target/debug utils 추가 후 `Release_Game|x64` MSBuild 성공.
- Vulkan `ERHILoadOp::Load` 보존 경로 추가 후 `Debug_Game|x64` MSBuild 성공.
- Vulkan `ERHILoadOp::Load` 보존 경로 추가 후 `Release_Game|x64` MSBuild 성공.
- 변경 범위 `git diff --check` 통과.
- 전체 `git diff --check` 는 빌드 산출물 `Build/Debug_Editor/Localization/ko-KR.yaml` trailing whitespace 로 실패한 적이 있음.

이번 작업 중 발견/수정한 컴파일 반례:

- `CVulkanRHIDevice::CreateGraphicsPipeline` 에서 `OwnerPtr::IsValid()` 를 호출하고 있었다.
  - `OwnerPtr` 는 `operator bool()` / `Get()` 스타일이다.
  - Windows Vulkan compile 가드를 열면서 발견했고 `!m_rhiSwapchain` 로 수정했다.
- `Engine.vcxproj` 는 원래 UTF-8 BOM 파일이다.
  - 기계 치환으로 BOM 이 빠질 수 있으므로 수정 후 BOM 유지 여부를 확인해야 한다.

기존 경고:

- `yaml-cppd.pdb` 관련 LNK4099 경고는 계속 보인다.

## 주의할 점

- `tasks/todo.md` 는 현재 다른 작업(ObjectId 제거) TODO 로 바뀌어 있으므로 Vulkan 작업 메모로 덮어쓰지 말 것.
- 사용자는 임시 설계를 싫어한다. Vulkan 작업도 “이름만 추가”로 끝내면 안 된다.
- 다만 Android/iOS 실기기 검증 없이 “완성”이라고 말하면 안 된다.
- 질문성 발화와 구현 지시를 구분할 것. 사용자가 묻는 경우 바로 구현하지 말고 판단을 먼저 답한다.
