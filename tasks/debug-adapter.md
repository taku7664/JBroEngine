# 스크립트 디버그 어댑터 (D-319, D-330)

> 2026-10-14 처음 쓰고 2026-10-15 다시 썼다. 독자: JBro Script Editor 의 `jbro-engine` 확장에 디버깅을 붙일 담당자와 어댑터를 고칠 사람.
> **지금 어댑터를 확장에 붙이지 않는다.** 지금 코드는 OS 디버거로 붙어 중단점에서 에디터 전체를 세운다. 이 방식은 D-330 로 버렸다 -
> 디버깅은 에디터 프로세스 안에서 엔진이 하고, 어댑터는 에디터를 세우지 않고 붙어 읽기만 한다. 확장이 붙일 계약은 D-330 3 단계에서 정해 이 문서에 채운다.
> 엔진 쪽 설계와 단계는 [script-debug-plan.md](./script-debug-plan.md).

## 1. 지금 있는 것

`source/JBroDebugAdapter` 의 `JBroDebugAdapter.exe` 는 표준 입출력으로 [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) 을 말한다.
떠 있는 씬 에디터(`JBroEditorHost.exe`)에 마이크로소프트 디버거 엔진(DbgEng)으로 붙어 스크립트 DLL 을 디버깅한다.

- 산출물: `source/JBroEngine/Build/x64/<구성>/DebugAdapter/` 에 실행 파일, 재배포용 DbgEng DLL 넷, `JBro.natvis` 가 함께 놓인다.
  에디터 실행 파일 옆에 두지 않는다 - 함께 놓이는 `dbghelp.dll` 이 에디터가 싣는 것을 바꾼다.
- 엔진 솔루션의 `/Tools/` 에 있고, `Tests/JBroTests.vcxproj` 가 빌드를 시킨다. 엔진 모듈은 JBroCore 만 링크한다.
- DbgEng 은 리포의 `source/JBroDebugAdapter/ThirdParty/DbgEng/` 에 있다. 출처·버전·배포 근거는 그 폴더의 `README.md`.
- 붙는 자리는 `DebugSession.cpp` 의 `AttachProcess(0, processId, DEBUG_ATTACH_DEFAULT)` 다. 이것이 OS 디버거 붙기다(2 절).

## 2. 지금 방식이 안 되는 까닭

OS 디버거로 붙으면 대상 프로세스에서 디버그 이벤트(중단점·한 걸음·모듈 실림)가 날 때마다 **운영체제가 그 프로세스의 모든 스레드를 세운다.**
스크립트는 에디터 프로세스 안에서 돌므로, 중단점에 걸리면 에디터 화면·입력·렌더가 함께 선다. 멈춘 동안 인스펙터를 보거나 오브젝트를 고를 수 없다.
지금 코드는 도는 동안 중단점을 바꿀 때도 대상을 잠깐 세운다(3 절).

D-330 는 이렇게 바꿨다(실측은 [script-debug-plan.md](./script-debug-plan.md) §2):

| 일 | 지금 어댑터 | 바뀐 방식 |
|---|---|---|
| 붙기 | `DEBUG_ATTACH_DEFAULT`(OS 디버거) | `DEBUG_ATTACH_NONINVASIVE \| DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND` - 세우지 않고 메모리와 심볼만 읽는다 |
| 중단점 넣기 | 어댑터가 DbgEng 으로 넣는다 | 어댑터는 소스 줄을 주소로 바꿔 엔진에 넘기고, **엔진이** int 3 을 넣고 뺀다 |
| 걸렸을 때 | 운영체제가 에디터를 통째로 세운다 | 엔진의 예외 처리기(VEH)가 받아 그 자리에서 에디터 화면만 그리는 루프를 돈다. 게임 갱신과 오디오는 선다 |
| 멈춤·재개·모듈 실림 알림 | DbgEng 디버그 이벤트 | 디버그 이벤트가 오지 않으므로 엔진이 통로로 보낸다(5 절의 미정) |
| 스택·변수 | 멈춘 스레드의 레지스터에서 | 엔진이 알려 준 `CONTEXT` 주소로 `.cxr` 한 뒤 읽는다. 데이터 모델의 `@$curthread...LocalVariables` 는 `.cxr` 을 따르지 않아 쓰지 못한다(실측) |
| 한 줄 가기 | DbgEng 의 한 걸음 | 엔진이 트랩 플래그와 임시 int 3 으로 한다 |
| 어댑터가 죽으면 | 넣어 둔 int 3 이 남아 에디터가 그 줄에서 죽을 수 있다 | 중단점 표를 엔진이 가지므로, 통로가 끊기면 엔진이 뺀다 |

## 3. 지금 어댑터의 동작 (바뀌기 전의 기록)

바꿀 때 그대로 가져갈 것과 버릴 것을 가르려고 남긴다.

- 요청: `attach` 만 받는다(`launch` 없음). 인자는 `processId`(없으면 `processName`, 기본 `JBroEditorHost.exe`, 같은 이름이 여럿이면 마지막에 뜬 것),
  `scriptModules`(스크립트 DLL 이름의 머리, 기본 `["GameScript"]`), `natvis`(기본은 어댑터 옆 `JBro.natvis`), `symbolPath`(기본은 어댑터 폴더).
- 받는 요청: `initialize`·`attach`·`setBreakpoints`·`setExceptionBreakpoints`(빈 목록)·`configurationDone`·`threads`·`stackTrace`·`scopes`·`variables`·
  `evaluate`·`continue`·`next`·`stepIn`·`stepOut`·`pause`·`disconnect`. 보내는 이벤트: `initialized`·`stopped`·`breakpoint`·`output`·`exited`·`terminated`.
- 에디터는 스크립트 DLL 을 `GameScript.dll.jbro.<pid>.<세대>.dll` 복사본으로 싣고, 다시 빌드하면 새 복사본을 싣는다. 어댑터는 실릴 때마다 대상을 세우고 중단점을 다시 건다. → 버린다(엔진이 알린다).
- 스크립트 PDB 는 빌드마다 다른 이름이다(`GameScript.<시각>.pdb`, `JBro.GameScript.targets`). 디버거가 쥔 PDB 와 겹치지 않아 붙은 채로 다시 빌드할 수 있다. → 그대로 간다.
- 도는 동안 온 요청은 대상을 몰래 세웠다 처리한다. → 버린다(세울 일이 없다).
- 변수는 DbgEng 데이터 모델로 읽고, `JBro.natvis` 가 `Float`·`Int32`·`Bool`·`String`·`Array`·`Vector2/3/4`·`Color`·`SafePtr`·`InstanceRef` 를 값으로 보인다. → natvis 는 그대로, 읽는 경로는 `.cxr` 기준으로 바꾼다.
- 어댑터가 죽으면 `%TEMP%\JBroDebugAdapter-<pid>.dmp` 를 남기고, `JBRO_DAP_LOG` 가 파일 경로면 DbgEng 출력을 거기 적는다. → 그대로 간다.
- `launch.json` 예(지금 모양): `{ "type": "jbro", "request": "attach", "name": "씬 에디터에 붙기", "processName": "JBroEditorHost.exe" }`

## 4. 시험과 실측 (지금 방식)

- `JBroTests` 의 `DebugAdapter` 묶음: 대상 프로그램 `Tests/DebugAdapterTarget` 이 에디터처럼 스크립트 DLL 을 세대별 복사본으로 싣는다.
  붙기·첫 멈춤이 중단점인지·스택·natvis 값·`Array` 펼치기·식 평가·한 줄 가기·다시 실은 복사본의 중단점·일시정지·떼기·어댑터가 죽어도 대상이 사는지를 본다.
  뮤테이션 다섯 중 넷을 잡았다. 방식을 바꾸면 이 묶음도 새 방식에 맞게 다시 쓴다.
- 실제 에디터(2026-10-14): 중단점·스택·변수·한 줄 가기·나가기·일시정지·도는 동안 중단점 걸기, 붙은 채 다시 빌드한 3 세대 복사본에서 멈춤, 떼기 뒤 에디터 생존을 확인했다.
  중단점에서 에디터 화면이 멈춘 것도 이때 보았다 - D-330 의 출발점이다.
- 새 방식의 조각(세우지 않고 붙어 `.cxr` 로 스택·natvis 값을 읽기, VEH 안의 루프)은 스크래치 호스트와 임시 코드로만 쟀다. [script-debug-plan.md](./script-debug-plan.md) §2.

## 5. 정해야 할 것

엔진 쪽 2 단계(중단점 루프)와 함께 정한다. 각 항목의 갈래와 추천은 [script-debug-plan.md](./script-debug-plan.md) §3 의 `미정`.

- 통로 위의 메시지(중단점 주소 넘기기, 멈춤·재개·모듈 실림 알림, `CONTEXT` 주소). 통로는 에디터마다 이름 있는 파이프 `\\.\pipe\jbro-debug-<pid>` 로 정했다(2026-10-15).
- ~~한 줄 가기에서 "다음 줄" 을 누가 판단하는가~~ → 엔진(2026-10-15). 어댑터는 스택·변수 읽기에 남는다.
- ~~확장이 붙이는 모양: 에디터의 `디버그 시작` 메뉴가 어댑터를 띄우는가~~ → 아니다(2026-10-15). 에디터의 `디버그 실행` 은 통로만 연다. 어댑터는 스크립트 편집기 확장이 띄워 붙인다.
- 확장이 붙이는 모양: `attach` 그대로인가, 에디터의 `디버그 시작` 메뉴(5 단계)가 어댑터를 띄우는가.

엔진 쪽 1~5 단계가 2026-10-15 에 모두 섰다([script-debug-plan.md](./script-debug-plan.md) §4.2~§4.5): 세우지 않고 붙기, 통로(파이프)로 중단점·멈춤·한 줄 가기, `.cxr` 스택·변수, 디버그 실행 메뉴.
어댑터는 `source/JBroDebugAdapter` 에서 새로 섰고, 실제 에디터+어댑터를 함께 돌리는 `DebugAdapter` 시험이 지난다. **남은 것은 스크립트 편집기 확장 쪽 연결뿐이다**(아래 §6).

## 6. 남은 것

- 위 5 절, 그리고 새 방식으로 어댑터를 고치기(D-330 3 단계)·확장 쪽 연결·실제 Script Editor 화면에서의 확인.
- 조건부 중단점·로그 포인트·데이터 중단점·변수 값 바꾸기·예외 거르기 설정은 없다.
- Release 구성의 스크립트 디버깅은 재지 않았다.
