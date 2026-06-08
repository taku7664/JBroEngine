#include "pch.h"
#include "Application.h"

#if JBRO_PLATFORM_ANDROID

#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Input/InputSystem.h"
#include "Engine/Core/Input/InputTypes.h"          // ETouchPhase
#include "Engine/Core/Platform/Mobile/MobilePlatform.h"

#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>

namespace
{
	constexpr const char* kLogTag = "JBroEngine";

	struct AndroidAppState
	{
		CGameApplication Application;
		bool             Initialized = false;
		bool             HasWindow   = false;
	};

	CMobilePlatform* GetMobilePlatform()
	{
		// Android 런타임의 플랫폼은 항상 CMobilePlatform 이다.
		return static_cast<CMobilePlatform*>(Engine.Platform.TryGet());
	}

	ETouchPhase ToTouchPhase(std::int32_t actionMasked)
	{
		switch (actionMasked)
		{
		case AMOTION_EVENT_ACTION_DOWN:
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			return ETouchPhase::Began;
		case AMOTION_EVENT_ACTION_UP:
		case AMOTION_EVENT_ACTION_POINTER_UP:
			return ETouchPhase::Ended;
		case AMOTION_EVENT_ACTION_CANCEL:
			return ETouchPhase::Cancelled;
		case AMOTION_EVENT_ACTION_MOVE:
		default:
			return ETouchPhase::Moved;
		}
	}

	void InjectMotionPointer(AInputEvent* event, std::size_t pointerIndex, ETouchPhase phase)
	{
		CMobilePlatform* platform = GetMobilePlatform();
		if (nullptr == platform)
		{
			return;
		}

		const std::int32_t pointerId = AMotionEvent_getPointerId(event, pointerIndex);
		const int x = static_cast<int>(AMotionEvent_getX(event, pointerIndex));
		const int y = static_cast<int>(AMotionEvent_getY(event, pointerIndex));
		platform->InjectTouch(pointerId, x, y, phase);
	}

	std::int32_t OnInputEvent(android_app* /*app*/, AInputEvent* event)
	{
		if (AINPUT_EVENT_TYPE_MOTION != AInputEvent_getType(event))
		{
			return 0;
		}

		const std::int32_t action = AMotionEvent_getAction(event);
		const std::int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
		const ETouchPhase phase = ToTouchPhase(actionMasked);

		switch (actionMasked)
		{
		case AMOTION_EVENT_ACTION_MOVE:
		case AMOTION_EVENT_ACTION_CANCEL:
		{
			// MOVE/CANCEL 은 활성 포인터 전체에 적용된다.
			const std::size_t pointerCount = AMotionEvent_getPointerCount(event);
			for (std::size_t i = 0; i < pointerCount; ++i)
			{
				InjectMotionPointer(event, i, phase);
			}
			break;
		}
		default:
		{
			// DOWN/UP/POINTER_DOWN/POINTER_UP 은 인덱스가 가리키는 단일 포인터.
			const std::size_t pointerIndex = static_cast<std::size_t>(
				(action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
			InjectMotionPointer(event, pointerIndex, phase);
			break;
		}
		}

		return 1;
	}

	void SyncSurfaceSizeFromWindow(android_app* app)
	{
		if (nullptr == app->window)
		{
			return;
		}
		CMobilePlatform* platform = GetMobilePlatform();
		if (nullptr == platform)
		{
			return;
		}
		const int width = ANativeWindow_getWidth(app->window);
		const int height = ANativeWindow_getHeight(app->window);
		platform->ResizeSurface(width, height);
	}

	void OnAppCmd(android_app* app, std::int32_t cmd)
	{
		AndroidAppState* state = static_cast<AndroidAppState*>(app->userData);
		if (nullptr == state)
		{
			return;
		}

		switch (cmd)
		{
		case APP_CMD_INIT_WINDOW:
			// 윈도우 준비됨 — 엔진 초기화 전이면 pending 으로 등록, 후면 서피스에 직접 주입.
			if (nullptr != app->window)
			{
				CMobilePlatform::SetPendingNativeWindow(app->window);
				state->HasWindow = true;
				if (state->Initialized)
				{
					if (CMobilePlatform* platform = GetMobilePlatform())
					{
						platform->SetNativeSurfaceHandle(app->window);
						SyncSurfaceSizeFromWindow(app);
					}
				}
			}
			break;

		case APP_CMD_TERM_WINDOW:
			state->HasWindow = false;
			CMobilePlatform::SetPendingNativeWindow(nullptr);
			if (CMobilePlatform* platform = GetMobilePlatform())
			{
				platform->SetNativeSurfaceHandle(nullptr);
			}
			break;

		case APP_CMD_WINDOW_RESIZED:
		case APP_CMD_CONFIG_CHANGED:
			if (state->Initialized)
			{
				SyncSurfaceSizeFromWindow(app);
			}
			break;

		case APP_CMD_GAINED_FOCUS:
			if (CMobilePlatform* platform = GetMobilePlatform())
			{
				platform->SetFocus(true);
			}
			break;

		case APP_CMD_LOST_FOCUS:
			if (CMobilePlatform* platform = GetMobilePlatform())
			{
				platform->SetFocus(false);
			}
			break;

		case APP_CMD_PAUSE:
			if (CMobilePlatform* platform = GetMobilePlatform())
			{
				platform->NotifyPause();
			}
			break;

		case APP_CMD_RESUME:
			if (CMobilePlatform* platform = GetMobilePlatform())
			{
				platform->NotifyResume();
			}
			break;

		default:
			break;
		}
	}
}

// native_app_glue 진입점. android_main 은 glue 가 만든 전용 스레드에서 실행된다.
extern "C" void android_main(android_app* app)
{
	AndroidAppState state;
	app->userData = &state;
	app->onAppCmd = OnAppCmd;
	app->onInputEvent = OnInputEvent;

	while (true)
	{
		int events = 0;
		android_poll_source* source = nullptr;

		// 초기화 전(윈도우 대기) 에는 블로킹, 실행 중에는 논블로킹으로 프레임을 돌린다.
		const int timeoutMillis = state.Initialized ? 0 : -1;
		while (ALooper_pollOnce(timeoutMillis, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
		{
			if (nullptr != source)
			{
				source->process(app, source);
			}
			if (0 != app->destroyRequested)
			{
				break;
			}
		}

		if (0 != app->destroyRequested)
		{
			break;
		}

		// 윈도우가 준비되면 1회 초기화(이 시점에 ANativeWindow 가 있어야 Vulkan surface 생성 가능).
		if (state.HasWindow && false == state.Initialized)
		{
			if (state.Application.InitializeApplication())
			{
				state.Initialized = true;
				__android_log_print(ANDROID_LOG_INFO, kLogTag, "Application initialized.");
			}
			else
			{
				__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Application initialization failed.");
				state.Application.FinalizeApplication();
				ANativeActivity_finish(app->activity);
				break;
			}
		}

		// 프레임 진행.
		if (state.Initialized)
		{
			if (false == state.Application.TickApplication())
			{
				ANativeActivity_finish(app->activity);
				break;
			}
		}
	}

	if (state.Initialized)
	{
		state.Application.FinalizeApplication();
		state.Initialized = false;
	}
}

#endif // JBRO_PLATFORM_ANDROID
