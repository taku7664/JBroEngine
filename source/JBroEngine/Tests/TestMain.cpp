#include <JBro/D3D12RHI/D3D12RHI.h>

#include <crtdbg.h>
#include <stdlib.h>

#include <exception>
#include <cstdlib>
#include <iostream>
#include <JBro/Types/Int.h>

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
    if (false == JBro::EnableD3D12ValidationForProcess())
    {
        std::cout << "note: no D3D12 debug layer here; "
            << "graphics tests cannot check for validation errors" << std::endl;
    }

    try
    {
        // 컴파일러 테스트는 그래픽도 파일 시스템도 거의 쓰지 않아 몇 초 안에 끝난다. 앞에 두어
        // 틀렸을 때 뒤의 긴 테스트를 기다리지 않게 한다(뮤테이션 한 개가 몇 분에서 몇 초로 준다).
        if (RunDelegateTests() != 0)
        {
            return 1;
        }
        if (RunInterpolationTests() != 0)
        {
            return 1;
        }
        if (RunFixedStringTests() != 0)
        {
            return 1;
        }
        if (RunTimeTests() != 0)
        {
            return 1;
        }
        if (RunScriptCompilerLexerTests() != 0)
        {
            return 1;
        }
        if (RunPhysics2DGeometryTests() != 0)
        {
            return 1;
        }
        if (RunPhysics2DCollisionTests() != 0)
        {
            return 1;
        }
        if (RunPhysics2DWorldTests() != 0)
        {
            return 1;
        }
        if (RunPhysics2DSystemTests() != 0)
        {
            return 1;
        }
        if (RunPolygonEditModelTests() != 0)
        {
            return 1;
        }
        if (RunScriptCompilerParserTests() != 0)
        {
            return 1;
        }
        if (RunScriptCompilerCommandLineTests() != 0)
        {
            return 1;
        }
        // 오디오 믹서는 장치 없이 몇 초 안에 끝난다(audio-plan §3-1).
        // 입력 상태 접기는 창 하나만 쓰고 1 초 안에 끝난다(D-214). 앞에 두어 뮤테이션이 빨리 돈다.
        if (RunInputSystemTests() != 0)
        {
            return 1;
        }
        if (RunInputChainTests() != 0)
        {
            return 1;
        }
        if (RunInputActionTests() != 0)
        {
            return 1;
        }
        if (RunSaveStorageTests() != 0)
        {
            return 1;
        }
        if (RunPackageTests() != 0)
        {
            return 1;
        }
        if (RunInputGamepadTests() != 0)
        {
            return 1;
        }
        if (RunInputTouchTests() != 0)
        {
            return 1;
        }
        if (RunAudioMixerTests() != 0)
        {
            return 1;
        }
        if (RunAudioIntegrationTests() != 0)
        {
            return 1;
        }
        // 텍스트 커널은 그래픽도 파일도 쓰지 않는다(text-plan §5 의 1 단계). 앞에 두어 뮤테이션이 빨리 끝나게 한다.
        if (RunTextLayoutTests() != 0)
        {
            return 1;
        }
        if (RunGlyphAtlasTests() != 0)
        {
            return 1;
        }
        if (RunTextRenderTests() != 0)
        {
            return 1;
        }
        // 태스크 관리자는 몇 초 안에 끝난다(D-209).
        if (RunTaskManagerTests() != 0)
        {
            return 1;
        }
        // 에디터 로딩과 상태 표시줄(D-236). 에디터를 두 번 띄울 뿐이라 앞에 둔다.
        if (RunEditorLoadingTests() != 0)
        {
            return 1;
        }
        // 에셋 시스템도 몇 초다. 워커 로드(D-236)의 뮤테이션이 스위트 끝까지 기다리지 않게 여기로 당겼다.
        if (RunAssetSystemTests() != 0)
        {
            return 1;
        }
        if (RunCanvasFoundationTests() != 0)
        {
            return 1;
        }
        if (RunCoreModelStressTests() != 0)
        {
            return 1;
        }
        if (RunFrameMemoryTests() != 0)
        {
            return 1;
        }
        if (RunLogTests() != 0)
        {
            return 1;
        }
        if (RunProfilerTests() != 0)
        {
            return 1;
        }
        if (RunCoreValueTypeTests() != 0)
        {
            return 1;
        }
        if (RunReflectionShapeTests() != 0)
        {
            return 1;
        }
        if (RunReflectionFieldTests() != 0)
        {
            return 1;
        }
        if (RunPropertyRegistryTests() != 0)
        {
            return 1;
        }
        if (RunReflectionCompoundTests() != 0)
        {
            return 1;
        }
        if (RunReflectionContainerTests() != 0)
        {
            return 1;
        }
        if (RunBuiltinComponentPropertyTests() != 0)
        {
            return 1;
        }
        if (RunScriptSchedulingTests() != 0)
        {
            return 1;
        }
        if (RunRendererContractTests() != 0)
        {
            return 1;
        }
        if (RunCameraView2DTests() != 0)
        {
            return 1;
        }
        if (RunPlatformContractTests() != 0)
        {
            return 1;
        }
        if (RunD3D12SmokeTests() != 0)
        {
            return 1;
        }
        if (RunD3D11SmokeTests() != 0)
        {
            return 1;
        }
        if (RunVulkanSmokeTests() != 0)
        {
            return 1;
        }
        if (RunTextureBindingTests() != 0)
        {
            return 1;
        }
        if (RunSpritePixelTests() != 0)
        {
            return 1;
        }
        if (RunLight2DPixelTests() != 0)
        {
            return 1;
        }
        if (RunReferenceSafetyTests() != 0)
        {
            return 1;
        }
        if (RunFramework2DSystemTests() != 0)
        {
            return 1;
        }
        if (RunNetworkHostTests() != 0)
        {
            return 1;
        }
        if (RunFramework3DSystemTests() != 0)
        {
            return 1;
        }
        if (RunMeshPixelTests() != 0)
        {
            return 1;
        }
        // 디버그 드로는 GPU 를 쓰므로 다른 픽셀 시험 옆이다. 앞에 두면 `InputTouchTests` 의 포인터 시험이 깨진다(time-plan §4).
        if (RunDebugDrawTests() != 0)
        {
            return 1;
        }
        if (RunSystemSchedulerTests() != 0)
        {
            return 1;
        }
        if (RunGameScriptTests() != 0)
        {
            return 1;
        }
        if (RunEditorApplicationTests() != 0)
        {
            return 1;
        }
        if (RunEditorUITests() != 0)
        {
            return 1;
        }
        if (RunEditorCommandTests() != 0)
        {
            return 1;
        }
        if (RunEditorObjectCommandTests() != 0)
        {
            return 1;
        }
        if (RunEditorLocalizationTests() != 0)
        {
            return 1;
        }
        if (RunEditorWidgetTests() != 0)
        {
            return 1;
        }
        if (RunEditorNotificationTests() != 0)
        {
            return 1;
        }
        if (RunEditorGuideFocusTests() != 0)
        {
            return 1;
        }
        if (RunEditorGuideTests() != 0)
        {
            return 1;
        }
        if (RunEditorControlPortTests() != 0)
        {
            return 1;
        }
        if (RunEditorShortcutTests() != 0)
        {
            return 1;
        }
        if (RunComponentMenuTableTests() != 0)
        {
            return 1;
        }
        if (RunGizmoModelTests() != 0)
        {
            return 1;
        }
        if (RunInputTests() != 0)
        {
            return 1;
        }
        if (RunContextBoundaryTests() != 0)
        {
            return 1;
        }
        if (RunScriptApiPreludeTests() != 0)
        {
            return 1;
        }
        if (RunPublicHeaderCompositionTests() != 0)
        {
            return 1;
        }
        if (RunScriptDLLLoaderTests() != 0)
        {
            return 1;
        }
        if (RunProjectFileTests() != 0)
        {
            return 1;
        }
        if (RunYamlTests() != 0)
        {
            return 1;
        }
        if (RunUuidTests() != 0)
        {
            return 1;
        }
        if (RunPlatformFileTests() != 0)
        {
            return 1;
        }
        if (RunAssetRegistryTests() != 0)
        {
            return 1;
        }
        if (RunSpriteLibraryTests() != 0)
        {
            return 1;
        }
        if (RunGameHostArgumentTests() != 0)
        {
            return 1;
        }
        if (RunReflectedYamlTests() != 0)
        {
            return 1;
        }
        if (RunCanvasFileTests() != 0)
        {
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "all tests passed.\n";
    return 0;
}
