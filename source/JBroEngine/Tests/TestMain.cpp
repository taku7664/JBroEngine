#include <exception>
#include <iostream>

int RunComponentPoolTests();
int RunWorldCanvasFoundationTests();

int main()
{
    try
    {
        if (RunComponentPoolTests() != 0)
        {
            return 1;
        }
        if (RunWorldCanvasFoundationTests() != 0)
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
