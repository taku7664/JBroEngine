# Asset System Follow-ups

PPU 작업 과정에서 발견된 자산 시스템 구조 문제들과 그 처치 후보. 본 문서는 카탈로그 — 진행 결정 전 검토용. 진행이 정해진 항목만 별도 todo.md 로 옮긴다.

---

## 발견된 문제

### P1. InspectorTool 의 Apply 가 비-영구 자산 전체를 unload 시킨다
- `Application/Editor/Main/Inspector/InspectorTool.cpp:698` 에서 단일 자산 옵션 저장 후 `assetManager->RefreshAssetRegistry()` 호출.
- `RefreshAssetRegistry` 내부에 `UnloadNonPersistentAssets` 가 들어있어 비-영구 자산(=프로젝트 자산 전부)이 unload 된다.
- 결과: 인스펙터에서 PPU 한 번 만지고 Apply 누르면 씬의 모든 스프라이트가 깨진다.

### P2. RefreshAssetRegistry 의 이름/동작 불일치
- "레지스트리 재구축" 이름인데 실제로는 unload + 재구축.
- 호출자가 의도치 않게 비-영구 자산을 통째로 날린다.

### P3. 자산 소유권 모델 — Persistent / Non-persistent 만 있음
- 씬에서 사용 중인 자산을 보호하는 중간 상태가 없다.
- 어떤 흐름이든 비-영구 자산을 unload 하면 사용 중이든 말든 죽고, SafePtr 가 죽어서 씬 깨짐.

### P4. AssetManager 가 매 프레임 read path 에서 mutex 잡는다
- `LoadAsset` 진입부에 `std::lock_guard lock(m_mutex)`.
- SpriteRenderSystem 이 매 프레임 N 개 스프라이트마다 LoadAsset → 매 프레임 N 번 lock/unlock.
- 워커 스레드(임포트/언로드) 와 경합 시 렌더 hot path 가 stall 될 위험.

### P5. 자산이 GPU 리소스를 직접 소유 — unload 가 곧 렌더 깨짐
- `CSpriteAsset` 이 `OwnerPtr<IRHITexture>` 보유.
- 자산 unload → 텍스처 destroy → 머티리얼 SafePtr<IRHITexture> 사망 → 렌더 깨짐.
- 자산 unload 의 부작용 범위가 너무 크다.

### P6. 프로젝트 로드 시 모든 자산을 선로드
- `LoadProject` 가 워커 풀로 모든 자산을 임포트 + 데이터 로드.
- "참조해야 메모리에 올라간다" 모델이 아님.
- lazy loading 으로 가려면 별도 큰 변경 필요.

### P7. FileWatcher 의 이벤트 합치기 인접성 가정
- ~~`_sendEventQueue` 순회 시 `[i]` 와 `[i+1]` 만 비교해 REMOVE+ADD → MOVED 합침.~~
- ~~동시 다중 변경에서 같은 FileId 의 이벤트가 인접하지 않을 수 있음.~~
- **오인된 가정**. 실제 워처는 mtime 폴링 기반이라 합치기 자체가 없고, ProjectManager 의 rename 처리는 events 벡터 전체를 순회. 인접성 가정 없음.

### P8. 자기-반향 — 디스크에 쓴 변경을 워처가 다시 감지
- 에디터 내부에서 SaveSpriteImportOptions 가 `.Jmeta` 쓰면 워처가 같은 변경을 잡아 ReloadAsset 을 또 호출.
- 같은 변경이 두 번 처리됨.
- **해소 완료** (v0.0.13). ProcessAssetEvent 진입부 `.Jmeta` 컷 + C1' 의 in-place 갱신 모델로 ReloadAsset 호출 자체가 없어짐.

### P9. 외부 `.Jmeta` 변경 동기화 부재 (협업 시)
- 동료가 PPU 바꾼 .Jmeta 를 git push, 내가 pull → 워처가 .Jmeta 만 보고 reload 할지 정책 없음.
- 워처가 .Jmeta 무시(C2) 하면 외부 변경이 안 들어옴. 무시 안 하면 P8.
- 현재는 .Jmeta 무시로 가서 외부 동기화 포기. 향후 협업 워크플로 도입 시 재검토.

---

## 처치 후보

### A. 인스펙터 Apply 의 RefreshAssetRegistry 호출 제거
- `InspectorTool.cpp:698` 한 줄 제거. 뒤 `ReloadAsset(updatedMetaData.Guid)` 만 남김.
- P1 직접 해소.
- **상태: 완료**. C1' 와 묶여 진행됨 (ReloadAsset 도 제거되고 in-place 갱신으로 대체).

### B. RefreshAssetRegistry 의 의미 분리
- `RefreshAssetRegistry` 가 `UnloadNonPersistentAssets` 를 부르지 않게.
- 이름 그대로 "레지스트리 재구축 = .Jmeta 재스캔" 만.
- 호출자 영향 점검: `SetAssetRootPath` 등은 unload 가 의도된 동작 → 명시적으로 한 줄 추가.
- P2 해소.
- **상태: 완료**. SetAssetRootPath 가 `UnloadNonPersistentAssets` + `LoadRegistryFromMetaFiles` 명시 호출. SaveAudioImportOptions 의 RefreshAssetRegistry 호출도 제거됨. GameBuildManager 는 새 의미가 정확히 의도와 일치해 변경 없음.

### C1'. SaveSpriteImportOptions 가 ReloadAsset 도 호출하지 않게 (PPU 한정)
- 현재 임포트 옵션은 PPU 만 있고, PPU 는 렌더 시 매번 곱셈으로 반영되므로 reload 불필요.
- 메모리의 SpriteAsset 의 ImportOptions 만 SetImportData 류로 갱신.
- 향후 옵션 영향 단계 분기:
  - A 단계 (렌더 시 곱셈만, 예: PPU) → 메모리 값만 갱신
  - B 단계 (CPU 메타 재계산, 예: Pivot, Slice 모드/그리드) → 위 + frames 재빌드
  - C 단계 (GPU 리소스 재생성, 예: Premultiply/ColorSpace/Mipmap) → 위 + GPU 텍스처 재생성 (= raw reload 와 동일)
- B/C 단계 옵션 도입 시점에 분기 추가. YAGNI.
- **상태: 완료, 균일화로 확장됨**. `IAsset::ApplyImportOptions(yaml)` 가상 메서드 도입. CSpriteAsset/CAudioAsset 가 자기 ApplyImportOptions 안에서 in-place 갱신. InspectorTool 의 Save* 가 자산 타입 분기 없이 `loaded->ApplyImportOptions(yaml)` 한 줄로 통일됨. 새 자산 타입 추가 시 가상 메서드만 override 하면 됨.

### C2. 워처가 `.Jmeta` 변경 무시
- 워처 콜백에서 `.Jmeta` 확장자 필터.
- 자기-반향(P8) 차단.
- 트레이드오프: 외부 .Jmeta 변경(P9) 안 잡힘 — 협업 시 한계, 현재 수용.
- **상태: 완료**. ProcessAssetEvent 진입부에 명시적 `.Jmeta` 컷 + 의도 주석. Created/Modified `.Jmeta` 는 무시, Deleted `.Jmeta` 는 기존 분기에서 raw 존재 시 메타 재생성.

### C3. FileWatcher 이벤트 합치기 — 인접성 가정 제거
- **점검 결과 P7 자체가 오인**. 현 워처는 ReadDirectoryChangesW 가 아니라 mtime 폴링 기반 (`WindowsFileWatcher.cpp`) 이라 REMOVE+ADD 합치기 자체가 없음. ProjectManager 의 `TryHandleAssetRename` 이 events 벡터 **전체** 를 순회하며 GUID/파일명 매칭 — 인접성 가정 없음.
- **실질 보강은 사용자 컨트롤 가능한 무시 패턴**으로 대체됨 (v0.0.13). `ProjectInfo.AssetWatchIgnorePatterns` + ProjectSettings UI + glob 매칭. 기본값에 임시파일 패턴(*.tmp, *.swp, ~$*, .DS_Store, Thumbs.db) 포함.
- **상태: 완료 (실질적으로 다른 형태로)**. P7 의 원래 가정(`ReadDirectoryChangesW` 기반) 이 본 코드베이스에 해당 없음 — 카탈로그 정리 의미만 남음.

### C4. Lazy loading + ref-count (별건 대형)

**결정 사항** (사용자 확정):

- **참조 단위**: 새 strong 핸들 타입 `AssetRef<T>` 도입 (현 SafePtr 는 weak 의미 유지).
- **Acquire/Release 시점**:
  - 씬 진입 시 일괄: 이미 존재하는 `CollectSceneLoadAssets` (ProjectManager.h:156) 확장. 씬이 자기 의존 자산 목록을 안다.
  - 컴포넌트 setter (동적 변경): 스크립트가 런타임에 SpriteGuid 를 바꾸는 경우 setter 안에서 Release 옛것 + Acquire 새것.
- **Persistent 의미 재정의**: "수동 로딩 + 잦은 unload 방지용 명시 플래그". 자동 로드 자산은 안 가짐. 사용자가 인스펙터에서 켜거나 ResourceRegistry 처럼 명시적으로 영구화한 자산만.
- **Prefetch**: 씬 진입 시 자산 집합을 백그라운드 워커로 미리 로드. 첫 참조 시 메인 스레드 IO hitching 방지.
- **씬 전환 시 자산 집합 diff**: 옛 씬 자산 집합 ∪ 새 씬 자산 집합 → release(옛-새), acquire(새-옛) 만 처리. 공유 자산은 그대로. unload 후 즉시 reload 낭비 방지.

P3, P6 동시 해소.

### C5. ReloadAsset 의 in-place data swap 화
- 자산 객체는 보존, 내부 데이터만 새 디스크 상태로 교체.
- `IAssetLoader::ReloadInto(IAsset&, Meta&)` 추가.
- ref-count 와 충돌 없이 reimport 가능.
- C4 와 묶일 때 의미 가장 큼. 단독으로도 P5 부분 완화.

### D. SpriteRenderer2D 에 SafePtr<CSpriteAsset> 런타임 캐시
- 컴포넌트에 런타임 캐시 필드(직렬화 X).
- SpriteRenderSystem 이 캐시 alive 일 때 LoadAsset 안 호출 — 매 프레임 mutex 진입 제거.
- C4 도입 후에는 `AssetRef<CSpriteAsset>` 으로 자연스럽게 흡수 (Acquire/Release 의 자연 포인트).
- P4 부분 완화.
- **상태: 완료**. `SafePtr<IAsset> SpriteAssetCache` + `AssetGuid CachedSpriteGuid` 두 필드 추가. SpriteRenderSystem 이 캐시 미스(GUID 변경/SafePtr 사망) 시에만 LoadAsset 호출.

### E. AssetManager mutex → shared_mutex
- read path (`LoadAsset`, `FindLoadedAsset` 등) 은 `shared_lock`.
- write path (`ReloadAsset`, `UnloadAsset`, `Register*`, `Unregister*` 등) 은 `unique_lock`.
- 매 프레임 read 가 워커 IO 와 경합해도 stall 줄어듦.
- P4 보강.
- **상태: 보류 (D 이후 우선순위 낮음)**. D 작업으로 SpriteRenderer2D 가 자산 캐시를 보유하여 정상 흐름에서 매 프레임 mutex 진입 자체가 사라짐. 또한 현 `recursive_mutex` → `shared_mutex` 전환은 재진입 경로(ReloadAsset, SetAssetRootPath, UnregisterAssetByPath, RegisterAssetByPath) 들을 internal `*Locked` helper 로 분리하는 중규모 리팩토링이 필요. 실제 워커 경합 stall 이 관측될 때 진행.

### F. GPU 리소스 소유권을 자산에서 분리
- `CSpriteAsset` 의 `OwnerPtr<IRHITexture>` 제거.
- 별도 RenderResource Cache 가 reference-counted 로 GPU 텍스처 소유.
- 자산 unload 가 렌더와 무관해짐.
- P5 근본 해소. C5 와 다른 접근. 큰 작업.

---

## 단기 잔여물 — v0.0.13 작업 직후 발견된 보강 항목

### S1. Rename 처리 경로에도 무시 패턴 적용
- `TryHandleAssetRename` / `TrySyncRenamedAssetMeta` 안에서 events 직접 순회 시 `IsAssetPathIgnored` 체크가 없음.
- 외부 도구가 `~$xxx` 같은 임시 파일을 옮기면 그게 자산 rename 으로 잘못 매칭될 수 있음.
- `IsImportableAssetPath` 가 결국 차단하기는 하지만 매칭 단계에서 미리 거르는 게 더 깨끗.
- **상태: 완료**. ProcessAssetEvents 의 rename 후보 루프 + TryHandleAssetRename + TrySyncRenamedAssetMeta 모두에 `IsAssetPathIgnored` 체크 추가.

### S2. ProjectSettings Multiline 의 줄바꿈
- `InputTextMultiline` 이 `\n` / `\r\n` 어느 쪽을 쓰는지 환경 의존. 현재 파싱이 둘 다 처리하므로 즉시 문제 없음.
- 한 번 실제 저장/재로드 사이클 돌려서 `*.tmp\r` 같이 \r 가 패턴에 끼지 않는지 검증.
- **상태: 완료**. UI 측 파싱은 이미 \r/\n 둘 다 분리자 처리. 추가로 LoadProject 의 YAML 패턴 읽기에 트레일링 \r / 양끝 공백 트림 추가 — 외부 편집/CRLF 혼선 방어.

### S3. ProjectSettings static 캐시 무효화
- `DrawCategoryAssetWatcher` 의 `static s_buffer / s_lastVec` 가 프로젝트 전환 시 stale 가능.
- `OnShow` 또는 첫 진입 시 명시적으로 캐시 무효화.
- **상태: 완료**. 백킹 버퍼를 `m_assetWatchIgnoreBuffer` 멤버로 끌어올리고 OnShow 에서 명시 재구축. DrawCategoryAssetWatcher 의 static 제거.

### S4. AudioImporterWindow 의 PPU 등가 처리 점검
- SpriteImporterWindow 의 PPU 위젯에 "(프로젝트 기본값)" 라벨이 들어갔지만 Audio 측에는 그런 옵션이 없는 것으로 추정 — 확인만.
- **상태: 완료 (작업 불필요)**. AudioImportOptions 에 PPU 개념 없음(`grep -i PPU` 결과 0건). 등가 처리 불필요.

### S5. 빌드 게임 경로의 PPU
- **상태: 완료 (2026-06-02)**. 빌드 manifest 바이너리 payload 에 `PixelsPerUnit` 을 추가하고,
  에디터 패키저 / `BuildGame.ps1` / 런타임 bootstrap 을 연결함.
- 기존 manifest 에 PPU tail 이 없으면 100으로 fallback 하도록 유지.
- 사용자 게임이 PPU≠100 이어도 빌드본이 manifest 값을 `EngineCore::PixelsPerUnit` 에 주입함.

---

## 의존 관계 / 영향

- A → P1 즉시 해소. 다른 후보의 전제 아님. 단독 진행 가능.
- C1' → A 의 확장 (PPU 만 있는 현 시점에 reload 자체 불필요로 함).
- C2 → P8 차단. P9 트레이드오프 수용.
- C3 → C2 와 독립. 워처 일임 모델의 견고함 전제.
- C4 → 큰 디자인 작업. C5 와 묶임. P3, P6 동시 해소.
- C5 → C4 의 일부, 또는 단독으로 P5 완화.
- D → P4 일부. C4 도입 시 자연스럽게 AssetRef 로 흡수.
- E → D 의 보강. 단독으로도 의미 있음.
- F → P5 근본. C5 와 다른 접근.
- B → 별건 청소. 다른 후보의 전제 아님.

---

## 권장 진행 순서

```
1차:  A → C1' → D                         ← 본 PPU 작업과 묶어 즉시      [완료 v0.0.12]
2차:  B (작은 청소)                         ← A 후속                       [완료 v0.0.12]
3차:  E (mutex 교체)                       ← 작은 별건                    [보류 — D 이후 우선순위 낮음]
4차:  C2 + C3 (워처 일임 + 무시 패턴)       ← 별건 중간 작업               [완료 v0.0.13]
5차:  S1 + S2 + S3 + S4 (잔여물 정리)      ← v0.0.13 직후 보강            [진행 중]
6차:  S5 (빌드 게임 PPU)                   ← BuildPipelineDesign.md 정리 후
7차:  C4 + C5 + 부분 F (근본 재설계)       ← 디자인 세션 + 별건 큰 작업
```

본 PPU 작업 묶음(1~5차)의 잔여물까지 끝나면 자산 시스템 단기 보강은 마무리. 6차는 빌드 파이프라인 작업과 묶이고, 7차는 별도 디자인 세션 필요.

---

## ResourceRegistry 와의 양립

- `Engine/Core/Resource/ResourceRegistry.h:23-24` 에 따라 등록 자산은 `IsPersistent = true` 마킹 → `UnloadNonPersistentAssets` 가 스킵.
- C4 의 새 Persistent 의미 ("수동 로딩 명시 플래그") 와 일치 — ResourceRegistry 가 자기가 들고 있는 자산을 명시적으로 영구화하는 행위.
- C4 도입 후 `AssetRef<T>` 가 자동 ref-count 가지면 ResourceRegistry 가 핸들만 들고 있어도 보호되어 IsPersistent 가 redundant 가능.
- 결정 사항: ResourceRegistry 가 (a) 핸들 + IsPersistent 두 겹 보호 (b) 핸들만 — 후자가 깨끗.

---

## 미결정 사항

- **외부 `.Jmeta` 변경 동기화 정책 장기 방향** — 현재 C2 로 무시. 협업 워크플로 도입 시 재검토.
- **`AssetRef<T>` 의 thread-safety** — 메인 스레드에서만 Acquire/Release 인가, 워커도 가능한가. C4 디자인 세션 항목.
- **prefetch 실패 정책** — 씬 진입 prefetch 중 일부 자산 로드 실패 시 씬 진입을 막을지, placeholder 로 진행할지. C4 디자인 세션 항목.
