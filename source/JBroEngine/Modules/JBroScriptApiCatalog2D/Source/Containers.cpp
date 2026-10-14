#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
using NativeArray = Array<Float>;
using NativeTable = Table<String, Float>;
template<auto Function, std::size_t N>
void AddContainerMember(ApiType& type, const char* name, const std::array<Parameter,N>& parameters, const char* returnType = nullptr, Bool mutates = false)
{
    auto member = DeclareFunction<Function>(name, parameters);
    if (returnType != nullptr)
    {
        member.Type = returnType;
    }
    member.MutatesReceiver = mutates;
    type.Members.Add(std::move(member));
}
void AddContainers(ApiCatalog& catalog)
{
    {
        auto& type = AddType(catalog, "Array", "JBro::Array<T>", ApiTypeKind::Value);
        AddContainerMember<&NativeArray::Size>(type, "Size", Parameters());
        AddContainerMember<&NativeArray::Capacity>(type, "Capacity", Parameters());
        AddContainerMember<&NativeArray::IsEmpty>(type, "IsEmpty", Parameters());
        AddContainerMember<&NativeArray::Reserve>(type, "Reserve", Parameters(In("capacity")), nullptr, true);
        AddContainerMember<&NativeArray::Resize>(type, "Resize", Parameters(In("size")), nullptr, true);
        AddContainerMember<&NativeArray::Clear>(type, "Clear", Parameters(), nullptr, true);
        AddContainerMember<&NativeArray::Reset>(type, "Reset", Parameters(In("capacity", "0")), nullptr, true);
        AddContainerMember<&NativeArray::Shrink>(type, "Shrink", Parameters(), nullptr, true);
        AddContainerMember<static_cast<Float&(NativeArray::*)(const Float&)>(&NativeArray::Add)>(type, "Add", Parameters(In("value", nullptr, "T")), "T", true);
        AddContainerMember<static_cast<Float&(NativeArray::*)(std::size_t, const Float&)>(&NativeArray::Insert)>(type, "Insert", Parameters(In("index"), In("value", nullptr, "T")), "T", true);
        AddContainerMember<&NativeArray::RemoveAt>(type, "RemoveAt", Parameters(In("index")), nullptr, true);
        AddContainerMember<&NativeArray::RemoveAtSwap>(type, "RemoveAtSwap", Parameters(In("index")), nullptr, true);
        AddContainerMember<&NativeArray::Pop>(type, "Pop", Parameters(), "T", true);
        AddContainerMember<static_cast<const Float&(NativeArray::*)() const noexcept>(&NativeArray::First)>(type, "First", Parameters(), "T");
        AddContainerMember<static_cast<const Float&(NativeArray::*)() const noexcept>(&NativeArray::Last)>(type, "Last", Parameters(), "T");
        AddContainerMember<&NativeArray::Contains<Float>>(type, "Contains", Parameters(In("value", nullptr, "T")));
        AddContainerMember<&NativeArray::IndexOf<Float>>(type, "IndexOf", Parameters(In("value", nullptr, "T")));
        type.Members.Add(DeclareProperty<&NativeArray::Size>("count"));
    }
    {
        auto& type = AddType(catalog, "Table", "JBro::Table<K,V>", ApiTypeKind::Value);
        AddContainerMember<&NativeTable::Size>(type, "Size", Parameters());
        AddContainerMember<&NativeTable::Capacity>(type, "Capacity", Parameters());
        AddContainerMember<&NativeTable::IsEmpty>(type, "IsEmpty", Parameters());
        AddContainerMember<&NativeTable::Reserve>(type, "Reserve", Parameters(In("size")), nullptr, true);
        AddContainerMember<&NativeTable::Clear>(type, "Clear", Parameters(), nullptr, true);
        AddContainerMember<&NativeTable::Reset>(type, "Reset", Parameters(In("size", "0")), nullptr, true);
        AddContainerMember<&NativeTable::Contains<String>>(type, "Contains", Parameters(In("key", nullptr, "K")));
        AddContainerMember<&NativeTable::Remove<String>>(type, "Remove", Parameters(In("key", nullptr, "K")), nullptr, true);
        AddContainerMember<&NativeTable::TryAdd<const String&, const Float&>>(type, "TryAdd", Parameters(In("key", nullptr, "K"), In("value", nullptr, "V")), nullptr, true);
        AddContainerMember<&NativeTable::InsertOrAssign<const String&, const Float&>>(type, "InsertOrAssign", Parameters(In("key", nullptr, "K"), In("value", nullptr, "V")), "V", true);
        AddContainerMember<static_cast<const Float*(NativeTable::*)(const String&) const>(&NativeTable::Find<String>)>(type, "Find", Parameters(In("key", nullptr, "K")), "V?");
        type.Members.Last().Projection = ApiProjectionKind::NullableReturn;
        type.Members.Last().HasOutputProjection = true;
        type.Members.Add(DeclareProperty<&NativeTable::Size>("count"));
    }
    {
        auto& type = AddType(catalog, "String", "JBro::String", ApiTypeKind::Value);
        AddContainerMember<&String::IsEmpty>(type, "IsEmpty", Parameters());
        AddContainerMember<&String::IsNotEmpty>(type, "IsNotEmpty", Parameters());
        AddContainerMember<&String::Reserve>(type, "Reserve", Parameters(In("capacity")), nullptr, true);
        AddContainerMember<&String::ReserveAdditional>(type, "ReserveAdditional", Parameters(In("additional")), nullptr, true);
        AddContainerMember<&String::Clear>(type, "Clear", Parameters(In("keepCapacity", "true")), nullptr, true);
        AddContainerMember<&String::Reset>(type, "Reset", Parameters(), nullptr, true);
        AddContainerMember<&String::Shrink>(type, "Shrink", Parameters(), nullptr, true);
        AddContainerMember<&String::StartsWith>(type, "StartsWith", Parameters(In("prefix")));
        AddContainerMember<&String::EndsWith>(type, "EndsWith", Parameters(In("suffix")));
        AddContainerMember<&String::Contains>(type, "Contains", Parameters(In("text")));
        AddContainerMember<&String::Substr>(type, "Substr", Parameters(In("offset"), In("count", "-1")));
        AddContainerMember<&String::Append>(type, "Append", Parameters(In("text")), nullptr, true);
        AddContainerMember<&String::ReplaceAll>(type, "ReplaceAll", Parameters(In("from"), In("to")), nullptr, true);
        AddContainerMember<&String::TrimLeft>(type, "TrimLeft", Parameters(), nullptr, true);
        AddContainerMember<&String::TrimRight>(type, "TrimRight", Parameters(), nullptr, true);
        AddContainerMember<&String::Trim>(type, "Trim", Parameters(), nullptr, true);
        AddContainerMember<&String::Trimmed>(type, "Trimmed", Parameters());
        AddContainerMember<&String::ToLower>(type, "ToLower", Parameters(), nullptr, true);
        AddContainerMember<&String::ToUpper>(type, "ToUpper", Parameters(), nullptr, true);
        AddContainerMember<&String::ToLowerCopy>(type, "ToLowerCopy", Parameters());
        AddContainerMember<&String::ToUpperCopy>(type, "ToUpperCopy", Parameters());
        AddContainerMember<&String::Split>(type, "Split", Parameters(In("delimiter"), In("skipEmpty", "false")));
        AddContainerMember<&String::FitsInLocalBuffer>(type, "FitsInLocalBuffer", Parameters());
        AddContainerMember<&String::LocalCapacity>(type, "LocalCapacity", Parameters());
    }
}
}
