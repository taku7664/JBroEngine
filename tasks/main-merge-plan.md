# main 통합 계획 - 로컬 main 과 원격 main (2026-11-19)

> **상태(2026-11-19): 병합했다 - 결정은 `todo.md` 의 D-406.** §1~§6 은 병합 전 조사이고 그대로 둔다. 사용자가 정한 것과 실제 한 것은 §6·§7 에 있다.
> §1~§5 를 쓸 때는 읽기와 `git fetch origin` 한 번만 했고, 가상 병합은 스크래치 폴더의 임시 오브젝트 자리에서 돌렸다.

문장의 구분: **[확인]** 은 이번에 직접 명령·파일로 본 것, **[추정]** 은 본 것에서 미룬 것, **[제안]** 은 내 권고다.

## 1. 지금 상태

| 항목 | 값 | 근거 |
|---|---|---|
| 분기점 | `7e855869` (2026-10-15) | [확인] `git merge-base main origin/main` |
| 로컬 main | `71f2e0ff` (2026-11-05), 분기 뒤 135 커밋 | [확인] `git rev-list --left-right --count` |
| 원격 main | **`60631437` (2026-11-19 04:58 KST), 분기 뒤 22 커밋** | [확인] fetch 뒤. 처음 받은 값(`ad26dcd4`, 20 커밋)에서 둘 늘었다 - `e3bb7b7e` 와 그것을 합친 병합 커밋(D-352) |
| 로컬 135 커밋의 백업 | **어느 원격 브랜치에도 없다** | [확인] `git rev-list --count HEAD --not --remotes=origin` = 135. 이 PC 디스크에만 있다 |
| 다른 원격 브랜치 | `origin/ide`·`origin/main-ik9ugi`·`origin/postprocess` 는 모두 양쪽 main 의 조상 | [확인] 새 내용 없음 |
| 바뀐 파일 | 로컬 778(+123,030 / -4,792), 원격 232(+66,192 / -4,534) | [확인] `git diff --shortstat` |
| 양쪽이 모두 고친 파일 | **114** | [확인] 두 `--name-only` 의 교집합 |
| 가상 병합의 충돌 | **66 파일**(내용 61, 양쪽 새로 만듦 5). 나머지 48 은 글자로는 자동 병합 | [확인] `git merge-tree --write-tree main origin/main` |
| 원격 작업을 한 곳 | 이 PC 가 아닌 다른 PC | [추정] 원격 `todo-3d.md` 가 "이 PC 에서 처음 빌드하며"(VS 2026 Build Tools), 원격 D-347 이 "이 기계에 Vulkan SDK 1.4.363.1 을 설치" 라고 적었는데 이 PC 의 `C:\VulkanSDK` 에는 `1.4.350.0` 만 있다 |
| 디스크 여유 | C: 23 GB, F: 50 GB | [확인] 병합용 워크트리와 빌드는 F: 에 두는 것이 안전하다 |

### 1.1 워크트리 8 개

| 워크트리 | 브랜치 / HEAD | main 에 있음 | 미커밋 |
|---|---|---|---|
| `Documents/GitHub/JBroEngine` (여기) | `main` `71f2e0ff` | - | 미추적 `output/`, `새로운 1.txt` (손대지 않음) |
| `.codex/worktrees/009b` | detached `77a36cad` | 예 | 없음 |
| `.codex/worktrees/imgui-code-editor-spike` | `codex/imgui-code-editor-spike` `7183e3a7` | 예 | 미추적 `source/JBroEngine/Build/` |
| `.codex/worktrees/jbroscript-language-server` | `codex/jbroscript-language-server` `40e3584b` | 예 | 없음 |
| `.claude/worktrees/distracted-vaughan-3d9ecc` | detached `1f0259cb` | 예 | 없음 |
| `F:/AI/wt/JBroEngine-scriptqa` | **`script-qa-fixes` `b29233fd`** | **아니오 - 28 커밋이 main 에 없다** | 없음 |
| `F:/AI/wt/net-bench-8811812` | detached `88118124` | 예 | 없음 |
| `F:/AI/wt/net-bench-head` | detached `3c374755` | 예 | 없음 |

### 1.2 main 에 없는 로컬 브랜치 (처음 알려진 것에 없던 사실)

- **`script-qa-fixes`**: 로컬 main(`71f2e0ff`) 위에 28 커밋(모두 2026-11-17, jbroc·스크립트 QA). **결정 D-371~D-387 을 이미 쓴다.**
  원격이 고친 파일과 12 개가 겹친다(`EditorApplication.cpp`, `EngineInstance.cpp`, `ScriptSdkMain.cpp`, `todo.md` 등). 세 번째 갈래다.
- `codex/script-editor-next`: 4 커밋(2026-10-15)이 main 에 없다. 스크립트 편집기 패널 표시 관련이다. 이번 통합 범위 밖으로 본다.

## 2. 양쪽이 모두 고친 파일 114 개

`*` 는 가상 병합에서 충돌한 파일이다(66). 표시가 없는 파일은 글자로는 합쳐지지만 뜻까지 맞는지는 빌드와 시험으로 봐야 한다.

- **루트·문서 (8)**: `CLAUDE.md`, `README.md`, `docs/ProjectRule.md`, `tasks/cpp-script-plan.md`\*, `tasks/postprocess-plan.md`\*, `tasks/script-user-scenario.md`\*, `tasks/todo-3d.md`\*, `tasks/todo.md`\*
- **빌드·번역 (5)**: `JBroEngine.slnx`, `Localization/en-US.yaml`, `Localization/ko-KR.yaml`, `Modules/JBroEditorHost/JBroEditorHost.vcxproj`, `Modules/JBroGameHost/JBroGameHost.vcxproj`
- **JBroAsset·JBroAssetTypes (10)**: `Asset.h`\*, `AssetSource.h`\*, `MaterialFile.h`\*, `JBroAsset.vcxproj`\*, `Asset.cpp`\*, `AssetSource.cpp`, `AssetTypeRules.cpp`\*, `MaterialFile.cpp`\*, `AssetTypes.h`\*, `AssetTypesReflection.h`
- **JBroEditor (11)**: `EditorApplication.h`\*, `EditorIcons.h`\*, `LocalizationKeys.h`, `Widget/Basic.h`\*, `Widget/Common.h`, `EditorApplication.cpp`\*, `Panel/AssetBrowserPanel.cpp`\*, `Panel/InspectorPanel.cpp`\*, `Panel/InspectorPanel.h`\*, `Widget/Basic.cpp`, `Widget/Common.cpp`
- **JBroFramework3DSystem (6)**: `Framework3D.h`\*, `Rendering/MeshLibrary.h`\*, `System/MeshRender3DSystem.h`\*, `Framework3D.cpp`\*, `Rendering/MeshLibrary.cpp`\*, `System/MeshRender3DSystem.cpp`\*
- **JBroGraphics (12)**: `BuiltinShaders.generated.h`, `Renderer.h`\*, `Shaders/BuiltinMesh.hlsl`\*, `Shaders/ShaderManifest.psd1`\*, `BuiltinMeshPS/VS` 생성 헤더 6 개\*, `BuiltinShaders.generated.cpp`\*, `Renderer.cpp`\*
- **JBroHost·JBroPackage (6)**: `GameBuild.h`, `EngineInstance.cpp`, `GameBuild.cpp`, `PackageCook.h`\*, `PackageFormat.h`\*, `PackageCook.cpp`\*
- **RHI·플랫폼 (5)**: `JBroD3D12RHI/D3D12Resources.cpp`, `JBroPlatform/Platform.h`, `JBroPlatform/WindowsPlatform.h`, `JBroVulkanRHI/VulkanDevice.cpp`, `JBroVulkanRHI/VulkanLoader.h`
- **스크립트 (4)**: `JBroScriptApiCatalog2D/Catalog.cpp`, `Definitions.cpp`\*, `Enums.cpp`, `JBroScriptSdk/ScriptSdkMain.cpp`
- **Tests (46)**: 충돌 25 개 - `BuiltinShaderTests`, `CameraView2DTests`, `ComputeProbe{Fill,Halve,ReadValues}CS_SPV.generated.h`, `ComputeTests`, `ConstantBufferTests`, `EditorApplicationTests`, `EditorObjectCommandTests`, `EditorUITests`, `EditorWidgetTests`, `Framework3DSystemTests`, `GameScriptTests`, `InputChainTests`, `InputSystemTests`, `InputTests`, `Light2DPixelTests`, `MeshPixelTests`, `NetworkHostTests`, `ScriptBreakTests`, `ScriptDLLLoaderTests`, `ScriptLanguageServerTests`, `SubresourceTests`, `TextLayoutTests`, `TextRenderTests`.
  자동 병합 21 개 - `BuiltinComponentPropertyTests`, `CanvasFileTests`, `ComputeProbeWriteValuesCS_SPV`, `ConstantProbe{PS,VS}_SPV`, `ContextBoundaryTests`, `DebugAdapterTests`, `Framework2DSystemTests`, `JBroTests.vcxproj`, `RendererContractTests`, `ScriptCompilerCommandLineTests`, `ScriptModuleEntryTests`, `ScriptSchedulingTests`, `SpriteLibraryTests`, `SpritePixelTests`, `SubresourceProbe{Depth,Sample,Tint}PS_SPV`, `SubresourceProbeVS_SPV`, `TestMain.cpp`, `VolumeProbePS_SPV`
- **기타 (1)**: `ThirdParty/README.md`

## 3. 겹침 표

핵심은 하나다. **양쪽이 3D 메시와 재질을 같은 이름으로 따로 만들었다.** `.jmat`, `MaterialFile.h`, `MaterialLibrary`, `MaterialDesc`, `Renderer::RegisterMaterial`, `CookedMesh`, `WriteCookedMesh`, `CreateMaterialAsset`, 번역 키 `assets.new_material` 이 양쪽에 다 있고 뜻이 다르다.

| 영역 | 로컬 구현 | 원격 구현 | 남길 쪽 [제안] 과 이유 | 충돌 파일 |
|---|---|---|---|---|
| **메시 에셋** | D-366. `.gltf`·`.glb` 가 곧 `Mesh` 에셋. `nlohmann-json` 으로 직접 읽고 **노드 변환까지 구워 파일 하나를 메시 한 덩어리로** 싣는다. `.obj` 는 확장자만 받고 읽기는 실패. 재질·스킨·애니메이션은 읽지 않는다. `MeshFile.h`·`MeshAssetData`·`IAssetSource::ReadSibling` | D-338~D-343. `.fbx .gltf .glb .obj` 가 `Model` 에셋이고 메시·재질·내장 그림이 하위 에셋. ufbx·cgltf 로 읽는다. 임포트 옵션(단위·법선·스무딩), 같은 그림 묶음 열쇠, 브라우저 펼치기. **노드 변환은 굽지 않고 `ModelNode` 에 들고만 있다** | **원격을 바탕으로.** 같은 확장자를 두 에셋 타입으로 동시에 받을 수 없고, 원격이 형식·하위 에셋·재질까지 더 넓다. **이러면 로컬의 glTF 직독 경로가 빠진다 - 빼는 것이라 확인이 필요하다(§6 Q1).** 빠지면 잃는 것: 노드 변환을 구워 놓는 동작(원격은 메시를 제 좌표 그대로 둔다). 모작의 "노드 계층 가져오기"(E8)가 이 자리를 다시 다룬다 | `Asset.h`, `AssetSource.h`, `Asset.cpp`, `AssetTypeRules.cpp`, `AssetTypes.h`, `JBroAsset.vcxproj`, `MeshLibrary.h/.cpp` - **8** |
| **재질 자료 모델** | D-347·D-348·D-368. 재질 = **사용자 셰이더 에셋(`.hlsl`) + 자리(`PostProcess`·`Sprite`·`Mesh`) + 적은 파라미터 + 텍스처**. `.jmat` 는 `Shader/Domain/Parameters/Textures` YAML(`MaterialDocument`). 쓰는 곳: 후처리 효과 `Material`, 스프라이트, 메시. 스크립트 서비스 `Material` 의 파라미터 덮어쓰기, 오브젝트별 인스턴스, 볼륨 섞기, 셰이더 그래프. `MaterialLibrary` 는 `JBroGraphics` | D-346·D-348~D-351. 재질 = **빌트인 셰이더 둘(`Standard` PBR·`Unlit`) + 고정 칸**(기본색·metallic·roughness·발광·컷아웃·그림 칸 다섯). `.jmat` 는 `Version/Material` 블록(`MaterialDesc` 의 리플렉션 표). 출처는 `.jmat` 과 모델의 하위 Material(읽기 전용). 빈 칸은 메시의 재질. `MaterialLibrary` 는 `JBroFramework3DSystem` | **둘 다 살린다 - 서로 다른 기능이다**(사용자 셰이더 재질과 PBR 재질). 원격 D-346 (4) 도 "사용자 HLSL 은 D-322 11 단계" 라고 뒤로 미뤘는데 그 11 단계가 로컬 D-347 이다. 문제는 **같은 확장자·같은 이름**이다. 푸는 길 둘(§6 Q2): (가) 병합에서는 이름만 갈라 나란히 두고 통합은 따로 결정, (나) 병합하면서 `.jmat` 하나로 통합(`shader` 칸이 빌트인 또는 사용자 셰이더). 권고는 (가) - 병합 검증의 기준이 "양쪽 시험이 그대로 지난다" 이어야 하는데 (나) 는 양쪽 시험을 다시 써야 한다 | `MaterialFile.h/.cpp`(양쪽 새 파일), `Asset.h/.cpp` 공유 - **2 + 공유 2** |
| **렌더러의 메시 경로** | 정점 32 B(위치·법선·UV). 메시 머티리얼은 사용자 `PSMain(JBroMeshInput)`. **`Light3D`(D-367): 뷰마다 라이트 블록 544 B - 빌트인 메시의 b1, 메시 머티리얼의 b2.** 라이트가 없으면 고정 방향광. 렌더 그래프(D-339)·HDR 장면과 톤맵(D-342) 위에 선다 | 정점 48 B(위치·법선·UV·**탄젠트**, 모델 정점과 같은 모양). 빌트인 메시 셰이더가 `Standard`·`Unlit`. **재질 상수 블록이 b1(48 B), 푸시 상수 80 B, 그림 칸 t0..t4.** 빛은 고정 방향광 0.75 + 주변광 0.25. 선형에서 셈하고 **출력에서 sRGB 로 싼다**(프레임 타깃이 UNORM 이라). sRGB 뷰·밉 체인, (재질, 메시) 정렬 묶음 | **정점은 원격(48 B) - 로컬의 상위 집합이다.** `BuiltinMesh.hlsl` 과 `Renderer` 의 메시 패스는 한쪽을 고를 수 없고 **새로 합쳐 써야 한다**: 원격의 `Standard`·`Unlit` 을 로컬의 `Light3D` 블록으로 비추고(자리 b1 이 겹쳐 옮겨야 한다), 로컬의 렌더 그래프·HDR 경로 위에 얹는다. **[미확인]** 원격의 "출력에서 sRGB 로 싼다" 가 로컬의 HDR 장면·톤맵과 어떻게 맞물리는지는 코드를 더 읽어야 안다. 생성 헤더 7 개는 고르지 않고 다시 굽는다 | `Renderer.h/.cpp`, `BuiltinMesh.hlsl`, `ShaderManifest.psd1`, 생성 헤더 7 - **11** |
| **`MeshRenderer3D` 필드** | 컴포넌트 헤더를 고치지 않았다 | 컴포넌트 헤더를 고치지 않았다 | **필드는 겹치지 않는다**(`meshId`·`mesh`·`materialId`·`material`·`tint`·`visible` 그대로, [확인]). 다른 것은 푸는 길이다. 로컬: `MeshLibrary::ResolveHandle(mesh)`, 재질은 `MaterialLibrary::Resolve(material, 오브젝트 번호)`(인스턴스), 화면 레이어는 그리지 않음(D-362). 원격: `ResolveAsset(mesh)` → 빌트인 → 정육면체(D-337), 재질은 칸 → 메시의 재질 → 기본. **원격의 차례 위에 로컬의 화면 레이어 제외와 인스턴스를 얹는다** | `MeshRender3DSystem.h/.cpp`, `Framework3D.h/.cpp` - **4** |
| **쿠킹(패키지)** | `BlobKind` 5 = `CompiledShader`(구운 셰이더 묶음), 6 = `CookedMesh`(로컬 형식). 쿡이 셰이더 컴파일러를 받는다 | `BlobKind` 5 = `CookedMesh`(32 B 머리 + 48 B 정점 + 재질 아이디), 6 = `CookedMaterial`. 모델은 메타만, 같은 그림은 한 번만 굽는다(D-351). 3D 게임 빌드 픽셀 시험 | **번호를 다시 매긴다**: 5 `CompiledShader`(로컬), 6 `CookedMesh`(원격 형식), 7 `CookedMaterial`, 개수 8. 로컬의 `WriteCookedMesh(MeshAssetData)` 는 메시 에셋 결정을 따라간다. `FormatVersion` 은 양쪽 모두 1 그대로다([확인]) - 올릴지는 병합 때 정한다. 전에 구운 `.jpak` 은 다시 구워야 한다 | `PackageFormat.h`, `PackageCook.h/.cpp` - **3** (`GameBuild.h/.cpp` 는 자동 병합) |
| **에디터** | `새 셰이더`·`새 머티리얼`·셰이더 그래프, 머티리얼 인스펙터(덮어쓰기 체크), 셰이더 인스펙터. 그 밖에 코드 편집기·스크립트 요약·Markdown·AI 에이전트·렌더 그래프 창·후처리 프로필 인스펙터·UI 컴포넌트 갈래 | `새 재질`, 재질 인스펙터(`MaterialDesc` 표, 모델 재질은 읽기 전용), 하위 에셋 펼치기, 모델 임포트 옵션, `같은 그림` 표식, `.jproject` 연결과 프로젝트 전환 막기(D-336), 3D 시작 장면(D-335), 빌트인 도형 목록(D-337). **`SetAssetMetaCommand` → `SetAssetFileCommand` 로 이름을 바꿨다.** 인스펙터 블록을 불 값 다섯에서 enum 으로. `FormLayout` 이름 칸 상한 | **둘 다 살린다.** `CreateMaterialAsset`·`assets.new_material`·재질 인스펙터는 재질 결정(Q2)을 따른다. 커맨드 이름은 원격을 따른다(로컬 쪽 사용처 8 파일 29 줄, 그중 로컬이 새로 쓴 줄은 1 - [확인]). **화면 말이 갈렸다: 로컬 "머티리얼", 원격 "재질"** - 하나로 정해야 한다(Q2) | `EditorApplication.h/.cpp`, `EditorIcons.h`, `Widget/Basic.h`, `AssetBrowserPanel.cpp`, `InspectorPanel.h/.cpp` - **7** |
| **스크립트 API 목록** | `Definitions.cpp` 에 줄 번호 형식으로 +892 / -222(UI·네트워크·머티리얼 핸들). 줄 번호 표를 고치는 도구(`tools/`, `b5963a6d`) | D-340. 정의 자리를 **줄 번호 대신 선언의 글자(닻)** 로 바꿨다(+1350 / -1345, 표 전체) | **원격 형식을 바탕으로** 로컬이 더한 줄을 닻 형식으로 다시 낸다. 손으로 합치지 않고 원격의 SDK 도구로 다시 만든다. [추정] 로컬의 줄 번호 도구는 쓸모가 없어진다 - 지울지는 그때 묻는다 | `Definitions.cpp` - **1** |
| **시험 러너** | 새 시험 파일 21 개, 기존 묶음에 호출 추가 | D-344. 시험 호출을 모두 `JBRO_CASE(...)` 로 감쌌다(`JBRO_TEST_CASES` 로 하나만 골라 돌린다). 원격 트리에서 `JBRO_CASE(` 가 든 줄은 1,153 이다([확인]). `TestCases.h`·`TestModels.h` | **원격 형식.** 로컬이 더한 호출과 새 파일 21 개도 감싼다. 충돌 25 개 가운데 16 개는 이 성격으로 보인다(`InputTests` 는 diff 를 읽어 [확인], 나머지 15 개는 원격 변경량이 "include 한 줄 + 감싼 줄 수" 꼴인 것에서 [추정]). 내용이 실제로 부딪히는 것은 `MeshPixelTests`·`Framework3DSystemTests`·`EditorApplicationTests`·`ComputeTests`·`ConstantBufferTests`·`SubresourceTests` | Tests - **25** |
| **Vulkan 탐침 시험 (같은 일을 두 번 했다)** | `38c024a9`(10-26, D-340). 탐침 SPIR-V 를 굽고 Vulkan 에서 그리기 시험 | `e3bb7b7e`(11-17, D-352). 같은 탐침을 굽고, **Vulkan RHI 결함 둘을 고쳤다**(형식 없는 저장 쓰기, 읽기 전이의 컴퓨트 단계). 시험 `TestComputeReadsWhatAPassDrew` | **원격.** 나중 것이고 RHI 수정이 들어 있다. 양쪽이 새로 구운 탐침 SPIR-V 11 개 중 8 개는 바이트가 같고 컴퓨트 3 개만 다르다([확인]. 원격 D-352 는 `-fspv-use-unknown-image-format` 으로 구웠다고 적었다) | 위 Tests 에 포함 (SPV 3, `ComputeTests`, `ConstantBufferTests`, `SubresourceTests`) |
| **`tasks` 문서** | `todo.md` +445(D-335~D-370), `postprocess-plan.md` +397, `ui-plan.md`·`ai-agent-plan.md` 등 새 문서 9 | `todo.md` +258(D-335~D-352), `todo-3d.md` +69(우선순위 절), `model-import-plan.md`·`material-plan.md` 새 문서 | **양쪽 다 싣고 번호를 고친다(§4).** 원격 `todo-3d.md` 가 남은 일로 둔 "5. 3D 조명 컴포넌트"·"7. 3D 스크립트 마무리"·"후처리" 는 로컬에서 이미 섰다(D-367·D-363·D-342~) - 병합 뒤 취소선으로 긋는다. `CLAUDE.md`·`README.md`·`ProjectRule.md` 는 글자로는 합쳐지지만 결정 번호 인용을 고쳐야 한다 | `todo.md`, `todo-3d.md`, `postprocess-plan.md`, `cpp-script-plan.md`, `script-user-scenario.md` - **5** |

합계: 8 + 2 + 11 + 4 + 3 + 7 + 1 + 25 + 5 = 66.

### 3.1 한쪽에만 있어 부딪히지 않는 것

- **로컬에만**: 화면 UI 위젯 전부(`Toggle2D`~`Navigation2D`, `Layout2D`, `ScrollView2D`, `Slider2D`, `Tween2D`), 새 모듈 `JBroScreenUi`·`JBroScriptRuntime`·`JBroShaderCompiler`, `Light3D`, `PostProcessVolume2D/3D`, 후처리 전체(렌더 그래프·블룸·LUT·프로필), 렌더 텍스처, 3D 스크립트(`GameScript3D`), 네트워크 소유권 위임, 에디터의 코드 편집기·AI 에이전트·Markdown, 빌드 산출물 자리(D-370)
- **원격에만**: `ModelDecoder`·`ModelImport`(ufbx·cgltf), 하위 에셋, 텍스처 색 공간·밉맵 옵션, `.jproject` 파일 연결, 3D 시작 장면, 빌트인 도형 다섯, `JBRO_TEST_CASES`

부딪히지 않아도 **맞물림은 확인해야 한다.** 예: 로컬이 `SpriteLibrary`·`RenderWorld2D` 를 `JBroScreenUi` 로 옮겼는데(이름 바꾸기 13 건) 원격이 그 옛 경로를 include 하는 새 코드를 썼는지는 빌드해 봐야 안다.

## 4. 결정 번호

분기점의 마지막 번호는 D-334 다. 그 뒤 **세 갈래**가 따로 매겼다([확인]).

| 갈래 | 범위 | 개수 | 날짜 | 트리 안의 인용 |
|---|---|---|---|---|
| 로컬 main | D-335 ~ D-370 | 36 | 10-15 ~ 11-05 | 288 파일 1,150 줄 (겹치는 D-335~352 만은 165 파일 583 줄) |
| 원격 main | D-335 ~ D-352 | 18 | 11-16 ~ 11-19 | 72 파일 235 줄 |
| `script-qa-fixes` (로컬, 미병합) | D-371 ~ D-387 | 17 | 11-17 | - |

**원격의 18 개 전부가 로컬 번호와 겹친다.** 같은 번호가 전혀 다른 결정이다(예: 로컬 D-348 = 메시 머티리얼, 원격 D-348 = `MaterialDesc`).

### 4.1 다시 매기는 안 [제안]

**원격 D-335~D-352 를 D-388~D-405 로 옮긴다(+53).** 이유:
- 원격 쪽 인용이 5 분의 1 이라 고칠 곳이 적다.
- 날짜로도 원격이 뒤다(로컬 D-370 이 11-05, 원격 D-335 가 11-16).
- `script-qa-fixes` 의 D-371~D-387 을 건드리지 않는다. 그 브랜치를 이번에 넣지 않더라도 나중에 다시 매길 일이 없다.
- 방법: 원격 main 에서 딴 준비 브랜치에 "번호만 고치는 커밋" 하나를 먼저 만들고 그것을 병합한다. 병합 뒤에 고치면 `D-348` 이 어느 쪽 것인지 줄마다 가려야 한다.
- 커밋 메시지의 옛 번호는 고치지 않는다(히스토리를 다시 쓰지 않는다). 아래 표를 `tasks/todo.md` 에 실어 찾게 한다. 로컬도 같은 방식으로 한 적이 있다(D-366 의 "커밋 메시지에는 D-365..D-368 로 적혔다").

대안: `script-qa-fixes` 를 버리거나 그쪽을 다시 매길 것이면 +36(D-371~D-388)도 된다.

| 원격 번호 | 새 번호 [제안] | 내용 | 겹치는 로컬 결정(다른 내용) |
|---|---|---|---|
| D-335 | D-388 | 2D 를 멈추고 3D 를 먼저 한다 | D-335 엔진 내장 코드 편집기 |
| D-336 | D-389 | `.jproject` 두 번 누르면 열고, 열린 에디터는 다른 프로젝트를 들이지 않는다 | D-336 스크립트 요약 독 |
| D-337 | D-390 | 빌트인 도형은 메시 칸에서 고르고 빈 메시는 정육면체 | D-337 Markdown 문서 뷰 |
| D-338 | D-391 | 3D 에셋은 모델로 가져온다(ufbx·cgltf, 하위 에셋) | D-338 에디터 내장 AI |
| D-339 | D-392 | 모델 임포트 옵션의 첫 판 | D-339 렌더 그래프 |
| D-340 | D-393 | 스크립트 API 목록의 정의 자리는 닻으로 | D-340 렌더 그래프 창 |
| D-341 | D-394 | 하위 에셋 목록은 `.jmeta` 가 든다 | D-341 패스별 GPU 시간 |
| D-342 | D-395 | `mesh` 칸은 에셋 시스템의 핸들 | D-342 후처리 효과 표·HDR·톤맵 |
| D-343 | D-396 | 내장 그림은 하위 `Texture`, 같은 그림은 묶음 열쇠 | D-343 블룸·LUT |
| D-344 | D-397 | `JBRO_TEST_CASES` / `JBRO_CASE` | D-344 후처리 프로필 |
| D-345 | D-398 | 패키지의 모델은 메타만, 메시는 `CookedMesh` | D-345 레이어 후처리 |
| D-346 | D-399 | 재질의 첫 판(`Standard`·`Unlit`) | D-346 후처리 볼륨 |
| D-347 | D-400 | 메시 정점 48 B(UV·탄젠트) | D-347 Shader·Material 에셋 |
| D-348 | D-401 | 재질은 `MaterialDesc` 하나, `.jmat` | D-348 메시 머티리얼·파라미터 덮어쓰기 |
| D-349 | D-402 | 메시는 재질로 그린다 | D-349 캔버스의 스크립트 필드 잇기 |
| D-350 | D-403 | 재질을 에디터에서 만들고 고친다 | D-350 fxc 오류 경로 |
| D-351 | D-404 | 같은 그림은 한 번, 3D 게임 빌드 실측 | D-351 위젯 1 부 |
| D-352 | D-405 | Vulkan RHI 탐침 시험과 결함 둘 | D-352 `InputField2D`·포커스 |

### 4.2 내용이 겹치는 결정 (번호가 아니라 주제)

병합 결정을 적을 때 `Updates`·`Obsoletes` 로 이어야 하는 짝이다.

| 주제 | 로컬 | 원격(새 번호) |
|---|---|---|
| 메시 에셋 | D-366 | D-391·D-394·D-395·D-398 |
| 재질 | D-347·D-348·D-368 | D-399·D-401·D-402·D-403 |
| 메시 정점 | D-348 (1) 의 32 B | D-400 의 48 B |
| 3D 빛 | D-367 | D-399 (1) "조명 컴포넌트가 오면 빛만 바뀐다" |
| Vulkan 탐침 | D-340 | D-405 |
| 2D·3D 순서 | D-351~D-362 가 2D UI 를 진행 | D-388 "2D 를 멈춘다" |

## 5. 병합 방식 [제안]

1. **원격 PC 의 푸시를 멈춘다.** 원격 main 은 오늘 새벽에도 움직였다. 기준 커밋(`60631437`)을 고정하지 않으면 병합하는 동안 다시 어긋난다.
2. **로컬 135 커밋을 원격에 백업 브랜치로 올린다.** 지금 이 PC 디스크에만 있다. main 이 아닌 새 브랜치로 올리면 강제 푸시도 히스토리 변경도 없다(푸시는 승인이 있어야 한다).
3. 병합은 F: 의 새 워크트리, 새 브랜치에서 한다. 이 폴더의 작업 트리와 미추적 파일은 건드리지 않는다.
4. 차례: 준비 커밋(원격 쪽 번호 다시 매기기, 이름이 부딪히는 타입 가르기) → 병합 커밋(충돌 66 개 풀기) → 뒷정리(생성 헤더 다시 굽기, `JBRO_CASE` 감싸기, 문서).
   병합 커밋 하나로 양쪽이 조상이 되므로 **원격 main 에는 빨리 감기로 올라간다** - 강제 푸시가 필요 없다.
5. 검증: 병합 브랜치에서 전체 빌드, `JBroTests` 전체, 세 백엔드 픽셀 시험. 병합 전 값은 로컬 main 과 원격 main 을 이 PC 에서 각각 재서 나란히 적는다.

**걸리는 점**
- 이 PC 의 Vulkan SDK 는 1.4.350.0, 원격이 SPIR-V 를 구운 것은 1.4.363.1 이다. [추정] 다시 구우면 바이트가 달라질 수 있다. SDK 를 올릴지는 설치라 사용자 확인이 필요하다.
- 원격의 세 백엔드 시험값은 다른 PC·다른 GPU 의 것이다. 이 PC 에서 원격 main 을 따로 빌드해 재야 "병합 전 값" 이 같은 조건이 된다.
- `BuiltinMesh.hlsl` 과 `Renderer.cpp` 의 메시 패스는 충돌 풀기가 아니라 새 코드다. 여기서 그림이 바뀌면 양쪽 픽셀 시험의 기대값을 다시 봐야 한다.

## 6. 사용자가 정할 것

- **Q1. 메시 에셋**: 원격의 모델 임포트를 바탕으로 하고 로컬의 glTF 직독(D-366)을 뺀다. 노드 변환을 굽는 동작이 빠진다. 괜찮은가.
- **Q2. 재질**: (가) 병합에서는 이름만 갈라 둘을 나란히 두고 통합은 뒤에 따로 정한다 / (나) 병합하면서 `.jmat` 하나로 통합한다. (가) 라면 `.jmat` 확장자와 `Material` 에셋 타입을 어느 쪽이 갖는가. 화면 말은 "머티리얼" 과 "재질" 중 무엇인가.
- **Q3. 결정 번호**: 원격 D-335~352 → D-388~405 로 옮긴다.
- **Q4. `script-qa-fixes`**: 이번 통합에 넣는가, 통합 뒤 따로 합치는가. 지금 누가 작업 중인 브랜치인지 나는 모른다.
- **Q5. 기준 커밋과 백업**: 원격 `60631437` 로 고정해도 되는가(다른 PC 의 작업을 멈출 수 있는가). 로컬 135 커밋을 백업 브랜치로 푸시해도 되는가.
- **Q6. 브랜치 이름**: 병합 브랜치와 백업 브랜치의 이름.
- **Q7. Vulkan SDK**: 이 PC 를 1.4.363.1 로 올릴 것인가, 지금 것으로 다시 굽고 차이를 적을 것인가.

**사용자 답(2026-11-19)**: "일단 메인에 올리는걸 최대한으로", "불칸은 됐어"(SDK 를 올리지 않는다). 브랜치 `main-merge`·백업 `local-main-backup`, `script-qa-fixes` 를 같이 넣는다,
재질은 나란히 두고 화면 말은 "머티리얼", 시험이 지나면 원격 main 에 푸시. Q1(로컬 glTF 직독 빼기)·Q3(번호 +53)은 제안대로 했다. Q5 의 "다른 PC 의 푸시 멈추기" 는 묻지 않았다 - 푸시 직전에 원격 main 이 그대로인지 다시 본다.

## 7. 진행 기록

- 2026-11-19: 1~3 단계(상태 확인·교집합·겹침 표) 끝. 4 단계(보고) 뒤 멈춤. 5 단계부터는 위 결정을 받은 뒤다.
- 2026-11-19: 사용자 결정(§6)대로 병합했다. 첫 병합은 원격 `main` 의 Model·두 머티리얼·후처리 갈래를 합쳤고, 그 뒤 원격에 올라온 `Light3D` 셋(D-353~D-355)을 준비 커밋 `eb44fbfe` 로 D-407~D-409 로 옮겨 다시 합쳤다. 로컬 `Light3D`(D-367)는 원격 설계로 대체했다(D-406 (8)).
- 시험 비교(모두 Debug, `source/JBroEngine` 에서 실행):

| 기준 | 커밋 | 묶음 | 결과 |
| --- | --- | --- | --- |
| 로컬 main + `script-qa-fixes`(병합 전) | `b29233fd` | 82 + 51 | 82 묶음 통과 뒤 EditorApplication 의 `TestNewProjectCreatesAndOpensIt` 가 실패(팝업이 가운데 자리에 서지 않음, 두 번 재현). 그 뒤 51 묶음은 따로 돌려 통과 |
| 원격 main(병합 전, `Light3D` 전) | `60631437` | 109 | 모두 통과(3 건 건너뜀), 1777 초 |
| 병합 결과 | 두 번째 병합 커밋 | 129 | 모두 통과, 2064 초. 로컬에서 실패하던 팝업 시험도 통과 |

- 라이트 블록을 파이프라인보다 먼저 묶어 후처리 그리기가 실패하던 것을 `Renderer::PushLightBlock`(링에 올리기만)으로 고쳤다. 묶는 것은 메시 패스가 파이프라인을 건 뒤 b2 로 한다.
