#include <JBro/D3D12RHI/D3D12RHI.h>

#include <crtdbg.h>
#include <stdlib.h>

#include <exception>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>

JBro::Int32 RunCanvasFoundationTests();
JBro::Int32 RunCoreModelStressTests();
JBro::Int32 RunFrameMemoryTests();
JBro::Int32 RunLogTests();
JBro::Int32 RunProfilerTests();
JBro::Int32 RunCoreValueTypeTests();
JBro::Int32 RunReflectionShapeTests();
JBro::Int32 RunReflectionFieldTests();
JBro::Int32 RunPropertyRegistryTests();
JBro::Int32 RunReflectionCompoundTests();
JBro::Int32 RunReflectionContainerTests();
JBro::Int32 RunBuiltinComponentPropertyTests();
JBro::Int32 RunScriptSchedulingTests();
JBro::Int32 RunSpritePixelTests();
JBro::Int32 RunLight2DPixelTests();
JBro::Int32 RunLight2DFrameworkTests();
JBro::Int32 RunProjectFileTests();
JBro::Int32 RunYamlTests();
JBro::Int32 RunDelegateTests();
JBro::Int32 RunInterpolationTests();
JBro::Int32 RunFixedStringTests();
JBro::Int32 RunUuidTests();
JBro::Int32 RunAssetRegistryTests();
JBro::Int32 RunAssetSystemTests();
JBro::Int32 RunSpriteLibraryTests();
JBro::Int32 RunPhysics2DGeometryTests();
JBro::Int32 RunPhysics2DCollisionTests();
JBro::Int32 RunPhysics2DWorldTests();
JBro::Int32 RunPhysics2DSystemTests();
JBro::Int32 RunPolygonEditModelTests();
JBro::Int32 RunGameHostArgumentTests();
JBro::Int32 RunPlatformFileTests();
JBro::Int32 RunReflectedYamlTests();
JBro::Int32 RunCanvasFileTests();
JBro::Int32 RunRendererContractTests();
JBro::Int32 RunCameraView2DTests();
JBro::Int32 RunPlatformContractTests();
JBro::Int32 RunD3D12SmokeTests();
JBro::Int32 RunD3D11SmokeTests();
JBro::Int32 RunVulkanSmokeTests();
JBro::Int32 RunTextureBindingTests();
JBro::Int32 RunReferenceSafetyTests();
JBro::Int32 RunFramework2DSystemTests();
JBro::Int32 RunNetworkHostTests();
JBro::Int32 RunFramework3DSystemTests();
JBro::Int32 RunMeshPixelTests();
JBro::Int32 RunSystemSchedulerTests();
JBro::Int32 RunGameScriptTests();
JBro::Int32 RunEditorApplicationTests();
JBro::Int32 RunEditorUITests();
JBro::Int32 RunEditorCommandTests();
JBro::Int32 RunEditorObjectCommandTests();
JBro::Int32 RunEditorLocalizationTests();
JBro::Int32 RunEditorWidgetTests();
JBro::Int32 RunEditorNotificationTests();
JBro::Int32 RunEditorGuideFocusTests();
JBro::Int32 RunEditorGuideTests();
JBro::Int32 RunEditorControlPortTests();
JBro::Int32 RunEditorShortcutTests();
JBro::Int32 RunComponentMenuTableTests();
JBro::Int32 RunGizmoModelTests();
JBro::Int32 RunInputTests();
JBro::Int32 RunInputSystemTests();
JBro::Int32 RunInputChainTests();
JBro::Int32 RunInputActionTests();
JBro::Int32 RunSaveStorageTests();
JBro::Int32 RunPackageTests();
JBro::Int32 RunInputGamepadTests();
JBro::Int32 RunInputTouchTests();
JBro::Int32 RunContextBoundaryTests();
JBro::Int32 RunScriptApiPreludeTests();
JBro::Int32 RunPublicHeaderCompositionTests();
JBro::Int32 RunScriptDLLLoaderTests();
JBro::Int32 RunScriptCompilerLexerTests();
JBro::Int32 RunScriptCompilerParserTests();
JBro::Int32 RunScriptCompilerCommandLineTests();
JBro::Int32 RunRendererBenchmark();
JBro::Int32 RunAudioMixerTests();
JBro::Int32 RunAudioIntegrationTests();
JBro::Int32 RunTextLayoutTests();
JBro::Int32 RunGlyphAtlasTests();
JBro::Int32 RunTextRenderTests();
JBro::Int32 RunTaskManagerTests();
JBro::Int32 RunTimeTests();
JBro::Int32 RunDebugDrawTests();
JBro::Int32 RunEditorLoadingTests();

namespace
{
    // **묶음 고르기**(`JBRO_TESTS`). 비어 있으면 모든 묶음이다. 쉼표로 나눈 조각 중 하나라도 묶음 이름(`Run…Tests` 에서 `Run` 과 `Tests` 를 뺀 것)에
    // 들어 있으면 그 묶음이 돈다 - `JBRO_TESTS=Light2D,EditorGuide`, `JBRO_TESTS=Editor`. 고치는 동안 관련 묶음만 돌리는 길이다(전에는 임시 훅을 손으로 넣었다).
    JBro::String g_suiteFilter;
    JBro::Int32 g_suitesRun = 0;

    JBro::Bool WantsSuite(const char* name)
    {
        if (g_suiteFilter.empty())
        {
            return true;
        }
        std::size_t start = 0;
        while (start <= g_suiteFilter.size())
        {
            std::size_t end = g_suiteFilter.find(',', start);
            if (end == JBro::String::npos)
            {
                end = g_suiteFilter.size();
            }
            const JBro::String piece = g_suiteFilter.substr(start, end - start);
            if (false == piece.empty() && std::strstr(name, piece.c_str()) != nullptr)
            {
                return true;
            }
            start = end + 1;
        }
        return false;
    }

    // 고른 묶음이면 돌리고 걸린 시간을 적는다(`[suite] 이름 초`). 고르지 않은 묶음은 지나간다. 묶음이 실패하면 거짓이다.
    JBro::Bool RunSuite(const char* name, JBro::Int32 (*runner)())
    {
        if (false == WantsSuite(name))
        {
            return true;
        }
        ++g_suitesRun;
        const auto started = std::chrono::steady_clock::now();
        const JBro::Int32 result = runner();
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
        // 글자로 만들어 찍는다 - `std::cout` 의 정밀도를 바꾸면 뒤의 시험이 찍는 숫자까지 바뀐다.
        char line[160] = {};
        std::snprintf(line, sizeof(line), "[suite] %s %.1f s", name, elapsed.count());
        std::cout << line << std::endl;
        return result == 0;
    }

    JBro::String ReadEnvironment(const char* name)
    {
        char* value = nullptr;
        std::size_t length = 0;
        JBro::String text;
        if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
        {
            text = value;
            free(value);
        }
        return text;
    }
}

int main()
{
    // **단언이 대화상자를 띄우면 안 된다.** 기본값은 "무시/다시 시도/취소" 창을
    // 띄우고 사람이 누를 때까지 기다린다 - 사람 없이 도는 자리에서는 그것이
    // 영원한 멈춤이고, 실패한 것과 멈춘 것을 구분할 수 없게 된다.
    // ImGui 의 IM_ASSERT 도 이 길을 탄다.
    _set_error_mode(_OUT_TO_STDERR);
    for (JBro::Int32 report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // **첫 D3D12 디바이스가 생기기 전에 켜야 한다.** 디버그 레이어는 프로세스 단위라
    // 디바이스가 하나라도 만들어진 뒤에 켜면 조용히 무시된다 - 그러면 검증을 켠 줄 알고
    // "아무 말도 없으니 맞다" 고 믿게 된다. 그래서 다른 무엇보다 먼저 여기서 켠다.
    // `JBRO_BENCH` 가 있으면 테스트 대신 렌더러 벤치마크만 돈다(D-110). 검증 레이어는 켜지 않는다 - 시간을 재는 자리다.
    char* bench = nullptr;
    std::size_t benchLength = 0;
    if (_dupenv_s(&bench, &benchLength, "JBRO_BENCH") == 0 && bench != nullptr)
    {
        free(bench);
        return RunRendererBenchmark();
    }
    g_suiteFilter = ReadEnvironment("JBRO_TESTS");
    // GPU 기반 검증은 기본으로 켠다(D-64). `JBRO_GPU_VALIDATION=0` 이면 디버그 레이어만 켠다 - 시간을 재거나 그래픽과 상관없는 묶음을 고칠 때다.
    const JBro::Bool gpuValidation = ReadEnvironment("JBRO_GPU_VALIDATION") != "0";
    if (false == gpuValidation)
    {
        std::cout << "note: GPU-based D3D12 validation is off (JBRO_GPU_VALIDATION=0)" << std::endl;
    }
    if (false == JBro::EnableD3D12ValidationForProcess(gpuValidation))
    {
        std::cout << "note: no D3D12 debug layer here; "
            << "graphics tests cannot check for validation errors" << std::endl;
    }

    try
    {
        // 컴파일러 테스트는 그래픽도 파일 시스템도 거의 쓰지 않아 몇 초 안에 끝난다. 앞에 두어
        // 틀렸을 때 뒤의 긴 테스트를 기다리지 않게 한다(뮤테이션 한 개가 몇 분에서 몇 초로 준다).
        if (false == RunSuite("Delegate", &RunDelegateTests))
        {
            return 1;
        }
        if (false == RunSuite("Interpolation", &RunInterpolationTests))
        {
            return 1;
        }
        if (false == RunSuite("FixedString", &RunFixedStringTests))
        {
            return 1;
        }
        if (false == RunSuite("Time", &RunTimeTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptCompilerLexer", &RunScriptCompilerLexerTests))
        {
            return 1;
        }
        if (false == RunSuite("Physics2DGeometry", &RunPhysics2DGeometryTests))
        {
            return 1;
        }
        if (false == RunSuite("Physics2DCollision", &RunPhysics2DCollisionTests))
        {
            return 1;
        }
        if (false == RunSuite("Physics2DWorld", &RunPhysics2DWorldTests))
        {
            return 1;
        }
        if (false == RunSuite("Physics2DSystem", &RunPhysics2DSystemTests))
        {
            return 1;
        }
        if (false == RunSuite("PolygonEditModel", &RunPolygonEditModelTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptCompilerParser", &RunScriptCompilerParserTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptCompilerCommandLine", &RunScriptCompilerCommandLineTests))
        {
            return 1;
        }
        // 오디오 믹서는 장치 없이 몇 초 안에 끝난다(audio-plan §3-1).
        // 입력 상태 접기는 창 하나만 쓰고 1 초 안에 끝난다(D-214). 앞에 두어 뮤테이션이 빨리 돈다.
        if (false == RunSuite("InputSystem", &RunInputSystemTests))
        {
            return 1;
        }
        if (false == RunSuite("InputChain", &RunInputChainTests))
        {
            return 1;
        }
        if (false == RunSuite("InputAction", &RunInputActionTests))
        {
            return 1;
        }
        if (false == RunSuite("SaveStorage", &RunSaveStorageTests))
        {
            return 1;
        }
        if (false == RunSuite("Package", &RunPackageTests))
        {
            return 1;
        }
        if (false == RunSuite("InputGamepad", &RunInputGamepadTests))
        {
            return 1;
        }
        if (false == RunSuite("InputTouch", &RunInputTouchTests))
        {
            return 1;
        }
        if (false == RunSuite("AudioMixer", &RunAudioMixerTests))
        {
            return 1;
        }
        if (false == RunSuite("AudioIntegration", &RunAudioIntegrationTests))
        {
            return 1;
        }
        // 텍스트 커널은 그래픽도 파일도 쓰지 않는다(text-plan §5 의 1 단계). 앞에 두어 뮤테이션이 빨리 끝나게 한다.
        if (false == RunSuite("TextLayout", &RunTextLayoutTests))
        {
            return 1;
        }
        if (false == RunSuite("GlyphAtlas", &RunGlyphAtlasTests))
        {
            return 1;
        }
        if (false == RunSuite("TextRender", &RunTextRenderTests))
        {
            return 1;
        }
        // 태스크 관리자는 몇 초 안에 끝난다(D-209).
        if (false == RunSuite("TaskManager", &RunTaskManagerTests))
        {
            return 1;
        }
        // 에디터 로딩과 상태 표시줄(D-236). 에디터를 두 번 띄울 뿐이라 앞에 둔다.
        if (false == RunSuite("EditorLoading", &RunEditorLoadingTests))
        {
            return 1;
        }
        // 에셋 시스템도 몇 초다. 워커 로드(D-236)의 뮤테이션이 스위트 끝까지 기다리지 않게 여기로 당겼다.
        if (false == RunSuite("AssetSystem", &RunAssetSystemTests))
        {
            return 1;
        }
        if (false == RunSuite("CanvasFoundation", &RunCanvasFoundationTests))
        {
            return 1;
        }
        if (false == RunSuite("CoreModelStress", &RunCoreModelStressTests))
        {
            return 1;
        }
        if (false == RunSuite("FrameMemory", &RunFrameMemoryTests))
        {
            return 1;
        }
        if (false == RunSuite("Log", &RunLogTests))
        {
            return 1;
        }
        if (false == RunSuite("Profiler", &RunProfilerTests))
        {
            return 1;
        }
        if (false == RunSuite("CoreValueType", &RunCoreValueTypeTests))
        {
            return 1;
        }
        if (false == RunSuite("ReflectionShape", &RunReflectionShapeTests))
        {
            return 1;
        }
        if (false == RunSuite("ReflectionField", &RunReflectionFieldTests))
        {
            return 1;
        }
        if (false == RunSuite("PropertyRegistry", &RunPropertyRegistryTests))
        {
            return 1;
        }
        if (false == RunSuite("ReflectionCompound", &RunReflectionCompoundTests))
        {
            return 1;
        }
        if (false == RunSuite("ReflectionContainer", &RunReflectionContainerTests))
        {
            return 1;
        }
        if (false == RunSuite("BuiltinComponentProperty", &RunBuiltinComponentPropertyTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptScheduling", &RunScriptSchedulingTests))
        {
            return 1;
        }
        if (false == RunSuite("RendererContract", &RunRendererContractTests))
        {
            return 1;
        }
        if (false == RunSuite("CameraView2D", &RunCameraView2DTests))
        {
            return 1;
        }
        if (false == RunSuite("PlatformContract", &RunPlatformContractTests))
        {
            return 1;
        }
        if (false == RunSuite("D3D12Smoke", &RunD3D12SmokeTests))
        {
            return 1;
        }
        if (false == RunSuite("D3D11Smoke", &RunD3D11SmokeTests))
        {
            return 1;
        }
        if (false == RunSuite("VulkanSmoke", &RunVulkanSmokeTests))
        {
            return 1;
        }
        if (false == RunSuite("TextureBinding", &RunTextureBindingTests))
        {
            return 1;
        }
        if (false == RunSuite("SpritePixel", &RunSpritePixelTests))
        {
            return 1;
        }
        if (false == RunSuite("Light2DPixel", &RunLight2DPixelTests))
        {
            return 1;
        }
        if (false == RunSuite("Light2DFramework", &RunLight2DFrameworkTests))
        {
            return 1;
        }
        if (false == RunSuite("ReferenceSafety", &RunReferenceSafetyTests))
        {
            return 1;
        }
        if (false == RunSuite("Framework2DSystem", &RunFramework2DSystemTests))
        {
            return 1;
        }
        if (false == RunSuite("NetworkHost", &RunNetworkHostTests))
        {
            return 1;
        }
        if (false == RunSuite("Framework3DSystem", &RunFramework3DSystemTests))
        {
            return 1;
        }
        if (false == RunSuite("MeshPixel", &RunMeshPixelTests))
        {
            return 1;
        }
        // 디버그 드로는 GPU 를 쓰므로 다른 픽셀 시험 옆이다. 앞에 두면 `InputTouchTests` 의 포인터 시험이 깨진다(time-plan §4).
        if (false == RunSuite("DebugDraw", &RunDebugDrawTests))
        {
            return 1;
        }
        if (false == RunSuite("SystemScheduler", &RunSystemSchedulerTests))
        {
            return 1;
        }
        if (false == RunSuite("GameScript", &RunGameScriptTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorApplication", &RunEditorApplicationTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorUI", &RunEditorUITests))
        {
            return 1;
        }
        if (false == RunSuite("EditorCommand", &RunEditorCommandTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorObjectCommand", &RunEditorObjectCommandTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorLocalization", &RunEditorLocalizationTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorWidget", &RunEditorWidgetTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorNotification", &RunEditorNotificationTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorGuideFocus", &RunEditorGuideFocusTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorGuide", &RunEditorGuideTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorControlPort", &RunEditorControlPortTests))
        {
            return 1;
        }
        if (false == RunSuite("EditorShortcut", &RunEditorShortcutTests))
        {
            return 1;
        }
        if (false == RunSuite("ComponentMenuTable", &RunComponentMenuTableTests))
        {
            return 1;
        }
        if (false == RunSuite("GizmoModel", &RunGizmoModelTests))
        {
            return 1;
        }
        if (false == RunSuite("Input", &RunInputTests))
        {
            return 1;
        }
        if (false == RunSuite("ContextBoundary", &RunContextBoundaryTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptApiPrelude", &RunScriptApiPreludeTests))
        {
            return 1;
        }
        if (false == RunSuite("PublicHeaderComposition", &RunPublicHeaderCompositionTests))
        {
            return 1;
        }
        if (false == RunSuite("ScriptDLLLoader", &RunScriptDLLLoaderTests))
        {
            return 1;
        }
        if (false == RunSuite("ProjectFile", &RunProjectFileTests))
        {
            return 1;
        }
        if (false == RunSuite("Yaml", &RunYamlTests))
        {
            return 1;
        }
        if (false == RunSuite("Uuid", &RunUuidTests))
        {
            return 1;
        }
        if (false == RunSuite("PlatformFile", &RunPlatformFileTests))
        {
            return 1;
        }
        if (false == RunSuite("AssetRegistry", &RunAssetRegistryTests))
        {
            return 1;
        }
        if (false == RunSuite("SpriteLibrary", &RunSpriteLibraryTests))
        {
            return 1;
        }
        if (false == RunSuite("GameHostArgument", &RunGameHostArgumentTests))
        {
            return 1;
        }
        if (false == RunSuite("ReflectedYaml", &RunReflectedYamlTests))
        {
            return 1;
        }
        if (false == RunSuite("CanvasFile", &RunCanvasFileTests))
        {
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << "\n";
        return 1;
    }

    // 고른 묶음만 돌았으면 "all tests passed." 라고 하지 않는다 - 그 줄은 전체가 지났다는 표시다.
    if (false == g_suiteFilter.empty())
    {
        if (g_suitesRun == 0)
        {
            std::cout << "test failure: JBRO_TESTS=" << g_suiteFilter << " matches no suite\n";
            return 1;
        }
        std::cout << "selected tests passed (" << g_suitesRun << " suites).\n";
        return 0;
    }
    std::cout << "all tests passed.\n";
    return 0;
}
