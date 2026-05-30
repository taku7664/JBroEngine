#pragma once
#include "Engine/Framework.h"
// 에디터(Application) 는 Core 서비스 + 씬/컴포넌트/시스템을 거의 다 만지므로
// umbrella 두 줄로 전부 가져온다.  Engine.lib 자체는 이 헤더를 PCH 에 박지 않는다.
#include "Engine/Core/CoreAll.h"
#include "Engine/GameFramework/GameFrameworkAll.h"
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
