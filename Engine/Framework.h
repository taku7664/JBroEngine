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

// 4단계 PCH 슬림화 — Core/* 헤더는 PCH 에서 모두 제거.
// 엔진/에디터 cpp 가 Core 서비스를 거의 다 만지므로 umbrella 한 줄 편의 제공:
//   #include "Core/CoreAll.h"
// Engine.lib 내부 cpp 는 self-contained 원칙으로 명시 include 권장 (점진).

// 3단계 PCH 슬림화 — GameFramework/* 헤더는 PCH 에서 모두 제거.
// 엔진/에디터 cpp 가 "씬 + 컴포넌트 + 시스템" 을 자주 같이 만지므로
// 한 줄 편의용 umbrella 를 제공: #include "GameFramework/GameFrameworkAll.h"
// 스크립트 사용자는 그대로 ScriptAPI.h umbrella 사용.

// 2단계 PCH 슬림화 — Editor/* 헤더는 모두 self-contained 가 되어 PCH 에서 제거.
// 각 cpp 는 자기가 쓰는 Editor 헤더를 직접 명시 include 한다.

#include "Application/Application.h"
