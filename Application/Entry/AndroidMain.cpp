#include "pch.h"
#include "Application.h"

#if JBRO_PLATFORM_ANDROID

#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Input/InputSystem.h"
#include "Engine/Core/Input/InputTypes.h"          // ETouchPhase
#include "Engine/Core/Platform/Mobile/MobilePlatform.h"

#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>

#include <unistd.h>   // chdir

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	constexpr const char* kLogTag = "JBroEngine";

	struct AndroidAppState
	{
		CGameApplication Application;
		bool             Initialized     = false;
		bool             AssetsExtracted = false;
	};

	CMobilePlatform* GetMobilePlatform()
	{
		// Android 런타임의 플랫폼은 항상 CMobilePlatform 이다.
		return static_cast<CMobilePlatform*>(Engine.Platform.TryGet());
	}

	// Content 아래 단일 asset(relPath)을 내부 저장소로 추출. 같은 크기면 스킵.
	// 반환: 추출됐거나 이미 최신이면 true.
	bool ExtractOneApkAsset(AAssetManager* assetManager, const std::string& relPath,
		const std::filesystem::path& destContentDir, std::vector<char>& buffer)
	{
		const std::string assetPath = std::string("Content/") + relPath;
		AAsset* asset = AAssetManager_open(assetManager, assetPath.c_str(), AASSET_MODE_STREAMING);
		if (nullptr == asset)
		{
			__android_log_print(ANDROID_LOG_WARN, kLogTag, "Asset extraction: cannot open %s", assetPath.c_str());
			return false;
		}

		const std::filesystem::path destPath = destContentDir / relPath;
		const std::uintmax_t assetLength = static_cast<std::uintmax_t>(AAsset_getLength64(asset));

		std::error_code dirEc;
		std::filesystem::create_directories(destPath.parent_path(), dirEc);

		// 이미 같은 크기로 추출돼 있으면 다시 쓰지 않는다(재실행/대용량 팩 시작 비용 절감).
		std::error_code statEc;
		if (std::filesystem::exists(destPath, statEc) && !statEc &&
			std::filesystem::file_size(destPath, statEc) == assetLength && !statEc)
		{
			AAsset_close(asset);
			return true;
		}

		std::FILE* outFile = std::fopen(destPath.string().c_str(), "wb");
		if (nullptr == outFile)
		{
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset extraction: cannot write %s", destPath.string().c_str());
			AAsset_close(asset);
			return false;
		}

		int readBytes = 0;
		while ((readBytes = AAsset_read(asset, buffer.data(), buffer.size())) > 0)
		{
			std::fwrite(buffer.data(), 1, static_cast<std::size_t>(readBytes), outFile);
		}
		std::fclose(outFile);
		AAsset_close(asset);
		__android_log_print(ANDROID_LOG_INFO, kLogTag, "Asset extracted: %s", relPath.c_str());
		return true;
	}

	// APK 안 assets/Content/** 를 앱 내부 저장소로 추출한다.
	// 엔진의 에셋/매니페스트 로더는 std::filesystem 기반(일반 파일)인데, APK 내부 asset 은
	// 일반 파일이 아니라 AAssetManager 로만 읽힌다(웹은 emscripten --preload-file 로 가상FS 에
	// 풀려 공짜로 해결되지만 Android 엔 그 단계가 없다). 추출 후 chdir 하면 current_path()
	// 기준 경로(Content/build_manifest.jbmanifest 등)가 그대로 동작한다.
	// AAssetManager 는 디렉토리 재귀 열거를 지원하지 않으므로, 패키지가 생성한
	// _assetindex.txt(상대 경로 목록)을 읽어 중첩 디렉토리까지 추출한다. 인덱스가 없으면
	// Content/ 최상위 파일만 추출하는 폴백을 쓴다.
	bool ExtractApkContentAssets(android_app* app)
	{
		if (nullptr == app->activity || nullptr == app->activity->assetManager ||
			nullptr == app->activity->internalDataPath)
		{
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset extraction: activity/assetManager/internalDataPath unavailable.");
			return false;
		}

		AAssetManager* assetManager = app->activity->assetManager;
		const std::filesystem::path internalRoot = app->activity->internalDataPath;
		const std::filesystem::path destContentDir = internalRoot / "Content";

		std::error_code errorCode;
		std::filesystem::create_directories(destContentDir, errorCode);
		if (errorCode)
		{
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset extraction: create_directories failed: %s", errorCode.message().c_str());
			return false;
		}

		std::vector<char> buffer(64 * 1024);
		int availableCount = 0;

		AAsset* indexAsset = AAssetManager_open(assetManager, "Content/_assetindex.txt", AASSET_MODE_BUFFER);
		if (nullptr != indexAsset)
		{
			const std::size_t indexLength = static_cast<std::size_t>(AAsset_getLength64(indexAsset));
			std::string indexText(indexLength, '\0');
			if (indexLength > 0)
			{
				AAsset_read(indexAsset, indexText.data(), indexLength);
			}
			AAsset_close(indexAsset);

			std::size_t start = 0;
			while (start < indexText.size())
			{
				std::size_t newline = indexText.find('\n', start);
				const std::size_t end = (std::string::npos == newline) ? indexText.size() : newline;
				std::string line = indexText.substr(start, end - start);
				start = (std::string::npos == newline) ? indexText.size() : (newline + 1);

				while (false == line.empty() && ('\r' == line.back() || ' ' == line.back() || '\t' == line.back()))
				{
					line.pop_back();
				}
				if (line.empty() || "_assetindex.txt" == line)
				{
					continue;
				}
				if (ExtractOneApkAsset(assetManager, line, destContentDir, buffer))
				{
					++availableCount;
				}
			}
		}
		else
		{
			// 폴백: 인덱스 없음 → Content/ 최상위 파일만 평면 열거.
			AAssetDir* assetDir = AAssetManager_openDir(assetManager, "Content");
			if (nullptr == assetDir)
			{
				__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset extraction: APK has no Content/ asset directory.");
				return false;
			}
			const char* fileName = nullptr;
			while (nullptr != (fileName = AAssetDir_getNextFileName(assetDir)))
			{
				if (std::string("_assetindex.txt") == fileName)
				{
					continue;
				}
				if (ExtractOneApkAsset(assetManager, fileName, destContentDir, buffer))
				{
					++availableCount;
				}
			}
			AAssetDir_close(assetDir);
		}

		if (0 != chdir(internalRoot.string().c_str()))
		{
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset extraction: chdir to internal data path failed.");
			return false;
		}

		__android_log_print(ANDROID_LOG_INFO, kLogTag, "Asset extraction complete: %d file(s) available, cwd=%s", availableCount, internalRoot.string().c_str());
		return availableCount > 0;
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

	// 윈도우가 준비된 직후(INIT_WINDOW) 1회 초기화한다. 엔진 초기화는 cmd 핸들러 안에서
	// 수행해야 한다 — android_main 의 ALooper_pollOnce(-1) 는 블로킹이라, 메인 루프 뒤에서
	// 초기화하면 이벤트가 없을 때 폴에 갇혀 영영 도달하지 못한다.
	void EnsureInitialized(android_app* app, AndroidAppState* state)
	{
		if (state->Initialized || nullptr == app->window)
		{
			return;
		}

		CMobilePlatform::SetPendingNativeWindow(app->window);

		// 엔진 초기화 전에 APK assets 를 내부 저장소로 추출 + chdir(파일 기반 로더가 읽도록).
		if (false == state->AssetsExtracted)
		{
			ExtractApkContentAssets(app);
			state->AssetsExtracted = true;
		}

		if (state->Application.InitializeApplication())
		{
			state->Initialized = true;
			SyncSurfaceSizeFromWindow(app);
			__android_log_print(ANDROID_LOG_INFO, kLogTag, "Application initialized.");
		}
		else
		{
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Application initialization failed.");
			state->Application.FinalizeApplication();
			ANativeActivity_finish(app->activity);
		}
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
			// 윈도우 준비됨. 최초면 여기서 엔진을 초기화하고(Vulkan surface 가 ANativeWindow 를
			// 요구), 재생성(term 후 복귀)이면 서피스 핸들만 다시 주입한다.
			if (nullptr != app->window)
			{
				if (state->Initialized)
				{
					CMobilePlatform::SetPendingNativeWindow(app->window);
					if (CMobilePlatform* platform = GetMobilePlatform())
					{
						platform->SetNativeSurfaceHandle(app->window);
						SyncSurfaceSizeFromWindow(app);
					}
				}
				else
				{
					EnsureInitialized(app, state);
				}
			}
			break;

		case APP_CMD_TERM_WINDOW:
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

		// 삼항을 호출 인자에 직접 두어 매 반복 재평가한다. 초기화 전에는 블로킹(-1) 으로
		// 이벤트를 기다리고(INIT_WINDOW 가 EnsureInitialized 를 호출해 Initialized 를 켠다),
		// 초기화 후에는 논블로킹(0) 이라 큐가 비면 즉시 빠져나와 프레임을 돈다.
		while (ALooper_pollOnce(state.Initialized ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
		{
			if (nullptr != source)
			{
				source->process(app, source);
			}
			if (0 != app->destroyRequested)
			{
				if (state.Initialized)
				{
					state.Application.FinalizeApplication();
					state.Initialized = false;
				}
				return;
			}
		}

		// 프레임 진행.
		if (state.Initialized)
		{
			if (false == state.Application.TickApplication())
			{
				state.Application.FinalizeApplication();
				state.Initialized = false;
				ANativeActivity_finish(app->activity);
				return;
			}
		}
	}
}

#endif // JBRO_PLATFORM_ANDROID
