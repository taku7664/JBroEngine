#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>
#include <JBro/Reflection/Field.h>
#include <array>
#include <tuple>
#include <type_traits>
#include <stdexcept>

namespace JBro::ScriptApiCatalog2D::Internal
{
    template<class> inline constexpr bool UnmappedType = false;
    template<class T> struct LanguageType
    {
        static String Name()
        {
            static_assert(UnmappedType<T>, "public script API type has no explicit language mapping");
            return {};
        }
    };
    template<class T> String TypeName()
    {
        return LanguageType<std::remove_cvref_t<T>>::Name();
    }
    template<class T> struct LanguageType<Array<T>>
    {
        static String Name() { return "Array<" + TypeName<T>() + ">"; }
    };
    template<class K, class V> struct LanguageType<Table<K, V>>
    {
        static String Name() { return "Table<" + TypeName<K>() + "," + TypeName<V>() + ">"; }
    };
    template<class T> struct LanguageType<T*>
    {
        static String Name() { return "Array<" + TypeName<T>() + ">"; }
    };
    template<class T, std::size_t N> struct LanguageType<T[N]>
    {
        static String Name() { return "Array<" + TypeName<T>() + ">"; }
    };
    template<class T> struct LanguageType<const T> : LanguageType<T> {};

    template<class T> struct FunctionTraits;
    template<class R, class C, class... A> struct FunctionTraits<R(C::*)(A...)>
    {
        using Return = R;
        using Arguments = std::tuple<A...>;
        static constexpr bool IsStatic = false;
    };
    template<class R, class C, class... A> struct FunctionTraits<R(C::*)(A...) const> : FunctionTraits<R(C::*)(A...)> {};
    template<class R, class C, class... A> struct FunctionTraits<R(C::*)(A...) noexcept> : FunctionTraits<R(C::*)(A...)> {};
    template<class R, class C, class... A> struct FunctionTraits<R(C::*)(A...) const noexcept> : FunctionTraits<R(C::*)(A...)> {};
    template<class R, class... A> struct FunctionTraits<R(*)(A...)>
    {
        using Return = R;
        using Arguments = std::tuple<A...>;
        static constexpr bool IsStatic = true;
    };
    template<class R, class... A> struct FunctionTraits<R(*)(A...) noexcept> : FunctionTraits<R(*)(A...)> {};

    enum class ParameterMode { Input, Output, Count };
    struct Parameter
    {
        const char* Name;
        const char* Default = nullptr;
        ParameterMode Mode = ParameterMode::Input;
        const char* LanguageOverride = nullptr;
    };
    constexpr Parameter In(const char* name, const char* defaultValue = nullptr, const char* languageOverride = nullptr)
    {
        return { name, defaultValue, ParameterMode::Input, languageOverride };
    }
    constexpr Parameter Out(const char* name) { return {name, nullptr, ParameterMode::Output}; }
    constexpr Parameter Count(const char* name) { return {name, nullptr, ParameterMode::Count}; }
    template<class... P> constexpr auto Parameters(P... values)
    {
        return std::array<Parameter, sizeof...(P)>{values...};
    }
    template<class T> String NativeTypeName()
    {
#if defined(_MSC_VER) && !defined(__clang__)
        return __FUNCSIG__;
#else
        return __PRETTY_FUNCTION__;
#endif
    }
    template<class Traits, std::size_t I>
    void AppendParameter(ScriptCompiler::ApiMember& member, const Parameter& info)
    {
        using Native = std::tuple_element_t<I, typename Traits::Arguments>;
        const String type = TypeName<Native>();
        member.NativeParameterNames.Add(info.Name);
        if (info.Mode == ParameterMode::Output)
        {
            member.NativeOutputParameters.Add(static_cast<UInt32>(I));
        }
        else if (info.Mode == ParameterMode::Input)
        {
            ScriptCompiler::ParameterInfo parameter;
            parameter.Name = info.Name;
            parameter.Type = info.LanguageOverride != nullptr ? String(info.LanguageOverride) : type;
            parameter.Borrowed = std::is_lvalue_reference_v<Native> && !std::is_const_v<std::remove_reference_t<Native>>;
            parameter.HasDefault = info.Default != nullptr;
            if (info.Default != nullptr)
            {
                parameter.DefaultValue = info.Default;
            }
            member.Parameters.Add(std::move(parameter));
        }
    }
    template<auto Function, std::size_t N, std::size_t... I>
    ScriptCompiler::ApiMember DeclareFunctionImpl(const char* name, const std::array<Parameter,N>& parameters,
        std::index_sequence<I...>)
    {
        using Traits = FunctionTraits<decltype(Function)>;
        static_assert(std::tuple_size_v<typename Traits::Arguments> == N, "public declaration parameter annotations do not match actual C++ callable");
        ScriptCompiler::ApiMember member;
        member.Name = name;
        member.NativeName = name;
        member.Type = TypeName<typename Traits::Return>();
        member.IsStatic = Traits::IsStatic;
        member.NativeSignature = NativeTypeName<decltype(Function)>();
        (AppendParameter<Traits,I>(member, parameters[I]), ...);
        return member;
    }
    template<auto Function, std::size_t N>
    ScriptCompiler::ApiMember DeclareFunction(const char* name, const std::array<Parameter,N>& parameters)
    {
        return DeclareFunctionImpl<Function>(name, parameters, std::make_index_sequence<N>{});
    }
    template<class T> struct FieldTraits;
    template<class T, class C> struct FieldTraits<T C::*> { using Type = T; };
    template<auto Pointer> ScriptCompiler::ApiMember DeclareField(const char* name, Bool writable = true)
    {
        ScriptCompiler::ApiMember member;
        member.Name = name;
        member.NativeName = name;
        member.Kind = ScriptCompiler::ApiMemberKind::Field;
        member.Type = TypeName<typename FieldTraits<decltype(Pointer)>::Type>();
        member.Writable = writable;
        member.NativeSignature = NativeTypeName<decltype(Pointer)>();
        return member;
    }
    template<auto Getter> String NativeGetterName()
    {
        const String signature = JBro::Detail::FieldName<Getter>::Text();
        const auto parameters = signature.find('(');
        return parameters == String::npos ? signature : signature.Substr(0, parameters);
    }
    template<auto Getter, auto Setter> ScriptCompiler::ApiMember DeclareProperty(const char* name)
    {
        using G = FunctionTraits<decltype(Getter)>;
        using S = FunctionTraits<decltype(Setter)>;
        static_assert(std::tuple_size_v<typename G::Arguments> == 0);
        static_assert(std::tuple_size_v<typename S::Arguments> == 1);
        static_assert(std::is_same_v<std::remove_cvref_t<typename G::Return>, std::remove_cvref_t<std::tuple_element_t<0,typename S::Arguments>>>);
        auto member = DeclareFunction<Getter>(name, Parameters());
        member.Kind = ScriptCompiler::ApiMemberKind::Field;
        member.NativeName = NativeGetterName<Getter>();
        member.Writable = true;
        member.NativeSignature += "; setter: " + NativeTypeName<decltype(Setter)>();
        return member;
    }
    template<auto Getter> ScriptCompiler::ApiMember DeclareProperty(const char* name)
    {
        auto member = DeclareFunction<Getter>(name, Parameters());
        member.Kind = ScriptCompiler::ApiMemberKind::Field;
        member.NativeName = NativeGetterName<Getter>();
        return member;
    }
}
