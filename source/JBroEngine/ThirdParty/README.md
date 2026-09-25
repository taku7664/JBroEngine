# ThirdParty

외부 라이브러리를 **소스째로 넣어 둔다.** 패키지 매니저도 서브모듈도 쓰지 않는다.

## 왜 소스를 직접 넣나

- **빌드가 재현된다.** 리포를 받은 시점에 필요한 것이 전부 있다. 네트워크도, 별도 받기 단계도 없다
- **버전이 흔들리지 않는다.** 올리는 것은 명시적인 커밋 하나이고, diff 로 무엇이 바뀌었는지 보인다
- 기존 엔진(`source/repos/JBroEngine/Engine/ThirdParty`)도 같은 방식이었다

대가는 리포 크기다. ImGui 하나가 4MB 쯤 된다. 그 정도는 받아들인다.

## 규칙

- **고치지 않는다.** 수정이 필요하면 그 이유와 함께 이 문서에 적고, 올릴 때마다 다시 적용해야 한다는 것을 알고 한다. 지금은 수정본이 없다
- **쓰는 것만 넣는다.** 예제, 다른 백엔드, 안 쓰는 헬퍼는 가져오지 않는다
- 라이선스 파일을 함께 둔다
- 각 라이브러리는 자기 빌드 단위(`.vcxproj`)를 갖는다. 엔진 모듈과 섞지 않는다

## include 규칙의 예외

`docs/ProjectRule.md` 는 모듈 공개 헤더를 `<JBro/...>` 로만 참조하게 한다.
**서드파티는 그 규칙 밖이다** — `<imgui.h>` 처럼 그 라이브러리가 정한 이름으로 include 한다.
래핑하지 않는다. 래퍼는 그 라이브러리를 올릴 때마다 같이 고쳐야 하는 두 번째 표면이 된다.

대신 **어느 모듈이 서드파티를 보는지는 그대로 통제된다.** 그 모듈의 `.vcxproj` 가
include 경로를 선언해야 하고, 선언하지 않은 모듈에서 include 하면 C1083 이다.
엔진 모듈 사이의 경계와 같은 방식이다.

## 들어 있는 것

### imgui — v1.92.9b-docking, MIT

에디터 UI 용이다(D-60). `IMGUI_HAS_DOCK` 이 있는 docking 브랜치다 —
도킹은 아직 master 에 합쳐지지 않았다(2026-09 확인).

**브랜치 HEAD 가 아니라 태그를 쓴다.** docking 브랜치 HEAD 는 `1.93.0 WIP` 인데 WIP 는
언제 무엇이 깨졌는지 알 수 없는 지점이다. 곧 백엔드를 쓰면서 "ImGui 버그인가 우리 버그인가"
를 가려야 하는데, 움직이는 바닥 위에서는 그 판단이 배로 어렵다. 태그는 저자가 여기까지는
괜찮다고 한 지점이다.

올릴 때는 같은 방식으로 `vX.Y.Z-docking` 태그에서 받는다:

```
https://raw.githubusercontent.com/ocornut/imgui/v1.92.9b-docking/<file>
```

⚠ `IMGUI_VERSION_NUM >= 19198` 부터 `IMGUI_HAS_TEXTURES` 가 있다 — 백엔드가 텍스처를
직접 만들고 지우는 새 방식이다. 기존 엔진의 `imgui_impl_dx11`(1.92.7)을 참고할 때
이 부분은 다를 수 있다.

코어만 가져왔다:

```
imconfig.h  imgui.h  imgui_internal.h
imgui.cpp  imgui_draw.cpp  imgui_tables.cpp  imgui_widgets.cpp
imstb_rectpack.h  imstb_textedit.h  imstb_truetype.h
imgui_demo.cpp
```

**백엔드는 가져오지 않았다.** 기존 엔진에 있던 것은 `imgui_impl_dx11` 인데 이 엔진은 D3D12 이고,
무엇보다 렌더는 `JBroRHI` 를 거쳐야 한다. 백엔드는 그 RHI 위에 직접 쓴다 —
`imgui_impl_dx12` 를 쓰려면 RHI 가 감춰 둔 D3D12 핸들을 도로 꺼내야 하고,
그러면 추상화를 뚫는 구멍이 하나 생긴다.

`imgui_demo.cpp` 는 남겨 두었다. 백엔드를 붙일 때 **데모 창 하나가 전체 경로를 증명한다.**

`imgui_stdlib`(std::string 헬퍼)와 `imgui_impl_win32` 는 필요해질 때 가져온다.

### stb — stb_image.h v2.30, 퍼블릭 도메인 / MIT

`JBroAsset` 의 이미지 디코더다(D-111). `ImageDecoder.cpp` 한 곳에서만 구현을 켜고(`STB_IMAGE_IMPLEMENTATION`),
PNG·JPEG·BMP·TGA 만 남긴다(`STBI_ONLY_*`). 파일은 열지 않는다(`STBI_NO_STDIO`) - 바이트는 플랫폼이 읽어 온다(D-112).
에디터의 임포트 경로에서만 돈다. 기존 엔진(`Engine/ThirdParty/stb`)에서 같은 판을 가져왔다. 헤더 하나라 빌드 단위는
따로 없다(Vulkan-Headers 와 같다).

`stb_truetype.h` v1.26 은 텍스트 커널 `JBroText` 의 폰트 읽기다(D-200). `github.com/nothings/stb` 의 master 에서 2026-09-25 에
받았다. `FontFace.cpp` 한 곳에서만 구현을 켠다(`STB_TRUETYPE_IMPLEMENTATION`). ImGui 에도 같은 판의 사본(`imgui/imstb_truetype.h`)이
있지만 ImGui 가 고친 것이고 `stbtt_` 이름이 겹치지 않게 `static` 으로 묶여 있어 가져다 쓰지 않는다. TTF·OTF(CFF)·`kern` 표·GPOS
쌍 조정(조회 형식 2, 부표 형식 1·2)을 읽는다. GPOS 확장 조회(형식 9)와 결합 문자 위치(mark), 합자(GSUB), 힌팅은 없다 - text-plan §3.4.
고친 것은 없다.

### Vulkan-Headers — SDK 1.4.350.0, Apache-2.0

`JBroVulkanRHI` 의 API 헤더다(D-108). `vulkan-1.dll` 은 실행 시간에 열므로 SDK 의 가져오기 라이브러리는
필요 없고, 헤더만 여기서 읽는다 - 그래서 클론이 Vulkan SDK 없이 빌드된다. 자세한 것은 폴더의 README 에.

### miniaudio — v0.11.25, MIT-0 / 퍼블릭 도메인

오디오다(D-197·D-198). **자기 빌드 단위가 있다**(`miniaudio/miniaudio.vcxproj`) - 구현 번역 단위 `miniaudio.cpp` 하나를
`JBroAudio`(믹서의 `ma_engine`)·`JBroAsset`(디코더)·`JBroPlatform`(출력 장치 `ma_device`) 셋이 함께 링크하므로, 구현이 모듈마다
있으면 중복 정의다. 설정 매크로는 `JBro.Common.props` 의 `JBroMiniaudioDefines` 한 곳이고 이 셋과 구현이 같은 값을 받는다 -
`ma_engine` 같은 구조체의 모양이 매크로에 따라 달라진다. 리소스 매니저는 끈다(파일을 스스로 열고 작업 스레드를 띄운다).
기존 엔진(`Engine/ThirdParty/miniaudio`)에서 같은 판을 가져왔다. 고친 것은 없다.

OGG(Vorbis)는 miniaudio 가 내장하지 않아 `stb/stb_vorbis.c`(v1.22)를 구현 번역 단위가 먼저 include 한다. miniaudio 가 그 디코더에
할당기를 넘기지 않으므로 Vorbis 스트리밍을 시작하면 CRT 에서 할당한다(audio-plan §3-1).
