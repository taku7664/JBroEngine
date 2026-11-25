# 스켈레탈 애니메이션 계획 (D-450)

todo-3d 의 "다음 순서" 9 번(스켈레탈 애니메이션과 뼈대·애니메이션·블렌드셰이프 가져오기)이다. **에디터 안 모델 에디터를 같이 감안한다** - 사용자가 2026-11-23 에
모델 에디터를 따로 만들 생각이라고 했고, 형태는 에디터 안 전용 창, 할 일은 뼈대·소켓·클립 편집, 리타기팅·뼈대 공유, 메시 직접 편집(웨이트 페인트 포함), 재질·LOD·콜라이더다.

표시: `[확정]` 사용자가 정했다, `[제안]` 아직 정하지 않았다.

## 1. 지금 상태

- 애니메이션 기반은 `Tween2D` 뿐이다. 기존 엔진(`JBroEngine_old`)도 스프라이트 애니메이션(`SpriteAnimator2D`·`AnimationClipAsset`)뿐이고 뼈대는 없었다(파일 목록으로 확인).
- 정점 `ModelVertex`(`JBroAsset/Include/JBro/Asset/ModelDecoder.h`)는 위치·법선·UV·탄젠트뿐이다. 모델의 메시·재질·내장 그림은 읽기 전용 하위 에셋이다(D-391, model-import-plan).
- 디코더 ufbx·cgltf 는 스킨과 애니메이션을 읽는다 - 새 디코더는 들이지 않아도 된다. Jolt(`ThirdParty/Jolt/Jolt/Skeleton/`·`Physics/Ragdoll/`)에 `Skeleton`·`SkeletonPose`·`SkeletonMapper`·`Ragdoll` 이 있다.
- RHI 의 `Storage` 버퍼는 GPU 가 채우는 것뿐이고(`RHI.h`), CPU 가 매 프레임 올리는 값은 상수 링이다.

## 2. 상용 엔진과 자료 (2026-11-23 조사)

| | Unreal | Unity | Godot |
|---|---|---|---|
| 뼈대 | 독립 Skeleton 에셋. 계층·이름이 거의 같은 메시가 나눠 쓴다(호환 뼈대는 양방향). 소켓을 여기서 편집 | 모델마다 Avatar. 같은 리그의 Avatar 를 복사해 클립을 같이 쓴다 | `Skeleton3D` 노드 안 뼈 배열 |
| 리타기팅 | 캐릭터마다 IK Rig, 둘을 잇는 IK Retargeter. 뼈 수·이름·방향이 달라도 된다 | Humanoid Avatar 매핑 | 가져올 때 BoneMap 으로 공통 프로필(`SkeletonProfileHumanoid`)에 이름을 맞춘다 |
| 붙이기 | 소켓(뼈 기준 오프셋). 뼈대 소켓과 그 메시만의 메시 소켓 | - | `BoneAttachment3D` 가 뼈 하나를 따르거나 덮어쓴다 |
| 메시·웨이트 편집 | 스켈레탈 메시 편집 플러그인 - 에디터 안에서 뼈 놓기·웨이트 페인트, 고치면 나머지를 자동 정규화(합 1) | - | - |
| 다시 가져오기 | 기본 메시만·기본+LOD·지오메트리만·스킨과 가중치만 등 여럿. 레거시 가져오기에서 메시 소켓이 중복되는 버그(UE-367501) | 가져온 클립을 손대면 원본에 새 클립이 생겨도 받지 않는다 | - |

- glTF: 정점의 `JOINTS_0`·`WEIGHTS_0` 는 보통 4 개 묶음이라 정점 하나에 뼈 4 개까지다. 스킨은 뼈 노드 목록과 역바인드 행렬이다.
- ufbx: 스킨 디포머가 정점별 가중치 배열을 그대로 준다. `ufbx_bake_anim` 이 노드 애니메이션을 키로 굽는다.
- ozz-animation(MIT): 런타임 샘플링·블렌딩과 glTF·FBX 를 읽는 오프라인 도구가 갈라져 있다. ACL(MIT, 헤더만): 애니메이션 압축, UE 5.3 부터 기본 코덱.
- 확인하지 못한 것: Unity 가 가져오기 설정을 `.meta` 에 둔다는 것은 간접 근거만 찾았다.

출처: [Skeletons in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/skeletons-in-unreal-engine),
[Skeleton Tree (UE 4.26)](https://docs.unrealengine.com/4.26/AnimatingObjects/SkeletalMeshAnimation/Persona/SkeletonTree),
[IK Rig Animation Retargeting](https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine),
[Skeleton Editing in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/skeleton-editing-in-unreal-engine),
[Skeletal Mesh Editor in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-editor-in-unreal-engine),
[UE-367501](https://issues.unrealengine.com/issue/UE-367501),
[Unity FBX Importer Rig 탭](https://docs.unity3d.com/6000.4/Documentation/Manual/FBXImporter-Rig.html),
[Unity Animation 탭](https://docs.unity3d.com/6000.2/Documentation/Manual/class-AnimationClip.html),
[Godot Retargeting 3D Skeletons](https://docs.godotengine.org/en/4.6/tutorials/assets_pipeline/retargeting_3d_skeletons.html),
[Godot BoneAttachment3D](https://docs.godotengine.org/en/4.3/classes/class_boneattachment3d.html),
[glTF Tutorial Skins](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html),
[ufbx Deformers](https://ufbx.github.io/elements/deformers), [ufbx Animation](https://ufbx.github.io/elements/animation),
[ozz-animation](https://guillaumeblanc.github.io/ozz-animation), [ACL](https://raw.githubusercontent.com/nfrechette/acl/develop/README.md).

## 3. 설계

### 3.1 `[확정]` 편집 결과를 두는 곳 (D-450)

**기본은 지금처럼 원본 파일 + `.jmeta` 가 진실이고, 메시나 웨이트를 고치려는 순간에만 "꺼내기"로 엔진 형식 에셋을 만든다. 꺼낸 뒤로는 그 파일이 진실이다.**

- 메타로 서는 것: 클립의 구간·이름·반복·이벤트, 소켓, 리타기팅 매핑, LOD·콜라이더 설정. 원본이 바뀌면 다시 읽고 메타의 설정을 얹는다.
- 메타로 서지 않는 것: 메시의 정점·폴리곤 편집과 웨이트 페인트 - 원본 FBX·glTF 를 고칠 수 없다. 이것만 꺼내기를 거친다.
- 3D "다음 순서" 12 번의 재질 꺼내기(모델의 하위 Material → `.jmat`)와 같은 규칙으로 묶는다 - 재질·메시·클립 모두 "하위 에셋은 읽기 전용, 고치려면 꺼낸다".
- 고른 까닭: Unreal 식(가져오면 늘 엔진 형식이 진실)은 편집이 자연스럽지만 원본과 두 벌이 되고 다시 가져오기 병합이 무겁다(Unreal 의 재가져오기 갈래·소켓 중복 버그).
  Unity 식(원본이 진실)만으로는 메시 편집이 서지 않는다. 지금 구조(D-391)를 지키면서 편집이 필요한 것만 꺼낸다.
- 열림: 꺼낸 메시의 형식·확장자, 꺼낸 뒤 원본이 바뀌면 알릴지(병합은 하지 않는다고 보지만 정하지 않았다).

### 3.2 `[확정]` 뼈대 단위 (D-451)

- `Skeleton` 은 모델의 하위 에셋이다(D-450 의 원본+메타를 따른다). 다른 모델이 메타에서 "이 뼈대를 쓴다" 를 가리킬 수 있고, 뼈 이름·계층이 같으면 클립을 같이 쓴다(Unreal 의 호환 뼈대).
- 소켓은 뼈대 소켓(그 뼈대를 쓰는 모든 메시)과 메시 소켓(그 메시만) 둘 다 둔다(Unreal 과 같다). 둘 다 메타에 적는다.
- 리타기팅은 두 단계다: 먼저 가져올 때 엔진의 휴머노이드 프로필로 뼈 이름을 맞추고(Godot 의 BoneMap·`SkeletonProfileHumanoid`), 그 뒤 체인 단위 리타기터(Unreal IK Rig·IK Retargeter).
- 열림: 휴머노이드 프로필의 뼈 이름 목록, "이 뼈대를 쓴다" 를 가리킨 모델의 뼈가 다를 때(빠진 뼈·더 있는 뼈)의 처리.

### 3.3 `[확정]` 뼈 표현·레이어·붙이기·컴포넌트 (D-453)

- **뼈 표현**: 뼈는 오브젝트가 아니라 `Animator3D` 안의 포즈 배열(뼈 번호 → 위치·회전·크기)이다. 계층 창에 뼈가 보이지 않고, 뼈를 보고 고치는 곳은 모델 에디터의 뼈대 트리와
  뼈 기즈모(뼈 번호로 고친다)다. 스크립트는 뼈 이름으로 포즈를 읽고 쓴다. Unity 도 뼈 오브젝트를 걷는 옵션(Optimize Game Objects)을 따로 두었다.
- **상하체 분리 - 레이어와 뼈 마스크**: `Animator3D` 에 상한이 정해진 레이어 배열(프레임 경로라 할당 없음). 레이어마다 클립·가중치·모드(`Override`·`Additive`)·뼈 마스크.
  레이어 0 은 전신 바탕이다. 마스크는 뼈마다 0~1 이고, 만들 때는 뿌리 뼈와 섞는 깊이(Unreal 의 Branch Filter·Blend Depth)를 고른 뒤 뼈별로 고친다(D-464 - 파일에는 뼈 이름마다 결과만 적는다). 뼈 이름으로 적어
  같은 뼈대를 쓰는 캐릭터가 같이 쓴다(D-451) - 뼈대의 메타에 둔다. `Additive` 클립은 메타에 표시와 기준 포즈(첫 프레임이나 다른 클립)를 적고 차이는 굽기에서 뺀다.
  계산은 레이어마다 포즈를 뽑아 아래에서 위로 `Override` 를 (레이어 가중치 x 마스크) 로 섞고 `Additive` 를 마지막에 더한다.
  **런타임은 1 단계부터 레이어다** - 하나로 시작해 나중에 끼우면 자료·저장 형식·스크립트 API 가 바뀐다. 마스크 편집 화면은 모델 에디터 단계(3), 그 전에는 인스펙터에서
  뿌리 뼈와 깊이만 고른다. IK 로 뼈를 덮어쓰기(조준·발 맞추기)는 2 차다.
- **붙이기**: 소켓 = 뼈 하나 + 오프셋(위치·회전·크기), 뼈대 소켓과 메시 소켓을 메타에 적는다(D-451). 소켓이 없어도 뼈 이름을 그대로 고른다(오프셋 0).
  새 컴포넌트(이름은 정하지 않았다, 가칭 `BoneAttachment3D`)가 붙일 오브젝트에 붙어 소켓을 가리키고, 따를 `Animator3D` 는 기본으로 가장 가까운 조상의 것이다.
  매 프레임 포즈 계산 뒤 소켓의 월드 트랜스폼을 그 오브젝트의 `Transform3D` 에 쓴다(제 로컬 값은 소켓에서의 덧 오프셋). 순서는 포즈 → 붙이기 → 물리·렌더.
  1 차는 따르기만 하고 뼈를 덮어쓰기(Godot `override_pose`)는 IK 와 함께 2 차다. `Transform3D` 에 부모 소켓 칸을 두는 것은 모든 3D 오브젝트에 칸이 생겨 고르지 않았다.
  인스펙터의 소켓 칸은 그 뼈대의 소켓·뼈 이름 드롭다운이고, 모델 에디터의 미리보기 붙이기는 미리보기 전용이다. 스크립트는 `GetSocketTransform` 으로 읽는다.
- **컴포넌트 구성**: 포즈는 `Animator3D`, 그리기는 지금 `MeshRenderer3D` 가 따른다(Unity·Godot 처럼 나눈다 - 메시 여럿인 캐릭터에 포즈 하나, Unreal 의 Leader Pose 와 같은 꼴).
  `Animator3D`: 뼈대(기본은 같은 오브젝트·자식의 스킨 메시의 것, 다른 뼈대도 가리킨다), 레이어 배열, 시작 때 재생, 속도, 시간 기준(일반·타임스케일 무시·고정 스텝),
  화면 밖 처리(기본: 시간만 흐르고 포즈는 계산하지 않는다 - Unity 의 컬링 모드 자리).
  `MeshRenderer3D`: 메시에 스킨이 있으면 자기나 가장 가까운 조상의 `Animator3D` 를 찾고, 바인드할 때 한 번 이름으로 "메시 뼈 번호 → 애니메이터 뼈 번호" 표를 만든다
  (프레임마다 문자열 비교 없음). 애니메이터가 없거나 빠진 뼈는 바인드 포즈다. 재질·그림자·반투명은 지금 칸 그대로이고, 경계 상자는 포즈의 뼈 위치에 메시 반지름을 더해 다시 잡는다.
  한 `Animator3D` 를 따르는 메시는 같은 포즈다 - 따로 움직일 부분은 제 `Animator3D` 를 둔다. `SkinnedMeshRenderer3D` 는 두지 않는다(재질·그림자 칸이 두 벌이 된다).
- **모델 놓기는 정하지 않았다**: 사용자가 오브젝트를 `.jobject` 에셋으로 둘 예정이고(`.jlayer`·`.jcanvas` 처럼), 모델 놓기는 그 계약을 따른다. 레이어 에셋은 고유한 참조다(D-452).
  `.jobject` 는 프리팹처럼 참조와 오버라이드다(D-454) - 놓은 고블린마다 에셋을 가리키고 다른 값만 든다. 나머지 `.jobject` 계약(형식·넣기·스폰)이 서면 모델 놓기를 정하고,
  모델을 펼치는 코드는 `.jobject` 넣기와 같은 오브젝트 트리 함수를 쓴다.

### 3.4 `[확정]` 나머지 기술 기본값 (D-456)

사용자에게 "남은 것은 기술 기본값이라 권고대로 확정으로 적고 진행한다" 고 알린 뒤 "시작해" 를 받았다(2026-11-23). 서드파티(ozz·ACL)는 들이지 않고 런타임을 자체로 시작한다.
7 의 뼈 번호는 CPU 자료(`ModelSkinWeights`)에서는 32 비트이고, 16 비트로 줄이는 것은 GPU 정점을 지을 때다(1 단계).

4. **미리보기와 게임을 같은 길로**: 모델 에디터 창은 모델마다 여는 비고유 패널(D-284 의 스프라이트 뷰어 꼴)이고 창마다 미리보기 캔버스와 커맨드 스택을 둔다(D-323 의 그래프 창 꼴).
   미리보기도 게임과 같은 `Animator3D`·스키닝·그림자 경로로 그린다.
5. **편집 형식과 런타임 형식 분리**: 클립은 엔진의 키프레임 형식으로 편집하고, 런타임 형식은 셰이더·메시 쿡처럼 굽기 산출물이다(에디터의 즉석 굽기와 `.jpak`).
   런타임을 자체로 하든 ozz 를 쓰든, 압축(ACL)을 붙이든 편집 도구는 그대로다. ozz·ACL 은 서드파티라 들이기 전에 확인한다. 1 차는 자체 선형·slerp 샘플러로 시작하는 것을 제안한다.
6. **물리**: 뼈대를 이름과 부모 번호의 배열로 두어 Jolt `Skeleton` 으로 바로 옮긴다. 모델 에디터의 콜라이더 탭이 래그돌로 이어지고 `SkeletonMapper` 가 애니메이션 뼈대와 래그돌 뼈대를 잇는다.
7. **정점·스키닝**: 스킨 메시만 뼈 번호 4 개(16 비트)·가중치 4 개를 더한 정점 형식, 가중치 합은 1(웨이트 페인트도 자동 정규화). 1 차는 정점 셰이더 스키닝, 뼈 행렬은 3x4 로
   상수 링에 256 개(12 KB). 그림자 패스가 많아 반복이 무거워지면 컴퓨트 스키닝으로 옮긴다.
8. **포즈 계산**: CPU, 프레임 경로라 할당 없이. 캐릭터가 많으면 물리처럼 `WorkerPool::ParallelFor`.
9. **상태 머신**: 1 차는 스크립트의 `Play`·`CrossFade`. 그래프는 2 차에 노드 그래프 프레임워크(D-323) 위에.

## 4. 단계

0. 정점 스킨 칸, `Skeleton`·`AnimationClip` 하위 에셋, 가져오기. → **glTF 가 섰다**(D-456, 2026-11-23): 디코더가 스킨을 뼈대로(부모가 앞에 오게 다시 늘어놓고 조인트 번호를 옮긴다,
   뼈 사이에 뼈가 아닌 노드가 끼면 접은 상대 자세, 뿌리 뼈의 부모 노드 행렬, 역바인드), `JOINTS_0`·`WEIGHTS_0` 을 정점마다 넷(합 1 로 다시 나눔, 같은 위치라도 가중치가 다르면 다른 정점),
   애니메이션을 뼈 이름 트랙으로(LINEAR·STEP, CUBICSPLINE 은 값 키만, 모프 가중치는 뒤) 읽는다. 임포트 배율이 위치·역바인드·뿌리 부모·위치 트랙에 함께 걸린다.
   하위 에셋 `Skeleton/이름`·`AnimationClip/이름`, `MeshData` 의 `skin`·`skeleton`, 에셋 시스템의 `SkeletonData`·`AnimationClipData`(동기·미룬 디코드 모두, 같은 모델은 작업 하나).
   **게임 패키지에 굽기가 섰다**(D-466, 2026-11-26): 쿡된 메시가 스킨과 뼈대 아이디를 들고, 뼈대·클립(설정 전 원본)이 제 블롭이며, 소켓·마스크·클립 설정은 실린 메타에서 건다.
   **FBX 의 스킨·애니메이션이 섰다**(D-467, 2026-11-26): 클러스터의 노드와 그 위 뼈 노드가 뼈대, 스킨 메시는 바인드 자리로 옮긴 정점과 뼈마다 하나의 역바인드, 스택마다 `ufbx_bake_anim`.
   남은 것: 전용 아이콘(글꼴의 글리프 이름을 맞춰 본 뒤 - 지금 뼈대는 오브젝트, 클립은 재생 아이콘).
1. `Animator3D`, 워커 포즈 계산, 정점 셰이더 스키닝(그림자 포함), 스크립트 API. → **1a 가 섰다**(D-457, 2026-11-23): 컴포넌트 `Animator3D`(갈래 `Animation`, 한 오브젝트에 하나 -
   `skeletonId`·`clipId`·`playOnStart`·`loop`·`speed` - 1c 에서 클립 목록으로 바뀌었다), 자세 셈 `AnimationPose`(쉬는 자세·트랙 묶기·STEP 과 선형·짧은 쪽 nlerp·덮어쓰기와 더하기·뼈 마스크·뼈 행렬·스키닝 행렬),
   시스템 `Animation3DSystem`(실행 순서 350 - 스크립트 뒤 렌더 추출 앞, 레이어 4 칸, 크로스페이드, 조작 함수 `Play`·`CrossFade`·`SetLayerWeight`·`SetLayerMode`·`SetLayerMask`).
   뼈대가 비면 이 오브젝트·자식의 스킨 메시가 잡은 뼈대다 - 메시가 실릴 때 제 뼈대를 함께 잡아(`MeshData::skeletonAsset`, 스프라이트의 텍스처와 같다) 프레임 경로에서 싣지 않는다.
   **1b 가 섰다**(D-458): 렌더러의 스킨 메시(`RegisterSkinnedMesh` - 뼈 번호·가중치의 둘째 정점 흐름)와 뼈 묶음(`SubmitSkinPalette` - 프레임 슬롯마다 상수 버퍼, 묶음 하나가 뼈 256 개 자리),
   정점 셰이더 스키닝(`BuiltinMesh.hlsl` 의 `VSSkinnedMain`·`VSUnlitSkinnedMain`·`VSShadowSkinnedMain` - 뼈는 픽셀 단계의 블록 다음 자리 `Standard` b5·`Unlit` b2·그림자 b1),
   프로그램 `MeshSkinned`·`MeshUnlitSkinned`·`MeshShadowSkinned`, 스킨 항목은 인스턴스로 묶지 않고 하나씩 그린다. 프레임워크는 스킨 메시면 자기나 가장 가까운 조상의 애니메이터
   행렬로 그리고 자리는 그 애니메이터 오브젝트의 것이다. 뼈 묶음은 렌더러 프레임마다 한 번 올리고 뷰·레이어가 함께 쓴다.
   **1c 가 섰다**(D-459): 클립은 애니메이터의 목록 `animationClipIds` 이고 레이어 0 은 `startClip` 번호로 시작한다. 스크립트 핸들 `Handle::Animator3D` 가 번호로
   `Play`·`CrossFade`·`Stop`·레이어 가중치·더하기·마스크를 컴포넌트에 쌓고(한 프레임 16 개), 시스템이 다음 갱신에 처리한 뒤 레이어의 시간·재생 중·클립 번호를 돌려 적는다.
   인스펙터는 목록 원소를 AnimationClip 고르기로 보인다.
   남은 것: 인스펙터의 레이어 편집(모델 에디터 단계), 클립을 이름으로 고르기, 화면 밖이면 자세를 건너뛰기, 워커로 포즈 나누기(캐릭터가 많아지면),
   셰이더 머티리얼(D-348)의 스킨 메시(지금 쉬는 자세로 그린다), 스킨 메시의 경계(지금 쉬는 자세의 공이라 점광 면의 걸러 내기가 움직인 메시를 놓칠 수 있다), 뼈 256 개 넘는 뼈대.
   알려진 한계: 더하기 레이어의 기준은 쉬는 자세다(클립의 첫 프레임이 아니다). 뼈대가 바뀌면 마스크의 뿌리를 번호로 들고 있어 다시 걸어야 맞다.
2. 소켓과 붙이기 컴포넌트. 노드 트리째 놓기(todo-3d 10 번)는 `.jobject` 계약 뒤(3.3). → **섰다**(D-460, 2026-11-25): 소켓은 모델 메타의 뼈대·메시 항목 `Sockets`(뼈 이름 + 오프셋)이고
   에셋 인스펙터에서 고친다(`bone` 은 뼈 이름 드롭다운). `BoneAttachment3D`(필드 `socket`)가 가장 가까운 조상 애니메이터의 소켓(뼈대 소켓 → 따르는 메시의 소켓 → 뼈 이름)을 따르고
   제 `Transform3D` 로컬 값은 소켓에서의 덧 오프셋이다. 붙이기 시스템은 순서 360(자세 뒤·렌더 앞). 스크립트는 `Animation3DService` 의 `FindSocket`·`GetSocketTransform`.
   남은 것: 캔버스 뷰의 소켓 표시와 끌어 고치기(3 단계), 뼈 덮어쓰기(IK), 붙은 콜라이더의 한 프레임 늦음(물리가 자세 앞이다).
3. 모델 에디터 창: 미리보기 캔버스·뼈대 트리·타임라인·클립 설정·소켓. 사용자와 정한 조각(2026-11-25): 3a 뼈대·재생 → 3b 클립 설정 → 3c 소켓 → 3d 레이어 뼈 마스크.
   → **3a 가 섰다**(D-461, 2026-11-25): 모델마다 여는 비고유 패널 `ModelEditorPanel`(도크 `ModelEditor`, 브라우저의 두 번 누르기와 우클릭 `모델 에디터에서 열기`, 하위 에셋으로 열면 그 뼈대·클립을 고른다).
   미리보기는 3D 프레임워크가 창마다 드는 제 캔버스(`Framework3D::CreatePreview` - 트랜스폼·메시·라이트·애니메이션·붙이기 시스템만)이고 `RenderPreviewView` 가 캔버스 뷰와 같은 궤도 카메라로 그리되 편집 카메라 기록은 건드리지 않는다.
   뼈대 트리·관절 덧그리기와 누르기로 고르기·애니메이션 목록·재생·반복·속도·시간 막대(`SetLayerTime`).
   **3b 가 섰다**(D-462, 2026-11-25): 클립 설정은 모델 메타의 애니메이션 항목 `Clip`(원본·구간·반복·이벤트), 구간 자르기와 원본에서 떼어 새 클립 만들기(다시 가져와도 남는다), 반복은 클립이 정한다(애니메이터의 `loop` 칸과
   스크립트의 반복 인자를 걷었다), 이벤트는 애니메이터 오브젝트의 스크립트가 `OnAnimationEvent` 로 받는다. 편집은 창의 커맨드 스택이고 이벤트는 타임라인 아래 줄에서 더하고 끈다.
   **3c 가 섰다**(D-463, 2026-11-25): 창 안 소켓 목록(뼈대 소켓과 메시 소켓, 고른 뼈에 추가, 이름·뼈·오프셋·삭제), 미리보기의 마름모와 기즈모로 끌기(손을 뗄 때 커맨드 하나), 캔버스 뷰가 고른 애니메이터·붙인 오브젝트의 소켓을 그린다.
   **3d 가 섰다**(D-464, 2026-11-25): 뼈 마스크는 뼈대 메타의 `Masks`(뼈 이름마다 가중치, 적지 않은 뼈는 0)이고, 모델 에디터가 마스크를 만들고(고른 뼈의 가지를 섞는 깊이로 채움) 뼈마다 고치며
   트리·관절에 가중치를 보이고 겹칠 클립을 미리보기의 레이어 1 에 그 마스크로 튼다. 애니메이터는 레이어 1..3 의 클립·가중치·더하기·마스크 이름 칸을 들고(칸이 바뀔 때만 다시 건다), 스크립트는 `UseLayerMask` 로 이름을 건다.
   **창을 다시 짰다**(D-465, 2026-11-26): 왼쪽 계층(뼈대 트리 안의 소켓·마스크·애니메이션), 가운데 미리보기, 오른쪽 디테일(고른 것 하나의 폼), 아래 눈금 있는 시간 축과 이벤트 트랙이다. 공용 위젯 `Widget::Timeline` 등을 새로 만들었다.
   3 단계(3a~3d)가 모두 섰다. 남은 것: 미리보기 전용 붙이기, 캔버스 뷰에서 소켓 끌기, 스킨이 아닌 메시의 노드 자리, 클립 칸을 이름으로 고르기, 창의 스택과 에디터의 스택이 같은 모델 메타를 고칠 때의 되돌리기 겹침.
4. 뼈대 공유와 리타기팅(이름 맞추기 → 체인).
5. 꺼내기(3.1), 메시 편집·웨이트 페인트·LOD·콜라이더·래그돌.
6. 그 뒤: 상태 머신 그래프, 블렌드셰이프, 압축.

## 5. `[열림]`

- 4 의 단계 순서.
- 모델 놓기 - `.jobject` 계약을 기다린다(3.3).
- 꺼낸 메시의 형식과 원본이 바뀐 뒤의 처리(3.1).
