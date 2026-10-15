#pragma once

// These named exports have C++ callers and share engine value-type ABI.
#pragma warning(push)
#pragma warning(disable: 4190)
#ifdef JBRO_GENERATED_PROBE_EXPORTS
#define JBRO_PROBE_EXPORT __declspec(dllexport)
#else
#define JBRO_PROBE_EXPORT
#endif
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
namespace JBro
{
    class GameScriptBase;
}
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_OwnedValues();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_NullableFailure();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_CustomOverrides();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_StaticStartup();
extern "C" JBRO_PROBE_EXPORT JBro::Float JBroGeneratedProbe_StaticService();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_InitializationGuard();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_InitializationCallback(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_InitializationMode(JBro::Int64 mode);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_InitializerScore();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_KeywordScore();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_DerivedRead();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_RequiredRead(JBro::GameScriptBase* script);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_BorrowedInterfaceRead();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_EqualityScore();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_MultipleBaseProperties();
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ChildResult(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlCount(JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlRhsCount(JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_ControlActive(JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_ControlAddScore(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_ControlRaiseScores(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_ControlHasOwner(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_ControlHasOwnerExplicit(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_ControlHasTransform(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_ControlHasNoTransformByEquality(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_MissingScriptRefByEquality(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_OwnerIdentityEqual(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_TransformIdentityEqual(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_DerivedScriptReferenceIdentity(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Bool JBroGeneratedProbe_ControlHasTransformSafe(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_ControlDisableOwnerSafe(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlNullFallback(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlGenericArray(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlOwnCount(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlScoreByName(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Float JBroGeneratedProbe_ControlBrighten(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_ControlRenameOwner(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Float JBroGeneratedProbe_ControlMoveOwner(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlUseCounter(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_ControlSwitchScalars(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Float JBroGeneratedProbe_ControlVectorMath(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_SetFirstValid(JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_TouchFirst(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_TouchFirstSafe(JBro::GameScriptBase* base);
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_CopyTouchCounts(
    const JBro::GameScriptBase* base, JBro::Int* before, JBro::Int* after,
    JBro::Int* lookups) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_CopyCounts(
    const JBro::GameScriptBase* base, JBro::Int* before, JBro::Int* after,
    JBro::Int* safeRuns, JBro::Int* valueBefore, JBro::Int* valueAfter) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_SetMaybeValid(JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_AddValue(JBro::GameScriptBase* base, JBro::Int value);
extern "C" JBRO_PROBE_EXPORT JBro::Int64 JBroGeneratedProbe_FirstValue(const JBro::GameScriptBase* base) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_CopyIndexCounts(
    const JBro::GameScriptBase* base, JBro::Int* before, JBro::Int* after) noexcept;
extern "C" JBRO_PROBE_EXPORT void JBroGeneratedProbe_SetOptionalNumbers(JBro::GameScriptBase* base);

#undef JBRO_PROBE_EXPORT
#pragma warning(pop)
