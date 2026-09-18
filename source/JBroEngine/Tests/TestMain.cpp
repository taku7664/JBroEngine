#include <JBro/D3D12RHI/D3D12RHI.h>

#include <crtdbg.h>
#include <stdlib.h>

#include <exception>
#include <cstdlib>
#include <iostream>

int RunCanvasFoundationTests();
int RunCoreModelStressTests();
int RunFrameMemoryTests();
int RunReflectionShapeTests();
int RunReflectionFieldTests();
int RunPropertyRegistryTests();
int RunReflectionCompoundTests();
int RunReflectionContainerTests();
int RunBuiltinComponentPropertyTests();
int RunScriptSchedulingTests();
int RunSpritePixelTests();
int RunProjectFileTests();
int RunYamlTests();
int RunUuidTests();
int RunAssetRegistryTests();
int RunReflectedYamlTests();
int RunCanvasFileTests();
int RunRendererContractTests();
int RunPlatformContractTests();
int RunD3D12SmokeTests();
int RunD3D11SmokeTests();
int RunVulkanSmokeTests();
int RunTextureBindingTests();
int RunReferenceSafetyTests();
int RunFramework2DSystemTests();
int RunFramework3DSystemTests();
int RunMeshPixelTests();
int RunSystemSchedulerTests();
int RunGameScriptTests();
int RunEditorApplicationTests();
int RunEditorUITests();
int RunEditorCommandTests();
int RunEditorObjectCommandTests();
int RunEditorLocalizationTests();
int RunEditorWidgetTests();
int RunGizmoModelTests();
int RunInputTests();
int RunContextBoundaryTests();
int RunScriptApiPreludeTests();
int RunPublicHeaderCompositionTests();
int RunScriptDLLLoaderTests();
int RunScriptCompilerLexerTests();
int RunScriptCompilerParserTests();
int RunScriptCompilerCommandLineTests();
int RunRendererBenchmark();

int main()
{
    // **단언이 대화상자를 띄우면 안 된다.** 기본값은 "무시/다시 시도/취소" 창을
    // 띄우고 사람이 누를 때까지 기다린다 - 사람 없이 도는 자리에서는 그것이
    // 영원한 멈춤이고, 실패한 것과 멈춘 것을 구분할 수 없게 된다.
    // ImGui 의 IM_ASSERT 도 이 길을 탄다.
    _set_error_mode(_OUT_TO_STDERR);
    for (int report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
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
        if (RunScriptCompilerLexerTests() != 0)
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
        if (RunReferenceSafetyTests() != 0)
        {
            return 1;
        }
        if (RunFramework2DSystemTests() != 0)
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
        if (RunAssetRegistryTests() != 0)
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
