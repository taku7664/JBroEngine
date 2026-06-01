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
- `_sendEventQueue` 순회 시 `[i]` 와 `[i+1]` 만 비교해 REMOVE+ADD → MOVED 합침.
- 동시 다중 변경에서 같은 FileId 의 이벤트가 인접하지 않을 수 있음.
- 합치기 실패 시 자산 GUID 가 끊김.

### P8. 자기-반향 — 디스크에 쓴 변경을 워처가 다시 감지
- 에디터 내부에서 SaveSpriteImportOptions 가 `.Jmeta` 쓰면 워처가 같은 변경을 잡아 ReloadAsset 을 또 호출.
- 같은 변경이 두 번 처리됨.
- **해소 방향 확정**: 워처가 `.Jmeta` 변경 무시 (C2). 현재 엔진에서 raw 직접 편집 기능이 없어 이걸로 충분.

### P9. 외부 `.Jmeta` 변경 동기화 부재 (협업 시)
- 동료가 PPU 바꾼 .Jmeta 를 git push, 내가 pull → 워처가 .Jmeta 만 보고 reload 할지 정책 없음.
- 워처가 .Jmeta 무시(C2) 하면 외부 변경이 안 들어옴. 무시 안 하면 P8.
- 현재는 .Jmeta 무시로 가서 외부 동기화 포기. 향후 협업 워크플로 도입 시 재검토.

---

## 처치 후보

### A. 인스펙터 Apply 의 RefreshAssetRegistry 호출 제거
- `InspectorTool.cpp:698` 한 줄 제거. 뒤 `ReloadAsset(updatedMetaData.Guid)` 만 남김.
- P1 직접 해소.

### B. RefreshAssetRegistry 의 의미 분리
- `RefreshAssetRegistry` 가 `UnloadNonPersistentAssets` 를 부르지 않게.
- 이름 그대로 "레지스트리 재구축 = .Jmeta 재스캔" 만.
- 호출자 영향 점검: `SetAssetRootPath` 등은 unload 가 의도된 동작 → 명시적으로 한 줄 추가.
- P2 해소.

### C1'. SaveSpriteImportOptions 가 ReloadAsset 도 호출하지 않게 (PPU 한정)
- 현재 임포트 옵션은 PPU 만 있고, PPU 는 렌더 시 매번 곱셈으로 반영되므로 reload 불필요.
- 메모리의 SpriteAsset 의 ImportOptions 만 SetImportData 류로 갱신.
- 향후 옵션 영향 단계 분기:
  - A 단계 (렌더 시 곱셈만, 예: PPU) → 메모리 값만 갱신
  - B 단계 (CPU 메타 재계산, 예: Pivot, Slice 모드/그리드) → 위 + frames 재빌드
  - C 단계 (GPU 리소스 재생성, 예: Premultiply/ColorSpace/Mipmap) → 위 + GPU 텍스처 재생성 (= raw reload 와 동일)
- B/C 단계 옵션 도입 시점에 분기 추가. YAGNI.

### C2. 워처가 `.Jmeta` 변경 무시
- 워처 콜백에서 `.Jmeta` 확장자 필터.
- 자기-반향(P8) 차단.
- 트레이드오프: 외부 .Jmeta 변경(P9) 안 잡힘 — 협업 시 한계, 현재 수용.

### C3. FileWatcher 이벤트 합치기 — 인접성 가정 제거
- 큐 순회 → 그룹화 (FileId 별 액션 시퀀스) → reduce.
- 같은 batch 안의 액션을 묶어 최종 의도 산출 (REMOVE+ADD → MOVED, ADD+REMOVE → 무시 등).
- 보강 예외 처리:
  - 임시 파일 패턴 필터 (`.tmp`, `~$xxx`, `xxx.swp`)
  - 처리 시점 존재 검증 (`std::filesystem::exists`) — ADD 받았는데 이미 삭제됐을 수 있음
  - FileId + 부모 디렉토리 조합 키 — NTFS FileId 재활용 대비
  - RENAMED_OLD 후 RENAMED_NEW 가 다음 폴링 사이클로 넘어간 경우 보류 큐 유지
- P7 해소.

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

### E. AssetManager mutex → shared_mutex
- read path (`LoadAsset`, `FindLoadedAsset` 등) 은 `shared_lock`.
- write path (`ReloadAsset`, `UnloadAsset`, `Register*`, `Unregister*` 등) 은 `unique_lock`.
- 매 프레임 read 가 워커 IO 와 경합해도 stall 줄어듦.
- P4 보강.

### F. GPU 리소스 소유권을 자산에서 분리
- `CSpriteAsset` 의 `OwnerPtr<IRHITexture>` 제거.
- 별도 RenderResource Cache 가 reference-counted 로 GPU 텍스처 소유.
- 자산 unload 가 렌더와 무관해짐.
- P5 근본 해소. C5 와 다른 접근. 큰 작업.

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
1차:  A → C1' → D                         ← 본 PPU 작업과 묶어 즉시
2차:  B (작은 청소)                         ← A 후속, 같은 세션 가능
3차:  E (mutex 교체)                       ← 작은 별건
4차:  C2 + C3 (워처 일임 모델 완성)         ← 별건 중간 작업
5차:  C4 + C5 + 부분 F (근본 재설계)        ← 디자인 세션 + 별건 큰 작업
```

1차 까지가 본 PPU 묶음 안. 2차~3차는 짧은 별건. 4차부터 별도 세션.

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
