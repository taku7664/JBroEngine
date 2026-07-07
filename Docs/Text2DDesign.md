# Text2D 설계

## 범위와 사용자 계약

`Text2D`는 UTF-8 한글·영문/라틴을 표시하는 월드 공간 2D 컴포넌트다. Windows D3D11과 WebGPU가 같은 폰트 데이터, 셰이핑, 레이아웃 규칙을 사용한다. 엔진 내장 폰트와 에디터 임시 대체 폰트는 제공하지 않는다.

폰트 탐색 순서는 컴포넌트의 `FontFamilyGuid`, 프로젝트 `DefaultFontFamilyGuid`, Family 선행 fallback, 프로젝트 공통 fallback, 선택된 face의 replacement glyph 순서다. 유효한 Font Family가 없으면 렌더 아이템을 만들지 않고 Inspector에 지속 경고, 콘솔에 오브젝트별 1회 경고를 표시한다. 사용자가 배포하는 폰트의 라이선스 확인 책임은 프로젝트에 있다.

## 공개 데이터

- `Text`, `FontFamilyGuid`, `FontStyle`
- `FontSizePixels`, `WidthPixels`, `HeightPixels`
- `OverflowMode` (`Overflow`, `Wrap`, `Clip`)
- `AutoSizeEnabled`, `MinFontSizePixels`, `MaxFontSizePixels`
- 수평 `Left/Center/Right`, 수직 `Top/Middle/Baseline/Bottom`
- `LineSpacing`, `LetterSpacingPixels`
- 독립 RGBA `FillEnabled/FillColor`, `OutlineEnabled/OutlineColor/OutlineWidthPixels`
- `PixelSnap`, `Offset`, `SortOrder`, `LayerMask`

Font Size와 레이아웃 크기는 저작 픽셀을 프로젝트 PPU로 나눈 로컬 단위다. CRLF와 CR은 LF로 정규화한다. `Overflow`는 명시적 개행만, `Wrap`은 지정 폭 줄바꿈과 자동 높이, `Clip`은 지정 영역에 맞춘 쿼드/UV 절단을 사용한다. Auto Size는 Clip에서만 Min~Max 범위를 축소 탐색한다.

## 폰트 자산

`FontFaceAsset`은 TTF/OTF 원본 바이트, face index, generation을 소유한다. `FontFamilyAsset`은 Regular, Bold, Italic, BoldItalic face와 Family 선행 fallback, 프로젝트 fallback 사용 여부를 저장한다. 스타일 face가 없으면 Regular를 사용한다. `.ttf`와 `.otf`는 Asset Browser에서 자동으로 FontFace로 등록되고 `.jfontfamily`는 FontFamily로 등록된다.

FreeType 2.13.3은 FTL, HarfBuzz 12.3.2는 MIT 라이선스로 소스 포함한다. 원문 고지는 각각 `Engine/ThirdParty/freetype/LICENSE.TXT`와 `Engine/ThirdParty/harfbuzz/COPYING`에 보존한다.

## 레이아웃과 캐시

FreeType이 glyph metrics와 SDF bitmap을 만들고 HarfBuzz가 glyph id, advance, offset을 계산한다. fallback 후보는 순환 방문을 차단한다. 레이아웃 결과는 RHI와 분리된 CPU 데이터이며 문자열·폰트·레이아웃 옵션이 바뀔 때만 다시 만든다. Transform, 색, 외곽선 폭, 정렬 순서는 기존 메시를 재사용한다.

face마다 1024x1024 RGBA8 아틀라스 페이지를 두고 필요한 glyph만 동기 생성한다. shelf allocator가 고정 UV를 배정하고 공간이 없으면 기존 페이지를 재배치하지 않고 새 페이지를 만든다. 한 문자열이 여러 페이지를 쓰면 같은 Entity를 가진 페이지별 RenderItem으로 제출한다.

## RHI와 렌더링

`IRHIDevice::UpdateTexture2D`는 위치, 크기, 데이터, row pitch를 받는다. D3D11은 box `UpdateSubresource`, WebGPU는 `wgpuQueueWriteTexture`, Vulkan은 staging buffer와 image barrier/copy로 구현한다.

Text 전용 HLSL/WGSL은 SDF에서 fill과 outline coverage를 한 draw로 합성해 반투명 중첩을 막는다. 실제 텍스트 bounds가 컬링, 클릭 선택, contour에 사용된다. 빈 문자열, 양쪽 스타일이 완전 투명한 경우, 유효 폰트가 없는 경우에는 제출하지 않는다.

## 엔진 통합

- FontFace/FontFamily loader와 자산 타입
- Text2D 리플렉션, YAML, prefab/복사 경로, SDK 공개 헤더
- 앱 런타임, 에디터 씬, 자산 씬의 `CTextRenderSystem`
- Project Settings의 기본 Family와 순서 보존 fallback 목록
- 빌드 manifest의 폰트 설정과 Windows/Web 소스 목록
- Scene View 실제 bounds 선택과 contour

게임 DLL의 별도 문자열 호출 API는 `(const char*, length)` 입력과 caller-owned buffer 출력 형태를 사용해야 한다. 컴포넌트 직렬화 저장 형식은 UTF-8 문자열이다.

## 검증 기준

- 직접 Family와 프로젝트 기본 Family의 우선순위, 모두 없을 때 미렌더링/경고
- 한글 음절·자모, 영문, 숫자, 잘못된 UTF-8와 replacement glyph
- fallback 순서와 순환 방지, 네 style과 Regular 대체
- LF/CRLF, Overflow/Wrap/Clip, Auto Size 경계
- fill/outline 독립 RGBA, 폭 0, Transform과 정렬·레이어
- atlas 중복 요청, 다중 페이지, scene 전환과 폰트 reload
- 저장·재로드, prefab, 복사·붙여넣기, 선택/contour
- Windows 패키지 실제 실행과 Web 소스/패키지 검증

## 제외 범위

Rich Text, caret/selection 입력기, 양쪽 정렬과 자동 하이픈, 엔진 내장 기본 폰트, 컬러 emoji/SVG glyph, 곡선·3D 조명 텍스트, 비동기 glyph 생성, MSDF는 초기 범위에서 제외한다.
