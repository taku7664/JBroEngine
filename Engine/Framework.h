#pragma once

#include "Core/Platform/PlatformDefines.h"

#if JBRO_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <clocale>

#include <string>
#include <string_view>
#include <array>
#include <queue>
#include <vector>
#include <mutex>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <functional>
#include <typeinfo>
#include <type_traits>
#include <format>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#endif

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_impl_dx11.h"
#include "ThirdParty/imgui/imgui_impl_win32.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "ThirdParty/imgui/imgui_stdlib.h"
#endif

#include "Utillity/Framework.h"

#include "Core/Core.h"
#include "Core/Debug/DebugDraw2D.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Debug/DebugRenderer2D.h"
#include "Core/Debug/DebugRenderer.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Input/Input.h"
#include "Core/Input/IInputMessageHandler.h"
#include "Core/Localization/LocalizationManager.h"
#include "Core/Logging/Logger.h"
#include "Core/Module/Module.h"
#include "Core/Game/GameModuleTypes.h"
#include "Core/Game/IGameModule.h"
#include "Core/Asset/AssetTypes.h"
#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Asset/IAssetRegistry.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/FileAsset.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Asset/MaterialAsset.h"
#include "Core/Platform/IPlatform.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/IRHISampler.h"
#include "Core/Task/TaskManager.h"
#include "Core/Random/RandomService.h"
#include "Core/Math/MathService.h"
#include "Core/Time/Time.h"
#include "Core/Network/NetworkTypes.h"
#include "Core/Network/INetworkManager.h"
#include "Core/Engine.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/Forward2DRenderer.h"

#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/ECS/EntityManager.h"
#include "GameFramework/ECS/ComponentPool.h"
#include "GameFramework/System/GameSystem.h"
#include "GameFramework/Scene/SceneTypes.h"
#include "GameFramework/Scene/SceneSnapshot.h"
#include "GameFramework/Scene/SceneSerializer.h"
#include "GameFramework/Scene/SceneManager.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Rendering/SpriteRenderSystem.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Prefab/PrefabTypes.h"
#include "GameFramework/Prefab/PrefabSerializer.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Scripting/ScriptSystem.h"
#include "GameFramework/Debug/SceneDebugDrawSystem.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/Physics2D/Physics2DGeometry.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/ImGuiUtillity.h"
#include "Editor/ImItem/ImText.h"
#include "Editor/ImItem/ImTree.h"
#include "Editor/ImItem/ImList.h"
#include "Editor/ImWindow/ImWindowFlag.h"
#include "Editor/ImWindow/ImWindowContext.h"
#include "Editor/ImWindow/ImWindow.h"
#include "Editor/ImWindow/ImDockWindow.h"
#include "Editor/ImWindow/ImCustomWindow.h"
#include "Editor/ImWindow/ImPopupWindow.h"
#include "Editor/ImEditor.h"
#endif

#include "Application/Application.h"
