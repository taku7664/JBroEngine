#pragma once
#include "Engine/Framework.h"
#include "Application.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/Editor.h"
#include "Editor/Helper/ImGuiHelper.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
#include "Editor/RootDockWindow.h"
#include "Editor/Main/MainDockWindow.h"

#include "Editor/Main/AssetBrowser/AssetBrowserTool.h"
#include "Editor/Main/Inspector/InspectorTool.h"
#include "Editor/Main/SceneView/SceneViewContour.h"
#include "Editor/Main/SceneView/SceneViewEditContext.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Editor/Main/GameView/GameViewTool.h"
#include "Editor/Main/Log/LogTool.h"
#include "Editor/Main/Hierarchy/HierarchyTool.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#endif
