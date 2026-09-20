#pragma once

#include <iostream>
#include <stdexcept>

namespace JBro::Network::Testing
{
    inline void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }
}
