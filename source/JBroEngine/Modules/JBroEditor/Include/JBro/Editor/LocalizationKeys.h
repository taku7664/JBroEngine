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
    inline constexpr const char* PanelSimulation = "panel.simulation";
    inline constexpr const char* PanelCanvasView = "panel.canvas_view";
    inline constexpr const char* PanelHierarchy = "panel.hierarchy";
    inline constexpr const char* PanelInspector = "panel.inspector";
    inline constexpr const char* PanelStats = "panel.stats";
    inline constexpr const char* PanelShortcuts = "panel.shortcuts";
    inline constexpr const char* PanelLog = "panel.log";
    inline constexpr const char* PanelProjectSettings = "panel.project_settings";
    inline constexpr const char* PanelProfiler = "panel.profiler";
    inline constexpr const char* PanelAssets = "panel.assets";

    // ── 시뮬레이션 뷰의 기즈모 (D-109) ─────────────────────────────────────────
    inline constexpr const char* GizmoTranslate = "gizmo.translate";
    inline constexpr const char* GizmoRotate = "gizmo.rotate";
    inline constexpr const char* GizmoScale = "gizmo.scale";
    inline constexpr const char* GizmoSpaceLocal = "gizmo.space_local";
    inline constexpr const char* GizmoSpaceWorld = "gizmo.space_world";
    inline constexpr const char* GizmoSpaceTooltip = "gizmo.space_tooltip";

    // ── 캔버스 뷰 (D-130) ────────────────────────────────────────────────
    inline constexpr const char* CanvasViewGrid = "canvas_view.grid";
    inline constexpr const char* CanvasViewFrame = "canvas_view.frame";
    inline constexpr const char* CanvasViewGridTooltip = "canvas_view.grid_tooltip";
    inline constexpr const char* CanvasViewGridSnap = "canvas_view.grid_snap";
    inline constexpr const char* CanvasViewGridSnapTooltip = "canvas_view.grid_snap_tooltip";
    inline constexpr const char* CanvasViewColliders = "canvas_view.colliders";
    inline constexpr const char* CanvasViewInsideFormat = "canvas_view.inside_format";
    inline constexpr const char* CanvasViewSelectedNone = "canvas_view.selected_none";
    inline constexpr const char* CanvasViewSelectedFormat = "canvas_view.selected_format";
    inline constexpr const char* CanvasViewSelectedCountFormat = "canvas_view.selected_count_format";
    inline constexpr const char* CanvasViewCameraFormat = "canvas_view.camera_format";
    inline constexpr const char* CanvasViewCamera3DFormat = "canvas_view.camera_3d_format";
    inline constexpr const char* CanvasViewCollidersTooltip = "canvas_view.colliders_tooltip";
    // 캔버스 뷰를 2D 라이트로 그릴지(D-291).
    inline constexpr const char* CanvasViewLighting = "canvas_view.lighting";
    inline constexpr const char* CanvasViewLightingTooltip = "canvas_view.lighting_tooltip";
    // 폴리곤 콜라이더의 포인트 편집(physics-plan §4 의 5). 필드 이름(`points`)과 같은 말을 쓴다 - 기존 엔진의 `버텍스` 는 따르지 않았다.
    inline constexpr const char* CanvasViewEditCollider = "canvas_view.edit_collider";
    inline constexpr const char* CanvasViewEditColliderTooltip = "canvas_view.edit_collider_tooltip";
    inline constexpr const char* CanvasViewPointDelete = "canvas_view.point_delete";
    inline constexpr const char* CanvasViewPointDeleteMin = "canvas_view.point_delete_min";
    inline constexpr const char* CanvasViewPointDeleteMinChain = "canvas_view.point_delete_min_chain";
    inline constexpr const char* CanvasViewEditPoints = "canvas_view.edit_points";
    inline constexpr const char* CanvasViewEditPointsNotPolygon = "canvas_view.edit_points_not_polygon";
    inline constexpr const char* CanvasViewPreviewLocale = "canvas_view.preview_locale";
    inline constexpr const char* CanvasViewSpaceWorld = "canvas_view.space_world";
    inline constexpr const char* CanvasViewSpaceUi = "canvas_view.space_ui";
    inline constexpr const char* CanvasViewSpaceTooltip = "canvas_view.space_tooltip";
    inline constexpr const char* CanvasViewReferenceRect = "canvas_view.reference_rect";
    inline constexpr const char* CanvasViewViewportSelectedCamera = "canvas_view.viewport_selected_camera";
    inline constexpr const char* CanvasViewPreviewLocaleTooltip = "canvas_view.preview_locale_tooltip";
    // 눈금을 월드 유닛으로 읽을지 픽셀로 읽을지(D-184, 기존 `canvas_view.unit.*`).
    inline constexpr const char* CanvasViewUnitWorld = "canvas_view.unit_world";
    inline constexpr const char* CanvasViewUnitPixel = "canvas_view.unit_pixel";
    inline constexpr const char* CanvasViewUnitTooltip = "canvas_view.unit_tooltip";

    // ── 시뮬레이션 뷰의 상태 표시 (D-131) ──────────────────────────────────────
    inline constexpr const char* SimulationViewPlaying = "simulation_view.playing";
    inline constexpr const char* SimulationViewStopped = "simulation_view.stopped";
    inline constexpr const char* SimulationViewNoCanvas = "simulation_view.no_canvas";
    inline constexpr const char* SimulationViewNoCamera = "simulation_view.no_camera";
    inline constexpr const char* SimulationViewCameraUnusable = "simulation_view.camera_unusable";
    // `primary` 가 없어 첫 활성 카메라로 그리는데 그런 카메라가 여럿일 때(D-187).
    inline constexpr const char* SimulationViewCameraAmbiguous = "simulation_view.camera_ambiguous";

    // ── 메뉴 ─────────────────────────────────────────────────────────────
    inline constexpr const char* MenuFile = "menu.file";
    inline constexpr const char* MenuEdit = "menu.edit";
    inline constexpr const char* MenuView = "menu.view";
    inline constexpr const char* MenuWindow = "menu.window";
    inline constexpr const char* MenuSimulation = "menu.simulation";
    inline constexpr const char* MenuSimulationPlay = "menu.simulation_play";
    inline constexpr const char* MenuSimulationStop = "menu.simulation_stop";
    inline constexpr const char* MenuSimulationPause = "menu.simulation_pause";
    inline constexpr const char* MenuSimulationStep = "menu.simulation_step";
    inline constexpr const char* MenuSimulationViewDebugDraw = "menu.simulation_view_debug_draw";
    inline constexpr const char* MenuSimulationCanvasDebugDraw = "menu.simulation_canvas_debug_draw";
    inline constexpr const char* MenuOpenProject = "menu.open_project";
    inline constexpr const char* MenuWindowEditor = "menu.window_editor";
    inline constexpr const char* MenuWindowImporter = "menu.window_importer";
    inline constexpr const char* MenuSaveCanvas = "menu.save_canvas";
    inline constexpr const char* MenuSaveProject = "menu.save_project";
    inline constexpr const char* MenuBuildGame = "menu.build_game";
    inline constexpr const char* MenuBuildScripts = "menu.build_scripts";
    inline constexpr const char* PanelBuildResults = "panel.build_results";
    inline constexpr const char* BuildResultsBuild = "build_results.build";
    inline constexpr const char* BuildResultsBuilding = "build_results.building";
    inline constexpr const char* BuildResultsClear = "build_results.clear";
    inline constexpr const char* BuildResultsErrors = "build_results.errors";
    inline constexpr const char* BuildResultsWarnings = "build_results.warnings";
    inline constexpr const char* BuildResultsEmpty = "build_results.empty";
    inline constexpr const char* BuildResultsSucceeded = "build_results.succeeded";
    inline constexpr const char* BuildResultsOpenHint = "build_results.open_hint";
    inline constexpr const char* NotifyScriptsBuilt = "notify.scripts_built";
    inline constexpr const char* NotifyScriptsBuildFailed = "notify.scripts_build_failed";
    inline constexpr const char* NotifyNoScriptProject = "notify.no_script_project";
    inline constexpr const char* NotifyBuildToolsMissing = "notify.build_tools_missing";
    inline constexpr const char* NotifyWindowsSdkMissing = "notify.windows_sdk_missing";
    inline constexpr const char* NotifyScriptCreated = "notify.script_created";
    inline constexpr const char* NotifyScriptsBuiltNext = "notify.scripts_built_next";
    inline constexpr const char* BuildResultsRunning = "build_results.running";
    inline constexpr const char* NotifyScriptsUnresolved = "notify.scripts_unresolved";
    inline constexpr const char* NotifyScriptsUnresolvedWhy = "notify.scripts_unresolved_why";
    inline constexpr const char* NotifyScriptCreatedNext = "notify.script_created_next";
    inline constexpr const char* NotifyScriptFaultStopped = "notify.script_fault_stopped";
    inline constexpr const char* NotifyScriptFault = "notify.script_fault";
    inline constexpr const char* NotifyScriptFaultAt = "notify.script_fault_at";
    inline constexpr const char* NotifyScriptFaultInHook = "notify.script_fault_in_hook";
    inline constexpr const char* NotifyScriptError = "notify.script_error";
    inline constexpr const char* NotifyScriptErrorAt = "notify.script_error_at";
    inline constexpr const char* ScriptFaultAccessViolation = "script_fault.access_violation";
    inline constexpr const char* ScriptFaultDivideByZero = "script_fault.divide_by_zero";
    inline constexpr const char* ScriptFaultStackOverflow = "script_fault.stack_overflow";
    inline constexpr const char* ScriptFaultCppException = "script_fault.cpp_exception";
    inline constexpr const char* ScriptFaultBreakpoint = "script_fault.breakpoint";
    inline constexpr const char* ScriptFaultOther = "script_fault.other";
    inline constexpr const char* EditorSettingsScriptEditor = "editor_settings.script_editor";
    inline constexpr const char* EditorSettingsScriptEditorHelp = "editor_settings.script_editor_help";
    inline constexpr const char* ScriptEditorSystem = "script_editor.system";
    inline constexpr const char* NotifyGameBuilt = "notify.game_built";
    inline constexpr const char* NotifyGameBuildFailed = "notify.game_build_failed";
    inline constexpr const char* NotifyGameBuildWarnings = "notify.game_build_warnings";
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
    inline constexpr const char* HierarchyCreateObject = "hierarchy.create_object";
    inline constexpr const char* HierarchyObjectHidden = "hierarchy.object_hidden";
    inline constexpr const char* HierarchyCreateChild = "hierarchy.create_child";
    inline constexpr const char* HierarchyDelete = "hierarchy.delete";
    inline constexpr const char* HierarchyCopy = "hierarchy.copy";
    inline constexpr const char* HierarchyPaste = "hierarchy.paste";
    inline constexpr const char* HierarchyPasteAsChild = "hierarchy.paste_as_child";
    // 오브젝트 메뉴의 `순서` 하위 메뉴(D-296). 같은 레이어·같은 renderOrder 끼리의 그리는 차례다.
    inline constexpr const char* HierarchyBringForward = "hierarchy.bring_forward";
    inline constexpr const char* HierarchyBringToFront = "hierarchy.bring_to_front";
    inline constexpr const char* HierarchySendBackward = "hierarchy.send_backward";
    inline constexpr const char* HierarchySendToBack = "hierarchy.send_to_back";
    inline constexpr const char* HierarchyUnparent = "hierarchy.unparent";
    inline constexpr const char* HierarchyUnnamed = "hierarchy.unnamed";
    inline constexpr const char* HierarchySearch = "hierarchy.search";
    inline constexpr const char* HierarchyAddLayer = "hierarchy.add_layer";
    inline constexpr const char* HierarchyLayerScreen = "hierarchy.layer_screen";
    inline constexpr const char* HierarchyLayerToWorld = "hierarchy.layer_to_world";
    inline constexpr const char* HierarchyLayerScreenTooltip = "hierarchy.layer_screen_tooltip";
    inline constexpr const char* HierarchyLayerScaleMode = "hierarchy.layer_scale_mode";
    inline constexpr const char* HierarchyLayerScreenTag = "hierarchy.layer_screen_tag";
    inline constexpr const char* HierarchyDeleteLayer = "hierarchy.delete_layer";
    inline constexpr const char* HierarchyLayerName = "hierarchy.layer_name";
    // 계층 맨 위의 캔버스 줄(D-186).
    inline constexpr const char* HierarchyCanvasUnsaved = "hierarchy.canvas_unsaved";
    inline constexpr const char* InspectorCanvasProperties = "inspector.canvas_properties";
    inline constexpr const char* InspectorCanvasBackground = "inspector.canvas_background";
    // 레이어를 골랐을 때의 인스펙터(D-279). 이름 칸은 `HierarchyLayerName`, 맞춤 방식은 `HierarchyLayerScaleMode` 를 같이 쓴다.
    inline constexpr const char* InspectorLayerProperties = "inspector.layer_properties";
    inline constexpr const char* InspectorLayerVisible = "inspector.layer_visible";
    inline constexpr const char* InspectorLayerBlend = "inspector.layer_blend";
    inline constexpr const char* InspectorLayerBlendTooltip = "inspector.layer_blend_tooltip";
    inline constexpr const char* InspectorLayerBlendNormal = "inspector.layer_blend_normal";
    inline constexpr const char* InspectorLayerBlendAdditive = "inspector.layer_blend_additive";
    inline constexpr const char* InspectorLayerBlendMultiply = "inspector.layer_blend_multiply";
    inline constexpr const char* InspectorLayerBlendScreen = "inspector.layer_blend_screen";
    inline constexpr const char* InspectorLayerBlendSubtract = "inspector.layer_blend_subtract";
    inline constexpr const char* InspectorLayerBlendLighten = "inspector.layer_blend_lighten";
    inline constexpr const char* InspectorLayerBlendDarken = "inspector.layer_blend_darken";
    inline constexpr const char* InspectorLayerBlendOverlay = "inspector.layer_blend_overlay";
    inline constexpr const char* InspectorLayerBlendSoftLight = "inspector.layer_blend_soft_light";
    inline constexpr const char* InspectorLayerBlendHardLight = "inspector.layer_blend_hard_light";
    inline constexpr const char* InspectorLayerBlendColorDodge = "inspector.layer_blend_color_dodge";
    inline constexpr const char* InspectorLayerBlendColorBurn = "inspector.layer_blend_color_burn";
    inline constexpr const char* InspectorLayerBlendDifference = "inspector.layer_blend_difference";
    inline constexpr const char* InspectorLayerOpacity = "inspector.layer_opacity";
    inline constexpr const char* InspectorLayerSourceAsset = "inspector.layer_source_asset";
    inline constexpr const char* InspectorLayerSourceMissing = "inspector.layer_source_missing";
    inline constexpr const char* InspectorLayerSourceReveal = "inspector.layer_source_reveal";
    inline constexpr const char* HierarchySaveLayerAsset = "hierarchy.save_layer_asset";
    inline constexpr const char* AssetsSaveLayerFailed = "assets.save_layer_failed";
    inline constexpr const char* LayerAssetLoadFailed = "layer_asset.load_failed";
    inline constexpr const char* InspectorLayerParallax = "inspector.layer_parallax";
    inline constexpr const char* InspectorLayerParallaxTooltip = "inspector.layer_parallax_tooltip";
    inline constexpr const char* InspectorLayerLit = "inspector.layer_lit";
    inline constexpr const char* InspectorLayerLitTooltip = "inspector.layer_lit_tooltip";
    inline constexpr const char* InspectorLayerSpace = "inspector.layer_space";
    inline constexpr const char* InspectorLayerSpaceWorld = "inspector.layer_space_world";
    inline constexpr const char* InspectorLayerSpaceScreen = "inspector.layer_space_screen";
    inline constexpr const char* HierarchyLayerEmpty = "hierarchy.layer_empty";
    inline constexpr const char* HierarchyLayerVisible = "hierarchy.layer_visible";

    // ── 인스펙터 ─────────────────────────────────────────────────────────
    inline constexpr const char* InspectorNothingSelected = "inspector.nothing_selected";
    inline constexpr const char* InspectorActive = "inspector.active";
    inline constexpr const char* InspectorName = "inspector.name";
    // 인스펙터 머리의 태그 칸(D-297). 목록은 프로젝트의 `Tags` 이고 맨 끝 항목이 새 태그를 더한다.
    inline constexpr const char* InspectorTag = "inspector.tag";
    inline constexpr const char* InspectorTagNone = "inspector.tag_none";
    inline constexpr const char* InspectorTagAdd = "inspector.tag_add";
    inline constexpr const char* InspectorTagAddConfirm = "inspector.tag_add_confirm";
    inline constexpr const char* InspectorTagNameHint = "inspector.tag_name_hint";
    inline constexpr const char* ProjectSettingsTags = "project_settings.tags";
    inline constexpr const char* InspectorEnabled = "inspector.enabled";
    inline constexpr const char* InspectorAddComponent = "inspector.add_component";
    // 컴포넌트 메뉴의 맨 위·맨 아래로(D-298).
    inline constexpr const char* InspectorAssetName = "inspector.asset_name";
    inline constexpr const char* InspectorAssetType = "inspector.asset_type";
    inline constexpr const char* InspectorAssetPath = "inspector.asset_path";
    inline constexpr const char* InspectorAssetId = "inspector.asset_id";
    inline constexpr const char* InspectorOpenInViewer = "inspector.open_in_viewer";
    inline constexpr const char* InspectorCopyComponent = "inspector.copy_component";
    inline constexpr const char* InspectorPasteComponent = "inspector.paste_component";
    inline constexpr const char* InspectorPasteComponentValues = "inspector.paste_component_values";
    inline constexpr const char* InspectorRemoveComponent = "inspector.remove_component";
    // 컴포넌트 머리의 메뉴 단추 툴팁(D-278).
    inline constexpr const char* InspectorComponentMenu = "inspector.component_menu";
    inline constexpr const char* InspectorNoComponentTypes = "inspector.no_component_types";
    inline constexpr const char* InspectorUnregisteredType = "inspector.unregistered_type";
    inline constexpr const char* InspectorUnknownComponent = "inspector.unknown_component";
    inline constexpr const char* InspectorUnresolvedScript = "inspector.unresolved_script";
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
    inline constexpr const char* StatsGameTime = "stats.game_time";
    inline constexpr const char* StatsFixedSteps = "stats.fixed_steps";
    inline constexpr const char* StatsRandomSeed = "stats.random_seed";
    inline constexpr const char* StatsDebugLines = "stats.debug_lines";
    inline constexpr const char* StatsDebugLinesDropped = "stats.debug_lines_dropped";
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
    inline constexpr const char* CommonAlreadyAdded = "common.already_added";

    // ── 잠긴 까닭 ────────────────────────────────────────────────────────
    //
    // 회색 항목에 마우스를 올렸을 때 뜨는 한 줄이다(D-181). 저장이 막힌 까닭은
    // 이미 `popup.save_blocked_while_playing` 이 있어 그것을 같이 쓴다.
    inline constexpr const char* BlockedNoProject = "blocked.no_project";
    inline constexpr const char* BlockedNoAssetHere = "blocked.no_asset_here";
    inline constexpr const char* BlockedNothingToUndo = "blocked.nothing_to_undo";
    inline constexpr const char* BlockedNothingToRedo = "blocked.nothing_to_redo";
    inline constexpr const char* BlockedClipboardEmpty = "blocked.clipboard_empty";
    inline constexpr const char* BlockedNotPlaying = "blocked.not_playing";
    inline constexpr const char* BlockedNotPaused = "blocked.not_paused";
    inline constexpr const char* BlockedProjectHasNoFile = "blocked.project_has_no_file";
    inline constexpr const char* BlockedPickAnImage = "blocked.pick_an_image";
    inline constexpr const char* BlockedLastLayer = "blocked.last_layer";
    inline constexpr const char* BlockedNoParent = "blocked.no_parent";
    inline constexpr const char* BlockedPanelNotOpen = "blocked.panel_not_open";
    inline constexpr const char* BlockedNoDrawing = "blocked.no_drawing";
    inline constexpr const char* BlockedAlreadyFront = "blocked.already_front";
    inline constexpr const char* BlockedAlreadyBack = "blocked.already_back";
    inline constexpr const char* CommandPaletteTitle = "command_palette.title";
    inline constexpr const char* CommandPaletteSearchHint = "command_palette.search_hint";
    inline constexpr const char* CommandPaletteNoMatch = "command_palette.no_match";

    // ── 컴포넌트 갈래 ────────────────────────────────────────────────────
    //
    // 키는 `component_category.<갈래>` 로 그 자리에서 지어진다(`EditorNames`).
    // 여기 적는 것은 **번역 파일에 무엇이 있어야 하는지를 남기기 위한 것**이다 -
    // 코드가 이 이름들을 직접 부르지는 않는다.
    // 필드 분류(`Category`)의 보이는 이름이다. 키는 `field_category.<분류, 띄어쓰기는 _>` 로 그 자리에서 지어진다(`EditorNames::FieldCategoryLabel`).
    inline constexpr const char* FieldCategoryWorldCache = "field_category.World_cache";
    inline constexpr const char* ComponentCategoryTransform = "component_category.Transform";
    inline constexpr const char* ComponentCategoryRendering = "component_category.Rendering";
    inline constexpr const char* ComponentCategoryPhysics = "component_category.Physics";
    inline constexpr const char* ComponentCategoryAudio = "component_category.Audio";
    inline constexpr const char* ComponentCategoryUi = "component_category.UI";
    inline constexpr const char* ComponentCategoryDefault = "component_category.Components";
    inline constexpr const char* ComponentCategoryScripts = "component_category.Scripts";

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
    inline constexpr const char* AssetsFindInBrowser = "assets.find_in_browser";
    inline constexpr const char* AssetsColumnName = "assets.column_name";
    inline constexpr const char* AssetsColumnType = "assets.column_type";
    inline constexpr const char* AssetsColumnModified = "assets.column_modified";
    inline constexpr const char* AssetsColumnId = "assets.column_id";
    inline constexpr const char* AssetsSortTooltip = "assets.sort_tooltip";
    inline constexpr const char* AssetsRescan = "assets.rescan";
    inline constexpr const char* AssetsNewCanvas = "assets.new_canvas";
    inline constexpr const char* AssetsNewScript = "assets.new_script";
    inline constexpr const char* AssetsScriptsRoot = "assets.scripts_root";
    inline constexpr const char* AssetsScriptsEmpty = "assets.scripts_empty";
    inline constexpr const char* AssetsOpenScript = "assets.open_script";
    inline constexpr const char* NewScriptName = "new_script.name";
    inline constexpr const char* NewScriptNameHint = "new_script.name_hint";
    inline constexpr const char* NewScriptAdvanced = "new_script.advanced";
    inline constexpr const char* NewScriptLanguage = "new_script.language";
    inline constexpr const char* NewScriptLanguageHint = "new_script.language_hint";
    inline constexpr const char* NewScriptFields = "new_script.fields";
    inline constexpr const char* NewScriptInvalidName = "new_script.invalid_name";
    inline constexpr const char* NewScriptReservedName = "new_script.reserved_name";
    inline constexpr const char* NewScriptTypeTaken = "new_script.type_taken";
    inline constexpr const char* NewScriptFileTaken = "new_script.file_taken";
    inline constexpr const char* NewScriptInvalidField = "new_script.invalid_field";
    inline constexpr const char* NewScriptFailed = "new_script.failed";
    inline constexpr const char* AssetsNewFontFamily = "assets.new_font_family";
    inline constexpr const char* AssetsNewStringTable = "assets.new_string_table";
    inline constexpr const char* AssetsNewSoundEffect = "assets.new_sound_effect";
    // 노드 그래프 창(D-323)과 노드 인스펙터(D-324). `FileWindow*` 는 파일을 여는 창(노드 그래프·사운드 이펙트)이 함께 쓰는 묻는 창이다.
    inline constexpr const char* NodeGraphInspectorTitle = "node_graph.inspector_title";
    inline constexpr const char* NodeGraphNoGraph = "node_graph.no_graph";
    inline constexpr const char* FileWindowUnsavedTitle = "file_window.unsaved_title";
    inline constexpr const char* FileWindowReloadMessage = "file_window.reload_message";
    inline constexpr const char* FileWindowReloadDiscard = "file_window.reload_discard";
    inline constexpr const char* FileWindowCloseMessage = "file_window.close_message";
    inline constexpr const char* FileWindowSaveAndClose = "file_window.save_and_close";
    inline constexpr const char* FileWindowCloseWithoutSaving = "file_window.close_without_saving";
    inline constexpr const char* NodeGraphTitle = "node_graph.title";
    inline constexpr const char* AssetsNewNodeGraph = "assets.new_node_graph";
    inline constexpr const char* NodeGraphSave = "node_graph.save";
    inline constexpr const char* NodeGraphNothingToSave = "node_graph.nothing_to_save";
    inline constexpr const char* NodeGraphReload = "node_graph.reload";
    inline constexpr const char* NodeGraphClose = "node_graph.close";
    inline constexpr const char* NodeGraphSaveFailed = "node_graph.save_failed";
    inline constexpr const char* NodeGraphReadFailed = "node_graph.read_failed";
    inline constexpr const char* NodeGraphProblems = "node_graph.problems";
    inline constexpr const char* NodeGraphAddNode = "node_graph.add_node";
    inline constexpr const char* NodeGraphCopy = "node_graph.copy";
    inline constexpr const char* NodeGraphPaste = "node_graph.paste";
    inline constexpr const char* NodeGraphNothingToPaste = "node_graph.nothing_to_paste";
    inline constexpr const char* NodeGraphDuplicate = "node_graph.duplicate";
    inline constexpr const char* NodeGraphDelete = "node_graph.delete";
    inline constexpr const char* NodeGraphFrame = "node_graph.frame";
    inline constexpr const char* NodeGraphDisconnect = "node_graph.disconnect";
    inline constexpr const char* NodeGraphDisconnectAll = "node_graph.disconnect_all";
    inline constexpr const char* NodeGraphNoSelection = "node_graph.no_selection";
    inline constexpr const char* NodeGraphManySelected = "node_graph.many_selected";
    inline constexpr const char* NodeGraphLinked = "node_graph.linked";
    inline constexpr const char* NodeGraphOnlyOne = "node_graph.only_one";
    inline constexpr const char* NodeGraphUnknownType = "node_graph.unknown_type";
    inline constexpr const char* NodeGraphPreview = "node_graph.preview";
    inline constexpr const char* NodeGraphNoOutput = "node_graph.no_output";
    inline constexpr const char* NodeGraphAddComment = "node_graph.add_comment";
    inline constexpr const char* NodeGraphComment = "node_graph.comment";
    inline constexpr const char* NodeGraphCommentTitle = "node_graph.comment_title";
    inline constexpr const char* NodeGraphCommentColor = "node_graph.comment_color";
    inline constexpr const char* NodeGraphRemoveWaypoint = "node_graph.remove_waypoint";
    inline constexpr const char* NodeGraphMinimap = "node_graph.minimap";
    inline constexpr const char* InspectorSoundEffectHint = "inspector.sound_effect_hint";
    inline constexpr const char* InspectorOpenSoundEffect = "inspector.open_sound_effect";
    inline constexpr const char* SoundEffectTitle = "sound_effect.title";
    inline constexpr const char* SoundEffectOpen = "sound_effect.open";
    inline constexpr const char* SoundEffectNone = "sound_effect.none";
    inline constexpr const char* SoundEffectSave = "sound_effect.save";
    inline constexpr const char* SoundEffectNothingToSave = "sound_effect.nothing_to_save";
    inline constexpr const char* SoundEffectReload = "sound_effect.reload";
    inline constexpr const char* SoundEffectClose = "sound_effect.close";
    inline constexpr const char* SoundEffectKindHelp = "sound_effect.kind_help";
    inline constexpr const char* SoundEffectApply = "sound_effect.apply";
    inline constexpr const char* SoundEffectSaveFailed = "sound_effect.save_failed";
    inline constexpr const char* SoundEffectReadFailed = "sound_effect.read_failed";
    inline constexpr const char* SoundEffectPreview = "sound_effect.preview";
    inline constexpr const char* SoundEffectTestSound = "sound_effect.test_sound";
    inline constexpr const char* SoundEffectPreviewHint = "sound_effect.preview_hint";
    inline constexpr const char* SoundEffectRevert = "sound_effect.revert";
    inline constexpr const char* SoundEffectRevertHelp = "sound_effect.revert_help";
    inline constexpr const char* SoundEffectBypass = "sound_effect.bypass";
    inline constexpr const char* SoundEffectBypassHelp = "sound_effect.bypass_help";
    inline constexpr const char* SoundEffectOutput = "sound_effect.output";
    inline constexpr const char* SoundEffectGraphFrequency = "sound_effect.graph_frequency";
    inline constexpr const char* SoundEffectGraphShape = "sound_effect.graph_shape";
    inline constexpr const char* SoundEffectGraphDynamics = "sound_effect.graph_dynamics";
    inline constexpr const char* SoundEffectGraphEcho = "sound_effect.graph_echo";
    inline constexpr const char* SoundEffectGraphReverb = "sound_effect.graph_reverb";
    inline constexpr const char* SoundEffectGraphChorus = "sound_effect.graph_chorus";
    inline constexpr const char* SoundEffectGraphPitch = "sound_effect.graph_pitch";
    inline constexpr const char* SoundEffectAboutReverb = "sound_effect.about_reverb";
    inline constexpr const char* SoundEffectAboutLowPass = "sound_effect.about_low_pass";
    inline constexpr const char* SoundEffectAboutHighPass = "sound_effect.about_high_pass";
    inline constexpr const char* SoundEffectAboutEcho = "sound_effect.about_echo";
    inline constexpr const char* SoundEffectAboutDistortion = "sound_effect.about_distortion";
    inline constexpr const char* SoundEffectAboutCompressor = "sound_effect.about_compressor";
    inline constexpr const char* SoundEffectAboutLimiter = "sound_effect.about_limiter";
    inline constexpr const char* SoundEffectAboutEqualizer = "sound_effect.about_equalizer";
    inline constexpr const char* SoundEffectAboutChorus = "sound_effect.about_chorus";
    inline constexpr const char* SoundEffectAboutPitchShift = "sound_effect.about_pitch_shift";
    inline constexpr const char* AssetsDuplicate = "assets.duplicate";
    inline constexpr const char* AssetsCopyPath = "assets.copy_path";
    // 파일 클립보드(D-182). 오브젝트 쪽의 복사·붙여넣기와 **다른 키**다 - 같은 말이라도
    // 한쪽 문구만 고치고 싶을 때 키가 하나면 둘 다 바뀐다.
    inline constexpr const char* AssetsCut = "assets.cut";
    inline constexpr const char* AssetsCopyFile = "assets.copy_file";
    inline constexpr const char* AssetsPaste = "assets.paste";
    inline constexpr const char* AssetsPasteFailed = "assets.paste_failed";
    inline constexpr const char* AssetsSelectionCount = "assets.selection_count";
    inline constexpr const char* PopupUnsavedCanvasTitle = "popup.unsaved_canvas_title";
    inline constexpr const char* PopupUnsavedCanvasMessage = "popup.unsaved_canvas_message";
    // 끝내기·프로젝트 바꾸기 전에 묻는 창(D-324).
    inline constexpr const char* ProjectSettingsRevertMessage = "project_settings.revert_message";
    inline constexpr const char* ProjectSettingsRevertDiscard = "project_settings.revert_discard";
    inline constexpr const char* PopupUnsavedWorkTitle = "popup.unsaved_work_title";
    inline constexpr const char* PopupUnsavedWorkMessage = "popup.unsaved_work_message";
    inline constexpr const char* PopupUnsavedWorkCanvas = "popup.unsaved_work_canvas";
    inline constexpr const char* CommonSaveAll = "common.save_all";
    inline constexpr const char* CommonDontSave = "common.dont_save";
    inline constexpr const char* CommonSaveAndOpen = "common.save_and_open";
    inline constexpr const char* CommonOpenWithoutSaving = "common.open_without_saving";
    inline constexpr const char* AssetsOpenCanvas = "assets.open_canvas";
    inline constexpr const char* AssetsOpenInSpriteViewer = "assets.open_in_sprite_viewer";
    inline constexpr const char* AssetsImport = "assets.import";
    inline constexpr const char* MenuImportSprite = "menu.import_sprite";
    inline constexpr const char* DialogImportTitle = "dialog.import_title";
    inline constexpr const char* DialogImportImages = "dialog.import_images";
    inline constexpr const char* DialogImportAudio = "dialog.import_audio";
    inline constexpr const char* DialogImportAssets = "dialog.import_assets";
    inline constexpr const char* MenuImportAudio = "menu.import_audio";
    inline constexpr const char* PopupImportFailed = "popup.import_failed";
    inline constexpr const char* SpriteViewerTitle = "sprite_viewer.title";
    inline constexpr const char* SpriteViewerLoading = "sprite_viewer.loading";
    inline constexpr const char* SpriteViewerFrame = "sprite_viewer.frame";
    inline constexpr const char* SpriteViewerFps = "sprite_viewer.fps";
    // 시트를 보는 배율과 피벗 표시(D-185, 기존 `확대`·`창에 맞추기`·`피벗 표시`·`가리킴`).
    inline constexpr const char* SpriteViewerZoom = "sprite_viewer.zoom";
    inline constexpr const char* SpriteViewerFit = "sprite_viewer.fit";
    inline constexpr const char* SpriteViewerShowPivot = "sprite_viewer.show_pivot";
    inline constexpr const char* SpriteViewerHoveredFrame = "sprite_viewer.hovered_frame";
    inline constexpr const char* SpriteViewerPlay = "sprite_viewer.play";
    inline constexpr const char* SpriteViewerStop = "sprite_viewer.stop";
    inline constexpr const char* SpriteViewerSelectToEdit = "sprite_viewer.select_to_edit";
    inline constexpr const char* SpriteViewerSelect = "sprite_viewer.select";
    // 프레임 고르기(D-165, 기존 `InspectorSpritePickFrame`·`...NoSheet`)
    inline constexpr const char* SpriteViewerPickHint = "sprite_viewer.pick_hint";
    inline constexpr const char* InspectorPickFrame = "inspector.pick_frame";
    inline constexpr const char* InspectorPickFrameNoSprite = "inspector.pick_frame_no_sprite";
    inline constexpr const char* InspectorTextNoFont = "inspector.text_no_font";
    inline constexpr const char* InspectorCameraSizeUnused = "inspector.camera_size_unused";
    inline constexpr const char* InspectorCameraPpuUnused = "inspector.camera_ppu_unused";
    inline constexpr const char* InspectorSpritePpuMismatch = "inspector.sprite_ppu_mismatch";
    inline constexpr const char* AssetsIconView = "assets.icon_view";
    inline constexpr const char* AssetsListView = "assets.list_view";
    inline constexpr const char* AssetsViewTooltip = "assets.view_tooltip";
    inline constexpr const char* CommonCancel = "common.cancel";
    // 경로 칸의 단추(D-164, 기존 `CommonBrowse`)
    inline constexpr const char* CommonBrowse = "common.browse";
    inline constexpr const char* DialogBrowseFolder = "dialog.browse_folder";
    inline constexpr const char* DialogBrowseFile = "dialog.browse_file";
    inline constexpr const char* InspectorTextureImportOptions = "inspector.texture_import_options";
    inline constexpr const char* InspectorAudioImportOptions = "inspector.audio_import_options";
    inline constexpr const char* InspectorFontImportOptions = "inspector.font_import_options";
    inline constexpr const char* InspectorFontFamilyFaces = "inspector.font_family_faces";
    inline constexpr const char* InspectorStringTable = "inspector.string_table";
    inline constexpr const char* InspectorNoStringKey = "inspector.no_string_key";
    inline constexpr const char* InspectorAudioFormat = "inspector.audio_format";
    inline constexpr const char* InspectorAudioLength = "inspector.audio_length";
    inline constexpr const char* InspectorAudioPreview = "inspector.audio_preview";
    inline constexpr const char* InspectorAudioPlay = "inspector.audio_play";
    inline constexpr const char* InspectorAudioStop = "inspector.audio_stop";
    inline constexpr const char* InspectorAudioLoop = "inspector.audio_loop";
    inline constexpr const char* InspectorAudioPosition = "inspector.audio_position";
    inline constexpr const char* InspectorAudioSpectrum = "inspector.audio_spectrum";
    inline constexpr const char* InspectorAudioSeekHint = "inspector.audio_seek_hint";
    inline constexpr const char* InspectorAudioOff = "inspector.audio_off";
    inline constexpr const char* InspectorAudioUnreadable = "inspector.audio_unreadable";
    inline constexpr const char* InspectorAudioBusMissing = "inspector.audio_bus_missing";
    inline constexpr const char* InspectorLayersEverything = "inspector.layers_everything";
    inline constexpr const char* InspectorLayersNothing = "inspector.layers_nothing";
    inline constexpr const char* InspectorObjectNone = "inspector.object_none";
    inline constexpr const char* InspectorObjectMissing = "inspector.object_missing";
    inline constexpr const char* ProjectSettingsAudio = "project_settings.audio";
    inline constexpr const char* ProjectSettingsAudioEffectRatioOff = "project_settings.audio_effect_ratio_off";
    inline constexpr const char* ProjectSettingsAudioDuckHelp = "project_settings.audio_duck_help";
    inline constexpr const char* ProjectSettingsAudioDefaultDevice = "project_settings.audio_default_device";
    inline constexpr const char* ProjectSettingsAudioDeviceHelp = "project_settings.audio_device_help";
    inline constexpr const char* ProjectSettingsAudioRefreshDevices = "project_settings.audio_refresh_devices";
    inline constexpr const char* ProjectSettingsPhysicsThreadsAuto = "project_settings.physics_threads_auto";
    inline constexpr const char* ProjectSettingsPhysicsThreadsSingle = "project_settings.physics_threads_single";
    inline constexpr const char* ProjectSettingsPhysicsThreadsWorkers = "project_settings.physics_threads_workers";
    inline constexpr const char* ProjectSettingsPhysicsThreadsHelp = "project_settings.physics_threads_help";
    inline constexpr const char* ProjectSettingsPhysicsRecommend = "project_settings.physics_recommend";
    inline constexpr const char* ProjectSettingsPhysicsRecommendHelp = "project_settings.physics_recommend_help";
    inline constexpr const char* ProjectSettingsPhysics = "project_settings.physics";
    inline constexpr const char* ProjectSettingsPhysicsLayers = "project_settings.physics_layers";
    inline constexpr const char* ProjectSettingsPhysicsLayersHelp = "project_settings.physics_layers_help";
    inline constexpr const char* ProjectSettingsPhysicsMatrix = "project_settings.physics_matrix";
    inline constexpr const char* ProjectSettingsPhysicsMatrixHelp = "project_settings.physics_matrix_help";
    inline constexpr const char* ProjectSettingsAudioMuteHelp = "project_settings.audio_mute_help";
    inline constexpr const char* ProjectSettingsAudioMasterHelp = "project_settings.audio_master_help";
    inline constexpr const char* ProjectSettingsAudioBusesRemoved = "project_settings.audio_buses_removed";
    inline constexpr const char* ProjectSettingsAudioRouting = "project_settings.audio_routing";
    inline constexpr const char* ProjectSettingsAudioNoSend = "project_settings.audio_no_send";
    inline constexpr const char* ProjectSettingsAudioParentHelp = "project_settings.audio_parent_help";
    inline constexpr const char* ProjectSettingsAudioSendHelp = "project_settings.audio_send_help";
    inline constexpr const char* StatsAudioSolo = "stats.audio_solo";
    inline constexpr const char* StatsAudioBuses = "stats.audio_buses";
    inline constexpr const char* StatsAudioVoices = "stats.audio_voices";
    inline constexpr const char* StatsAudioSaved = "stats.audio_saved";
    inline constexpr const char* StatsAudioVirtual = "stats.audio_virtual";
    inline constexpr const char* StatsAudioDevice = "stats.audio_device";
    inline constexpr const char* StatsAudioNoDevice = "stats.audio_no_device";
    inline constexpr const char* StatsAudioStolen = "stats.audio_stolen";
    inline constexpr const char* ProjectSettingsAudioEffects = "project_settings.audio_effects";
    inline constexpr const char* ProjectSettingsAudioEffectOff = "project_settings.audio_effect_off";
    inline constexpr const char* ProjectSettingsAudioAddBus = "project_settings.audio_add_bus";
    inline constexpr const char* ProjectSettingsAudioRemoveBus = "project_settings.audio_remove_bus";
    inline constexpr const char* ProjectSettingsAudioBusesHelp = "project_settings.audio_buses_help";
    inline constexpr const char* ProjectSettingsAudioNoBuses = "project_settings.audio_no_buses";
    inline constexpr const char* ProjectSettingsAudioDuplicate = "project_settings.audio_duplicate";
    inline constexpr const char* ProjectSettingsInput = "project_settings.input";
    inline constexpr const char* ProjectSettingsInputLayersHelp = "project_settings.input_layers_help";
    inline constexpr const char* ProjectSettingsInputDefaultLayers = "project_settings.input_default_layers";
    inline constexpr const char* ProjectSettingsInputAddLayer = "project_settings.input_add_layer";
    inline constexpr const char* ProjectSettingsInputMoveUp = "project_settings.input_move_up";
    inline constexpr const char* ProjectSettingsInputRemove = "project_settings.input_remove";
    inline constexpr const char* ProjectSettingsInputDuplicate = "project_settings.input_duplicate";
    inline constexpr const char* ProjectSettingsInputActionsHelp = "project_settings.input_actions_help";
    inline constexpr const char* ProjectSettingsInputNoActions = "project_settings.input_no_actions";
    inline constexpr const char* ProjectSettingsInputAddAction = "project_settings.input_add_action";
    inline constexpr const char* ProjectSettingsInputRemoveAction = "project_settings.input_remove_action";
    inline constexpr const char* ProjectSettingsInputAddBinding = "project_settings.input_add_binding";
    inline constexpr const char* ProjectSettingsInputRemoveBinding = "project_settings.input_remove_binding";
    inline constexpr const char* ProjectSettingsInputAnyGamepad = "project_settings.input_any_gamepad";
    inline constexpr const char* ProjectSettingsInputSetHelp = "project_settings.input_set_help";
    inline constexpr const char* ProjectSettingsInputTooMany = "project_settings.input_too_many";
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
    inline constexpr const char* ProjectSettingsText = "project_settings.text";
    inline constexpr const char* ProjectSettingsFontsHelp = "project_settings.fonts_help";
    inline constexpr const char* ProjectSettingsGameLanguages = "project_settings.game_languages";
    inline constexpr const char* ProjectSettingsGameLanguagesHelp = "project_settings.game_languages_help";
    inline constexpr const char* ProjectSettingsSave = "project_settings.save";
    inline constexpr const char* ProjectSettingsRevert = "project_settings.revert";
    inline constexpr const char* ProjectSettingsSaved = "project_settings.saved";
    inline constexpr const char* ProjectSettingsFixedDeltaTime = "project_settings.fixed_delta_time";
    inline constexpr const char* ProjectSettingsMaxFixedSteps = "project_settings.max_fixed_steps";
    inline constexpr const char* ProjectSettingsMaxDeltaTime = "project_settings.max_delta_time";
    inline constexpr const char* ProjectSettingsRandomSeed = "project_settings.random_seed";
    inline constexpr const char* ProjectSettingsLinearFilter = "project_settings.linear_filter";
    inline constexpr const char* ProjectSettingsIgnorePatterns = "project_settings.ignore_patterns";

    // ── 로그 (D-133) ─────────────────────────────────────────────────────
    inline constexpr const char* LogClear = "log.clear";
    inline constexpr const char* LogAutoScroll = "log.auto_scroll";
    inline constexpr const char* LogEmpty = "log.empty";
    inline constexpr const char* LogEarlierOmitted = "log.earlier_omitted";
    inline constexpr const char* LogTrace = "log.trace";
    inline constexpr const char* LogDebug = "log.debug";
    inline constexpr const char* LogInfo = "log.info";
    inline constexpr const char* LogWarning = "log.warning";
    inline constexpr const char* LogError = "log.error";
    inline constexpr const char* PanelEditorSettings = "panel.editor_settings";
    inline constexpr const char* MenuSettingsEditor = "menu.settings_editor";
    inline constexpr const char* EditorSettingsShortcuts = "editor_settings.shortcuts";
    inline constexpr const char* EditorSettingsScripts = "editor_settings.scripts";
    inline constexpr const char* BuildResultsNoDiagnostic = "build_results.no_diagnostic";
    inline constexpr const char* NotifyScriptsReloaded = "notify.scripts_reloaded";
    inline constexpr const char* NotifyScriptsReloadNotes = "notify.scripts_reload_notes";
    inline constexpr const char* NotifyScriptsReloadFailed = "notify.scripts_reload_failed";
    inline constexpr const char* NotifyScriptsReloadKept = "notify.scripts_reload_kept";
    inline constexpr const char* NotifyScriptsReloadPending = "notify.scripts_reload_pending";
    inline constexpr const char* EditorSettingsSearchHint = "editor_settings.search_hint";
    inline constexpr const char* EditorSettingsPressKey = "editor_settings.press_key";
    inline constexpr const char* EditorSettingsCaptureHint = "editor_settings.capture_hint";
    inline constexpr const char* EditorSettingsClearBinding = "editor_settings.clear_binding";
    inline constexpr const char* EditorSettingsEmptyBinding = "editor_settings.empty_binding";
    inline constexpr const char* EditorSettingsReset = "editor_settings.reset";
    inline constexpr const char* EditorSettingsResetTooltip = "editor_settings.reset_tooltip";
    inline constexpr const char* EditorSettingsResetAll = "editor_settings.reset_all";
    inline constexpr const char* EditorSettingsResetAllTitle = "editor_settings.reset_all_title";
    inline constexpr const char* EditorSettingsResetAllMessage = "editor_settings.reset_all_message";
    inline constexpr const char* EditorSettingsResetAllConfirm = "editor_settings.reset_all_confirm";
    inline constexpr const char* EditorSettingsClash = "editor_settings.clash";
    inline constexpr const char* EditorSettingsShadows = "editor_settings.shadows";
    inline constexpr const char* EditorSettingsShadowed = "editor_settings.shadowed";
    inline constexpr const char* EditorSettingsNoMatch = "editor_settings.no_match";

    // ── 로딩과 상태 표시줄 (D-217·D-236) ───────────────────────────────────
    // 캔버스를 열 때 워커로 에셋을 읽는 묶음의 이름이다. 상태 표시줄과 태스크 목록이 보인다.
    inline constexpr const char* TaskLoadCanvas = "task.load_canvas";
    inline constexpr const char* NotifyCanvasAssetsFailedTitle = "notify.canvas_assets_failed_title";
    // 수와 첫 사유(파일)를 받는다.
    inline constexpr const char* NotifyCanvasAssetsFailedMessage = "notify.canvas_assets_failed_message";
    inline constexpr const char* StatusBarShowTasks = "status_bar.show_tasks";
    inline constexpr const char* StatusBarOpenLog = "status_bar.open_log";

    // ── 가이드(D-251) ────────────────────────────────────────────────────
    inline constexpr const char* MenuHelp = "menu.help";
    inline constexpr const char* MenuHelpGuides = "menu.help_guides";
    inline constexpr const char* GuideNext = "guide.next";
    inline constexpr const char* GuideDone = "guide.done";
    inline constexpr const char* GuideConfirmNote = "guide.confirm_note";
    inline constexpr const char* GuideProgressDone = "guide.progress_done";
    inline constexpr const char* GuideNeedSelectedObject = "guide.need_selected_object";
    inline constexpr const char* GuideNeedTargetObject = "guide.need_target_object";
    // 필드 이름과 기다리는 값을 받는다(`field.edit` 의 `Value`, D-273).
    inline constexpr const char* GuideNeedFieldValue = "guide.need_field_value";
    inline constexpr const char* GuideNeedPointCollider = "guide.need_point_collider";
    // 포인트 번호를 받는다.
    inline constexpr const char* GuideNeedPolygonPoint = "guide.need_polygon_point";
    inline constexpr const char* GuideTargetDisabled = "guide.target_disabled";
    inline constexpr const char* GuideTargetDisabledBecause = "guide.target_disabled_because";
    inline constexpr const char* GuideSkip = "guide.skip";
    inline constexpr const char* GuideBack = "guide.back";
    inline constexpr const char* GuideBackFirstStep = "guide.back_first_step";
    inline constexpr const char* GuideProgress = "guide.progress";
    inline constexpr const char* GuideFinished = "guide.finished";
    inline constexpr const char* GuideAddComponentTitle = "guide.add_component_title";
    inline constexpr const char* GuideAddComponentSelectTitle = "guide.add_component_select_title";
    inline constexpr const char* GuideAddComponentSelectBody = "guide.add_component_select_body";
    inline constexpr const char* GuideAddComponentFieldTitle = "guide.add_component_field_title";
    inline constexpr const char* GuideAddComponentFieldBody = "guide.add_component_field_body";
    inline constexpr const char* GuideAddComponentAddTitle = "guide.add_component_add_title";
    inline constexpr const char* GuideAddComponentAddBody = "guide.add_component_add_body";
    inline constexpr const char* GuideBuildGameTitle = "guide.build_game_title";
    inline constexpr const char* GuideBuildGameStepTitle = "guide.build_game_step_title";
    inline constexpr const char* GuideBuildGameStepBody = "guide.build_game_step_body";
}
