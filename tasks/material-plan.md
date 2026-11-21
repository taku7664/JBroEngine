# 재질 계획 (D-399)

> 상태: **완료** (2026-11-17 시작, 2026-11-18 다섯 단계 완료). 남은 것은 §4. todo-3d 우선순위 4. 사용자와 토론해 정한 것(2026-11-17, 사용자: "추천대로 가자"): 아래 §2 의 여덟 항목.
> 에디터 화면은 2D 를 따르고, 3D 만의 것이라 달라야 하면 먼저 묻는다(재질 미리보기 구는 물었고 뒤로 미뤘다).
> **2026-11-19 main 통합(D-406)**: 이 계획의 재질은 이제 "표준 머티리얼" 이다. 로컬 main 의 셰이더 머티리얼(D-347, 사용자 `.hlsl`)과 같은 `.jmat`·같은 이름이 겹쳐 이름을 갈랐다 -
> 렌더러 `RegisterMaterial`·`UpdateMaterial`·`UnregisterMaterial`·`GetMaterialCount` → `...MeshMaterial...`, `MeshSubmit::material` → `meshMaterial`, 3D 프레임워크의 `MaterialLibrary` → `MeshMaterialLibrary`,
> 에디터 `CreateMaterialAsset` → `CreateStandardMaterialAsset`(메뉴 `새 표준 머티리얼`). 빌트인 셰이더의 블록 자리는 머티리얼 b1, 라이트 b2 이고 `Standard` 는 `Light3D` 로 빛을 받는다. 화면 말은 "머티리얼" 이다.

## 1. 지금 있는 것 (2026-11-17 실측)

- **RHI 는 준비되어 있다.** 상수 버퍼 슬롯 b1~b7·프레임 슬롯마다의 업로드 링·장치 상한(D-325), mip 첨부와 읽기(D-326·D-327), sRGB 포맷(`RGBA8UnormSrgb`)과
  텍스처의 `mipLevels`. Vulkan 은 그리기 시험만 남았다. todo-3d 가 적은 "RHI 의 상수 버퍼 바인딩이 먼저" 는 풀려 있다.
- 셰이더는 매니페스트 한 곳(`ShaderManifest.psd1`)이 세 바이트코드를 굽고 프로그램 표를 낸다(D-329). 자료형은 `ShaderData`. `.jshader` 에셋 파일은 없다.
- 메시 셰이더(`BuiltinMesh`)는 정점이 위치·법선뿐(렌더러의 `MeshVertex`, 24 바이트)이고, 빛은 고정 방향광 하나 + 주변광, 색은 인스턴스 색이다.
- `MeshRenderer3D::materialId`·`material` 칸이 있지만 아무도 읽지 않는다. `MaterialAsset`·`ShaderAsset` 은 빈 껍데기이고 확장자 `.jmat` 만 등록되어 있다.
- 모델 임포트(D-391)가 재질의 이름·기본색·metallic·roughness·그림 칸 다섯(기본색·노멀·MR·AO·발광)과 그림의 첫 쓰임(색 공간의 `Auto`)을 CPU 자료에 읽어 둔다.
  하위 Material 레코드가 있고, 내장 그림은 하위 Texture, 바깥 그림은 일반 Texture 에셋이다. CPU 정점(`ModelVertex`)은 위치·법선·UV·탄젠트(48 바이트)다.
- 텍스처 옵션 `colorSpace`·`mipmaps` 는 값만 저장되어 있다(D-392 (a)). 같은 그림의 묶음 열쇠(`AssetRecord::sharedImageKey`)는 에디터가 보일 뿐 아직 공유하지 않는다(D-396).
  (둘 다 이 계획에서 섰다 - 3 단계 D-402, 5 단계 D-404.)

## 2. 정한 것 (사용자 확인 2026-11-17)

1. **셰이딩 모델**: glTF 와 같은 **metallic-roughness PBR** 을 빌트인 `Standard` 셰이더로 둔다. 임포트한 재질의 값이 1:1 로 맞고, 지금의 고정 방향광 하나로도
   GGX 를 계산한다 - 조명 컴포넌트(todo-3d 우선순위 5)가 오면 빛만 바뀐다. 빛을 받지 않는 `Unlit` 도 둔다.
2. **재질의 출처는 둘**: 모델의 하위 **Material**(파일에서 온 것, 읽기 전용 - Unity 와 같다)과 사용자가 만들고 고치는 **`.jmat`**(브라우저 우클릭 `새 재질`).
   모델의 재질을 고치고 싶으면 "재질 꺼내기"(하위 Material 을 `.jmat` 로 복사)인데 뒤다.
3. **빈 칸은 메시의 재질**: `materialId` 가 비면 그 메시가 파일에서 쓰던 재질이고, 칸에 넣으면 덮어쓴다 - 모델의 메시를 고르기만 해도 텍스처가 입혀져 나온다.
   메시 자료(쿡된 블롭 포함)에 제 재질의 하위 아이디를 둔다. 재질이 없으면 회색 기본 재질(빈 메시가 정육면체인 것과 같은 원칙, D-390).
4. **셰이더는 빌트인만**(`Standard`·`Unlit`). 사용자 HLSL 은 postprocess-plan 11 단계(`.jshader`·에디터의 즉석 굽기)에서 Shader 에셋이 생기는 길이 하나 늘 뿐 재질은 바뀌지 않는다.
5. **정점에 UV·탄젠트**(렌더러 정점 24 → 48 바이트, `ModelVertex` 와 같은 모양). 텍스처 옵션의 효과를 켠다 - `colorSpace` 는 sRGB 뷰, `mipmaps` 는 밉 체인 만들기와 올리기.
   첫 판의 그림 칸은 **기본색·MR·발광**(AO 는 MR 과 같은 그림이면 함께), **노멀맵은 파일에 탄젠트가 있는 메시만** 쓴다(없는 파일의 계산은 MikkTSpace - D-392 의 남은 일).
6. **알파는 불투명과 컷아웃**(알파 테스트, 문턱). 반투명은 뒤에서 앞으로 정렬해야 해서 뒤다.
7. **에디터**: `.jmat` 인스펙터는 셰이더의 파라미터 표로 칸을 그리고 그림 칸은 기존 에셋 칸 위젯이다(2D 와 같다). **재질 미리보기 구와 썸네일은 뒤**(3D 고유 화면 -
   모델 미리보기 창과 함께, 그때 다시 묻는다). 첫 판은 색 견본 칸만이다.
8. **렌더러**: 그리기 묶음의 키를 (파이프라인, 재질, 메시)로 넓히고, 재질 값은 재질마다 상수 버퍼 한 덩어리(b1)로 링에 올린다. 인스턴스 색은 남아 재질 위에 곱한다.
   같은 그림을 한 번만 읽는 것(D-396 의 열쇠)을 여기서 붙인다.

## 3. 단계

각 단계는 빌드·시험(관련 묶음은 `JBRO_TEST_CASES` 로)·뮤테이션·문서 뒤 커밋한다. 픽셀 시험은 세 백엔드에서 잰다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | **정점 형식**: 렌더러 `MeshVertex` 에 UV·탄젠트(48 바이트), 메시 셰이더의 입력, `MeshLibrary` 가 모델 메시의 UV·탄젠트를 올림, 빌트인 도형 다섯에 UV(정육면체·평면은 면마다 0..1, 구·캡슐·원기둥은 경위도). 그림은 바뀌지 않는다(기존 픽셀 시험 그대로) | `[완료]` 2026-11-17 · D-400 · `MeshVertex` 48 바이트(UV 24·탄젠트 32, 속성 7·8 - 인스턴스 속성 2..6 의 번호를 밀지 않으려고 뒤에 붙였다), 모델 메시는 같은 모양이라 통째로 옮김(`static_assert`), 빌트인 다섯을 모두 `MeshBuilder` 로 짓고(정육면체도) 탄젠트는 `ComputeTangents` 한 곳, `MeshLibrary::BuildBuiltin` · 시험 `Framework3DSystem` 의 `TestBuiltinShapesCarryUvsAndTangents` · 이 단계에서 Vulkan SDK 1.4.363.1 을 이 기계에 설치했다(SPIR-V 를 다시 굽고, Vulkan 시험이 검증 레이어로 돈다) |
| 2 | **재질 자료**: `MaterialData`(셰이더·기본색·metallic·roughness·발광·알파 모드와 문턱·그림 칸) 와 `AssetSystem` 의 Material 풀. 출처는 모델의 하위 Material(그림 칸은 내장 그림의 하위 아이디, 바깥 그림은 레지스트리의 Texture)과 `.jmat`(YAML, 리플렉션). 메시 자료에 제 재질의 하위 아이디. 패키지 쿡(모델 재질도 게임이 모델을 읽지 않고 읽는다) | `[완료]` 2026-11-17 · D-401 · `MaterialDesc`·`.jmat`(`MaterialFile.h` - 리플렉션 표 하나가 파일의 키이고 3·4 단계 인스펙터의 칸이다), `AssetSystem` 의 `MaterialData` 풀(빌드의 `CookedMaterial` → 모델의 하위 Material → `.jmat` 차례), `MeshData::material`, `ModelImport` 의 `FindModelSubAssetId`·`FindModelMeshMaterialId`·`MakeModelMaterialDesc`(그림 칸: 내장 그림은 하위 Texture, 바깥 그림은 모델 폴더 기준 경로 - `..` 를 접는다), 디코더가 발광 색과 glTF `MASK`(컷아웃)를 읽음, 쿡된 메시 머리에 재질 아이디(16 → 32 바이트)와 블롭 `CookedMaterial` · 시험 `AssetSystem` 의 `TestMaterialsLoadFromModelsAndFiles`(재질 둘인 OBJ 포함), `Package` 의 모델 시험, `ModelDecoder` 의 발광·컷아웃 |
| 3 | **렌더러**: `Standard`(PBR·GGX·고정 방향광)·`Unlit` 셰이더, 재질 상수 버퍼(b1)와 그림 칸·샘플러, 묶음 키 (파이프라인, 재질, 메시), sRGB 뷰와 밉 체인(D-392 의 효과), 컷아웃. `MaterialLibrary`(Framework3DSystem, `MeshLibrary` 와 같은 모양)와 `MeshRender3DSystem` 의 고르기(칸 → 메시의 재질 → 기본 회색) | `[완료]` 2026-11-17 · D-402 · `BuiltinMesh.hlsl` 의 `Standard`(GGX·Schlick-Smith·Schlick, 고정 방향광, 선형에서 셈하고 sRGB 로 쌈)와 `Unlit`, 렌더러의 `RegisterMaterial`·`MeshMaterialDesc`·재질 상수 블록(b1)·그림 칸 t0..t4(기본색·발광·MR·노멀·차폐 - `Unlit` 은 앞의 둘만)·`TextureUploadOptions`(sRGB 뷰·밉 체인), 묶음 키 (재질, 메시, 차례) 정렬, 푸시 상수 80 B(카메라 위치·탄젠트 있음), `MaterialLibrary`(그림은 (아이디, sRGB)마다 하나를 나눠 씀), `MeshData::hasTangents`(쿡된 메시 깃발 2 번 비트), D3D12 의 mip 1 이상 올리기 · 시험 `MeshPixel` 의 `TestMaterialsDrawTheirPicturesAndValues`(세 백엔드)와 셰이더 식을 CPU 로 다시 쓴 `ExpectStandard` |
| 4 | **에디터**: 브라우저 우클릭 `새 재질`(`.jmat`), 재질 인스펙터(셰이더·파라미터·그림 칸, 색 견본), 모델의 하위 Material 은 읽기 전용으로 보임, `materialId` 칸 목록에 `.jmat` 와 `모델 / 재질` | `[완료]` 2026-11-18 · D-403 · 브라우저 우클릭 `새 재질`(`CreateMaterialAsset` - 기본값의 `.jmat` 을 만들고 고름), 인스펙터의 `재질` 블록(리플렉션 표 그대로 - 그림 칸은 `...TextureId` 라 Texture 목록, 색은 색 고르개가 견본), 편집은 **그 파일**을 쓰는 커맨드(`SetAssetMetaCommand` 를 `SetAssetFileCommand` 로 넓힘), 모델의 하위 Material 은 값을 보이되 회색·안내 한 줄(`DisableScope`), `FormLayout` 의 이름 칸이 표 폭의 절반까지(긴 이름은 잘리고 올리면 다 보임) · 시험 `EditorApplication` 의 `TestTheEditorMakesAndEditsMaterials`(화면 `material_inspector`·`material_model_inspector`) · 기존 엔진의 재질 화면은 이 기계에 소스가 없어 읽지 못했다(`C:\Users\박주형\...` 가 없음) - 같은 모양의 선례(폰트 패밀리·임포트 옵션)를 따랐다 |
| 5 | **같은 그림 한 번 읽기**(D-396 의 열쇠로 텍스처를 나눠 씀), 패키지 레코드에 이름·열쇠 채우기(D-398 의 남은 것), 3D 게임 빌드로 같은 그림이 나오는지 실측(todo-3d 우선순위 10 의 일부) | `[완료]` 2026-11-18 · D-404 · 세 자리에서 한 번: 에셋 시스템은 같은 열쇠의 그림이 실려 있으면 픽셀을 옮겨 받고(`CopySharedImage` - 모델을 다시 읽지 않는다, `TextureData::sharedImageKey`), 재질 라이브러리는 (아이디 또는 같은 그림 열쇠, sRGB)마다 렌더러 텍스처 하나를 나누되 재질마다 제 그림 핸들을 따로 들어 한쪽 옵션을 바꾸면 갈라진다, 쿡은 같은 열쇠의 그림을 한 번만 굽고 나머지는 레코드만(`CookReport::sharedTextures`) - 게임은 같은 열쇠의 블롭을 읽는다(`CookedImageOf`). 패키지의 하위 레코드는 모델 메타에서 느슨한 스캔과 같은 함수(`MakeModelSubRecord`)로 이름·열쇠를 채운다(`FillRegistry`, 형식은 그대로) · 시험 `AssetSystem` 의 `TestTheSameEmbeddedImageIsReadOnce`, `MeshPixel` 의 `TestA3DGameBuildDrawsWhatTheEditorDraws`(세 백엔드 - `BuildGame` 의 패키지만으로 그린 것이 느슨한 것과 바이트까지 같다, 에셋 9·약 5.3 KB) |

## 4. 열린 것 `[열림]`

- 재질 미리보기 구·썸네일 - 3D 고유 화면이라 모델 미리보기 창과 함께 할 때 묻는다.
- 재질 꺼내기(모델의 하위 Material → `.jmat`).
- ~~반투명(정렬)·양면~~ → 2026-11-21 D-443(`alpha: Blend`·`doubleSided`, glTF 의 `BLEND`·`doubleSided`). ~~노멀맵의 탄젠트 계산(MikkTSpace, 서드파티 확인 대상)은 남았다.~~ → 2026-11-21 D-444.
  반투명에서 남긴 것: 정렬은 오브젝트 자리 하나라 서로 파고드는 반투명 메시나 한 메시 안의 겹친 면은 차례가 틀릴 수 있다(순서 무관 투명도 없음). 섞기는 sRGB 로 싼 값끼리다(스프라이트와 같다).
  레이어가 다르면 레이어 차례가 먼저다(레이어마다 뷰). 셰이더 머티리얼은 반투명을 받지 않는다(늘 불투명).
- 사용자 셰이더 - postprocess-plan 11 단계.
- 조명 컴포넌트(방향광·점광) - todo-3d 우선순위 5, 새 컴포넌트라 사용자 확인 뒤. 그때까지 `Standard` 는 고정 방향광 하나다.
- 같은 그림의 **CPU 픽셀은 아직 그림마다 한 벌**이다(5 단계는 디코드·GPU 업로드·패키지 블롭을 한 번으로 줄였고, 에셋 시스템의 `TextureData` 는 복사다). 픽셀 버퍼를 나눠 쥐는 형이 생기면 줄인다.
- 3 단계에서 남긴 확인: 셰이더 식 뮤테이션, sRGB 밉의 선형 평균, ~~노멀맵 있는 시험 모델~~(D-444 의 `TestCalculatedTangentsBendTheNormalMap`), `materialId` 칸 목록의 시험.
  그림의 `colorSpace: Linear` 가 칸의 기본(sRGB)을 이기는 것은 2026-11-21 D-442 로 시험했다(`MeshPixel`, 회색 128 이 0.737 로 나온다). sRGB 밉의 선형 평균은 여전히 코드로만 봤다.
