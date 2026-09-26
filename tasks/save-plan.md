# 세이브 저장소 계획

> 결정은 [todo.md](./todo.md) 의 D-218 (4), 계약은 [ProjectRule.md](../docs/ProjectRule.md) §7.1 의 세이브 항목이다.
> 입력의 런타임 리바인딩(D-218 (3))이 사용자 설정을 남길 자리가 필요해 함께 세웠다.

## 1. 기존 엔진

`Engine/Core/Save/ISaveStorage.h`·`SaveStorage.cpp`, `Engine/Core/FileSystem/UserDataPath.cpp`.

- 뿌리는 `%LOCALAPPDATA%\<제품명>\Saves`(웹은 IDBFS 로 마운트한 `/UserData/Saves`, 그 밖은 `$HOME/.local/share/<제품명>`)다.
  제품명은 파일 이름에 쓸 수 있게 다듬고, 비었으면 `JBroEngine-Unnamed` 다. 로그와 같은 뿌리를 쓴다.
- 슬롯은 납작한 파일 이름이고 `/`·`\`·`:`·`..` 를 거절한다. 없는 슬롯은 첫 실행이라 로그를 남기지 않는다.
- `Flush` 는 데스크톱에서 확인뿐이고, 웹은 IndexedDB 로 넘기는 비싼 일이라 저장 뒤에 한 번 부른다. 종료 때 한 번 민다.

아팠던 것:

- **S1. 제자리 덮어쓰기.** `ofstream(trunc)` 로 열고 쓴다. 쓰는 중에 꺼지면(정전·강제 종료·디스크 가득) 옛 세이브도 새 세이브도 없다.
- **S2. DLL 경계의 `std::vector`.** `ReadBytes(slot, std::vector<uint8_t>&)` 를 게임 DLL 이 부르면 호스트가 DLL 의 벡터를 키운다.
  두 모듈의 CRT 힙이 다르면 깨진다(같은 빌드 설정이라 드러나지 않았을 뿐이다).
- **S3. 예약 이름.** `NUL`·`CON`·`COM1`(확장자를 붙여도)은 장치라 쓰기가 오류 없이 사라진다. 끝의 점·공백은 Windows 가 지워 다른 파일이 된다.
- **S4. 좁은 문자 환경 변수.** `getenv` 는 사용자 이름의 한글을 코드 페이지로 깨뜨린다(기존은 그래서 `_wdupenv_s` 로 바꿨다). 환경 변수는
  띄운 쪽이 지우거나 바꿔 넘길 수 있다.
- **S5. 에디터와 게임이 한 폴더.** 에디터에서 제품명을 모르면 기본 이름을 썼지만, 제품명을 알면 에디터의 재생이 실제 게임의 세이브를 덮는다.

## 2. 설계

- **모듈.** 새 Tier S 모듈 `JBroSaveTypes` 에 `System::ISaveStorage`(POD 인자만)·`Service::SaveService`·두 컨텍스트와 D-37 확장 블록이 있다.
  구현 `SaveStorage` 는 `JBroHost` 에 있고 파일은 `IPlatform` 으로만 만진다. 입력·오디오와 같은 모양이다(호스트가 소유하고 블록을 내며,
  스크립트 DLL 은 로드 때 묶는다). 스크립트가 `<JBro/Host/SaveStorage.h>` 를 include 하면 C1083 이다(`/p:JBroTierProbe=Save`).
- **바꿔 넣는 쓰기(S1).** `<슬롯>.writing` 에 다 쓴 뒤 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 로 바꿔 넣는다. 옆 파일을 쓰지 못하면
  지우고 옛 세이브는 그대로다. `.writing` 으로 끝나는 슬롯 이름은 거절한다.
- **DLL 의 힙(S2).** 인터페이스는 `GetSize` 와 `Read(buffer, capacity)` 이고, 서비스(게임 DLL 사본)가 크기를 묻고 제 `Array` 를 키운다.
  리바인딩 글자(`InputService::WriteBindingOverrides`)도 같은 방식이다.
- **이름(S3).** 128 바이트까지, 제어 문자·`<>:"/\|?*`·`..`·앞뒤 공백·끝의 점·예약 장치 이름(확장자·뒤 공백 포함)을 거절한다. 한글은 받는다.
  거절은 매번 경고한다 - 저장이 조용히 사라지면 안 된다.
- **폴더(S4·S5).** `IPlatform::GetUserDataFolder` 가 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` 로 묻는다. 뿌리는
  `<앱 데이터>/<제품명>/Saves` 이고 **에디터는 `EditorSaves`**(`EngineConfig::editorSaves`)다. 제품명은 `.jproject` 의 `Build.ProductName` 이고
  설정 창에서 고치면 폴더를 옮긴다. `EngineConfig::saveFolder` 가 있으면 그것을 쓴다(시험의 임시 폴더).
- **폴더는 처음 쓸 때 만든다.** 여는 것만으로 사용자 폴더에 빈 폴더를 남기지 않는다 - 세이브를 쓰지 않는 게임과 프로젝트를 여는 시험이 그렇다.
- 엔진이 내려갈 때 `Close` 가 한 번 민다(기존과 같다).

## 3. 실측과 검증

- `SaveStorageTests`: 슬롯 이름 서른 가지, 폴더 이름(기호·에디터·빈 이름·끝 공백·한글), `GetUserDataFolder` 가 `LOCALAPPDATA` 와 같은 폴더,
  한글 경로에서 쓰고 되읽기·짧게 덮기·옆 파일이 남지 않기·버퍼가 모자라면 거절·빈 슬롯·**옆 파일을 쓸 수 없을 때 옛 세이브가 남기**·
  밖으로 나가는 이름과 장치 이름의 거절·지우기(없는 것도 참)·닫은 뒤와 묶지 않은 서비스.
- `ScriptDLLLoaderTests`: 실제 호스트가 연 프로젝트에서 스크립트 DLL 이 세이브를 쓰고 되읽고(바이트는 DLL 의 힙), 파일이 준 폴더에 놓이고,
  호스트 사본의 서비스가 같은 슬롯을 읽는다. 리바인딩 글자도 DLL 이 받는다.

- 뮤테이션: 25 개 중 21 개가 첫 판에 제 검사로 잡혔다. 옆 파일 지우기(쓰기 실패)·읽은 만큼으로 줄이기·실패한 읽기의 비우기는 가짜 플랫폼
  (`FlakyFilePlatform`: 절반만 쓰고 끊기·바꿔 넣기 실패·크기를 물은 뒤 파일 줄이기)으로 시험을 보태 잡았고, 제품명의 첫 빈 이름 처리는 뒤의 처리와
  **동치**라 지웠다. 바꿔 넣기가 실패할 때의 옆 파일 지우기를 더해 26 번째로 잡았다. 제어 문자 변이는 첫 판에 관계없는 컴파일러 시험의 실패로
  "잡혔다" 고 나왔다 - 실패 문구로 다시 재어 이름 검사가 잡는 것을 확인했다.
- 에디터의 `editorSaves` 한 줄은 자동 시험이 없다(에디터의 엔진 설정을 밖에서 볼 길이 없다). 폴더 이름을 고르는 `MakeFolder` 는 시험이 잰다.

## 4. 남은 것

- `[열림]` 웹(IDBFS 마운트와 `Flush` 의 `syncfs`)·Android 의 뿌리 - 그 플랫폼이 설 때.
- `[열림]` 슬롯 목록(세이브 고르기 화면), 비동기 쓰기(큰 세이브), 클라우드 동기화 - 필요해지면.
- `[열림]` 로그 파일의 뿌리를 같은 앱 데이터 폴더로 옮길지 - 로그 파일이 설 때.
