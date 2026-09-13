#include <exception>
#include <iostream>

int RunCanvasFoundationTests();
int RunCoreModelStressTests();
int RunFrameMemoryTests();
int RunReflectionShapeTests();
int RunReflectionFieldTests();
int RunPropertyRegistryTests();
int RunReflectionCompoundTests();
int RunBuiltinComponentPropertyTests();
int RunScriptSchedulingTests();
int RunSpritePixelTests();
int RunProjectFileTests();
int RunRendererContractTests();
int RunPlatformContractTests();
int RunD3D12SmokeTests();
int RunReferenceSafetyTests();
int RunFramework2DSystemTests();
int RunSystemSchedulerTests();
int RunGameScriptTests();
int RunEditorApplicationTests();
int RunContextBoundaryTests();
int RunScriptApiPreludeTests();
int RunPublicHeaderCompositionTests();
int RunScriptDLLLoaderTests();

int main()
{
    try
    {
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
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "all tests passed.\n";
    return 0;
}
