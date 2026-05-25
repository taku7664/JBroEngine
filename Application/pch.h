#pragma once
#include "Engine/Framework.h"
#include "Application.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/Editor.h"
#include "Editor/EditorComponentMenu.h"
#include "Editor/RootDockWindow.h"
#include "Editor/Main/MainDockWindow.h"

#include "Editor/Main/InspectorTool.h"
#include "Editor/Main/SceneViewTool.h"
#include "Editor/Main/GameViewTool.h"
#include "Editor/Main/HierarchyTool.h"
#include "Editor/Main/AssetBrowserTool.h"
#include "Editor/Main/LogTool.h"
#include "Editor/Main/ProjectSettingsWindow.h"
#endif
