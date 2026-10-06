#pragma once

#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>

namespace JBro::Network::Testing
{
    inline void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }
}
