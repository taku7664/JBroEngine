#include <JBro/Types/SafePtr.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    class SafeTarget final : public JBro::EnableSafeFromThis<SafeTarget>
    {
    public:
        explicit SafeTarget(int value)
            : Value(value)
        {
        }

        int Value = 0;
    };

    void TestSafePtrExpiresWithOwner()
    {
        JBro::SafePtr<SafeTarget> safe;
        {
            JBro::OwnerPtr<SafeTarget> owner = JBro::MakeOwnerPtr<SafeTarget>(42);
            safe = owner->SafeFromThis();
            Check(safe.IsValid(), "SafePtr must be valid while its owner is alive");
            Check(safe->Value == 42, "SafePtr must access the owned object");
        }

        Check(false == safe.IsValid(), "SafePtr must expire after owner destruction");
        Check(safe.TryGet() == nullptr, "expired SafePtr must return nullptr");
    }
}

int RunReferenceSafetyTests()
{
    TestSafePtrExpiresWithOwner();
    std::cout << "Reference safety tests passed.\n";
    return 0;
}
