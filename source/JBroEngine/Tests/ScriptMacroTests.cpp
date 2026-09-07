#include <JBro/Script/Macros.h>

// This translation unit intentionally includes no other headers: the marker is standalone.
namespace ScriptMacroProbe
{
    JBRO_SCRIPT(ForwardDeclared);

    JBRO_SCRIPT(ForwardDeclared)
    {
    public:
        constexpr int Value() const
        {
            return 17;
        }
    };

    JBRO_SCRIPT(Derived) final : public ForwardDeclared
    {
    };

    JBRO_SCRIPT(DefaultAccess)
    {
        int privateByDefault = 0;
    };

    template<typename T>
    concept HasPublicMarker = requires(T value)
    {
        value.privateByDefault;
    };

    static_assert(ForwardDeclared{}.Value() == 17);
    static_assert(Derived{}.Value() == 17);
    static_assert(false == HasPublicMarker<DefaultAccess>);
}
