# 스크립트 디버그 어댑터 (D-319)

> 2026-10-14 작성. 독자: JBro Script Editor 의 `jbro-engine` 확장에 디버깅을 붙일 담당자.
> 어댑터 실행 파일과 그 시험까지는 섰다. 확장에 붙이는 일은 이 문서를 보고 한다(사용자 결정: "어댑터만 만들고 넘기기").

## 1. 무엇이 섰나

`source/JBroDebugAdapter` 의 `JBroDebugAdapter.exe` 는 표준 입출력으로 [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) 을 말한다.
떠 있는 씬 에디터(`JBroEditorHost.exe`)에 마이크로소프트 디버거 엔진(DbgEng)으로 붙어 스크립트 DLL 을 디버깅한다.

- 산출물: `source/JBroEngine/Build/x64/<구성>/DebugAdapter/` 에 실행 파일, 재배포용 DbgEng DLL 넷, `JBro.natvis` 가 함께 놓인다.
  에디터 실행 파일 옆에 두지 않는다 - 함께 놓이는 `dbghelp.dll` 이 에디터가 싣는 것을 바꾼다.
- 엔진 솔루션의 `/Tools/` 에 있고, `Tests/JBroTests.vcxproj` 가 빌드를 시킨다. 엔진 모듈은 JBroCore 만 링크한다.
- DbgEng 은 리포의 `source/JBroDebugAdapter/ThirdParty/DbgEng/` 에 있다. 출처·버전·배포 근거는 그 폴더의 `README.md`.

## 2. 확장이 할 일

`package.json` 의 `contributes.debuggers` 에 타입 하나를 더하고, 어댑터 실행 파일을 띄운다(`DebugAdapterExecutable`).
VS Code 의 `cppvsdbg` 는 라이선스상 마이크로소프트 제품 전용이라 쓰지 않는다(D-318).

| 항목 | 값 |
|---|---|
| 요청 | `attach` 만 받는다. `launch` 는 아직 없다 - 에디터는 사용자가 띄워 둔다 |
| `processId` | 붙을 에디터의 프로세스 번호. 없으면 `processName` 으로 찾는다 |
| `processName` | 기본 `JBroEditorHost.exe`. 같은 이름이 여럿이면 마지막에 뜬 것 |
| `scriptModules` | 스크립트 모듈로 볼 DLL 파일 이름의 머리. 기본 `["GameScript"]` |
| `natvis` | 기본은 어댑터 옆의 `JBro.natvis` |
| `symbolPath` | 기본은 어댑터 폴더. 스크립트 DLL 이 PDB 의 절대 경로를 품고 있어 보통은 줄 필요가 없다 |

받는 요청: `initialize`·`attach`·`setBreakpoints`·`setExceptionBreakpoints`(빈 목록)·`configurationDone`·`threads`·`stackTrace`·`scopes`·`variables`·
`evaluate`·`continue`·`next`·`stepIn`·`stepOut`·`pause`·`disconnect`. 보내는 이벤트: `initialized`·`stopped`·`breakpoint`·`output`·`exited`·`terminated`.

`launch.json` 예:

```json
{ "type": "jbro", "request": "attach", "name": "씬 에디터에 붙기", "processName": "JBroEditorHost.exe" }
```

## 3. 동작에서 알아 둘 것

- **중단점은 소스 줄로 든다.** 에디터는 스크립트 DLL 을 `GameScript.dll.jbro.<pid>.<세대>.dll` 복사본으로 싣고, 다시 빌드하면 새 복사본을 싣는다.
  어댑터는 스크립트 모듈이 실릴 때마다 대상을 잠깐 세우고 그 모듈의 심볼을 읽어 중단점을 다시 건다. 줄이 바뀌면 `breakpoint` 이벤트를 보낸다.
- **붙은 채로 다시 빌드해도 된다.** 스크립트 PDB 가 빌드마다 다른 이름이라(`GameScript.<시각>.pdb`, `JBro.GameScript.targets`) 디버거가 쥔 PDB 와 겹치지 않는다.
- **도는 동안 온 요청**(중단점 바꾸기 따위)은 대상을 몰래 세웠다 처리하고 다시 돌린다. 한 줄씩 가는 중에는 걸음이 끝날 때까지 미룬다.
- **변수는 DbgEng 데이터 모델(`dx` 와 같은 것)로 읽는다.** `JBro.natvis` 가 `Float`·`Int32`·`Bool`·`String`·`Array`·`Vector2/3/4`·`Color`·`SafePtr`·`InstanceRef` 를 값으로 보인다.
- **어댑터가 끝나도 에디터는 산다**(`DEBUG_PROCESS_DETACH_ON_EXIT`). 클라이언트가 끊거나 입력이 닫히면 중단점을 떼고 떼어 낸다.
- 어댑터가 죽으면 `%TEMP%\JBroDebugAdapter-<pid>.dmp` 를 남긴다. `JBRO_DAP_LOG` 가 파일 경로면 DbgEng 출력과 중단점 식의 실패를 거기 적는다.

## 4. 시험과 실측

- `JBroTests` 의 `DebugAdapter` 묶음: 대상 프로그램 `Tests/DebugAdapterTarget` 이 에디터처럼 스크립트 DLL 을 세대별 복사본으로 싣는다.
  붙기·첫 멈춤이 중단점인지·스택·natvis 값·`Array` 펼치기·식 평가·한 줄 가기·다시 실은 복사본의 중단점·일시정지·떼기·어댑터가 죽어도 대상이 사는지를 본다.
  뮤테이션 다섯 중 넷을 잡았다(붙기 끼어들기 세기·끝날 때 떼기·실린 모듈에 다시 걸기·natvis). 남은 하나(소멸자의 출력 콜백 떼기)는 바로 뒤의 COM 포인터 순서와 겹치는 이중 안전장치다.
- 실제 에디터(2026-10-14): 중단점·스택·변수·한 줄 가기·나가기·일시정지·도는 동안 중단점 걸기, 붙은 채 다시 빌드한 3 세대 복사본에서 멈춤, 떼기 뒤 에디터 생존을 확인했다.

## 5. 남은 것

- 확장 쪽 연결(위 2 절)과 실제 Script Editor 화면에서의 확인.
- 붙는 방식이 바뀐다(D-325): OS 디버거 대신 프로세스를 세우지 않는 붙기, 중단점은 엔진이 넣고 스택·변수는 엔진이 넘긴 `CONTEXT` 로 읽는다. [script-debug-plan.md](./script-debug-plan.md) §3.5.
- 조건부 중단점·로그 포인트·데이터 중단점·변수 값 바꾸기·예외 거르기 설정은 없다.
- 어댑터가 **중단점을 건 채 도는 중에** 죽으면 대상 코드에 int 3 이 남아 에디터가 그 줄에서 멈춰 죽을 수 있다. 떼고 끝나는 길(끊기·입력 닫힘)은 이 일이 없다.
- 일시정지로 서면 사용자가 볼 스레드로 첫 스레드(에디터의 메인)를 고른다. 렌더 스레드에서 멈추고 싶으면 스레드 목록에서 고른다.
- Release 구성의 스크립트 디버깅은 재지 않았다.
