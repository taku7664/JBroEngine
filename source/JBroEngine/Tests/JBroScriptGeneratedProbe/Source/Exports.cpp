#include "Guard.cpp"

extern "C" __declspec(dllexport) void JBroGeneratedProbe_SetFirstValid(JBro::GameScriptBase* base) noexcept
{
    static_cast<GuardProbe*>(base)->target1 = JBro::Nullable<Link>(Link{});
}

extern "C" __declspec(dllexport) void JBroGeneratedProbe_CopyCounts(
    const JBro::GameScriptBase* base, JBro::Int* before, JBro::Int* after,
    JBro::Int* safeRuns, JBro::Int* valueBefore, JBro::Int* valueAfter) noexcept
{
    const auto& script = *static_cast<const GuardProbe*>(base);
    *before = script.before;
    *after = script.after;
    *safeRuns = script.safeRuns;
    *valueBefore = script.valueBefore;
    *valueAfter = script.valueAfter;
}

extern "C" __declspec(dllexport) void JBroGeneratedProbe_SetMaybeValid(JBro::GameScriptBase* base) noexcept
{
    static_cast<GuardProbe*>(base)->maybe = JBro::Nullable<JBro::Int>(JBro::Int(5));
}

extern "C" __declspec(dllexport) void JBroGeneratedProbe_AddValue(JBro::GameScriptBase* base, JBro::Int value)
{
    static_cast<GuardProbe*>(base)->values.Add(value);
}

extern "C" __declspec(dllexport) std::int64_t JBroGeneratedProbe_FirstValue(const JBro::GameScriptBase* base) noexcept
{
    return static_cast<const GuardProbe*>(base)->values[0].Get();
}

extern "C" __declspec(dllexport) void JBroGeneratedProbe_CopyIndexCounts(
    const JBro::GameScriptBase* base, JBro::Int* before, JBro::Int* after) noexcept
{
    const auto& script = *static_cast<const GuardProbe*>(base);
    *before = script.indexBefore;
    *after = script.indexAfter;
}

extern "C" __declspec(dllexport) void JBroGeneratedProbe_SetOptionalNumbers(JBro::GameScriptBase* base)
{
    auto& numbers = static_cast<GuardProbe*>(base)->optionalNumbers;
    numbers.value.Add(7);
    numbers.present = true;
}
