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
    inline constexpr const char* PanelCanvasView = "panel.canvas_view";
    inline constexpr const char* PanelHierarchy = "panel.hierarchy";
    inline constexpr const char* PanelInspector = "panel.inspector";
    inline constexpr const char* PanelStats = "panel.stats";
    inline constexpr const char* PanelShortcuts = "panel.shortcuts";
    inline constexpr const char* PanelLog = "panel.log";
    inline constexpr const char* PanelProjectSettings = "panel.project_settings";
    inline constexpr const char* PanelProfiler = "panel.profiler";
    inline constexpr const char* PanelAssets = "panel.assets";

    // ── 게임 뷰의 기즈모 (D-109) ─────────────────────────────────────────
    inline constexpr const char* GizmoTranslate = "gizmo.translate";
    inline constexpr const char* GizmoRotate = "gizmo.rotate";
    inline constexpr const char* GizmoScale = "gizmo.scale";

    // ── 캔버스 뷰 (D-130) ────────────────────────────────────────────────
    inline constexpr const char* CanvasViewGrid = "canvas_view.grid";
    inline constexpr const char* CanvasViewFrame = "canvas_view.frame";
    inline constexpr const char* CanvasViewGridTooltip = "canvas_view.grid_tooltip";
    inline constexpr const char* CanvasViewColliders = "canvas_view.colliders";
    inline constexpr const char* CanvasViewInsideFormat = "canvas_view.inside_format";
    inline constexpr const char* CanvasViewCollidersTooltip = "canvas_view.colliders_tooltip";
    inline constexpr const char* CanvasViewFrameTooltip = "canvas_view.frame_tooltip";

    // ── 게임 뷰의 상태 표시 (D-131) ──────────────────────────────────────
    inline constexpr const char* GameViewPlaying = "game_view.playing";
    inline constexpr const char* GameViewStopped = "game_view.stopped";
    inline constexpr const char* GameViewNoCanvas = "game_view.no_canvas";
    inline constexpr const char* GameViewNoCamera = "game_view.no_camera";

    // ── 메뉴 ─────────────────────────────────────────────────────────────
    inline constexpr const char* MenuFile = "menu.file";
    inline constexpr const char* MenuEdit = "menu.edit";
    inline constexpr const char* MenuWindow = "menu.window";
    inline constexpr const char* MenuSimulation = "menu.simulation";
    inline constexpr const char* MenuSimulationPlay = "menu.simulation_play";
    inline constexpr const char* MenuSimulationStop = "menu.simulation_stop";
    inline constexpr const char* MenuSimulationPause = "menu.simulation_pause";
    inline constexpr const char* MenuOpenProject = "menu.open_project";
    inline constexpr const char* MenuWindowEditor = "menu.window_editor";
    inline constexpr const char* MenuWindowImporter = "menu.window_importer";
    inline constexpr const char* MenuSaveCanvas = "menu.save_canvas";
    inline constexpr const char* MenuSaveProject = "menu.save_project";
    inline constexpr const char* DockMain = "dock.main";
    inline constexpr const char* PopupSaveBlockedWhilePlaying = "popup.save_blocked_while_playing";
    inline constexpr const char* MenuSettings = "menu.settings";
    inline constexpr const char* MenuSettingsProject = "menu.settings_project";
    inline constexpr const char* MenuDebug = "menu.debug";
    inline constexpr const char* MenuDebugCpuProfiler = "menu.debug_cpu_profiler";
    inline constexpr const char* MenuDebugStats = "menu.debug_stats";
    inline constexpr const char* MenuDebugLog = "menu.debug_log";
    inline constexpr const char* MenuExit = "menu.exit";
    inline constexpr const char* MenuUndo = "menu.undo";
    inline constexpr const char* MenuRedo = "menu.redo";
    inline constexpr const char* MenuUnsaved = "menu.unsaved";

    // ── 대화상자·팝업 ────────────────────────────────────────────────────
    inline constexpr const char* DialogSaveCanvasTitle = "dialog.save_canvas_title";
    inline constexpr const char* DialogCanvasFilter = "dialog.canvas_filter";
    inline constexpr const char* PopupSaveFailed = "popup.save_failed";
    inline constexpr const char* PopupOpenProjectFailed = "popup.open_project_failed";
    inline constexpr const char* DialogOpenProjectTitle = "dialog.open_project_title";
    inline constexpr const char* DialogProjectFilter = "dialog.project_filter";
    // 새 프로젝트(D-160, 기존 루트 도크의 `MenuFileNewProject`)
    inline constexpr const char* MenuNewProject = "menu.new_project";
    inline constexpr const char* DialogNewProjectFolder = "dialog.new_project_folder";
    inline constexpr const char* NewProjectLocation = "new_project.location";
    inline constexpr const char* NewProjectName = "new_project.name";
    inline constexpr const char* NewProjectFramework = "new_project.framework";
    inline constexpr const char* NewProjectNameHint = "new_project.name_hint";
    inline constexpr const char* CommonCreate = "common.create";
    inline constexpr const char* PopupNewProjectFailed = "popup.new_project_failed";
    inline constexpr const char* NewProjectInvalidName = "new_project.invalid_name";
    inline constexpr const char* NewProjectAlreadyExists = "new_project.already_exists";
    inline constexpr const char* NewProjectCannotWrite = "new_project.cannot_write";

    // ── 계층 ─────────────────────────────────────────────────────────────
    inline constexpr const char* HierarchyNoProject = "hierarchy.no_project";
    inline constexpr const char* HierarchyEmpty = "hierarchy.empty";
    inline constexpr const char* HierarchyCreateObject = "hierarchy.create_object";
    inline constexpr const char* HierarchyObjectHidden = "hierarchy.object_hidden";
    inline constexpr const char* HierarchyCreateChild = "hierarchy.create_child";
    inline constexpr const char* HierarchyDelete = "hierarchy.delete";
    inline constexpr const char* HierarchyCopy = "hierarchy.copy";
    inline constexpr const char* HierarchyPaste = "hierarchy.paste";
    inline constexpr const char* HierarchyPasteAsChild = "hierarchy.paste_as_child";
    inline constexpr const char* HierarchyUnparent = "hierarchy.unparent";
    inline constexpr const char* HierarchyUnnamed = "hierarchy.unnamed";
    inline constexpr const char* HierarchySearch = "hierarchy.search";
    inline constexpr const char* HierarchyAddLayer = "hierarchy.add_layer";
    inline constexpr const char* HierarchyDeleteLayer = "hierarchy.delete_layer";
    inline constexpr const char* HierarchyLayerName = "hierarchy.layer_name";
    inline constexpr const char* HierarchyLayerEmpty = "hierarchy.layer_empty";
    inline constexpr const char* HierarchyLayerVisible = "hierarchy.layer_visible";

    // ── 인스펙터 ─────────────────────────────────────────────────────────
    inline constexpr const char* InspectorNothingSelected = "inspector.nothing_selected";
    inline constexpr const char* InspectorActive = "inspector.active";
    inline constexpr const char* InspectorName = "inspector.name";
    inline constexpr const char* InspectorEnabled = "inspector.enabled";
    inline constexpr const char* InspectorAddComponent = "inspector.add_component";
    inline constexpr const char* InspectorMoveComponentUp = "inspector.move_up";
    inline constexpr const char* InspectorMoveComponentDown = "inspector.move_down";
    inline constexpr const char* InspectorCopyComponent = "inspector.copy_component";
    inline constexpr const char* InspectorPasteComponent = "inspector.paste_component";
    inline constexpr const char* InspectorPasteComponentValues = "inspector.paste_component_values";
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
    inline constexpr const char* StatsObjects = "stats.objects";
    inline constexpr const char* StatsLayers = "stats.layers";
    inline constexpr const char* StatsSelected = "stats.selected";
    inline constexpr const char* StatsPools = "stats.pools";
    inline constexpr const char* StatsNoPools = "stats.no_pools";
    inline constexpr const char* StatsUndo = "stats.undo";
    inline constexpr const char* StatsDirty = "stats.dirty";
    inline constexpr const char* StatsClean = "stats.clean";

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
    inline constexpr const char* AssetsRoot = "assets.root";
    inline constexpr const char* AssetsFolderEmpty = "assets.folder_empty";
    inline constexpr const char* AssetsNewFolder = "assets.new_folder";
    inline constexpr const char* AssetsNewFolderFailed = "assets.new_folder_failed";
    inline constexpr const char* AssetsRename = "assets.rename";
    inline constexpr const char* AssetsRenameFailed = "assets.rename_failed";
    inline constexpr const char* AssetsDelete = "assets.delete";
    inline constexpr const char* AssetsDeleteAsk = "assets.delete_ask";
    inline constexpr const char* AssetsDeleteFolderAsk = "assets.delete_folder_ask";
    inline constexpr const char* AssetsDeleteManyAsk = "assets.delete_many_ask";
    inline constexpr const char* AssetsDeleteFailed = "assets.delete_failed";
    inline constexpr const char* AssetsMoveFailed = "assets.move_failed";
    inline constexpr const char* AssetsReveal = "assets.reveal";
    inline constexpr const char* AssetsRescan = "assets.rescan";
    inline constexpr const char* AssetsOpenInSpriteViewer = "assets.open_in_sprite_viewer";
    inline constexpr const char* AssetsImport = "assets.import";
    inline constexpr const char* MenuImportSprite = "menu.import_sprite";
    inline constexpr const char* DialogImportTitle = "dialog.import_title";
    inline constexpr const char* DialogImportImages = "dialog.import_images";
    inline constexpr const char* PopupImportFailed = "popup.import_failed";
    inline constexpr const char* SpriteViewerTitle = "sprite_viewer.title";
    inline constexpr const char* SpriteViewerLoading = "sprite_viewer.loading";
    inline constexpr const char* SpriteViewerFrame = "sprite_viewer.frame";
    inline constexpr const char* SpriteViewerFps = "sprite_viewer.fps";
    inline constexpr const char* SpriteViewerPlay = "sprite_viewer.play";
    inline constexpr const char* SpriteViewerStop = "sprite_viewer.stop";
    inline constexpr const char* SpriteViewerSelectToEdit = "sprite_viewer.select_to_edit";
    inline constexpr const char* SpriteViewerSelect = "sprite_viewer.select";
    // 프레임 고르기(D-165, 기존 `InspectorSpritePickFrame`·`...NoSheet`)
    inline constexpr const char* SpriteViewerPickHint = "sprite_viewer.pick_hint";
    inline constexpr const char* InspectorPickFrame = "inspector.pick_frame";
    inline constexpr const char* InspectorPickFrameNoSprite = "inspector.pick_frame_no_sprite";
    inline constexpr const char* AssetsIconView = "assets.icon_view";
    inline constexpr const char* AssetsListView = "assets.list_view";
    inline constexpr const char* AssetsViewTooltip = "assets.view_tooltip";
    inline constexpr const char* CommonCancel = "common.cancel";
    // 경로 칸의 단추(D-164, 기존 `CommonBrowse`)
    inline constexpr const char* CommonBrowse = "common.browse";
    inline constexpr const char* DialogBrowseFolder = "dialog.browse_folder";
    inline constexpr const char* DialogBrowseFile = "dialog.browse_file";
    inline constexpr const char* InspectorTextureImportOptions = "inspector.texture_import_options";
    inline constexpr const char* InspectorSpriteImportOptions = "inspector.sprite_import_options";
    inline constexpr const char* CommonOk = "common.ok";

    // ── 프로파일러 (D-138) ───────────────────────────────────────────────
    inline constexpr const char* ProfilerHint = "profiler.hint";
    inline constexpr const char* ProfilerEmpty = "profiler.empty";
    inline constexpr const char* ProfilerSection = "profiler.section";
    inline constexpr const char* ProfilerTime = "profiler.time";
    inline constexpr const char* ProfilerCalls = "profiler.calls";
    inline constexpr const char* ProfilerShare = "profiler.share";
    inline constexpr const char* ProfilerFrame = "profiler.frame";

    // ── 프로젝트 설정 (D-137) ────────────────────────────────────────────
    inline constexpr const char* ProjectSettingsNoFile = "project_settings.no_file";
    inline constexpr const char* ProjectSettingsGeneral = "project_settings.general";
    inline constexpr const char* ProjectSettingsPaths = "project_settings.paths";
    inline constexpr const char* ProjectSettingsLanguage = "project_settings.language";
    inline constexpr const char* ProjectSettingsNoLanguages = "project_settings.no_languages";
    inline constexpr const char* ProjectSettingsBuild = "project_settings.build";
    inline constexpr const char* ProjectSettingsSave = "project_settings.save";
    inline constexpr const char* ProjectSettingsRevert = "project_settings.revert";
    inline constexpr const char* ProjectSettingsSaved = "project_settings.saved";
    inline constexpr const char* ProjectSettingsLinearFilter = "project_settings.linear_filter";

    // ── 로그 (D-133) ─────────────────────────────────────────────────────
    inline constexpr const char* LogClear = "log.clear";
    inline constexpr const char* LogAutoScroll = "log.auto_scroll";
    inline constexpr const char* LogEmpty = "log.empty";
    inline constexpr const char* LogTrace = "log.trace";
    inline constexpr const char* LogDebug = "log.debug";
    inline constexpr const char* LogInfo = "log.info";
    inline constexpr const char* LogWarning = "log.warning";
    inline constexpr const char* LogError = "log.error";
}
