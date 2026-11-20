# 3D 물리 계획 (Jolt Physics)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-11-19 사용자 결정("Jolt 로 가자")으로 시작했다(D-414, todo-3d 우선순위 9).
> 같은 날 Jolt v5.7.0 을 `ThirdParty/Jolt` 에 넣고 혼자 빌드되는 것까지 했다. 같은 날 §2 의 제안이 모두 그대로 확정됐다("제안대로 진행", D-415)
> - 아래 §2 의 표시는 확정이다. 1 단계(커널 `JBroPhysics3D`)와 2 단계(컴포넌트와 어댑터, D-416)가 섰다 - 에디터에서 재생하면 3D 오브젝트가 떨어져 선다.
> 3 단계(충돌·트리거 훅과 `Physics3DService` 질의, D-417)도 섰다 - 3D 스크립트 DLL 이 훅을 받고 반직선을 쏜다. 4 단계(캔버스 뷰의 3D 콜라이더 선·메시 콜라이더, D-418)도 섰다.
> 2026-11-20 사용자 지시("권고안대로 마쳐 달라, 가성비보다 장기적으로 건강한 구조")로 5 단계(D-419)와 6 단계(CCD D-425·조인트 D-426·캐릭터 D-427), §4 의 열림 다섯(D-420~D-424)을 마쳤다.
> 모호한 곳은 권고안으로 정했고 각 결정에 고른 까닭이 있다. 남은 것은 §4 의 `[열림]` 이다.

## 0. 한 줄 요약

3D 의 충돌·강체 풀이는 **Jolt 에 맡기고**, 엔진이 보이는 모양(컴포넌트·스크립트 훅·질의 서비스·에디터)은 **2D 물리를 그대로 따른다.**
Jolt 타입은 새 커널 모듈 하나 밖으로 나가지 않는다 - 컴포넌트와 스크립트는 Jolt 를 모르고, 나중에 커널을 바꿔도 그 둘은 그대로다.

## 1. 지금 상태 (2026-11-19)

| 자리 | 있는 것 |
|---|---|
| 컴포넌트 | (시작할 때) `Component::Rigidbody3D`(`velocity`·`mass` 뿐), `Component::Collider3D`(`size` 뿐). D-53 의 골격이었다 → 2 단계에서 §2.2 의 필드가 됐다 |
| 핸들 | (시작할 때) `Handle::Rigidbody3D`(`Get/SetVelocity`·`Get/SetMass`), `Handle::Collider3D` → 2 단계에서 `Get/SetLinearVelocity`·`Get/SetAngularVelocity` |
| 시스템 | (시작할 때) 없었다 → 2 단계의 `Physics3DSystem` |
| 스크립트 훅 | `GameScript3D` 에 충돌·트리거 훅이 없다(UI 훅만). 2D 는 `OnCollisionEnter/Stay/Exit`·`OnTriggerEnter/Stay/Exit` |
| 질의 | 없다. 2D 는 `Physics2DService` 의 `Raycast`·`OverlapBox`·`OverlapPoint`·`OverlapCircle`·`CircleCast`·`BoxCast` |
| 에디터 | 콜라이더 표시가 없다. 2D 는 콜라이더를 그리고 폴리곤 포인트를 편집한다 |
| 레이어 | 프로젝트의 `physicsLayers` 이름 표(호스트)와 `PhysicsLayerMask`(시작할 때 Framework2D, 2 단계에서 Core 로) |
| Jolt | `ThirdParty/Jolt`(v5.7.0, 커밋 `cf16769bb0ede9c45d57d06068ff0689e9702f5c`, MIT). `Jolt.vcxproj` 가 `Jolt.cmake` 의 기본 목록(130 개 `.cpp`)을 정적 라이브러리로 빌드한다. 아직 아무 모듈도 링크하지 않는다 |

2D 쪽 구조(physics-plan §3): 캔버스를 모르는 커널 `JBroPhysics2D`(월드·바디·도형·질의) → 어댑터 `Physics2DSystem`(Framework2DSystem, 컴포넌트를 바디로 옮기고 결과를
`Transform2D` 에 쓰고 훅을 부른다) → 스크립트 서비스 `Physics2DService`. 고정 스텝은 호스트의 `TimeSystem` 이 준다(D-242). 좁은 판정의 워커 수는 빌드 설정 `Build.PhysicsThreads`(D-223).

## 2. 설계 (2026-11-19 확정, D-415)

### 2.1 층 (확정, D-415)

```
ThirdParty/Jolt (정적 라이브러리, 고치지 않음)
   ↑ include 경로를 선언한 모듈은 하나뿐
Modules/JBroPhysics3D   - 새 Tier E 커널 모듈. 캔버스를 모른다. 공개 헤더는 JBro 타입만(Vector3·Quaternion·Handle 번호),
                          Jolt 헤더는 Source/ 안에서만 include 한다. 월드·바디 만들기/지우기·스텝·접촉 사건 꺼내 가기·질의.
   ↑
Modules/JBroFramework3DSystem 의 Physics3DSystem - 어댑터. 2D 의 Physics2DSystem 자리.
   ↑
Framework3D 의 Physics3DService(스크립트) · GameScript3D 의 충돌 훅
```

- 2D 처럼 커널을 따로 두는 까닭: 캔버스 없이 시험할 수 있고, Jolt 를 보는 모듈이 하나로 묶인다. ThirdParty README 의 "래핑하지 않는다" 는
  라이브러리 API 를 통째로 옮겨 적는 래퍼를 말한다 - 이 모듈은 엔진이 쓰는 물리 연산만 JBro 타입으로 내는 커널 경계다.
- 대안: 커널 모듈 없이 `Physics3DSystem` 이 Jolt 를 곧장 부른다. 파일은 줄지만 Framework3DSystem 전체가 Jolt 헤더를 볼 수 있게 된다.

### 2.2 컴포넌트 (확정, D-415) - 2D 의 필드 이름을 따른다

`Rigidbody3D`:

| 필드 | 2D 대응 | 비고 |
|---|---|---|
| `bodyType` (`Dynamic`·`Kinematic`·`Static`) | `bodyType` | `BodyType2D` 와 같은 값. 이름은 `BodyType3D` |
| `linearVelocity` (`Vector3`, 저장 안 함) | `linearVelocity` | 지금의 `velocity` 를 바꾼 이름. 핸들의 `Get/SetVelocity` 는 `Get/SetLinearVelocity` 로 맞춘다(2D 핸들과 같게) |
| `angularVelocity` (`Vector3`, 저장 안 함) | `angularVelocity`(Float) | 라디안/초 |
| `mass`·`gravityScale`·`linearDamping`·`angularDamping`·`canSleep` | 같음 | |
| `freezePositionX/Y/Z`·`freezeRotationX/Y/Z` | `freezePositionX/Y`·`fixedRotation` | Jolt 의 허용 자유도(`EAllowedDOFs`)로 간다 |

`Collider3D`:

| 필드 | 비고 |
|---|---|
| `shape` (`Box`·`Sphere`·`Capsule`·`Mesh`) | `Mesh` 는 `MeshRenderer3D` 와 같은 메시 에셋. Jolt 의 메시 도형은 움직이지 않는 바디만 받으므로 Dynamic 에 붙이면 경고하고 볼록 껍질로 바꾼다 `[열림]` |
| `offset`·`size`·`radius`·`height` | 2D 의 `offset`·`size`·`radius`. 캡슐은 `radius`·`height` |
| `isTrigger`·`friction`·`restitution`·`layer`·`mask` | 2D 와 같은 이름·범위 |

- **`PhysicsLayerMask` 를 JBroCore 로 옮긴다.** 차원과 무관한 공개 값 타입은 Core 에 한 번이다(ProjectRule). 지금은 Framework2D 에 있다.
- ~~2D 의 조인트(`DistanceJoint2D`·`HingeJoint2D`)는 뒤로 둔다(§3 의 6).~~ → D-426: `DistanceJoint3D`·`HingeJoint3D`(§3.6).

### 2.3 스텝과 스레드 (확정, D-415)

- 고정 스텝은 2D 와 같은 `OnFixedUpdate`(호스트 `TimeSystem`). 한 고정 스텝에 Jolt `Update` 한 번, 충돌 스텝 1.
- **할당:** Jolt 의 할당 훅(`JPH::Allocate` 등)을 등록하고, 스텝 임시 메모리는 미리 잡은 `TempAllocatorImpl` 하나를 쓴다 - 프레임 경로에 힙 할당이 없다(ProjectRule).
  바디·도형을 만들 때의 할당은 프레임 경로가 아니다.
- **스레드:** 1 단계는 Jolt 의 `JobSystemSingleThreaded` 로 시작한다. ~~그다음 `JBroTask` 위에 Jolt `JobSystem` 을 구현해 `Build.PhysicsThreads`(D-223)를 따른다.~~
  → D-419 에서 바꿨다: 워커는 **물리 전용 Jolt 스레드 풀**(`JobSystemThreadPool`)이고 `Build.PhysicsThreads` 를 따른다. `TaskManager` 는 공개 함수가 메인 스레드 전용이고
  넣을 때마다 `OwnerPtr<TaskGroup>` 을 힙에 만든다 - Jolt 는 워커 안에서 잡을 만들고 스텝마다 수십 개를 넣으므로 그 위에 세우면 계약(메인 전용)과 규칙(프레임 경로 힙 할당 금지)을 둘 다 어긴다.
  2D 물리도 같은 까닭으로 제 워커를 든다(D-223). Jolt 의 풀은 시작할 때만 할당하고(잡·장벽 고정 표) 그 할당도 커널의 훅을 지난다.
  → **D-428 에서 원래 계획대로 되돌렸다:** 워커는 `JBroTask` 가 든다. `TaskManager` 를 고치지 않고 그 옆에 나눴다 합치는 일의 원시 `WorkerPool` 을 두었다 -
  어느 스레드에서나(워커 안에서도) 넣고, 넣을 때 할당하지 않으며(고리 큐를 `Start` 에서 한 번 잡는다), 콜백이 없다. 커널의 `TaskJobSystem`(`JobSystemWithBarrier`)이 Jolt 의 잡을
  미리 잡은 자유 목록에서 꺼내 그 위에 넣는다. 모듈마다 스레드를 들지 않는다는 ProjectRule 의 규칙(D-209)을 지키려는 것이다 - D-419 는 그 규칙의 적히지 않은 예외를 하나 더 만들었었다.
- 결정론(`JPH_CROSS_PLATFORM_DETERMINISTIC`)은 켜지 않는다 - 네트워크 복제(network-plan)는 스냅숏 델타라 필요하지 않다 `[열림]`.

### 2.4 스크립트 (확정, D-415) - 2D 와 같은 이름

- `GameScript3D` 에 `OnCollisionEnter/Stay/Exit(const Collision3D&)`·`OnTriggerEnter/Stay/Exit(const Collision3D&)`. `Collision3D` 는 POD(상대 `GameObject` 핸들, 접촉점, 법선, 충격량)다(DLL 경계).
- Jolt 의 접촉 콜백은 워커 스레드에서 온다 - 커널이 사건을 고정 용량 버퍼에 모으고, 어댑터가 스텝 뒤 메인 스레드에서 꺼내 훅을 부른다(2D 의 D-207 과 같은 꺼내 가기).
- `Physics3DService`: `Raycast`·`SphereCast`·`BoxCast`·`OverlapSphere`·`OverlapBox` 와 `RaycastHit3D`. 2D 의 `OverlapPoint` 는 3D 에서 쓸 데가 적어 뺀다.

### 2.5 에디터 (확정, D-415)

- 캔버스 뷰가 고른 오브젝트의 콜라이더를 선으로 그린다(2D 의 콜라이더 표시 단추를 3D 에도). 크기 손잡이는 뒤로 둔다.
- 인스펙터는 리플렉션으로 그대로 나온다. 프로젝트 설정의 물리 레이어 표는 2D 와 함께 쓴다.

## 3. 단계

| # | 할 것 | 완료 조건 | 상태 |
|---|---|---|---|
| 0 | Jolt 를 소스째 넣고 자기 빌드 단위 | `Jolt.lib` 가 경고 없이 빌드 | `[완료]` 2026-11-19 · D-414 |
| 1 | 커널 `JBroPhysics3D`: 월드·바디(상자·구·캡슐)·스텝·할당 훅·싱글 스레드 잡 | 캔버스 없는 시험: 바닥 위에 떨어진 상자가 멈춘다, 구가 경사를 굴러간다, 할당 훅을 거치고 스텝 중 힙 할당 0 | `[완료]` 2026-11-19 · D-415 · §3.1 |
| 2 | 컴포넌트 필드(§2.2)·`PhysicsLayerMask` 를 Core 로·어댑터 `Physics3DSystem` | 에디터에서 재생하면 상자가 떨어져 바닥에 서고, 멈추면 제자리. 레이어·마스크가 거른다 | `[완료]` 2026-11-19 · D-416 · §3.2 |
| 3 | 접촉 사건과 스크립트 훅·`Physics3DService` 질의 | 3D 시험 DLL 의 스크립트가 `OnCollisionEnter`·`OnTriggerEnter` 를 받고 `Raycast` 가 맞는다 | `[완료]` 2026-11-19 · D-417 · §3.3 |
| 4 | 에디터: 콜라이더 그리기·메시 콜라이더 | 캔버스 뷰 스크린숏에 콜라이더 선, 메시 바닥 위에 상자가 선다 | `[완료]` 2026-11-20 · D-418 · §3.4 |
| 5 | 잡 시스템을 `JBroTask` 위로·`Build.PhysicsThreads` | 바디 1000 개에서 워커 수에 따라 스텝 시간이 준다 | `[완료]` 2026-11-20 · D-419 · D-428(`JBroTask` 의 `WorkerPool` 위) · §3.5 |
| 6 | 뒤의 것: 조인트·캐릭터 컨트롤러(`CharacterVirtual`)·CCD(`EMotionQuality::LinearCast`) | 항목마다 따로 정한다 | `[완료]` 2026-11-20 · CCD D-425 · 조인트 D-426 · 캐릭터 D-427 · §3.6 |

### 3.1 1 단계에서 한 것과 실측 (D-415)

- 공개 헤더 `JBro/Physics3D/World.h` 하나: `World`(바디 만들기·지우기·자세·속도·깨어 있음·userData·중력·`Step`), `BodyDef`·`ShapeDesc`(상자·구·캡슐, `offset`),
  `GetAllocationStats`. Jolt 헤더는 `Source/` 의 두 파일만 본다 - `World.cpp`(월드)와 `JoltRuntime.cpp`(프로세스 전역 준비: 할당 훅·로그·타입 등록, 월드 수로 세우고 내린다).
- **층:** Jolt 의 오브젝트 층은 16 비트 번호 하나라 2D 의 32 비트 `layer`·`mask` 가 들어가지 않는다. 그래서 (layer, mask, 움직이는가) 조합마다 번호를 매기는 고정 표(1024)를 두고,
  짝 거르기는 그 표로 2D 와 같은 식을 잰다. 브로드페이즈는 움직이지 않는 것·움직이는 것 둘이다.
- **모든 축을 고정한 동적 바디**는 Jolt 가 받지 않으므로(자유도 0) 키네마틱으로 받는다. 상자의 둥근 모서리(볼록 반지름)는 Jolt 가 가장 짧은 반 크기 안으로 스스로 줄여 얇은 판도 받는다(처음에 커널도 줄였는데 뮤테이션이 같은 동작임을 보여 걷어냈다).
- **할당:** 깨어 있는 몸이 움직이면 Jolt 가 브로드페이즈 트리를 다시 지으며(`QuadTree::UpdatePrepare`) **스텝마다 둘씩** 할당 훅을 지났다(상자 20 개, 120 스텝에 240 번 - 호출 스택으로 확인).
  잠든 더미에서는 0 이라 처음 시험은 이것을 놓쳤다 - 시험의 상자를 잠들지 못하게 해서 찾았다. Jolt 의 구조라 피할 수 없어, 훅 뒤에 크기 칸(16 B~64 KiB)마다 놓인 블록을 다시 쓰는 풀을 두었다.
  지금은 같은 장면에서 훅 240 번, OS 힙 0 번이다. 풀은 블록을 프로세스가 끝날 때까지 돌려주지 않는다.
- **겹침 여유:** 상자는 0.5 가 아니라 0.48 에 선다 - Jolt 가 쌓인 것이 떨지 않게 접촉을 `mPenetrationSlop`(0.02)만큼 겹친 채 둔다. 시험은 그 범위로 본다.
- 시험 묶음 `Physics3DWorld`(여섯): 떨어져 서고 잠드는 상자, 20 도 경사를 미끄러지지 않고 구르는 구, 마스크·트리거로 지나가는 바디, 중력 배율·축 고정·질량(무거운 상자가 가벼운 것을 밀고 간다)·모든 축 고정,
  도형 거절·번호 수명(지운 번호가 같은 자리의 새 바디로 살아나지 않는다)·offset·두 번째 월드, 스텝 중 OS 힙 0.

### 3.2 2 단계에서 한 것과 실측 (D-416)

- **한 오브젝트의 콜라이더 여럿**(사용자 결정 "2D 처럼 여럿"): 고체 콜라이더는 그 오브젝트의 바디 하나에 복합 도형(Jolt `StaticCompoundShape`)으로 든다.
  마찰·반발은 콜라이더마다다 - 커널이 잎 도형의 userData 에 둘을 싣고, 접촉 콜백(`SurfaceMixer`)이 닿은 두 잎의 값을 섞는다(마찰은 기하 평균, 반발은 큰 쪽 - Jolt 의 기본 섞기).
  트리거 콜라이더는 저마다 키네마틱 센서 바디이고 스텝마다 오브젝트의 자세로 옮긴다. 레이어·마스크는 Jolt 가 바디마다 하나라 고체 콜라이더 여럿은 **첫 콜라이더의 값**을 쓴다(§4).
- **커널 확장:** `BodyDef.shapes`(도형 여럿), `ShapeDesc.friction/restitution`, `SetShapes`(질량은 지금 질량으로 새 도형의 관성 모양에 다시 나눈다 - Jolt 가 밀도로 다시 셈하지 않게),
  `SetBodyProperties`(질량·중력 배율·감쇠·축 고정·잠듦·레이어, 바꾸면 깨운다 - 처음에는 깨우지 않아 중력을 0 으로 둔 동안 잠든 몸이 중력을 되돌려도 떨어지지 않았다).
- **컴포넌트:** `Rigidbody3D` 는 `bodyType`·`linearVelocity`·`angularVelocity`·`mass`·`gravityScale`·감쇠 둘·축 고정 여섯·`canSleep`(열넷), `Collider3D` 는 `shape`(Box·Sphere·Capsule)·`offset`·`size`·`radius`·`height`(캡슐 전체 높이, 반지름 두 배보다 짧으면 구)·`isTrigger`·`friction`·`restitution`·`layer`·`mask`(열).
  크기는 트랜스폼의 월드 크기를 곱한다 - 구는 세 축 중 큰 것, 캡슐 반지름은 X·Z 중 큰 것, 높이는 Y. 핸들은 `Get/SetVelocity` 를 2D 와 같은 `Get/SetLinearVelocity` 로 바꾸고 각속도를 더했다.
- **`PhysicsLayerMask`·`PhysicsLayerCount`·`AllPhysicsLayers` 를 Core 로**(`Types/PhysicsLayerMask.h`, 리플렉션은 `Reflection/PhysicsLayerMaskReflection.h`). 타입 이름(`JBro.PhysicsLayerMask`)은 그대로라 파일·에디터가 바뀌지 않는다.
  `jbroc` 의 API 목록(`JBroScriptApiCatalog2D` 의 정의 자리)이 옛 헤더를 가리켜 스크립트 시험 빌드가 멈췄다 - 새 자리로 고쳤다.
- **어댑터 `Physics3DSystem`**(Framework3DSystem, 고정 스텝, 실행 순서 200): 2D 어댑터의 구조 그대로 - 바뀐 것만 지문으로 알아 다시 먹이고, 정적인 몸은 에디터가 옮긴 자리를 따르고,
  움직이는 몸은 커널이 주인이되 지난번에 쓴 값과 다르면 누가 옮긴 것으로 보고 따른다. 월드 자세는 `Transform3DSystem` 과 같은 식으로 부모를 거슬러 직접 잰다(고정 스텝이 그 시스템보다 먼저 돈다).
  되쓰기는 부모의 역으로 로컬에 돌려 쓴다. 에디터·게임 호스트가 `JBroPhysics3D`·`Jolt` 를 링크한다(게임 호스트는 3D 빌드만).
- 시험: `Physics3DWorld` 여덟(복합 도형 아령, 경사 위 마찰 0·1 상자, 제자리 도형·성질 바꾸기 더함), 에디터 `TestPlayingRunsPhysicsAndStoppingPutsItBackIn3D` -
  상자 0.48·세운 캡슐 0.98·마스크로 빠진 상자는 떨어짐·구 위에 선 두 콜라이더 토템 1.48·트리거를 지나 바닥에 선 상자·크기 2 상자 0.98, 떨어지는 동안 속도 되쓰기, X 회전만 막은 채 세 축으로 돌린 상자, 재생 중 콜라이더 키우기, 정지하면 제자리. 뮤테이션 열셋이 모두 잡혔다.

### 3.3 3 단계에서 한 것과 실측 (D-417)

- **커널의 접촉 사건:** 하나의 접촉 듣개(`ContactTracker`)가 표면 섞기와 사건 모으기를 함께 한다. Jolt 는 잎 도형 쌍마다 알리므로 바디 쌍마다 닿은 잎 수를 세어
  0→1 이 시작, 1→0 이 끝이다. 유지는 스텝마다 쌍 하나에 한 번, 시작한 스텝에는 시작만 있다(2D 와 같다). 법선은 사건의 A 에서 B 로 간다.
  **잠들 때:** Jolt 는 바디가 잠들면 그 접촉 캐시를 비우며 "제거" 로 알린다 - 처음에는 이것을 떨어진 것으로 보아 바닥에 놓인 상자가 잠드는 순간 끝이 나왔다.
  그래서 제거 알림은 세기만 줄이고 스텝 뒤에 가린다: 한쪽이라도 잠들었으면 잠든 쌍으로 두고(끝이 아니다, 깨어 다시 닿으면 새 시작도 아니다), 둘 다 깨어 있는데 다시 닿지 않았으면 그때 끝이다.
  정적 바디는 Jolt 에서 늘 "활성 아님" 이라 잠듦은 움직이는 바디에만 따진다(처음에 이것을 놓쳐 잠든 상자를 들어 올려도 끝이 나지 않았다).
  바디를 지우면 Jolt 는 그 접촉을 따로 알리지 않으므로 커널이 지우기 전에 끝을 만들어 다음 스텝의 끝에 싣는다. 잠든 동안은 유지가 나오지 않는다(2D 와 같다).
- **트리거:** 센서 바디는 키네마틱이라 그대로 두면 동적인 몸과만 겹친다. 움직이는(켜진 동적·키네마틱 몸이 있는) 오브젝트의 트리거만 `triggerSeesStatic` 으로 정적·키네마틱 몸과의 겹침도 본다 -
  정적인 트리거와 정적인 바닥은 2D 처럼 만나지 않는다. 한 오브젝트의 트리거와 그 오브젝트의 몸이 겹친 것은 알리지 않는다. 센서도 바디처럼 userData 가 오브젝트 아이디다.
- **커널 질의:** `Raycast`·`RaycastAll`(바디마다 한 번, 거리 순)·`OverlapSphere`·`OverlapBox`(돌린 상자)·`SphereCast`·`BoxCast`. 레이어 마스크는 조합 표의 레이어로 거르고 트리거도 든다.
  밀기의 법선은 Jolt 의 밀어낸 축의 반대(쏜 쪽을 향한다)이고 뒷면은 맞지 않는다.
- **스크립트(Tier S, Framework3D):** `Collision3D`·`RaycastHit3D`(2D 와 같은 모양, `ComponentTypes3D.h`), `System::IPhysics3DSystem`, `Service::Physics3DService`(`GetFramework3DServices().Physics3D`),
  `GameScript3D::OnCollisionEnter/Stay/Exit`·`OnTriggerEnter/Stay/Exit`. 판번호: `Framework3DSystemContextAbiVersion` 2(물리 슬롯), `Framework3DServiceContextAbiVersion` 4(서비스와 훅의 가상 함수 표).
- **어댑터:** `Physics3DSystem` 이 `IPhysics3DSystem` 을 구현하고, 스텝 뒤 끝 → 시작 → 유지 차례로 두 오브젝트의 `GameScript3D` 훅을 `ScriptCallGuard` 로 부른다(발송 중 파괴는 미룬다, 2D 와 같다).
  사건·질의가 오브젝트를 찾는 표(`owners`)는 스텝마다 본 오브젝트를 표시하고 사건을 보낸 뒤 지운다 - 이번 스텝에 지운 오브젝트의 끝 훅까지는 남는다. 질의는 마지막 스텝의 바디를 본다(2D 는 지금의 컴포넌트).
- 시험: `Physics3DWorld` 열(접촉 사건 - 시작 한 번·유지·잠들어도 끝 아님·들어 올리면 끝·지우면 끝·트리거 시작과 끝·움직이는 트리거만 정적을 봄, 질의 - 반직선·마스크·짧은 반직선·방향 없음·모든 반직선·구와 상자 겹침·구와 상자 밀기),
  `ScriptHotReload` 의 `TestA3DScriptHearsCollisionsAndQueries`(시험 DLL 의 `ProbeContact3D`·`ProbeListener3D`: 바닥 충돌 시작 1·유지 31·법선 y -1·상대는 정적, 바닥도 같은 착지를 듣고 상대를 안다, 트리거는 콜라이더 쌍마다라 들어감 3·나감 2 - 상자와 아래로 삐져나온 트리거 구가 게이트를 지나고 트리거 구는 바닥에 남는다, 반직선 0.48). 뮤테이션 열다섯 중 열넷이 잡혔다(남은 하나는 쓰이지 않는 법선 뒤집기).

### 3.4 4 단계에서 한 것과 실측 (D-418)

- **사용자 결정 둘:** (1) `Collider3D.convex` 칸을 둔다(유니티와 같다). 켜면 메시의 볼록 껍질로 어떤 몸에도 쓰고, 끈 채 움직이는 몸(켜진 동적·키네마틱 `Rigidbody3D`)이나
  트리거에 붙으면 껍질로 바꾸고 경고를 한 번 남긴다. (2) 빈 `meshId` 는 같은 오브젝트의 `MeshRenderer3D` 가 그리는 메시를 쓴다(빈 `materialId` 와 같은 규칙).
- **컴포넌트:** `ColliderShape3D::Mesh`(뒤에 더함), `meshId`·`mesh`(해석 패스가 채우는 짝, D-395)·`convex` - `Collider3D` 필드가 열셋이 됐다.
- **커널:** `ShapeType::ConvexHull`(점 넷 이상, 한 평면의 점이면 Jolt 가 납작한 껍질을 만든다)·`ShapeType::Mesh`(삼각형, 움직이는 몸은 받지 않는다 - 만들기와 `SetShapes` 모두 거절).
- **감는 방향:** 엔진의 메시는 바깥에서 볼 때 **시계**가 앞면이다(D-108, 모델 임포트도 파일의 반시계를 뒤집어 둔다). Jolt 는 **반시계** 쪽만 막는다 -
  처음에는 그대로 넘겨 정육면체 바닥의 윗면이 뒷면이 되었고, 공이 바닥을 뚫고 아랫면 안쪽에 걸렸다(y -0.5). 콜라이더 메시를 꺼내는 `GatherColliderMesh` 가 삼각형마다 감기를 뒤집는다.
- **공용 길 `Framework3DSystem/Physics/ColliderMesh.h`:** `FindColliderMeshSource`(빈 `meshId` 면 렌더러의 메시)·`GatherColliderMesh`(빌트인은 메시 라이브러리가 짓고, 모델 메시는 에셋 시스템의 CPU 자료).
  물리 어댑터와 캔버스 뷰가 같은 길로 메시를 찾는다. 어댑터는 콜라이더마다 점 사본을 두고 메시·자료 세대·크기가 바뀔 때만 다시 읽고 곱한다(프레임마다 메시를 읽지 않는다).
- **캔버스 뷰:** 콜라이더 단추가 3D 에도 선다. 상자는 열둘의 모서리, 구는 세 원, 캡슐은 두 끝 원과 옆 윤곽·네 줄, 메시는 삼각형의 변(4096 개까지). 색은 2D 와 같다(고체 초록·고른 것 파랑·트리거 노랑).
  크기 규칙은 물리와 같다(구는 세 축 중 큰 크기, 캡슐 반지름은 X·Z 중 큰 것). 선은 바닥 격자처럼 토막 내어 투영한다.
- 시험: `Physics3DWorld` 열하나(메시 바닥 위의 껍질 상자 0.48, 움직이는 몸의 메시 거절, 점 셋 껍질 거절), 에디터 `TestMeshCollidersIn3D`(정육면체 메시 바닥 위에 구의 껍질 0.491·볼록 상자 0.98,
  경고 한 줄, 공중의 정적 정육면체 안에서 위로 쏜 반직선 - 볼록이면 그 자리에서 맞고 삼각형이면 뒷면을 지나 빠져나간다), `TestTheCanvasViewDrawsColliders3D`(상자·구·캡슐 트리거·메시를 놓고 단추를 끄고 켜면 14509 픽셀씩 바뀐다).
- **단일 `Raycast` 의 뒷면:** 설정 없는 Jolt 길은 삼각형의 뒷면도 맞아 메시 상자 안에서 쏜 반직선이 윗면 안쪽에 걸렸고 `RaycastAll` 과 달랐다. 설정을 받는 길로 바꿔 볼록은 속이 찬 것, 삼각형은 뒷면을 맞지 않는다.
- ~~`[열림]` 모델 메시(빌트인이 아닌) 콜라이더는 시험하지 않았다.~~ → D-424: glTF 사각형 둘(위를 보는 것·같은 삼각형을 거꾸로 감은 것)을 모델로 읽어 정적 메시 콜라이더로 깔았다.
  위를 보는 쪽에 떨어뜨린 공은 0.48 에 서고, 거꾸로 감은 쪽은 뚫고 떨어진다(-16.9) - 물리가 막는 앞면이 렌더러가 그리는 앞면과 같다. 모델 메시의 감기 뒤집기를 빼면 이 시험이 죽는다.

### 3.5 5 단계에서 한 것과 실측 (D-419)

- **커널:** `World::SetWorkerCount`(0 이면 `JobSystemSingleThreaded`, 그 위면 그 수의 워커 - D-419 에는 Jolt 의 `JobSystemThreadPool`, D-428 부터 `JBroTask` 의 `WorkerPool` 위의 `TaskJobSystem`,
  상한 `MaxWorkerCount` 16 - 2D 와 같은 값), 같은 수면 아무것도 하지 않는다. D-428 실측: 같은 시험에서 메인 한 스레드 6229 ms, 워커 넷 1479 ms(4.2 배 - 기계가 더 바빴던 때라 두 값 모두 D-419 보다 크다).
  `RecommendWorkerCount(일감, 코어 수)`: 256 미만이면 0, 그 위로 256 마다 하나, `코어 - 1` 과 16 을 넘지 않는다. `[가정]` 기준 256 은 실측 하나(아래)에서 고른 것이다 - 2D 의 1024 보다 낮은 까닭은
  Jolt 가 넓은 판정·섬 풀이까지 나눠 같은 일감에서 더 많이 얻기 때문이다.
- **실측(디버그 빌드, 16 스레드 PC):** 서로 닿은 상자 1000 개의 60 스텝이 메인 한 스레드 5172 ms, 워커 넷 1174 ms(4.4 배). 더미의 높이는 같다(시험 `TestWorkersSpeedUpALargePile`).
- **설정:** 빌드 캔버스를 읽어 일감을 세는 길을 2D 코드에서 떼어 `JBroHost` 의 `SumBuildCanvasWork` 로 옮겼다 - 2D·3D 가 함께 쓰고 세는 것만 다르다. 3D 는 `PhysicsThreads3D.h`
  (`CountPhysicsWork3D` = 켜진 `Collider3D` 수, `RecommendProjectPhysicsWorkers3D`, `ResolvePhysicsWorkerCount3D`)와 `Framework3D::SetPhysicsWorkerCount`. 에디터(프로젝트 열기·설정 저장·"추천 값 사용"·게임 빌드의 고정 값)와
  게임 호스트(3D 구성)가 3D 에도 연결된다. 시험 `TestThePhysicsThreadsSettingReachesPlayIn3D`(Single 0·Auto 는 600 콜라이더의 3D 기준·Workers 3).

### 3.6 6 단계 - 뒤의 것 (D-425~)

항목마다 사용자 지시("권고안대로, 장기적으로 건강한 구조")에 따라 권고안으로 정했다. 고른 것과 까닭은 각 결정에 있다.

- **CCD(D-425):** 동적인 몸은 모두 Jolt 의 `LinearCast` 다. 켜는 칸은 없다 - 2D 도 동적인 몸 모두에 저절로다(D-234). Jolt 는 한 스텝에 제 안쪽 반지름의 일정 몫보다 멀리 가는 몸만
  쓸어 보아 느린 몸에는 값이 거의 들지 않는다. 2D 와 달리 **동적인 몸끼리도** 본다(Jolt 가 그렇게 한다 - 2D 의 `[열림]` 하나를 3D 는 처음부터 넘는다). 트리거·키네마틱은 받지 않는다(트리거는 D-423 의 쓸기).
  시험 `TestFastBodiesDoNotTunnel`: 반지름 0.05 의 공이 한 스텝에 3 을 가며 두께 0.1 의 벽 앞에서 서고, 떠 있는 얇은 상자를 지나치지 않고 민다. `[열림]` 회전으로만 빠른 몸(도는 긴 막대의 끝)은 보지 않는다 - Jolt 의 `LinearCast` 가 이동만 쓴다.

- **조인트(D-426):** 2D 와 같은 두 컴포넌트·같은 필드 이름(`DistanceJoint3D`·`HingeJoint3D`, 앵커는 `Vector3`), 경첩에만 `axis`(이 오브젝트 로컬, 기본 X) 하나를 더했다.
  스크립트 핸들과 스크립트 필드 타입(`FieldType::DistanceJoint3D`·`HingeJoint3D`)도 2D 처럼 둔다. 2D 에 없는 조인트 종류(고정·슬라이더·원뿔)는 넣지 않았다 - 2D 와 짝이 맞는 것만.
  - 커널: `World::CreateDistanceJoint`·`CreateHingeJoint`·`Set*`·`DestroyJoint`·`GetHingeAngle`. 조인트는 자리마다 정의를 들고 Jolt 의 `LocalToBodyCOM` 공간으로 세운다.
    앵커는 바디 원점 기준으로 받고 커널이 질량 중심으로 옮긴다 - 그래서 **바디의 도형이 바뀌면(`SetShapes`) 그 바디의 조인트를 같은 정의로 다시 세운다**(Jolt 의 `SetShape` 는 제약에 알리지 않아 핀이 어긋났다).
    바디를 지우면 그 조인트도 지운다(Jolt 의 제약은 바디를 가리킨다).
  - 경첩의 각 0 은 "두 바디의 법선이 겹칠 때" 다. 어댑터가 처음 이을 때(상대·축이 바뀔 때) 이 오브젝트의 축·법선을 지금 자세로 상대 로컬(없으면 월드)에 옮겨 두어, 몸이 다시 만들어져도 각 0 이 옮지 않는다(2D 의 기준 각과 같은 일).
    Jolt 의 body1 을 상대, body2 를 이 오브젝트로 넣어 각과 모터가 "이 오브젝트가 상대에 대해, 축 둘레 오른손" 이다.
  - **한계는 ±180° 다**(2D 는 ±360°). Jolt 의 경첩이 반 바퀴까지 받는다. 0 을 넘는 lowerAngle·0 아래의 upperAngle 은 0 으로 본다. 필드의 범위도 그렇게 좁혔다.
  - `collideConnected` 가 거짓이면 두 바디의 접촉을 접촉 확인 콜백이 버린다(쌍마다 조인트 수를 센 표). 질의는 그대로 맞는다.
  - 시험: `Physics3DWorld` 의 `TestJoints`(단단한 거리 3·밧줄·경첩의 핀과 각의 부호·한계·모터·충돌 끄기와 켜기·바디와 함께 지워짐·도형을 바꿔도 핀), 에디터 `TestJointsIn3D`(월드에 박은 문과 기둥에 매단 추,
    자동 상대 앵커·자동 거리, 핸들, 끈 조인트가 풀림).
  - ~~`[열림]` 캔버스 뷰에 조인트를 그리지 않는다.~~ → D-430: 거리는 두 앵커를 잇는 선, 경첩은 핀의 고리와 축 선(2D 와 같은 색). 조인트를 끊는 힘(break force)은 2D 에도 없다.
- **캐릭터 컨트롤러(D-427):** 새 컴포넌트 `CharacterController3D`(유니티의 CharacterController 와 같은 자리·같은 칸 이름 - `center`·`radius`·`height`·`slopeLimit`·`stepOffset`, 그리고 2D 처럼 `layer`·`mask`).
  2D 에는 짝이 없다 - 2D 의 캐릭터는 지금처럼 키네마틱 몸으로 한다. 커널은 Jolt 의 `CharacterVirtual`.
  - **이동은 쌓아 두고 다음 고정 스텝이 먹인다**(`Move(motion)`, 힘·충격량 D-420 과 같은 길). 유니티의 `Move` 는 그 자리에서 옮기지만, 그러면 핸들이 물리에 닿아야 하고 고정 스텝의 차례(캐릭터 → 몸 → 사건)가 깨진다.
    `IsGrounded`·`GetVelocity` 는 마지막 스텝의 것이다. 중력은 넣지 않는다(유니티와 같다 - 스크립트가 이동에 넣는다).
  - 커널의 스텝 차례: 사건 비우기 → 캐릭터 옮기기(`ExtendedUpdate` - 미끄러지기·턱 오르기·바닥 붙기) → 몸 풀이. 캐릭터의 접촉은 캐릭터 리스너가 몸의 접촉과 **같은 표**로 센다 -
    고체에 닿으면 충돌 훅, 트리거면 트리거 훅이 양쪽에 간다(캐릭터 쪽 `bodyType` 은 Kinematic). 유니티의 `OnControllerColliderHit` 같은 따로 된 훅은 두지 않았다.
  - 캐릭터는 같은 캡슐의 키네마틱 몸을 하나 든다 - 동적인 몸이 부딪히고 질의가 맞는다. 사건·질의의 userData 는 그 몸에 있다. Jolt 는 그 몸을 재우지 않아 서 있는 캐릭터의 끝도 미뤄지지 않는다(시험이 본다).
  - **이것이 그 오브젝트의 콜라이더다** - 같은 오브젝트의 고체 `Collider3D`·`Rigidbody3D` 는 몸이 되지 않는다(트리거 콜라이더는 센서 그대로). 캡슐은 늘 선다. 크기는 월드 크기를 곱하고(반지름은 X·Z 중 큰 것), `stepOffset` 은 월드 단위다.
  - Jolt 의 참조 계수 객체를 `new` 식 없이 만드는 `MakeJoltRef` 를 커널에 두었다(그 클래스의 할당 함수로 자리를 받고 `std::construct_at`, ProjectRule 의 D-88 길).
  - 시험: `Physics3DWorld` 의 `TestCharacters`(떨어져 섬·질의가 맞음·지우면 몸도 사라짐·정적인 바닥과 벽에 닿으면 충돌 사건·벽 앞에서 멈춤·낮은 턱 오르기·높은 턱에 막힘·마스크가 뺀 벽은 지나감·동적인 상자 밀기·트리거 한 번 들어갔다 나옴·트리거 안에 서 있다가 옮겨져도 나옴),
    에디터 `TestCharacterControllerIn3D`(핸들의 `Move` 로 걸어 서고 벽 앞에서 멈추며 트리거 훅을 들음, 같은 오브젝트의 몸은 생기지 않고 끄면 보통 몸이 됨).
  - ~~`[열림]` 캐릭터끼리 부딪히기(Jolt 의 `CharacterVsCharacterCollision`)는 걸지 않았다 - 서로의 키네마틱 몸은 지나간다.~~ → D-432: 재 보니 이미 부딪힌다 - 캐릭터의 질의가 상대 캐릭터의 키네마틱 몸에 닿는다. `CharacterVsCharacterCollision` 은 걸지 않는다. ~~캔버스 뷰에 캡슐을 그리지 않는다.~~ → D-430: 콜라이더 단추로 선 캡슐을 그린다(물리와 같은 크기 규칙, 늘 선다). ~~아주 빠른 캐릭터가 얇은 트리거를 건너뛰는 것은 D-423 의 쓸기를 받지 않는다.~~ → D-433: 캐릭터의 안쪽 몸도 D-423 처럼 지나온 길을 쓴다.

## 4. `[열림]`

- ~~메시 콜라이더를 Dynamic 바디에 붙였을 때(볼록 껍질로 바꿀지, 거절할지).~~ → D-418: `convex` 칸, 끈 채 움직이는 몸이면 껍질과 경고 한 번.
- 메시 콜라이더의 크기 손잡이·콜라이더 편집 모드(2D 의 폴리곤 포인트 편집 같은 것)는 없다. 선을 그리기만 한다.
- 큰 메시는 캔버스 뷰가 삼각형 4096 개까지만 그린다.
- ~~한 오브젝트의 고체 콜라이더 여럿이 서로 다른 `layer`·`mask` 를 가지면 첫 콜라이더의 값만 쓴다(D-416).~~ → D-421: 레이어는 도형마다다. 커널이 바디마다 잎 도형 표(마찰·반발·레이어·마스크)를 들고
  잎 도형에는 그 자리(꼬리표 + 번호)만 싣는다. 넓은 판정은 잎들의 레이어·마스크를 합친 것으로 느슨하게 거르고, 접촉 확인 콜백(`OnContactValidate`)이 닿은 두 잎으로,
  질의는 도형 필터(`ShapeFilter`)가 맞은 잎으로 다시 거른다. 프로젝트의 레이어 충돌 표(`PhysicsIgnoredLayerPairs`)도 3D 가 따른다(전에는 2D 만) - 풀이 함수 `ResolvePhysicsIgnoredLayers` 는 `JBroHost` 로 옮겼다.
- ~~트리거 센서는 스텝마다 오브젝트 자세로 순간 이동한다. 빠르게 움직이는 트리거는 사이를 건너뛸 수 있다.~~ → D-423: 스텝 뒤에 지나온 길을 쓴다(도형 밀기). 그 스텝에 제 안쪽 반지름보다
  멀리 간 동적인 고체 몸은 트리거를, 어댑터가 `MoveContinuously` 로 옮긴 트리거 센서는 고체를 길 위에서 찾고, 끝 자리에서 닿아 있지 않으면 "지나쳤다" 로 본다 - 그 스텝에 시작, 다음 스텝에 끝.
  잎마다의 레이어(D-421)와 프로젝트 레이어 표, `triggerSeesStatic` 를 같이 따른다. 끝 자리에서 닿은 쌍은 Jolt 의 보통 사건이 맡는다.
  `[열림]` 회전만 빠른 것(제자리에서 도는 긴 막대)은 쓸지 않는다 - 길은 질량 중심의 이동이다. 고체끼리 뚫는 것은 CCD(§3 의 6)가 맡는다.
- ~~스크립트의 힘·충격량(2D 의 `AddForce` 류)은 아직 없다.~~ → D-420: `Rigidbody3D` 의 `AddForce`·`AddForceAtPosition`·`AddTorque`·`AddImpulse`·`AddImpulseAtPosition`·`AddAngularImpulse`·`WakeUp`·`IsSleeping`(컴포넌트와 핸들),
  핸들은 2D 처럼 모든 칸을 읽고 쓴다. 위치를 준 것은 월드 원점의 모멘트로 모아 두고 물리가 그 스텝의 질량 중심으로 토크를 푼다(2D 와 같은 길).
- ~~3D 질의는 마지막 고정 스텝에서 맞춘 바디를 본다(2D 는 지금의 컴포넌트를 본다).~~ → D-422: 어댑터의 동기화를 고정 스텝에서 떼어(`SyncBodies`) 질의가 묻기 전에 부른다.
  힘을 먹이는 것(`ApplyPendingForces`)과 사건은 고정 스텝에만 남는다. 맞추기는 바뀐 것만 커널에 먹이지만 질의마다 콜라이더를 한 번 훑는다 - 2D 의 질의도 콜라이더를 훑으므로 같은 값이다.
  `[열림]` 한 프레임에 질의를 아주 많이 하는 게임이면 "바뀐 것이 없으면 건너뛰기" 가 필요하다 - 컴포넌트가 바뀜을 알리는 길이 엔진에 생기면 그것으로 바꾼다.
- `RaycastAll` 과 겹침은 한 오브젝트의 고체 콜라이더 여럿을 한 번으로 본다(한 바디다). 2D 의 `RaycastAll` 은 콜라이더마다 따로 든다.
- SSE4·AVX2 같은 CPU 기능 매크로를 켤지 - 켜면 그 기능이 없는 CPU 에서 게임이 뜨지 않는다. 지금은 기본(SSE2)이다.
- 웹(Emscripten)·안드로이드 빌드에 Jolt 를 넣는 것은 그 플랫폼 빌드를 세울 때 확인한다.
- Jolt 를 올릴 때: `Jolt/` 를 통째로 바꾸고 `Jolt.vcxproj` 의 목록을 새 `Jolt.cmake` 에서 다시 뽑는다(ThirdParty/README).
