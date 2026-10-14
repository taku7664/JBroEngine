#include <JBro/ScriptApiCatalog2D/Internal/Declarations.h>

#if defined(JBRO_TEST_API_CATALOG_UNKNOWN_TYPE)
namespace
{
    struct MissingLanguageMapping {};
    const auto RejectedUnknownType = JBro::ScriptApiCatalog2D::Internal::TypeName<MissingLanguageMapping>();
}
#endif

#if defined(JBRO_TEST_API_CATALOG_PARAMETER_COUNT)
namespace JBro::ScriptApiCatalog2D::Internal
{
    template<> struct LanguageType<void> { static String Name() { return "Void"; } };
    template<> struct LanguageType<JBro::Float> { static String Name() { return "Float"; } };
    void OneParameter(JBro::Float) {}
    const auto RejectedParameterCount = DeclareFunction<&OneParameter>("OneParameter", Parameters());
}
#endif
