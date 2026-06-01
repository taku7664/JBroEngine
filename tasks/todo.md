# TODO

## Goal
Ref<T> 가 guid 를 `File::Guid`(std::filesystem::path) 로 저장해 호스트↔게임 DLL 경계에서
ABI/레이아웃 불일치로 값이 깨지는 문제를, **고정 길이 POD 버퍼(char[])** 저장으로 바꿔 해결한다.

## Confirmed facts
- 호스트(에디터)는 instance+offset 에서 guid 를 정상 읽음(a15d70e3...), 게임 DLL 의 OnCreate 는 같은 메모리를 null 로 읽음.
- 원인: fs::path 객체(내부 힙 포인터/복잡 레이아웃)가 DLL 경계를 넘으면 동일 바이트를 다르게 해석.
- guid 는 고정 길이 식별자라 가변 문자열 불필요.

## Success Criteria
- TestScript 의 `if (RefObject)` 가 Play 의 OnCreate 에서 정상 해석(드롭한 오브젝트가 null 아님).
- 직렬화 포맷(씬 파일의 `RefObject: <guid>` 문자열)은 변경 없음.
- Engine/Application/게임 DLL 빌드 통과.

## Plan
- [ ] ReflectionTypes.h: `RefBase` 를 POD 버퍼(`char Guid[64]`) + 접근자로 정의(단일 출처)
- [ ] Ref.h: `Ref<T> : public RefBase` 가 POD 접근자 사용, `RefDetail::*` 시그니처를 `const char*` 로
- [ ] Ref.cpp: `RefDetail::*` 가 const char* 를 받아 내부에서 File::Guid 구성(경계엔 POD 만)
- [ ] SceneSerializer.cpp: Ref 케이스(쓰기/읽기/스크립트필드/재직렬화) 를 RefBase 버퍼로
- [ ] ScriptSystem.cpp: ApplyPendingFields Ref 케이스 → RefBase 버퍼; 진단 로그 제거
- [ ] LiveCompileManager.cpp: 스냅샷 Ref → RefBase 버퍼
- [ ] InspectorTool.cpp: Ref 표시/드롭 → RefBase 버퍼
- [ ] 모든 임시 진단 로그 제거 (DiagRefGet, play-start, ApplyPendingFields dump)

## Verification
- [ ] Engine + Application + 게임 DLL 빌드 (exe 자유 상태에서)
- [ ] (사용자) Play → OnCreate 에서 RefObject 정상

## Review
### Changed
- ReflectionTypes.h: `RefBase` 를 POD(`char Guid[64]` + IsNull/Clear/GuidText/SetGuidText)로 정의.
- Ref.h: `Ref<T>` 가 POD 버퍼 사용, `RefDetail::*` 시그니처 `const char*`. 모든 임시 진단 제거.
- Ref.cpp: const char* 받아 내부에서만 File::Guid 구성.
- SceneSerializer.cpp: Ref 쓰기/읽기/스크립트필드 = RefBase 버퍼(AssetGuid 와 분리).
- ScriptSystem.cpp: ApplyPendingFields Ref = RefBase->SetGuidText; 진단 전부 제거. (clearAfter=false 에디트타임 보존 유지)
- LiveCompileManager.cpp: 스냅샷 Ref = RefBase->GuidText.
- InspectorTool.cpp: Ref 표시/드롭/클리어 = RefBase 버퍼.
- Physics2DSystem.cpp: JBRO_PHYSICS_DEBUG_LOG 0 (물리 로그 OFF).

### Verified
- Engine + Application(에디터) + 게임 DLL 모두 빌드 EXIT 0 (exe 자유 상태).
- 잔여 참조(TargetGuid/DiagRefGet/[Ref] 로그) 0개 확인. SDK 스테이징 최신.

### Not Verified
- 런타임 동작(Play 에서 OnCreate 가 RefObject 정상 해석) — 에디터 GUI 실행이 필요해 사용자 확인 대기.

### Risks
- AssetGuid 스크립트 프로퍼티(`Asset` 멤버, File::Guid 직접 저장)도 동일한 호스트↔DLL 경계 문제를 가짐(빈 값이라 미발현). 스크립트에서 asset 참조를 쓰게 되면 같은 POD 처리 필요 — 별도 작업.
