# 3D 물리 계획 (Jolt Physics)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-11-19 사용자 결정("Jolt 로 가자")으로 시작했다(D-414, todo-3d 우선순위 9).
> 같은 날 Jolt v5.7.0 을 `ThirdParty/Jolt` 에 넣고 혼자 빌드되는 것까지 했다. 같은 날 §2 의 제안이 모두 그대로 확정됐다("제안대로 진행", D-415)
> - 아래 §2 의 표시는 확정이다. 1 단계(커널 `JBroPhysics3D`)가 섰다. 다음은 2 단계(컴포넌트와 어댑터)다.

## 0. 한 줄 요약

3D 의 충돌·강체 풀이는 **Jolt 에 맡기고**, 엔진이 보이는 모양(컴포넌트·스크립트 훅·질의 서비스·에디터)은 **2D 물리를 그대로 따른다.**
Jolt 타입은 새 커널 모듈 하나 밖으로 나가지 않는다 - 컴포넌트와 스크립트는 Jolt 를 모르고, 나중에 커널을 바꿔도 그 둘은 그대로다.

## 1. 지금 상태 (2026-11-19)

| 자리 | 있는 것 |
|---|---|
| 컴포넌트 | `Component::Rigidbody3D`(`velocity`·`mass` 뿐), `Component::Collider3D`(`size` 뿐). D-53 의 골격이다 |
| 핸들 | `Handle::Rigidbody3D`(`Get/SetVelocity`·`Get/SetMass`), `Handle::Collider3D` |
| 시스템 | 없다. 재생해도 3D 오브젝트는 떨어지지 않는다 |
| 스크립트 훅 | `GameScript3D` 에 충돌·트리거 훅이 없다(UI 훅만). 2D 는 `OnCollisionEnter/Stay/Exit`·`OnTriggerEnter/Stay/Exit` |
| 질의 | 없다. 2D 는 `Physics2DService` 의 `Raycast`·`OverlapBox`·`OverlapPoint`·`OverlapCircle`·`CircleCast`·`BoxCast` |
| 에디터 | 콜라이더 표시가 없다. 2D 는 콜라이더를 그리고 폴리곤 포인트를 편집한다 |
| 레이어 | 프로젝트의 `physicsLayers` 이름 표(호스트)와 `PhysicsLayerMask`(Framework2D 의 값 타입) |
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
- 2D 의 조인트(`DistanceJoint2D`·`HingeJoint2D`)는 뒤로 둔다(§3 의 6).

### 2.3 스텝과 스레드 (확정, D-415)

- 고정 스텝은 2D 와 같은 `OnFixedUpdate`(호스트 `TimeSystem`). 한 고정 스텝에 Jolt `Update` 한 번, 충돌 스텝 1.
- **할당:** Jolt 의 할당 훅(`JPH::Allocate` 등)을 등록하고, 스텝 임시 메모리는 미리 잡은 `TempAllocatorImpl` 하나를 쓴다 - 프레임 경로에 힙 할당이 없다(ProjectRule).
  바디·도형을 만들 때의 할당은 프레임 경로가 아니다.
- **스레드:** 1 단계는 Jolt 의 `JobSystemSingleThreaded` 로 시작한다. 그다음 `JBroTask` 위에 Jolt `JobSystem` 을 구현해 `Build.PhysicsThreads`(D-223)를 따른다.
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
| 2 | 컴포넌트 필드(§2.2)·`PhysicsLayerMask` 를 Core 로·어댑터 `Physics3DSystem` | 에디터에서 재생하면 상자가 떨어져 바닥에 서고, 멈추면 제자리. 레이어·마스크가 거른다 | |
| 3 | 접촉 사건과 스크립트 훅·`Physics3DService` 질의 | 3D 시험 DLL 의 스크립트가 `OnCollisionEnter`·`OnTriggerEnter` 를 받고 `Raycast` 가 맞는다 | |
| 4 | 에디터: 콜라이더 그리기·메시 콜라이더 | 캔버스 뷰 스크린숏에 콜라이더 선, 메시 바닥 위에 상자가 선다 | |
| 5 | 잡 시스템을 `JBroTask` 위로·`Build.PhysicsThreads` | 바디 1000 개에서 워커 수에 따라 스텝 시간이 준다 | |
| 6 | 뒤의 것: 조인트·캐릭터 컨트롤러(`CharacterVirtual`)·CCD(`EMotionQuality::LinearCast`) | 항목마다 따로 정한다 | |

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

## 4. `[열림]`

- 메시 콜라이더를 Dynamic 바디에 붙였을 때(볼록 껍질로 바꿀지, 거절할지).
- SSE4·AVX2 같은 CPU 기능 매크로를 켤지 - 켜면 그 기능이 없는 CPU 에서 게임이 뜨지 않는다. 지금은 기본(SSE2)이다.
- 웹(Emscripten)·안드로이드 빌드에 Jolt 를 넣는 것은 그 플랫폼 빌드를 세울 때 확인한다.
- Jolt 를 올릴 때: `Jolt/` 를 통째로 바꾸고 `Jolt.vcxproj` 의 목록을 새 `Jolt.cmake` 에서 다시 뽑는다(ThirdParty/README).
