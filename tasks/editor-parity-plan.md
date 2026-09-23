# 에디터 이식 대조표와 단계 계획 (D-127)

기존 엔진: `C:\Users\박주형\source\repos\JBroEngine` (`ProjectRule.md` §11.0).
현재 에디터: `source/JBroEngine/Modules/JBroEditor`.

2026-09-21 에 사용자가 에디터를 띄워 놓고 지적한 것에서 출발했다. 지적은 이랬다.

1. 계층 뷰에서 **순서 바꾸기와 부모 해제**가 제대로 되지 않는다.
2. **게임 뷰에 기즈모가 뜨고 편집이 된다.** 게임 뷰는 시뮬레이션 뷰이고,
   유니티의 씬 뷰에 해당하는 것은 기존 엔진에서 **캔버스 뷰**였다.
3. **루트 도크 - 메인 도크 - 임포터 도크** 구조를 띠지 않는다.
4. **메뉴가 제대로 이식되지 않았다.**
5. 우측 상단의 **X 표시**가 원래는 없어야 한다.
6. `ImEditor` 자체가 이식되지 않은 것으로 보인다.
7. 에디터 관련 **유틸 함수를 함수화해 일관되게 재사용**해야 한다.

## 1. 실측 — 규모 대조

| 구간 | 기존 엔진 | 현재 | 비고 |
| --- | --- | --- | --- |
| 에디터 창·도구 소스 | 18,481줄 | 5,125줄 | 아래 §2 가 차이의 내역이다 |
| 공용 위젯 계층 | `ImItem/` 23종 | `Widget/` 13파일 | **거의 이식되어 있다**(D-79). 여기는 구멍이 아니다 |
| 로컬라이징 키 | 658개 | 61개 | 화면 수 차이에 비례한다 |
| 창 프레임워크 | `ImWindow/` 5종 + `ImEditor` 1,257줄 | `EditorPanel` + `EditorApplication` | 훅은 의도적으로 줄였다(D-70). 구멍은 **도크 계층**이다 |

## 2. 대조표 — 기존에 있고 우리에게 없는 것

> **이 절은 2026-09-21 에 처음 조사한 그때의 상태다.** "현재" 칸과 "판정" 칸은 그날의 것이고,
> 그 뒤에 무엇이 섰는지는 §3 의 단계와 §5 의 표가 갖는다. 그날 **없음** 이던 것 대부분은
> 지금 서 있다 - 그 자리에 `→` 로 어디서 섰는지만 적어 둔다. 역사와 현재를 한 칸에 섞으면
> 둘 다 믿을 수 없게 된다.

### 2.1 창 구조

| 기존 | 현재 | 판정 |
| --- | --- | --- |
| `CRootDockWindow` — 전체화면 도크 뿌리, 메뉴 막대(파일·설정·디버그), 프로젝트 없이도 뜬다 | `##EditorRoot` 한 겹 | 그때 **없음** → 완료 (D-134) |
| `CMainDockWindow` — 루트에 도킹되는 둘째 도크, 자기 메뉴 막대(시뮬레이션·편집·창), **프로젝트를 열어야 생긴다** | 없음 | 그때 **없음** → 완료 (D-134), 수명은 우리 쪽이 다르다(§3 의 6) |
| 임포터 창 — 도킹하지 않는 떠 있는 대화상자(`SpriteImporter`·`AudioImporter`) | 없음 | 그때 **없음** → 스프라이트는 뿌리 도크의 뷰어로 완료 (D-155·D-156), 오디오는 해당 없음 |
| `IMWINDOW_FLAG_NO_CLOSE_BUTTON` — 창마다 닫기 단추를 고른다. 메인 도크와 루트 도크가 끈다 | `HasCloseButton()` 은 있으나 **아무 패널도 끄지 않는다** | 그때 정책 미적용 → 완료 (§3 의 2) |
| 도크 노드 버튼(닫기·창 메뉴) 끄기가 `CImDockWindow` 기본값 | 안 끈다 → **탭 줄 오른쪽 끝에 X 가 나온다** | 그때 지적 ⑤ → 완료 (§3 의 2), 지금은 X 가 없다 |

### 2.2 도구 창

| 기존 도구 | 현재 | 판정 |
| --- | --- | --- |
| `CCanvasViewTool`(1,645줄) + `CanvasViewContour`·`CanvasViewEditContext`·`CanvasViewCoordinates` | 없음 | 그때 **없음 — 지적 ②의 핵심** → 완료 (D-130·D-149·D-150·D-157·D-170·D-172) |
| `CGameViewTool`(210줄) — 그림만 붙이고 **기즈모도 편집도 없다**. 상태 오버레이·입력 게이팅 | `GameViewPanel` 이 기즈모를 그리고 편집한다 | 그때 **역할이 뒤바뀌어 있었다** → 완료 (D-130), 지금 게임 뷰는 보기만 한다 |
| `CLayerTool`(695줄) — 계층 + 레이어. 행 3분할 드롭, 레이어 행 드롭, 우클릭 `부모 해제`, 눈 아이콘 | `HierarchyPanel` — 세 구역 드롭·부모 해제·뿌리 순서·레이어까지 섰다(D-128·D-135) | 맞춤 |
| `CInspectorTool`(3,068줄) | `InspectorPanel`(1,183줄) | 레이아웃은 사용자가 좋다고 했다. 기능 차이는 §4 에서 따로 잰다 |
| `CAssetBrowserTool`(2,652줄) | `AssetBrowserPanel` — 두 칸·길잡이·새 폴더·이름·삭제·끌어 옮기기·탐색기(D-139), 아이콘 보기(D-147)·다중 선택(D-141)·새 캔버스와 캔버스 열기(D-174) | `에셋 추가` 의 나머지(재질·프리팹·폰트·이펙트·애니메이션)는 그 에셋이 없어 열림 |
| `CLogTool`(117줄) | 없음 | 그때 **없음** → 완료 (§3 의 10) |
| `CShortcutReferenceTool`(69줄) | 없음 | 그때 **없음** → 완료 (D-132) |
| `CCpuProfilerWindow`(425줄)·`CGpuProfilerWindow`(334줄) | `StatsPanel` + `ProfilerPanel`(D-138) | CPU 는 섰다. GPU 타임스탬프는 RHI 에 없다 |
| `CProjectSettingsWindow`(826줄)·`CBuildSettingsWindow`(1,156줄) | `ProjectSettingsPanel` 이 섰다(D-137). 빌드 설정은 빌드 시스템이 없어 보류 | 반쯤 |
| 임포터 4종(`SpriteImporter`·`AudioImporter`·`SpriteViewer`·`SpriteImportOptionsEditor`) | 인스펙터의 임포트 옵션만 | 그때 **없음** → 스프라이트 셋은 완료 (D-155·D-156·D-159·D-173), 오디오는 해당 없음 |
| `EffectEditorWindow`·`AssetInspectorPreview`·`EditorAudioPreview` | 없음 | 오디오·이펙트가 없어 보류 |

### 2.3 메뉴

기존 루트 도크: **파일**(새 프로젝트 / 프로젝트 열기 / 프로젝트 저장 / 빌드→플랫폼별·일괄),
**설정**(프로젝트 설정 / 빌드 설정), **디버그**(GPU 프로파일링 / CPU 프로파일링),
오른쪽 끝에 스크립트 빌드 상태.

기존 메인 도크: **시뮬레이션**(재생 토글 / 일시정지 토글), **편집**(실행 취소 / 다시 실행 /
복사 / 붙여넣기), **창**(에디터→자식 창 토글, 임포터→스프라이트·오디오).

그때의 우리: **파일**(캔버스 저장 / 종료), **편집**(실행 취소 / 다시 실행), **창**(패널 토글).
→ **시뮬레이션·설정·디버그 메뉴가 통째로 없고, 파일 메뉴는 프로젝트를 다루지 못한다** 는 것이 그날의 판정이었다.

지금은 기존과 같은 차례로 선다(§3 의 7): 뿌리에 **파일**(새 프로젝트·프로젝트 열기·프로젝트 저장·캔버스 저장·종료)·
**설정**(프로젝트 설정)·**디버그**(CPU 프로파일러·통계·로그), 메인 도크에 **시뮬레이션**·**편집**(실행 취소·다시 실행·
복사·붙여넣기·자식으로 붙여넣기·삭제)·**창**(에디터 패널·임포터). 빌드·빌드 설정·GPU 프로파일링은 그 기능이 없어
넣지 않았다.

### 2.4 그 밖

| 기존 | 현재 | 판정 |
| --- | --- | --- |
| `CEditorShortcutManager`(259줄) — 단축키를 한곳에 두고 `CanExecute`/`Execute`/`GetShortcutText` | `EditorApplication` 안에 `ImGui::Shortcut` 이 흩어져 있다 | 그때 **없음 — 지적 ⑦** → 완료 (D-132·D-166) |
| `EditorGuiActions`(476줄) — 오브젝트 추가·복사·붙여넣기·컴포넌트 추가·삭제 메뉴를 **한 벌로** 만들어 계층·캔버스 뷰가 함께 쓴다 | 계층 패널이 자기 안에서 직접 그린다 | 그때 **없음 — 지적 ⑦** → 완료 (D-132·D-166~D-168·D-170) |
| `EditorDragDrop`(152줄) — 드래그 꾸러미 이름과 해석을 한곳에 | 패널 안 상수 | 그때 **없음** → 완료 (D-154) |
| `EditorSimulationGuard` — 시뮬레이션 중 저장·빌드를 막고 사유를 말한다 | 시뮬레이션 개념 자체가 없다 | 그때 **없음** → 완료 (D-153) |
| `EditorSessionPersistence` — 도크 배치·카메라를 프로젝트에 저장 | 없다 | 그때 **없음** → 완료 (D-146) |
| `EditorTheme`(95줄) | `EditorTheme`(222줄) | 있다 |

### 2.5 엔진 쪽 전제

- **시뮬레이션 개념이 엔진에 없다.** 기존은 `CanvasManager::IsSimulationPlaying()` 이 있고
  재생/정지/일시정지가 캔버스 상태를 스냅숏하고 되돌린다. 우리 `EngineInstance::Tick` 은
  언제나 게임을 돌린다 — 그래서 에디터 화면이 곧 **돌아가는 게임**이고, 편집을 그 위에 얹게 됐다.
  캔버스 뷰가 서려면 이것이 먼저 있어야 한다.
- **한 프레임에 렌더 타깃이 하나다.** `Renderer::BeginFrame(FrameTarget)` 이 프레임 전체의
  타깃을 정하고 모든 뷰가 거기로 간다. 기존 `ImEditor` 는 캔버스 뷰 RT·게임 뷰 RT·레이어
  썸네일·프로파일러 프리뷰를 **각각** 그렸다. 캔버스 뷰와 게임 뷰를 같은 프레임에 보이려면
  **뷰마다 타깃**이 필요하다.

## 3. 단계

단계마다 빌드·테스트를 통과시키고 커밋한다. 화면을 바꾼 단계는 띄워서 보고 무엇을 봤는지
적는다(§11.4).

- `[완료]` **0. 기존 엔진 경로를 규칙에 박는다.** (`ProjectRule.md` §11.0, `CLAUDE.md`)
- `[완료]` **1. 계층 뷰를 기존 수준으로.** 행 3분할 드롭(앞·자식·뒤), 루트 순서 바꾸기,
  빈 영역 드롭으로 부모 해제, 우클릭 `부모 해제` 항목, 선택은 누를 때가 아니라 뗄 때.
  → 지적 ①. 엔진 쪽에 **루트 순서**가 필요하다(기존은 `CreationOrder` 로 표시 순서를 잡는다).
- `[완료]` **2. 닫기 단추 정책.** 도크 노드의 닫기·창 메뉴 단추를 끄고, 패널마다
  `HasCloseButton` 을 실제로 고른다. → 지적 ⑤.
- `[완료]` **3. 게임 뷰를 시뮬레이션 뷰로 되돌린다.** 기즈모·편집을 떼고, 상태 오버레이
  (재생 중 / 정지됨 / 활성 캔버스 없음 / 카메라 없음)와 입력 게이팅을 붙인다. → 지적 ②의 앞쪽.
- `[완료]` **4. 시뮬레이션 재생·정지·일시정지.** 엔진에 상태를 두고, 메뉴·단축키·
  `EditorSimulationGuard` 가 그것을 읽는다.
- `[완료]` **5. 캔버스 뷰 신설.** 편집 카메라(팬·줌), 격자, 선택 표시, 피킹, 기즈모,
  우클릭 맥락 메뉴. 렌더러에 **뷰마다 타깃**을 넣는 일이 여기 들어간다. → 지적 ②의 뒤쪽.
- `[완료]` **6. 루트 도크 - 메인 도크.** 두 겹 도크와 메뉴 나눔. → 지적 ③.
  `[완료]` **뿌리에 붙는 파일 창**(D-155). 스프라이트 뷰어가 메인 도크와 탭으로 선다.
  가려져 있어도 열면 앞으로 나오고, 미리보기·아이콘은 그림 비율을 지킨다(D-159, 실제 에디터에서 칸 나눔·재생까지 확인).
  `[열림]` 크게 키운 픽셀 아트는 선형 샘플러라 흐리다(기존도 같다). 점 샘플링은 렌더 백엔드의 그리기 콜백이 든다.
  `[완료]` **스프라이트 가져오기**(D-156). 밖의 그림을 복사해 등록하고 뷰어로 연다.
  `[열림]` 오디오 임포터는 오디오가 없어 두지 않았다.
  `[열림]` **메인 도크의 수명**은 기존과 다르다 - 기존은 프로젝트를 열어야 생겼고 우리는 늘 있다.
  프로젝트가 없을 때도 패널이 "열린 프로젝트가 없습니다" 를 말하는 쪽이 지금 구조에 맞고,
  없애면 빈 창만 남는다. 프로젝트를 여닫는 길이 더 자라면 다시 본다.
- `[완료]` **7. 메뉴 이식.** 파일(프로젝트 열기·캔버스 저장·종료)은 뿌리, 시뮬레이션·편집·
  창(에디터)은 메인 도크. → 지적 ④.
  `[완료]` **프로젝트 저장·설정·디버그 메뉴**(D-151). 기존과 같은 차례로 선다.
  `[완료]` **새 프로젝트**(D-160). 기존과 같이 폴더를 고르고 팝업에서 이름(과 우리 파일이 요구하는 2D/3D)을 받아
  세우고 바로 연다. "런처가 맡는다" 고 미뤘으나 런처에도 만드는 길이 없었다 - 어디서도 프로젝트를 만들 수 없었다.
  임포터 메뉴는 D-156 에서 섰다.
  `[열림]` **빌드·빌드 설정·GPU 프로파일링 메뉴**는 그 기능이 없어 넣지 않았다 -
  눌러도 아무 일도 없는 항목은 없는 것보다 나쁘다.
- `[완료]` **8. 단축키 관리자와 단축키 참조 창.** 흩어진 `ImGui::Shortcut` 을 한곳으로. → 지적 ⑦.
- `[완료]` **9. 공용 에디터 동작(`EditorActions`).** 오브젝트 추가·복사·붙여넣기·삭제·
  컴포넌트 추가 메뉴를 한 벌로 만들어 계층·캔버스 뷰·인스펙터가 함께 쓴다. → 지적 ⑦.
- `[완료]` **10. 로그 창.**
- `[완료]` **10-1. 레이어 UI.** 엔진에 레이어가 있고 `.jcanvas` 도 적는데 에디터에는 다룰 길이
  하나도 없었다. 계층 창이 레이어를 머리로 두고 오브젝트를 묶어 보인다(기존 `CLayerTool` 과 같은 자리).
- `[완료]` **11-1. 프로젝트 설정 창**(D-137). `.jproject` 의 값을 보고 고친다. 쓰기는 원문을
  타고 가며 아는 키만 바꾸므로 주석도 모르는 키도 남는다.
- `[진행 예정]` **11-2. 빌드 설정 창·임포터 창.** 빌드 시스템과 오디오가 엔진에 없다.
  그것이 서면 함께 선다.
- `[완료]` **11-3. CPU 프로파일러**(D-138). 엔진에 계측이 없어 `JBro/Core/Profiler.h` 부터 세웠다.
  `[열림]` **GPU 프로파일러**는 RHI 에 타임스탬프 질의가 없다. 그것이 서면 같은 창에 붙는다.
  `[완료]` 기존 CPU 프로파일러가 내던 **캔버스 쪽 숫자**(오브젝트·풀·선택·되돌리기)는
  통계 창으로 갔다(D-145). 스크립트 풀·코루틴은 그 기능이 아직 없다.
- `[완료]` **12. 에셋 브라우저**(D-139). 두 칸(폴더 나무 | 내용), 길잡이 줄, 새 폴더·이름 바꾸기·
  삭제(묻고 한다)·폴더로 끌어 옮기기·탐색기에서 보기·다시 훑기. `.jmeta` 가 늘 함께 움직인다.
  `[완료]` **다중 선택**(D-141). Ctrl 로 더하고 빼고 Shift 로 닻에서 누른 줄까지 고른다.
  고른 것은 한 꾸러미로 함께 옮겨지고 한 번에 지워진다. 테스트가 세 파일을 골라 지우고
  고르지 않은 넷째가 남는 것까지 확인한다.
  `[완료]` **아이콘 보기**(D-147). 에디터가 자기 썸네일을 만들어 든다(`EditorThumbnails`).
  같은 그림이 인스펙터의 미리보기로도 선다.

## 4. 아직 재지 않은 것

- `[완료]` **에디터 세션**(D-146). 보던 캔버스·편집 카메라·에디터 언어가 프로젝트 파일에
  남고, 다시 열면 그 자리에서 이어진다. 언어는 설정 창에서 고른다.
  창 배치도 `<프로젝트파일>.layout.ini` 로 남고, 읽은 배치가 기본 배치를 이긴다.
- 인스펙터의 기능 차이(3,068줄 대 1,183줄). 레이아웃은 사용자가 좋다고 했으므로
  **무엇이 없는지만** 따로 재서 여기 적는다. 재어 보니 다음과 같다(D-142).
  `[완료]` **이름 칸**. 오브젝트의 이름을 고칠 길이 에디터에 하나도 없었다.
  `[완료]` **활성·사용 토글이 커맨드를 거친다.** 예전에는 값을 그대로 써 되돌릴 수 없었다.
  활성은 고른 것 전체에 간다.
  `[열림]` **캔버스·레이어 인스펙터.** 우리 `Layer` 는 이름·순서·보임뿐이라 보여 줄 것이
  계층 창과 겹친다. 기존의 합성 속성(공간·배율 방식·시차·자기 텍스처·안전 영역)은 엔진에 없다.
  `[완료]` **콜라이더 모양**(D-143). 기존은 캔버스 뷰가 그렸고 우리도 그리로 넣었다.
  상자는 돌면 기울어지고, 트리거는 색이 다르며, 툴바의 토글로 감출 수 있다.
  `[열림]` **접촉점·강체 상태 표시.** 기존은 실행 중의 접촉점과 강체 값을 인스펙터에 적었다.
  우리 2D 물리에서 그 값을 꺼내는 길이 아직 없다(`Collision2D` 는 콜백으로만 온다).
  `[완료]` **에셋 미리보기**(D-147). 임포트 옵션 위에 그 에셋의 그림이 선다.
  `[열림]` **애니메이션 클립 편집기**와 오디오·재질·폰트 임포트 옵션. 그 에셋 타입이 없다.
  `[완료]` **필드 편집이 고른 것 전체에 간다.** 다시 재어 보니 `CollectEditTargets` 가 이미
  최상위 선택 전체에 같은 델타를 얹고 있었다(`TestMultiEditPicksTheSameOrdinalEverywhere`).
  열림으로 적었던 것은 잘못 잰 것이었다.
- `[완료]` **3D 의 캔버스 뷰**(D-136). 궤도 카메라(오른쪽 끌기로 돌고 휠로 거리)와 원근 투영으로
  선다. 게임 카메라가 없어도 그린다.
  `[완료]` **3D 의 기즈모와 고르기**(D-140). `Renderer::GetLastEditorViewCamera` 가 이번 프레임에
  실제로 그린 편집 카메라를 내주고, `CanvasViewPanel::MakeGizmoCamera` 가 그것으로 손잡이를 세운다.
  같은 행렬을 에디터가 한 번 더 세우지 않으므로 그림과 손잡이가 갈리지 않는다.
  고르기는 오브젝트의 자리를 화면으로 투영해 마우스와의 거리를 재고, 선택은 점 하나로 표시한다.
  같은 카메라로 **바닥 격자**(y=0 평면)도 깐다. 선은 토막 내어 이어 카메라 평면을 가로지르는
  선도 남고, 바라보는 점 둘레의 스무 칸만 그린다. 켠 프레임과 끈 프레임의 백버퍼를 견줘
  26,741 픽셀이 달라지는 것을 테스트가 확인한다.
  `[열림]` **진짜 선택 윤곽**은 남았다. 메시의 경계 상자를 에디터가 얻는 길이 없어
  짐작한 상자로 두르면 맞지 않는 테두리가 되는데, 맞지 않는 테두리는 없는 것보다 나쁘다.
- `[완료]` **들어가 고르기**(D-157, 기존 `CCanvasViewEditContext`). 누르면 맨 위 부모, 두 번 누르면 안으로,
  빈 곳을 두 번 누르면 밖으로.
- `[완료]` **격자 눈금의 숫자**(D-144). 선마다 좌표를 적는다. 겹치면 건너뛴다.
  `[열림]` 픽셀 표시 토글은 한 유닛이 몇 픽셀인지 프로젝트를 대표하는 값이 없어 넣지 않았다.
- `[완료]` **스프라이트 알파 외곽선**(D-149, 기존 `CCanvasViewContour`). 불투명한 픽셀의
  바깥 변을 모아 그린다. 폴리곤으로 잇지는 않는다 - 그리는 데에는 이을 필요가 없다.
- `[완료]` **겹쳐 그리기의 좌표 기준**(D-150). 2D 캔버스 뷰의 격자·테두리·기즈모·집는 칸이
  전부 13 픽셀쯤 어긋나 있었다. 화면 픽셀을 읽는 회귀 검사가 그 자리를 지킨다.
- `[완료]` **캔버스 뷰의 피킹 칸**(D-148). 에셋이 정한 칸 크기와 피벗을 `AssetSystem::GetSprite`
  에서 얻는다. 전에 "볼 길이 없다" 고 적은 것은 잘못 잰 것이었다.
- `[완료]` **캔버스 뷰의 상자 선택**(D-136). 빈 곳에서 끌면 상자가 따라오고, 놓으면 **닿은** 것을
  모두 고른다. 끌지 않고 누른 것은 상자가 아니라 클릭이다.
  `[열림]` **폴리곤 버텍스 편집**은 `PolygonCollider2D` 에 해당하는 컴포넌트가 우리 2D 에 없다 -
  콜라이더가 생기면 그 UI 와 함께 볼 자리다.
- `AssetInspectorPreview`·`EffectEditor`·오디오 관련은 엔진에 해당 기능이 없어 보류한다.
  보류의 근거는 "없어서" 이지 "안 옮겨서" 가 아니다.

## 5. 기존 에디터 파일 대조표 (2026-09-22)

기존 `Application/Editor/` 의 소스 74개를 하나씩 우리 쪽과 맞댔다. "기억으로 없다고 적지 않는다" 는 규칙(§11.0)을
표로 지킨 것이다. **상태**: 완료 = 같은 일을 하는 것이 있다 · 열림 = 없고 이유가 적혀 있다 · 해당 없음 = 그 기능이
엔진에 없거나 다른 곳이 맡는다.

| 기존 파일 | 우리 쪽 | 상태 |
|---|---|---|
| `RootDockWindow` | `EditorApplication::DrawRootDock` + 뿌리 탭(메인·파일 창) | 완료 (D-134·D-155) |
| `Main/MainDockWindow` | `DrawMainDock` · 메뉴 · 가려져도 도크 유지 | 완료 (D-134·D-151·D-155) |
| `Main/CanvasView/CanvasViewTool` | `CanvasViewPanel` (격자·눈금·콜라이더·기즈모·상자 선택·맞춤·단위 토글) | 완료 (D-136·D-143·D-144·D-184), `화면에 맞추기`(화면 공간 레이어 없음)와 `버텍스 삭제`(폴리곤 콜라이더 없음)는 해당 없음 |
| `Main/CanvasView/CanvasViewContour` | `EditorSpriteContours` | 완료 (D-149) |
| `Main/CanvasView/CanvasViewCoordinates` | `WorldToScreen`·`ScreenToWorld` (그린 화면 기준) | 완료 (D-150) |
| 캔버스 뷰의 텍스트 오버레이 | `DrawOverlay` (선택·카메라·들어간 곳) | 완료 (D-172), 픽셀/유닛 토글은 전역 PPU 가 없어 열림 |
| `Main/CanvasView/CanvasViewEditContext` | `MapToLevel`·들어가 고르기 | 완료 (D-157) |
| 캔버스 뷰의 오브젝트 우클릭 메뉴 | `EditorActions::DrawObjectMenu` (계층 줄과 같은 한 벌) | 완료 (D-170) |
| `Main/GameView/GameViewTool` | `GameViewPanel` (보기만, 상태 글자) | 완료 (D-130) |
| `Main/Guizmo/Guizmo2D`·`Guizmo3D`·`EditorGuizmoController` | `GizmoModel`·`GizmoEditing`·`Widget::Gizmo` (로컬·월드 포함) | 완료 (D-109·D-140·D-171) |
| `Main/Inspector/InspectorTool` | `InspectorPanel` (이름·활성·필드·다중 편집·프레임 고르기·컴포넌트 복사와 붙여넣기) | 완료 (D-142·D-165·D-167), 레이어 인스펙터는 해당 없음(이름·보임이 전부이고 계층의 줄과 메뉴에 있다, D-183), 캔버스 인스펙터는 열림 |
| `Main/Importer/SpriteFramePick` | `EditorApplication::BeginSpriteFramePick` + 뷰어의 고르기 줄 | 완료 (D-165) |
| `Main/Inspector/AssetInspectorPreview` | 인스펙터 미리보기 + 머리 네 줄·뷰어에서 열기 | 완료 (D-147·D-173) |
| `Main/Inspector/EditorAudioPreview` | — | 해당 없음 (오디오 없음) |
| `Main/Inspector/EffectEditorWidget`·`EffectEditorWindow` | — | 해당 없음 (이펙트 없음) |
| `Main/Inspector/ButtonRectFit` | — | 해당 없음 (`Button2D` 없음) |
| `Main/Layers/LayerTool` | `HierarchyPanel` (레이어 머리·순서·부모 해제·보임·오브젝트 눈·Shift 범위 선택) | 완료 (D-128·D-135·D-163·D-169), 이름 고치기가 한 커맨드인 것은 D-183, 캔버스 줄은 열림(캔버스 설정이 없다) |
| `Engine/Editor/ImEditor` (창·팝업·미룬 일·뷰 타깃·캔버스 뷰 선택/숨김) | `EditorApplication` · `EditorPanel` · `EditorPopup` | 완료 (D-163 에서 공개 기능 하나씩 대조), 카메라 컬링 통계·GPU 미리보기는 열림 |
| `Main/AssetBrowser/AssetBrowserTool`·`Utils`·`AssetHandler` | `AssetBrowserPanel` (두 칸·파일 다루기·다중 선택·아이콘·끌어 놓기·파일 클립보드) | 완료 (D-139·D-141·D-147·D-154·D-182), `.meta` 보이기는 해당 없음(레지스트리를 본다), 즐겨찾기는 기존도 빈 제목줄 |
| `Main/EditorAssetPickDialog` | 에셋 칸의 검색 드롭다운 + 브라우저에서 열기 | 완료 (D-118·D-155) |
| `Main/Importer/SpriteImporterWindow`·`ImporterWindowBase` | `ImportAssetFile`·"가져오기..." | 완료 (D-156) |
| `Main/Importer/SpriteViewerWindow`·`SpriteFramePick`·`SpriteImportOptionsEditor` | `SpriteViewerWindow` + `InspectorPanel::DrawAssetOptions` | 완료 (D-155·D-159·D-185), 확대·창에 맞추기·피벗 표시·가리킴은 D-185, 창 메뉴(열기·탭 닫기)는 해당 없음(브라우저에서 열고 탭의 `x` 로 닫는다) |
| `Main/Importer/AudioImporterWindow` | — | 해당 없음 (오디오 없음) |
| `Main/Log/LogTool` | `LogPanel` | 완료 (D-133) |
| `Main/ShortcutReference/ShortcutReferenceTool` | `ShortcutPanel` | 완료 (D-132) |
| `Main/Debug/CpuProfilerWindow` | `ProfilerPanel` + `StatsPanel` | 완료 (D-138·D-145), 스크립트 풀·코루틴은 해당 없음 |
| `Main/Debug/GpuProfilerWindow` | — | 열림 (RHI 타임스탬프 질의 없음) |
| `Main/ProjectSettingsWindow` | `ProjectSettingsPanel` (일반·경로·빌드·언어) | 완료 (D-137·D-146), 입력·오디오·폰트는 해당 없음 |
| `Main/BuildSettingsWindow`·`Build/*` | — | 열림 (빌드 시스템 없음) |
| `Command/EditorCommandManager` | `EditorCommandManager` | 완료 |
| `Command/EditorObjectCommands`·`EditorLayerCommands` | `ObjectCommands`·`HierarchyCommands`·`LayerCommands` | 완료 (D-135·D-142) |
| `Command/EditorCanvasCommands`·`EditorFileCommands` | 캔버스 저장·열기 요청, 에셋 파일 조작 | 완료 (D-139) |
| `EditorSessionPersistence` | `SaveEditorSession`·`.layout.ini` | 완료 (D-146) |
| `EditorSimulationGuard` | `SaveCanvas` 의 재생 중 거절 | 완료 (D-153) |
| `EditorDragDrop` | `Widget/AssetDrag.h` | 완료 (D-154), `.jlayer` 드롭은 해당 없음(레이어 에셋 없음) |
| `EditorContext` | `EditorApplication` | 완료 |
| `Gui/EditorGuiActions` | `EditorActions` | 완료 (D-132), `PasteObjectsAsChild` 는 D-166, 컴포넌트 복사·붙여넣기는 D-167, `spawnWorldPos`·`ResolveTargetLayer` 는 D-168, `DrawAddComponentMenu`(갈래·다중성·`이미 추가됨`)는 D-180 |
| `Gui/EditorMessagePopup` | `MessagePopup` | 완료 |
| `Shortcut/EditorShortcutManager` | `EditorShortcuts` | 완료 (D-132), 표를 항목마다 견주어 빠져 있던 `PasteObjectsAsChild` 를 채웠다 (D-166), 잠긴 까닭(`WhyBlocked`)은 D-181 |
| `Theme/EditorTheme` | `EditorTheme` | 완료 |
| `ImItem/*` (20여 종) | `Widget/*` (+ `Basic.h`·`PathField.h`) · 패널 소스 검사 | 완료 (D-152·D-164), 오디오 위젯·`ImReferenceField` 는 열림 |
| `Localization/EditorReflectionLabels` | `JBro/Editor/EditorNames.h` 의 `DisplayTypeName`·`ComponentCategoryLabel` | 완료 (D-180), 필드 라벨은 해당 없음 (필드 이름으로 보인다, §11.2) |
| `Localization/EditorLocalizationKeys` | `JBro/Editor/LocalizationKeys.h` + `Localization/{ko-KR,en-US}.yaml` | 완료 (D-184 에서 대조표에 채움), 키 수는 658 대 232 인데 차이는 거의 다 없는 기능(오디오·이펙트·빌드·폰트·프리팹·스크립트·머티리얼·애니메이션)의 것이다 |
| `Icons/FontAwesomeIcons` | `JBro/Editor/EditorIcons.h` | 완료 (D-184 에서 대조표에 채움), 기존이 **실제로 쓰는** 글리프는 넷(`X_MARK`·`EYE`·`EYE_SLASH`·`ELLIPSIS_VERTICAL`)이고 앞의 셋은 우리도 쓴다. 마지막 하나는 스크립트 스키마 위젯 전용이라 해당 없음 |
| `Path/EditorPathUtils` | `JBro/Editor/EditorPaths.h` 의 `JoinPath`·`FolderOf`·`LeafOfPath` | 완료 (D-173), 그전까지는 패널마다 따로 있었다 |
| `Script/ScriptSchema` | — | 해당 없음 (JBroScript 미구현) |

`ImPathField` 는 경로 칸에 "찾아보기" 단추를 단다. `Widget::PathField` 로 섰다(D-164) - 프로젝트 설정의 경로 여섯이 쓴다. `ImReferenceField` 는 오브젝트 참조 칸인데, 그런 필드를 가진 컴포넌트가 아직
없다(스크립트가 생기면 필요해진다).
