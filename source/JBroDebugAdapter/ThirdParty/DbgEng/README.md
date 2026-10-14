# DbgEng (Debugging Tools for Windows)

디버그 어댑터가 싣는 마이크로소프트 디버거 엔진이다(D-319). 고치지 않은 원본 그대로 둔다.

| 항목 | 값 |
|---|---|
| 출처 | NuGet `Microsoft.Debugging.Platform.DbgEng` 20260319.1511.0 (소유자 Microsoft·WinDbg) |
| 파일 | `x64/dbgeng.dll`·`dbgcore.dll`·`dbgmodel.dll`·`dbghelp.dll` (파일 버전 10.0.29547.1002) |
| 배포 근거 | Windows SDK 라이선스의 Distributable Code 절이 가리키는 [REDIST 목록](https://learn.microsoft.com/en-us/legal/windows-sdk/redist)의 "Debugging Tools for Windows": `Dbgeng.dll`·`Dbgcore.dll`·`Dbgmodel.dll`·`Dbghelp.dll` 을 프로그램의 일부로 배포할 수 있다 |
| 뺀 것 | 패키지의 `msdia140.dll` 은 위 목록에 없어 싣지 않는다. 없어도 PDB 를 읽는 것을 확인했다(D-319) |

헤더와 import 라이브러리는 Windows SDK(`DbgEng.h`·`DbgModel.h`·`dbgeng.lib`)의 것을 쓴다.
`System32` 에 있는 DbgEng 은 쓰지 않는다 - 확장 DLL 과 `dbghelp.dll` 을 거절했다(`SECURE: File not allowed to be loaded`).
버전을 올리면 이 표와 `todo.md` 의 D-319 를 고친다.
