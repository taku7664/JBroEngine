#include <exception>
#include <iostream>

int RunWorldCanvasFoundationTests();

int main()
{
    try
    {
        if (RunWorldCanvasFoundationTests() != 0) return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << "\n";
        return 1;
    }

    std::cout << "all tests passed.\n";
    return 0;
}
