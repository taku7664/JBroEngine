#pragma once

#include <JBro/Types/String.h>

namespace JBro::ScriptCompiler::Internal
{
    // Qualified type syntax has already been parsed. Discard only its trivia;
    // source ranges and original spelling stay in the syntax tree.
    inline String QualifiedName(std::string_view spelling)
    {
        String name;
        for (std::size_t index = 0; index < spelling.size(); ++index)
        {
            const char character = spelling[index];
            if (character == '/' && index + 1 < spelling.size() && spelling[index + 1] == '/')
            {
                while (index < spelling.size() && spelling[index] != '\n')
                {
                    ++index;
                }
            }
            else if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
            {
                name += character;
            }
        }
        return name;
    }
}
