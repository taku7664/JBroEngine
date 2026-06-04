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
- 현재 엔진 Vulkan 코드는 모바일 매크로 내부가 많아서 Windows 빌드만으로 실제 Vulkan 경로 전체를 검증한 것은 아니다.

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
- `CVulkanRHIDevice`
- `CVulkanSwapchain`
- `CVulkanCommandContext`
- `CVulkanBuffer`
- `CVulkanTexture`
- `CVulkanSampler`
- `CVulkanProgram`
- `CVulkanGraphicsPipeline`

문서:

- `tasks/MobilePlatformPlan.md` 에 모바일/Vulkan 방향과 남은 작업 기록됨.

## 아직 안 된 것

Vulkan SDK / Windows 프로젝트 연결:

- `$(VULKAN_SDK)\Include` include path 추가 필요
- `$(VULKAN_SDK)\Lib` library path 추가 필요
- `vulkan-1.lib` link 추가 필요
- 현재는 Windows Vulkan 런타임 확인만 했고 엔진 프로젝트 링크 설정은 아직 붙이지 않았다.

Vulkan backend 완성:

- descriptor set / descriptor layout
- uniform buffer binding
- texture/sampler binding
- initial texture data upload
- image layout transition
- staging buffer
- SPIR-V shader cook pipeline
- pipeline vertex input mapping
- render target texture path
- validation layer / debug utils

Mobile package:

- Android Gradle template
- Android native entry / JNI / `ANativeWindow` 연결
- Android NDK Vulkan compile/link
- iOS Xcode template
- iOS MoltenVK include/lib/framework 연결
- iOS `CAMetalLayer` / MoltenVK surface 연결

## 다음 작업 순서 추천

1. Windows Vulkan SDK 를 엔진 프로젝트에 연결한다.
   - `Engine.vcxproj` 또는 별도 props 에 `$(VULKAN_SDK)\Include`, `$(VULKAN_SDK)\Lib`, `vulkan-1.lib` 추가.
   - 하드코딩 `C:\VulkanSDK\1.4.350.0` 보다 `$(VULKAN_SDK)` 를 우선한다.

2. Vulkan 코드를 Windows에서도 컴파일 가능한 형태로 확장한다.
   - 현재는 `JBRO_PLATFORM_MOBILE` guard 때문에 Windows Vulkan compile 검증이 제한적이다.
   - Windows Vulkan backend까지 열지 여부는 결정 필요.
   - 단순 모바일용이면 Android NDK compile 로 검증해야 한다.

3. shader cook 계약을 잡는다.
   - runtime `RHIProgramDesc::Source` 에 SPIR-V binary pointer, `SourceSize` 에 byte size 를 넣는 방향.
   - `glslangValidator` 또는 `glslc` 를 build/cook 단계에서 호출한다.

4. Vulkan descriptor binding 을 구현한다.
   - 현재 SpriteRenderer2D 경로는 constant buffer, texture, sampler 를 바인딩한다.
   - Vulkan 은 descriptor set layout / pool / set update 가 필요하다.

5. Android 최소 surface path 를 만든다.
   - Android Activity/JNI 에서 `ANativeWindow*` 를 `CMobilePlatform::SetNativeSurfaceHandle` 로 전달.
   - surface size 변경 시 `ResizeSurface`.
   - pause/resume 시 `NotifyPause` / `NotifyResume`.

## 검증 이력

최근 확인:

- `Debug_Game|x64` MSBuild 성공.
- 변경 범위 `git diff --check` 통과.
- 전체 `git diff --check` 는 빌드 산출물 `Build/Debug_Editor/Localization/ko-KR.yaml` trailing whitespace 로 실패한 적이 있음.

기존 경고:

- `yaml-cppd.pdb` 관련 LNK4099 경고는 계속 보인다.

## 주의할 점

- `tasks/todo.md` 는 현재 다른 작업(ObjectId 제거) TODO 로 바뀌어 있으므로 Vulkan 작업 메모로 덮어쓰지 말 것.
- 사용자는 임시 설계를 싫어한다. Vulkan 작업도 “이름만 추가”로 끝내면 안 된다.
- 다만 Android/iOS 실기기 검증 없이 “완성”이라고 말하면 안 된다.
- 질문성 발화와 구현 지시를 구분할 것. 사용자가 묻는 경우 바로 구현하지 말고 판단을 먼저 답한다.
