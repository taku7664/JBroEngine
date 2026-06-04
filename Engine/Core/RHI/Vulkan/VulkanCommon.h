#pragma once

#include "Core/Platform/PlatformDefines.h"

#if JBRO_PLATFORM_MOBILE || JBRO_PLATFORM_WINDOWS
#define JBRO_RHI_VULKAN 1
#else
#define JBRO_RHI_VULKAN 0
#endif

#if JBRO_RHI_VULKAN
#if JBRO_PLATFORM_ANDROID
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#if JBRO_PLATFORM_IOS
#define VK_USE_PLATFORM_METAL_EXT
#endif
#if JBRO_PLATFORM_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#if JBRO_PLATFORM_ANDROID
#include <vulkan/vulkan_android.h>
#endif
#if JBRO_PLATFORM_IOS
#include <vulkan/vulkan_metal.h>
#endif
#if JBRO_PLATFORM_WINDOWS
#include <vulkan/vulkan_win32.h>
#endif
#endif
