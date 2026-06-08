# ============================================================================
# JBroEngine — Android native runtime library (libJBroGame.so)
# ----------------------------------------------------------------------------
# Cross-compiles the engine runtime + game scripts for Android (arm64-v8a).
# Mirrors the Web build source subset (BuildScripts/Web/web_game_sources.txt)
# minus web-specific backends, plus Mobile platform + Vulkan RHI.
#
# Invoke through BuildScripts/Android/BuildAndroidNative.ps1 (sets toolchain,
# ABI, API level, STL, generator). Output: libJBroGame.so.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(JBroGame LANGUAGES C CXX)

# Repo root = two levels up from PlatformBuild/Android.
get_filename_component(JBRO_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Game project whose Contents are linked statically into the .so.
# Override with -DJBRO_CONTENT_PATH=<abs path to <Project>/Contents>.
if(NOT DEFINED JBRO_CONTENT_PATH)
    set(JBRO_CONTENT_PATH "${JBRO_REPO_ROOT}/SampleProject/Contents")
endif()
get_filename_component(JBRO_CONTENT_PATH "${JBRO_CONTENT_PATH}" ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Engine runtime sources (web baseline minus web-specific, plus mobile/vulkan)
# ----------------------------------------------------------------------------
set(JBRO_ENGINE_SOURCES
    Application/Application.cpp

    Engine/Application/Application.cpp
    Engine/Utillity/Base/Error.cpp
    Engine/Utillity/File/FilePath.cpp
    Engine/Utillity/File/FileUtillities.cpp
    Engine/Utillity/String/StringUtillity.cpp

    Engine/Core/Asset/AssetManager.cpp
    Engine/Core/Asset/AssetMetaFile.cpp
    Engine/Core/Asset/AssetPackage.cpp
    Engine/Core/Asset/AssetPath.cpp
    Engine/Core/Asset/AssetRegistry.cpp
    Engine/Core/Asset/AudioAsset.cpp
    Engine/Core/Asset/AudioEffectAsset.cpp
    Engine/Core/Asset/FileAsset.cpp
    Engine/Core/Asset/MaterialAsset.cpp
    Engine/Core/Asset/SpriteAsset.cpp
    Engine/Core/Build/BuildManifest.cpp
    Engine/Core/Audio/EmptyAudio/EmptyAudioDevice.cpp
    Engine/Core/Debug/Debug.cpp
    Engine/Core/Debug/DebugDraw.cpp
    Engine/Core/Debug/DebugDraw2D.cpp
    Engine/Core/Engine.cpp
    Engine/Core/EngineCore.cpp
    Engine/Core/ScriptCore.cpp
    Engine/Core/FileSystem/FileSystem.cpp
    Engine/Core/Game/GameModuleLoader.cpp
    Engine/Core/Input/Input.cpp
    Engine/Core/Input/InputSystem.cpp
    Engine/Core/Logging/Logger.cpp
    Engine/Core/Math/MathService.cpp
    Engine/Core/Module/Module.cpp
    Engine/Core/Network/NetworkManager.cpp
    Engine/Core/Random/RandomService.cpp
    Engine/Core/Renderer/Forward2DRenderer.cpp
    Engine/Core/Renderer/RenderResourceCache.cpp
    Engine/Core/Renderer/RenderResources2D.cpp
    Engine/Core/Renderer/RenderScene.cpp
    Engine/Core/Resource/ResourceRegistry.cpp
    Engine/Core/RHI/EmptyRHIDevice.cpp
    Engine/Core/Task/Task.cpp
    Engine/Core/Task/TaskGroup.cpp
    Engine/Core/Task/TaskManager.cpp
    Engine/Core/Time/Time.cpp

    # Android-specific platform + RHI
    Engine/Core/Platform/Mobile/MobilePlatform.cpp
    Engine/Core/Platform/Mobile/MobileRenderSurface.cpp
    Engine/Core/RHI/Vulkan/VulkanBuffer.cpp
    Engine/Core/RHI/Vulkan/VulkanCommandContext.cpp
    Engine/Core/RHI/Vulkan/VulkanGraphicsPipeline.cpp
    Engine/Core/RHI/Vulkan/VulkanProgram.cpp
    Engine/Core/RHI/Vulkan/VulkanRHIDevice.cpp
    Engine/Core/RHI/Vulkan/VulkanSampler.cpp
    Engine/Core/RHI/Vulkan/VulkanSwapchain.cpp
    Engine/Core/RHI/Vulkan/VulkanTexture.cpp

    Engine/GameFramework/Audio/AudioSystem.cpp
    Engine/GameFramework/Component/BuiltinComponentRegistry.cpp
    Engine/GameFramework/Object/GameObject.cpp
    Engine/GameFramework/Object/Ref.cpp
    Engine/GameFramework/Physics2D/Physics2DGeometry.cpp
    Engine/GameFramework/Physics2D/Physics2DSystem.cpp
    Engine/GameFramework/Prefab/PrefabSerializer.cpp
    Engine/GameFramework/Reflection/ReflectionRegistry.cpp
    Engine/GameFramework/Rendering/GameCamera.cpp
    Engine/GameFramework/Rendering/SpriteRenderSystem.cpp
    Engine/GameFramework/Scene/Scene.cpp
    Engine/GameFramework/Scene/SceneManager.cpp
    Engine/GameFramework/Scene/SceneSerializer.cpp
    Engine/GameFramework/Serialization/ComponentSerializer.cpp
    Engine/GameFramework/Serialization/ObjectSerializer.cpp
    Engine/GameFramework/Scripting/GameScript.cpp
    Engine/GameFramework/Scripting/ScriptSystem.cpp
    Engine/GameFramework/System/GameSystem.cpp
    Engine/GameFramework/Transform/TransformSystem.cpp
)

# yaml-cpp (compiled from source, static define).
set(JBRO_YAMLCPP_SOURCES
    Engine/ThirdParty/yaml-cpp/src/binary.cpp
    Engine/ThirdParty/yaml-cpp/src/convert.cpp
    Engine/ThirdParty/yaml-cpp/src/depthguard.cpp
    Engine/ThirdParty/yaml-cpp/src/directives.cpp
    Engine/ThirdParty/yaml-cpp/src/emit.cpp
    Engine/ThirdParty/yaml-cpp/src/emitfromevents.cpp
    Engine/ThirdParty/yaml-cpp/src/emitter.cpp
    Engine/ThirdParty/yaml-cpp/src/emitterstate.cpp
    Engine/ThirdParty/yaml-cpp/src/emitterutils.cpp
    Engine/ThirdParty/yaml-cpp/src/exceptions.cpp
    Engine/ThirdParty/yaml-cpp/src/exp.cpp
    Engine/ThirdParty/yaml-cpp/src/fptostring.cpp
    Engine/ThirdParty/yaml-cpp/src/memory.cpp
    Engine/ThirdParty/yaml-cpp/src/node.cpp
    Engine/ThirdParty/yaml-cpp/src/node_data.cpp
    Engine/ThirdParty/yaml-cpp/src/nodebuilder.cpp
    Engine/ThirdParty/yaml-cpp/src/nodeevents.cpp
    Engine/ThirdParty/yaml-cpp/src/null.cpp
    Engine/ThirdParty/yaml-cpp/src/ostream_wrapper.cpp
    Engine/ThirdParty/yaml-cpp/src/parse.cpp
    Engine/ThirdParty/yaml-cpp/src/parser.cpp
    Engine/ThirdParty/yaml-cpp/src/regex_yaml.cpp
    Engine/ThirdParty/yaml-cpp/src/scanner.cpp
    Engine/ThirdParty/yaml-cpp/src/scanscalar.cpp
    Engine/ThirdParty/yaml-cpp/src/scantag.cpp
    Engine/ThirdParty/yaml-cpp/src/scantoken.cpp
    Engine/ThirdParty/yaml-cpp/src/simplekey.cpp
    Engine/ThirdParty/yaml-cpp/src/singledocparser.cpp
    Engine/ThirdParty/yaml-cpp/src/stream.cpp
    Engine/ThirdParty/yaml-cpp/src/tag.cpp
)

# Prefix engine/yaml sources with repo root absolute path.
set(JBRO_ABS_SOURCES "")
foreach(src ${JBRO_ENGINE_SOURCES} ${JBRO_YAMLCPP_SOURCES})
    list(APPEND JBRO_ABS_SOURCES "${JBRO_REPO_ROOT}/${src}")
endforeach()

# ----------------------------------------------------------------------------
# Game project static script sources (from <Project>/Contents).
#   pch.cpp, GameModule.cpp, GeneratedScriptRegistry.cpp + Scripts/*.cpp
# ----------------------------------------------------------------------------
set(JBRO_SCRIPT_SOURCES "")
foreach(name pch.cpp GameModule.cpp GeneratedScriptRegistry.cpp)
    if(EXISTS "${JBRO_CONTENT_PATH}/${name}")
        list(APPEND JBRO_SCRIPT_SOURCES "${JBRO_CONTENT_PATH}/${name}")
    endif()
endforeach()
file(GLOB_RECURSE JBRO_SCRIPT_GLOB CONFIGURE_DEPENDS "${JBRO_CONTENT_PATH}/Scripts/*.cpp")
list(APPEND JBRO_SCRIPT_SOURCES ${JBRO_SCRIPT_GLOB})

# ----------------------------------------------------------------------------
# Android entry (native_app_glue + AndroidMain) — included once they exist.
# ----------------------------------------------------------------------------
set(JBRO_ENTRY_SOURCES "")
if(EXISTS "${JBRO_REPO_ROOT}/Application/Entry/AndroidMain.cpp")
    list(APPEND JBRO_ENTRY_SOURCES "${JBRO_REPO_ROOT}/Application/Entry/AndroidMain.cpp")
endif()
if(DEFINED ANDROID_NDK AND EXISTS "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
    list(APPEND JBRO_ENTRY_SOURCES "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
endif()

# ----------------------------------------------------------------------------
# Shared library target.
# ----------------------------------------------------------------------------
add_library(JBroGame SHARED
    ${JBRO_ABS_SOURCES}
    ${JBRO_SCRIPT_SOURCES}
    ${JBRO_ENTRY_SOURCES}
)

set_target_properties(JBroGame PROPERTIES OUTPUT_NAME "JBroGame")

# Include roots mirror the Web build order:
#   . / Application / Engine / Engine/ThirdParty / yaml-cpp/src / Content / Content/Scripts
target_include_directories(JBroGame PRIVATE
    "${JBRO_REPO_ROOT}"
    "${JBRO_REPO_ROOT}/Application"
    "${JBRO_REPO_ROOT}/Engine"
    "${JBRO_REPO_ROOT}/Engine/ThirdParty"
    "${JBRO_REPO_ROOT}/Engine/ThirdParty/yaml-cpp/src"
    "${JBRO_CONTENT_PATH}"
    "${JBRO_CONTENT_PATH}/Scripts"
)
if(DEFINED ANDROID_NDK AND EXISTS "${ANDROID_NDK}/sources/android/native_app_glue")
    target_include_directories(JBroGame PRIVATE
        "${ANDROID_NDK}/sources/android/native_app_glue")
endif()

target_compile_definitions(JBroGame PRIVATE
    JBRO_PLATFORM_ANDROID
    JBRO_GAME
    JBRO_RHI_VULKAN
    YAML_CPP_STATIC_DEFINE
    VK_USE_PLATFORM_ANDROID_KHR
)

# Vulkan + Android system libs from the NDK sysroot.
find_library(JBRO_VULKAN_LIB vulkan)
find_library(JBRO_LOG_LIB log)
find_library(JBRO_ANDROID_LIB android)
target_link_libraries(JBroGame PRIVATE
    ${JBRO_VULKAN_LIB}
    ${JBRO_LOG_LIB}
    ${JBRO_ANDROID_LIB}
)
