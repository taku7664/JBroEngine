# Lessons

## Lesson: 호스트↔게임 DLL 경계를 넘는 멤버는 POD 여야 한다

Pattern:
- 게임 스크립트 인스턴스(게임 DLL 소유)에 `File::Guid`(=`std::filesystem::path`, 내부 힙 포인터/복잡 레이아웃) 멤버를 두고, 호스트(에디터)가 그 메모리에 값을 써 넣었다.
- 호스트는 정상 값으로 읽는데 게임 DLL 코드는 같은 메모리를 빈 값으로 읽었다. (`Ref<GameObject>` 가 OnCreate 에서 항상 null)
- 원인: fs::path 같은 객체는 ABI/레이아웃이 모듈마다 미묘하게 달라 경계 너머로 "객체째" 공유하면 깨진다. (예전 핫리로드 AssetGuid 이중해제 크래시도 같은 계열)

Rule:
- 호스트와 게임 DLL이 공유하는 데이터(스크립트 인스턴스 필드, 경계를 넘는 인자/반환)는 **POD(고정 크기 바이트/문자열 버퍼)** 로 둔다. `std::string`/`std::filesystem::path`/STL 컨테이너를 멤버로 그대로 공유하지 않는다.
- 경계 함수 시그니처도 `const char*` 등 POD 로 받고, 무거운 타입은 한쪽(Engine.lib) 내부에서만 구성한다.

Example:
- `Ref<T>` 저장부를 `File::Guid TargetGuid` → `RefBase { char Guid[64]; }` (POD) 로 변경. 직렬화 문자열 포맷은 그대로 유지. 해석 시점에만 `File::Guid(buf)` 로 변환.

## Lesson: 디버그 진단은 "보이는 채널"로

Pattern:
- DLL 진단을 `Core::Debug->Warning` 으로 보냈는데 로그 창에 안 떠서, 코드가 실행 안 된 줄 오해하고 헤맸다.

Rule:
- 이미 화면에 잘 뜨는 로그와 **같은 채널/레벨**(이 프로젝트는 `CSystemLog::Info`)로 진단을 보낸다. 안 보이면 "코드 미실행"이 아니라 "채널 차이"부터 의심한다.
