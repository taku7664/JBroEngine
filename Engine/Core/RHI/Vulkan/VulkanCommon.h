#pragma once

#include "Core/Platform/PlatformDefines.h"

#if JBRO_PLATFORM_MOBILE
#include <vulkan/vulkan.h>
#if JBRO_PLATFORM_ANDROID
#include <vulkan/vulkan_android.h>
#endif
#if JBRO_PLATFORM_IOS
#include <vulkan/vulkan_metal.h>
#endif
#endif
