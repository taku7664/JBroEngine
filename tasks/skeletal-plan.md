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

### 3.2 `[제안]` 나머지

1. **뼈대 단위**: `Skeleton` 은 모델의 하위 에셋이되, 다른 모델이 메타에서 "이 뼈대를 쓴다" 를 가리킬 수 있다 - 같은 이름·계층이면 클립을 같이 쓴다(Unreal 의 호환 뼈대).
   소켓은 뼈대 소켓과 메시 소켓 둘 다. 리타기팅은 가져올 때 엔진 휴머노이드 프로필로 이름을 맞추고(Godot), 뒤에 체인 리타기터(Unreal IK Rig).
2. **뼈 표현**: 오브젝트가 아니라 컴포넌트 안 포즈 배열. 모델 에디터의 뼈 기즈모는 뼈 번호로 고친다. 캔버스에서 붙이기는 소켓을 가리키는 붙이기 컴포넌트(Godot `BoneAttachment3D` 꼴).
3. **컴포넌트**: 새 컴포넌트는 `Animator3D` 하나. 스킨 메시면 `MeshRenderer3D` 가 자기나 조상의 `Animator3D` 포즈로 스키닝한다. 노드 트리째 놓기(todo-3d 10 번)를 같이 한다 -
   메시가 여럿인 캐릭터가 한 `Animator3D` 를 나눠 쓰려면 필요하다.
4. **미리보기와 게임을 같은 길로**: 모델 에디터 창은 모델마다 여는 비고유 패널(D-284 의 스프라이트 뷰어 꼴)이고 창마다 미리보기 캔버스와 커맨드 스택을 둔다(D-323 의 그래프 창 꼴).
   미리보기도 게임과 같은 `Animator3D`·스키닝·그림자 경로로 그린다.
5. **편집 형식과 런타임 형식 분리**: 클립은 엔진의 키프레임 형식으로 편집하고, 런타임 형식은 셰이더·메시 쿡처럼 굽기 산출물이다(에디터의 즉석 굽기와 `.jpak`).
   런타임을 자체로 하든 ozz 를 쓰든, 압축(ACL)을 붙이든 편집 도구는 그대로다. ozz·ACL 은 서드파티라 들이기 전에 확인한다. 1 차는 자체 선형·slerp 샘플러로 시작하는 것을 제안한다.
6. **물리**: 뼈대를 이름과 부모 번호의 배열로 두어 Jolt `Skeleton` 으로 바로 옮긴다. 모델 에디터의 콜라이더 탭이 래그돌로 이어지고 `SkeletonMapper` 가 애니메이션 뼈대와 래그돌 뼈대를 잇는다.
7. **정점·스키닝**: 스킨 메시만 뼈 번호 4 개(16 비트)·가중치 4 개를 더한 정점 형식, 가중치 합은 1(웨이트 페인트도 자동 정규화). 1 차는 정점 셰이더 스키닝, 뼈 행렬은 3x4 로
   상수 링에 256 개(12 KB). 그림자 패스가 많아 반복이 무거워지면 컴퓨트 스키닝으로 옮긴다.
8. **포즈 계산**: CPU, 프레임 경로라 할당 없이. 캐릭터가 많으면 물리처럼 `WorkerPool::ParallelFor`.
9. **상태 머신**: 1 차는 스크립트의 `Play`·`CrossFade`. 그래프는 2 차에 노드 그래프 프레임워크(D-323) 위에.

## 4. 단계 `[제안]`

0. 정점 스킨 칸, `Skeleton`·`AnimationClip` 하위 에셋, 가져오기.
1. `Animator3D`, 워커 포즈 계산, 정점 셰이더 스키닝(그림자 포함), 스크립트 API.
2. 노드 트리째 놓기(todo-3d 10 번), 소켓과 붙이기 컴포넌트.
3. 모델 에디터 창: 미리보기 캔버스·뼈대 트리·타임라인·클립 설정·소켓.
4. 뼈대 공유와 리타기팅(이름 맞추기 → 체인).
5. 꺼내기(3.1), 메시 편집·웨이트 페인트·LOD·콜라이더·래그돌.
6. 그 뒤: 상태 머신 그래프, 블렌드셰이프, 압축.

## 5. `[열림]`

- 3.2 의 아홉 항목과 4 의 단계 순서.
- 꺼낸 메시의 형식과 원본이 바뀐 뒤의 처리(3.1).
