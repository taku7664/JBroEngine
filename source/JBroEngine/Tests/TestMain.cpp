#include <exception>
#include <iostream>

int RunCanvasFoundationTests();
int RunCoreModelStressTests();
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
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "all tests passed.\n";
    return 0;
}
