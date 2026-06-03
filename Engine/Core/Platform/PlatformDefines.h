#pragma once

#if defined(__ANDROID__) && !defined(JBRO_PLATFORM_ANDROID)
#define JBRO_PLATFORM_ANDROID
#endif

#if defined(__INTELLISENSE__) && !defined(JBRO_PLATFORM_WINDOWS) && !defined(JBRO_PLATFORM_WEB) && !defined(JBRO_PLATFORM_ANDROID) && !defined(JBRO_PLATFORM_IOS)
#define JBRO_PLATFORM_WINDOWS
#endif

#if defined(__INTELLISENSE__) && !defined(JBRO_EDITOR) && !defined(JBRO_GAME)
#define JBRO_EDITOR
#endif

#if defined(JBRO_PLATFORM_WEB)
#undef JBRO_PLATFORM_WEB
#define JBRO_PLATFORM_WEB 1
#else
#define JBRO_PLATFORM_WEB 0
#endif

#if defined(JBRO_PLATFORM_WINDOWS)
#undef JBRO_PLATFORM_WINDOWS
#define JBRO_PLATFORM_WINDOWS 1
#else
#define JBRO_PLATFORM_WINDOWS 0
#endif

#if defined(JBRO_PLATFORM_ANDROID)
#undef JBRO_PLATFORM_ANDROID
#define JBRO_PLATFORM_ANDROID 1
#else
#define JBRO_PLATFORM_ANDROID 0
#endif

#if defined(JBRO_PLATFORM_IOS)
#undef JBRO_PLATFORM_IOS
#define JBRO_PLATFORM_IOS 1
#else
#define JBRO_PLATFORM_IOS 0
#endif

#if JBRO_PLATFORM_ANDROID || JBRO_PLATFORM_IOS
#define JBRO_PLATFORM_MOBILE 1
#else
#define JBRO_PLATFORM_MOBILE 0
#endif

#if defined(JBRO_EDITOR)
#undef JBRO_EDITOR
#define JBRO_EDITOR 1
#else
#define JBRO_EDITOR 0
#endif

#if defined(JBRO_GAME)
#undef JBRO_GAME
#define JBRO_GAME 1
#else
#define JBRO_GAME 0
#endif

#if !JBRO_PLATFORM_WINDOWS && !JBRO_PLATFORM_WEB && !JBRO_PLATFORM_MOBILE
#define JBRO_PLATFORM_UNKNOWN 1
#else
#define JBRO_PLATFORM_UNKNOWN 0
#endif
