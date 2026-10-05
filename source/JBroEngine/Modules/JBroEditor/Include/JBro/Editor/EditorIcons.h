#pragma once

namespace JBro::Icons
{
    // 에디터가 쓰는 Material Design Icons 글리프다(D-277). UTF-8 로 적어 두어 글자와 이어 붙일 수 있다.
    // 아이콘 글꼴이 없는 기계에서는 네모로 나온다 - 그때도 에디터는 뜬다.
    //
    // 글리프는 U+F0000 위(보충 사용자 영역)에 있어 UTF-8 로 4 바이트다. ImGui 가 이것을 읽으려면
    // `IMGUI_USE_WCHAR32` 가 켜져 있어야 한다(`imconfig.h`) - 꺼지면 모두 네모가 된다.
    //
    // 이름은 쓰는 뜻으로 짓고(`Grid`·`Play`), 주석에 코드 포인트와 MDI 의 이름(`mdi-...`)·쓰는 자리를 적는다(D-278).
    // 실제로 쓰는 것만 둔다. 쓰지 않는 글리프를 미리 나열하면 어느 것이 화면에 있는지 알 수 없다.
    inline constexpr const char* GripLines    = "\xF3\xB1\x8B\xB0"; // F12F0 drag-horizontal-variant - 목록 행의 손잡이
    inline constexpr const char* Xmark        = "\xF3\xB0\x85\x96"; // F0156 close - 지우기·삭제 표시, 검색칸 지우기
    inline constexpr const char* Eye          = "\xF3\xB0\x88\x88"; // F0208 eye - 레이어·오브젝트 보이기
    inline constexpr const char* EyeSlash     = "\xF3\xB0\x88\x89"; // F0209 eye-off - 감추기
    inline constexpr const char* Search       = "\xF3\xB0\x8D\x89"; // F0349 magnify - 검색칸
    inline constexpr const char* FolderOpen   = "\xF3\xB0\x9D\xB0"; // F0770 folder-open - 경로 칸 찾아보기, 열린 폴더
    inline constexpr const char* Move         = "\xF3\xB0\x86\xBE"; // F01BE cursor-move - 기즈모 이동
    inline constexpr const char* Rotate       = "\xF3\xB0\x91\xA7"; // F0467 rotate-right - 기즈모 회전
    inline constexpr const char* Scale        = "\xF3\xB0\xA9\xA8"; // F0A68 resize - 기즈모 크기
    inline constexpr const char* SpaceLocal   = "\xF3\xB0\xB5\x89"; // F0D49 axis-arrow - 기즈모 로컬 축
    inline constexpr const char* SpaceWorld   = "\xF3\xB0\x87\xA7"; // F01E7 earth - 기즈모 월드 축
    inline constexpr const char* Grid         = "\xF3\xB0\x8B\x81"; // F02C1 grid - 격자
    inline constexpr const char* GridSnap     = "\xF3\xB0\x8D\x87"; // F0347 magnet - 격자 스냅
    inline constexpr const char* Colliders    = "\xF3\xB0\x80\x81"; // F0001 vector-square - 콜라이더 보이기
    inline constexpr const char* Frame        = "\xF3\xB1\xA3\xB5"; // F18F5 fit-to-screen-outline - 선택에 맞추기
    inline constexpr const char* ViewWorld    = "\xF3\xB0\xA6\x82"; // F0982 map-outline - 월드 레이어 보기
    inline constexpr const char* ViewScreen   = "\xF3\xB0\xA8\x87"; // F0A07 monitor-dashboard - 화면(UI) 레이어 보기·UI 태그
    inline constexpr const char* Language     = "\xF3\xB0\x97\x8A"; // F05CA translate - 미리 볼 언어
    inline constexpr const char* Ruler        = "\xF3\xB0\x91\xAD"; // F046D ruler - 눈금 단위
    inline constexpr const char* EditCollider = "\xF3\xB1\x88\xA5"; // F1225 vector-polyline-edit - 콜라이더 편집
    inline constexpr const char* Success      = "\xF3\xB0\x97\xA0"; // F05E0 check-circle - 성공 표시
    inline constexpr const char* Warning      = "\xF3\xB0\x80\xA6"; // F0026 alert - 경고 표시
    inline constexpr const char* Error        = "\xF3\xB0\x85\x99"; // F0159 close-circle - 오류 표시
    inline constexpr const char* Info         = "\xF3\xB0\x8B\xBC"; // F02FC information - 정보 표시
    inline constexpr const char* Plus         = "\xF3\xB0\x90\x95"; // F0415 plus - 추가
    inline constexpr const char* Minus        = "\xF3\xB0\x8D\xB4"; // F0374 minus - 빼기
    inline constexpr const char* Canvas       = "\xF3\xB0\x8B\xB5"; // F02F5 image-filter-hdr - 캔버스
    inline constexpr const char* Layer        = "\xF3\xB0\xA7\xBE"; // F09FE layers-outline - 레이어
    inline constexpr const char* GameObject   = "\xF3\xB0\x86\xA7"; // F01A7 cube-outline - 오브젝트
    inline constexpr const char* Folder       = "\xF3\xB0\x89\x8B"; // F024B folder - 폴더
    inline constexpr const char* FileImage    = "\xF3\xB0\x88\x9F"; // F021F file-image - 텍스처·스프라이트 에셋
    inline constexpr const char* FileAudio    = "\xF3\xB0\x9D\x9A"; // F075A music - 오디오 에셋
    inline constexpr const char* FileFont     = "\xF3\xB0\x9B\x96"; // F06D6 format-font - 폰트 에셋
    inline constexpr const char* File         = "\xF3\xB0\x88\xA4"; // F0224 file-outline - 그 밖의 에셋
    inline constexpr const char* ViewList     = "\xF3\xB0\x95\xB2"; // F0572 view-list - 목록 보기
    inline constexpr const char* ViewGrid     = "\xF3\xB0\x95\xB0"; // F0570 view-grid - 아이콘 보기
    inline constexpr const char* Home         = "\xF3\xB0\x9A\xA1"; // F06A1 home-outline - 에셋 뿌리
    inline constexpr const char* FolderPlus   = "\xF3\xB0\xAE\x9D"; // F0B9D folder-plus-outline - 새 폴더
    inline constexpr const char* Refresh      = "\xF3\xB0\x91\x90"; // F0450 refresh - 다시 스캔
    inline constexpr const char* Delete       = "\xF3\xB0\xA7\xA7"; // F09E7 delete-outline - 삭제·제거
    inline constexpr const char* OpenExternal = "\xF3\xB0\x8F\x8C"; // F03CC open-in-new - 밖에서 열기
    inline constexpr const char* Menu         = "\xF3\xB0\x87\x99"; // F01D9 dots-vertical - 컴포넌트 메뉴
    inline constexpr const char* ArrowUp      = "\xF3\xB0\x81\x9D"; // F005D arrow-up - 위로
    inline constexpr const char* ArrowDown    = "\xF3\xB0\x81\x85"; // F0045 arrow-down - 아래로
    inline constexpr const char* Copy         = "\xF3\xB0\x86\x8F"; // F018F content-copy - 복사
    inline constexpr const char* Play         = "\xF3\xB0\x90\x8A"; // F040A play - 재생
    inline constexpr const char* Stop         = "\xF3\xB0\x93\x9B"; // F04DB stop - 정지
    inline constexpr const char* Pause        = "\xF3\xB0\x8F\xA4"; // F03E4 pause - 일시정지
    inline constexpr const char* StepFrame    = "\xF3\xB0\x93\x97"; // F04D7 step-forward - 한 프레임
    inline constexpr const char* ClearLog     = "\xF3\xB0\x97\xA9"; // F05E9 delete-sweep - 로그 지우기
    inline constexpr const char* AutoScroll   = "\xF3\xB0\x9E\x92"; // F0792 arrow-collapse-down - 자동 스크롤
    inline constexpr const char* Debug        = "\xF3\xB0\xA8\xB0"; // F0A30 bug-outline - 추적·디버그
    inline constexpr const char* NoCamera     = "\xF3\xB0\xAF\x9B"; // F0BDB video-off-outline - 카메라 없음
    inline constexpr const char* Save         = "\xF3\xB0\x86\x93"; // F0193 content-save - 저장
    inline constexpr const char* Question     = "\xF3\xB0\x98\xA5"; // F0625 help-circle-outline - 확인 팝업
    inline constexpr const char* Solo         = "\xF3\xB0\x8B\x8B"; // F02CB headphones - 혼자 듣기

    // 위의 글리프 전부다. 시험이 이것을 돌며 모두 글꼴 안에 있는지 본다 - 글리프를 더하면 여기에도 더한다.
    inline constexpr const char* All[] = {
        GripLines, Xmark, Eye, EyeSlash, Search, FolderOpen, Move, Rotate, Scale, SpaceLocal, SpaceWorld, Grid, GridSnap,
        Colliders, Frame, ViewWorld, ViewScreen, Language, Ruler, EditCollider, Success, Warning, Error, Info, Plus,
        Minus, Canvas, Layer, GameObject, Folder, FileImage, FileAudio, FileFont, File, ViewList, ViewGrid,
        Home, FolderPlus, Refresh, Delete, OpenExternal, Menu, ArrowUp, ArrowDown, Copy, Play, Stop, Pause, StepFrame,
        ClearLog, AutoScroll, Debug, NoCamera, Save, Question, Solo
    };

    // 아이콘 글꼴이 덮는 유니코드 범위다. 글꼴을 합칠 때 이 범위만 아이콘 글꼴에서 가져온다.
    // MDI 7.4.47 의 글리프는 F0001..F1D17 에 있다(자리 표시 `blank` 하나만 F68C).
    inline constexpr unsigned int RangeBegin = 0xF0000;
    inline constexpr unsigned int RangeEnd = 0xF1FFF;
}
