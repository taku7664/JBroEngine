# 워크트리 분기 계획

[todo.md](./todo.md) 의 Stage 를 어떻게 나눠 병렬로 진행할지, 그리고 **나누기 전에 무엇을 끝내둬야
하는지**를 정리한다.

## 결론 먼저

- **먼저 `Stage B0`(골격 선언)** — 각 워크트리가 채울 타입·함수의 이름과 시그니처를 못박는다.
  선언만 덧붙이는 작업이라 B·C 보다 앞에 둘 수 있고, 먼저 해야 B·C 가 맞출 목표가 생긴다.
- **Stage B(ECS 걷어내기)와 C(네임스페이스·이름)는 절대 나누지 않는다.** 둘 다 전 파일을 건드린다.
- 나누면 워크트리 5 개가 나오지만, 1 인 작업이면 **2~3 개가 실용적**이다.

## 0. 전제

워크트리는 커밋된 베이스를 공유한다. 커밋은 완료됐다.

```
997207d  Write down the rules and plan the new tree is being built against
79ac541  Split the engine into per-module build units
641e654  Initial commit
```

남은 전제는 **B0 · B · C 를 끝내는 것**이다.

## 1. 왜 B·C 를 먼저 끝내야 하는가

| Stage | 건드리는 범위 | 병렬 가능? |
|---|---|---|
| **B. ECS 걷어내기** | `Canvas` · `World` 제거 · `GameObject` · 컴포넌트 전부 · 시스템 전부 | **불가** |
| **C. 네임스페이스·이름** | 전 파일 (`namespace`, 타입명, 차원 마커) | **불가** |
| D. 의존 방향 | Platform · RHI · Runtime · Editor | 부분 가능 |
| E. 2D/3D 배타 | 빌드 설정 · 템플릿 | 가능 |
| F·G. 식별자·참조 | Core · Runtime · 신규 파일 위주 | 가능 |
| H. 컨텍스트·핫리로드 | 신규 파일 · EngineInstance | D4 이후 가능 |

B 는 **타입이 무엇인지**를, C 는 **그 타입을 뭐라 부르는지**를 정한다.
둘이 끝나기 전에 분기하면 모든 브랜치가 나중에 통째로 개명당한다.

## 2. 분기 전에 끝내야 할 것 — "골격"

골격 = **구현이 아니라 선언**이다. 각 워크트리가 나중에 채울 타입의 **이름과 시그니처만**
미리 못박아 두면, 분기 후 공유 헤더를 건드릴 일이 사라진다.

### 2.1 필수 (이게 안 되면 분기 금지)

- [x] **커밋** — Stage A 결과와 문서가 `main` 에 올라가 있다
- [x] **Stage B0 완료** — 공유 헤더 선언 확정 (아래 2.2, todo.md 의 Stage B0)
- [x] **Stage B 완료** — 오브젝트-컴포넌트 모델 전환, 빌드·테스트 통과
- [x] **Stage C 완료** — 네임스페이스·이름 적용, 빌드·테스트 통과
- [x] Debug / Release x64 빌드 통과 + `JBroTests` 통과
- [ ] 골격 커밋

### 2.2 공유 헤더 선언 확정

구현은 비어 있어도 된다. **이름과 시그니처가 확정되면 된다.**

```cpp
// JBroCore/Include/JBro/Core/Core.h
namespace JBro
{
    using InstanceId = std::uint64_t;      // F1 이 생성기를 채운다
}

// JBroRuntime/Include/JBro/Runtime/Ref.h        ← 신규. 선언만
namespace JBro
{
    struct InstanceHandle                  // 8B. 런타임 위치
    {
        std::uint32_t Slot;
        std::uint32_t Gen;
    };
    struct InstanceRef                         // 24B POD. 저장부
    {
        InstanceId     ObjectId;
        InstanceId     ComponentId;
        InstanceHandle Cached;
    };
    template<typename T> class Ref;        // G2·G3 이 채운다
}

// JBroRuntime/Include/JBro/Runtime/Context.h    ← 신규. 빈 구조체
namespace JBro
{
    struct EngineContext {};               // H1
    struct SystemContext {};               // H1
    struct ServiceContext {};              // H1
}
```

`Core.h` 에 넣는 것은 **`InstanceId` 하나뿐이다.**
`GraphicsApi` 와 `SurfaceHandle` 을 Core 로 옮기자던 앞선 안은 철회했다 —
전자는 그래픽스 개념이라 RHI 에 두고, 후자는 플랫폼이 만드는 것이라 Platform 에 둔다.
그러면 의존이 `RHI → Platform` 방향이 되어 F17 이 Core 를 건드리지 않고 해소된다(W-platform 담당).

**이 파일들이 골격 단계에서 확정되면 여러 워크트리가 같은 헤더를 동시에 건드리는 일이 없어진다.**
이게 골격 작업의 핵심 목적이다.

### 2.3 골격 완료 판정

> 위 헤더를 열어서 **타입 이름이 더 이상 바뀌지 않을 것**이라고 말할 수 있으면 골격이 끝난 것이다.

## 3. 분기 구성

각 워크트리는 **자기가 소유한 파일만** 수정한다.

| 워크트리 | 담당 | 소유 파일 | 의존 |
|---|---|---|---|
| **W-build** | E (2D/3D 배타) | `*.vcxproj` · `JBro.Common.props` · `*.slnx` · 프로젝트 템플릿 | 없음 |
| **W-platform** | D2 · D3 | `JBroPlatform/**` · `JBroRHI/**` | 없음 |
| **W-framework** | D1 · D5 | `JBroRuntime/GameSystem.h` · `SystemScheduler.h` · `JBroFramework2D/**` | 없음 |
| **W-ref** | F · G | `JBroCore/Source/InstanceId.cpp` · `JBroRuntime/Ref.*` · `GameInstance.*` | 없음 |
| **W-host** | D4 · H | `EngineInstance.*` · `JBroEditor/**` · `Context.*` · 스크립트 로더 | **W-ref** |

### 왜 이렇게 갈리나

- **W-platform ↔ W-ref**: 둘 다 `JBroCore` 를 건드릴 뻔했다.
  `InstanceId` 는 2.2 에서 미리 넣고, `GraphicsApi` / `SurfaceHandle` 은 Core 로 옮기지 않고
  각자 제자리(RHI / Platform)에 두기로 해서 둘이 만날 일이 없어졌다.
- **W-framework ↔ W-ref**: `JBroRuntime` 안에서 `GameSystem.h` 와 `GameInstance.h` 로 갈린다.
  같은 모듈이지만 다른 파일이다.
- **W-host 만 의존이 있다** — 핫 리로드(H5)가 `Ref` 캐시 무효화를 필요로 한다.

### 명령

```bash
# 골격 커밋 후
git worktree add ../JBro-build     -b work/build
git worktree add ../JBro-platform  -b work/platform
git worktree add ../JBro-framework -b work/framework
git worktree add ../JBro-ref       -b work/ref
git worktree add ../JBro-host      -b work/host
```

각 워크트리는 `Build/` 출력이 분리되므로 동시 빌드가 가능하다.

## 4. 충돌 규칙

- **자기 소유 파일 밖을 고치지 않는다.** (MUST)
- 공유 헤더(2.2 목록)를 고쳐야 하면 **`main` 에 먼저 반영하고 각 워크트리가 rebase 한다.** (MUST)
  워크트리에서 직접 고치면 5 갈래로 갈라진다.
- 새 파일은 자기 모듈 안에만 만든다. (MUST)
- vcxproj 는 **W-build 만** 수정한다. 다른 워크트리가 파일을 추가하면
  glob(`Source\**\*.cpp`)이 잡으므로 vcxproj 를 건드릴 필요가 없다. (MUST)

## 5. 병합 순서

```
1. W-build       빌드 설정만이라 다른 것에 영향 없음. 먼저 넣어도 무해
2. W-platform    독립
3. W-framework   독립
4. W-ref         가장 큼. 여기까지 들어가야 W-host 가 완성 가능
5. W-host        마지막
```

매 병합마다 Debug / Release 빌드 + 테스트를 통과시키고 다음으로 간다.

## 6. 언제 워크트리가 과한가

**1 인 작업에 5 개는 과하다.** 워크트리의 이득은 "동시에 여러 사람이" 또는 "빌드를 돌려두고
다른 걸 만지는" 상황에서 나온다. 혼자 순차로 진행할 거면 브랜치 하나로 충분하고,
워크트리 관리 비용만 늘어난다.

실용적인 선택:

| 상황 | 권장 |
|---|---|
| 혼자, 순차 진행 | 워크트리 없이 브랜치 하나 |
| 혼자, 긴 빌드를 돌려두고 다른 작업 | **2 개** — 주 작업 + 실험 |
| 여럿이 동시 작업 | 위 5 개 구성 |

**어느 경우든 골격(2 절)은 먼저 끝내야 한다.** 이건 워크트리를 쓰든 안 쓰든 같다 —
B·C 가 전 파일을 건드리기 때문이다.

## 7. 요약

```
[커밋 완료] ──▶ Stage B0 ──▶ Stage B ──▶ Stage C ──▶ 골격 커밋
                 선언만        ECS 제거    이름 정리        │
                 덧붙이기      ├─ 전 파일을 건드린다 ─┤      │
                              └─ 직렬. 나눌 수 없다 ─┘      │
                                                            ▼
                            ┌──────────┬──────────┬──────────┬──────────┐
                         W-build  W-platform  W-framework  W-ref ──▶ W-host
                            │          │          │          │          │
                            └──────────┴──────────┴──────────┴──────────┘
                                        병합 (위 순서)
```
