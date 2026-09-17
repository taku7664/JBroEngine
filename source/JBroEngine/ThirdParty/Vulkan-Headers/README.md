# Vulkan-Headers — SDK 1.4.350.0 (VK_HEADER_VERSION 350), Apache-2.0

`JBroVulkanRHI` 가 읽는 Vulkan API 헤더다(D-108). Vulkan SDK `1.4.350.0` 의 `Include/` 에서 그대로 가져왔고,
`vulkan_core.h` 가 include 하는 `vk_video/` 표준 헤더까지만 담았다. 쓰는 것만 넣는 규칙대로
`vulkan.hpp`(C++ 래퍼)·다른 플랫폼 헤더(`vulkan_xlib.h` 등)·`vk_layer.h`·`vk_icd.h` 는 가져오지 않았다.

**빌드에 Vulkan SDK 가 필요하지 않은 이유가 이 폴더다.** 모듈은 `vulkan-1.dll` 을 실행 시간에 열고
(`VK_NO_PROTOTYPES`, `VulkanLoader.h`), 헤더는 여기서 읽는다. SDK 가 필요한 것은 셰이더를 다시 굽는
`Compile.ps1`(SPIR-V 를 내는 dxc)뿐이다.

올릴 때는 같은 SDK 버전의 네 파일과 `vk_video/` 를 통째로 바꾸고 이 머리말의 버전을 고친다:

```
include/vulkan/vk_platform.h
include/vulkan/vulkan.h
include/vulkan/vulkan_core.h
include/vulkan/vulkan_win32.h
include/vk_video/*.h
```

라이선스: 각 파일 머리의 `SPDX-License-Identifier: Apache-2.0`(Khronos Group). 수정본은 없다.
