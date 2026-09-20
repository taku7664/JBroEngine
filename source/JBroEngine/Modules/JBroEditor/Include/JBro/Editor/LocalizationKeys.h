#pragma once

// 화면에 나오는 글자의 키다(ProjectRule §11.2).
//
// **한곳에 모으는 이유**: 키를 부르는 자리마다 문자열로 적으면 오타가 조용히
// 폴백으로 떨어지고, 로케일 파일과 맞는지 훑어볼 방법도 없다. 여기 있는 것이
// 곧 번역해야 할 목록이다.
//
// 이름은 `점.으로.구분한.소문자` 다. 앞부분이 화면(패널) 이름이다.
namespace JBro::LocKeys
{
    // ── 패널 제목 ────────────────────────────────────────────────────────
    inline constexpr const char* PanelGame = "panel.game";
    inline constexpr const char* PanelHierarchy = "panel.hierarchy";
    inline constexpr const char* PanelInspector = "panel.inspector";
    inline constexpr const char* PanelStats = "panel.stats";
    inline constexpr const char* PanelAssets = "panel.assets";

    // ── 게임 뷰의 기즈모 (D-109) ─────────────────────────────────────────
    inline constexpr const char* GizmoTranslate = "gizmo.translate";
    inline constexpr const char* GizmoRotate = "gizmo.rotate";
    inline constexpr const char* GizmoScale = "gizmo.scale";

    // ── 메뉴 ─────────────────────────────────────────────────────────────
    inline constexpr const char* MenuFile = "menu.file";
    inline constexpr const char* MenuEdit = "menu.edit";
    inline constexpr const char* MenuWindow = "menu.window";
    inline constexpr const char* MenuSaveCanvas = "menu.save_canvas";
    inline constexpr const char* MenuExit = "menu.exit";
    inline constexpr const char* MenuUndo = "menu.undo";
    inline constexpr const char* MenuRedo = "menu.redo";
    inline constexpr const char* MenuUnsaved = "menu.unsaved";

    // ── 대화상자·팝업 ────────────────────────────────────────────────────
    inline constexpr const char* DialogSaveCanvasTitle = "dialog.save_canvas_title";
    inline constexpr const char* DialogCanvasFilter = "dialog.canvas_filter";
    inline constexpr const char* PopupSaveFailed = "popup.save_failed";

    // ── 계층 ─────────────────────────────────────────────────────────────
    inline constexpr const char* HierarchyNoProject = "hierarchy.no_project";
    inline constexpr const char* HierarchyEmpty = "hierarchy.empty";
    inline constexpr const char* HierarchyCreateObject = "hierarchy.create_object";
    inline constexpr const char* HierarchyCreateChild = "hierarchy.create_child";
    inline constexpr const char* HierarchyDelete = "hierarchy.delete";
    inline constexpr const char* HierarchyCopy = "hierarchy.copy";
    inline constexpr const char* HierarchyPaste = "hierarchy.paste";
    inline constexpr const char* HierarchyUnnamed = "hierarchy.unnamed";
    inline constexpr const char* HierarchySearch = "hierarchy.search";

    // ── 인스펙터 ─────────────────────────────────────────────────────────
    inline constexpr const char* InspectorNothingSelected = "inspector.nothing_selected";
    inline constexpr const char* InspectorActive = "inspector.active";
    inline constexpr const char* InspectorEnabled = "inspector.enabled";
    inline constexpr const char* InspectorAddComponent = "inspector.add_component";
    inline constexpr const char* InspectorMoveComponentUp = "inspector.move_up";
    inline constexpr const char* InspectorMoveComponentDown = "inspector.move_down";
    inline constexpr const char* InspectorRemoveComponent = "inspector.remove_component";
    inline constexpr const char* InspectorNoComponentTypes = "inspector.no_component_types";
    inline constexpr const char* InspectorUnregisteredType = "inspector.unregistered_type";
    inline constexpr const char* InspectorUnknownComponent = "inspector.unknown_component";
    inline constexpr const char* InspectorUndrawableType = "inspector.undrawable_type";
    inline constexpr const char* InspectorTooDeep = "inspector.too_deep";
    inline constexpr const char* InspectorTooLong = "inspector.too_long";
    inline constexpr const char* InspectorMultipleSelected = "inspector.multiple_selected";

    // ── 통계 ─────────────────────────────────────────────────────────────
    // 값이 printf 형식이다. **번역도 같은 지정자를 같은 차례로 담아야 한다** -
    // 인자는 코드가 넘기므로, 어긋나면 틀린 크기로 읽는다.
    inline constexpr const char* StatsFrameTime = "stats.frame_time";
    inline constexpr const char* StatsPerSecond = "stats.per_second";
    inline constexpr const char* StatsFrameCount = "stats.frame_count";
    inline constexpr const char* StatsViews = "stats.views";
    inline constexpr const char* StatsSprites = "stats.sprites";
    inline constexpr const char* StatsDropped = "stats.dropped";

    // ── 목록 위젯 ────────────────────────────────────────────────────────
    inline constexpr const char* ListAddElement = "list.add_element";
    inline constexpr const char* ListRemoveElement = "list.remove_element";
    inline constexpr const char* ListElementCount = "list.element_count";

    // ── 공용 ─────────────────────────────────────────────────────────────
    inline constexpr const char* CommonSearch = "common.search";
    inline constexpr const char* CommonClear = "common.clear";
    inline constexpr const char* CommonNoMatches = "common.no_matches";

    // ── 에셋 칸 ──────────────────────────────────────────────────────────
    inline constexpr const char* AssetNone = "asset.none";
    inline constexpr const char* AssetMissing = "asset.missing";
    inline constexpr const char* AssetsEmpty = "assets.empty";
    inline constexpr const char* AssetsNotWatching = "assets.not_watching";
    inline constexpr const char* InspectorTextureImportOptions = "inspector.texture_import_options";
    inline constexpr const char* InspectorSpriteImportOptions = "inspector.sprite_import_options";
    inline constexpr const char* CommonOk = "common.ok";
}
