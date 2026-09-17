#include <JBro/ScriptCompiler/SourceText.h>

#include <utility>

namespace JBro::ScriptCompiler
{
    SourceText::SourceText(String path, String text)
        : m_path(std::move(path))
        , m_text(std::move(text))
    {
    }
}
