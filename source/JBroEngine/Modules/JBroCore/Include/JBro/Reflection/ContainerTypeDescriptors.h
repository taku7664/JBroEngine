#pragma once

#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/StringTypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <new>
#include <utility>

namespace JBro
{
    // 컨테이너를 **타입을 모른 채** 만지는 함수 묶음이다.
    //
    // 인스펙터도 직렬화도 `Array<Vec2>` 라는 것을 알 필요가 없다. 개수를 묻고,
    // 원소 주소를 받고, 하나 더하고 지운다 - 그 다음은 원소의 설명자가 답한다.
    //
    // **표는 인덱스로 지목할 수 없다**(`TableOps` 의 계약 그대로). open addressing
    // 이라 슬롯이 조밀하지 않고, 삽입 한 번에 리해시가 나면 슬롯 번호가 전부
    // 무효가 된다. 그래서 순회는 슬롯 커서로, 수정은 키로 한다.

    template <typename T>
    const ArrayOps& ArrayOpsOf()
    {
        static const ArrayOps ops = [] {
            ArrayOps made;
            made.GetSize = [](const void* array) noexcept -> std::size_t {
                return static_cast<const Array<T>*>(array)->Size();
            };
            made.GetElement = [](void* array, std::size_t index) noexcept -> void* {
                Array<T>* self = static_cast<Array<T>*>(array);
                return index < self->Size() ? &(*self)[index] : nullptr;
            };
            made.GetConstElement =
                [](const void* array, std::size_t index) noexcept -> const void* {
                const Array<T>* self = static_cast<const Array<T>*>(array);
                return index < self->Size() ? &(*self)[index] : nullptr;
            };
            made.AddDefault = [](void* array) noexcept -> bool {
                // 자리를 못 잡는 것은 실패다. `Add` 가 던지면 여기서 멈춘다 -
                // 이 함수들은 `noexcept` 라 밖으로 새어 나가면 프로세스가 죽는다.
                try
                {
                    static_cast<Array<T>*>(array)->Add(T{});
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            };
            made.RemoveAt = [](void* array, std::size_t index) noexcept -> bool {
                Array<T>* self = static_cast<Array<T>*>(array);
                if (index >= self->Size())
                {
                    return false;
                }
                // 뒤를 한 칸씩 당긴다. **순서를 지킨다** - 배열에서 순서는 뜻이
                // 있고(그래서 배열이다), 마지막 것을 끌어다 덮으면 그 뜻이 깨진다.
                for (std::size_t at = index + 1; at < self->Size(); ++at)
                {
                    (*self)[at - 1] = std::move((*self)[at]);
                }
                self->Resize(self->Size() - 1);
                return true;
            };
            made.Clear = [](void* array) noexcept {
                static_cast<Array<T>*>(array)->Clear();
            };
            return made;
        }();
        return ops;
    }

    template <typename Key, typename Value>
    const TableOps& TableOpsOf()
    {
        using TableType = Table<Key, Value>;
        static const TableOps ops = [] {
            TableOps made;
            made.GetSize = [](const void* table) noexcept -> std::size_t {
                return static_cast<const TableType*>(table)->Size();
            };
            made.BeginSlot = [](const void* table) noexcept -> std::size_t {
                const TableType* self = static_cast<const TableType*>(table);
                for (std::size_t slot = 0; slot < self->Capacity(); ++slot)
                {
                    if (self->IsSlotOccupied(slot))
                    {
                        return slot;
                    }
                }
                return TableOps::InvalidSlot;
            };
            made.NextSlot = [](const void* table, std::size_t slot) noexcept -> std::size_t {
                const TableType* self = static_cast<const TableType*>(table);
                for (std::size_t at = slot + 1; at < self->Capacity(); ++at)
                {
                    if (self->IsSlotOccupied(at))
                    {
                        return at;
                    }
                }
                return TableOps::InvalidSlot;
            };
            made.GetKeyAt = [](const void* table, std::size_t slot) noexcept -> const void* {
                const TableType* self = static_cast<const TableType*>(table);
                return self->IsSlotOccupied(slot) ? &self->KeyAt(slot) : nullptr;
            };
            made.GetValueAt = [](void* table, std::size_t slot) noexcept -> void* {
                TableType* self = static_cast<TableType*>(table);
                return self->IsSlotOccupied(slot) ? &self->ValueAt(slot) : nullptr;
            };
            made.ContainsKey = [](const void* table, const void* key) noexcept -> bool {
                if (key == nullptr)
                {
                    return false;
                }
                return static_cast<const TableType*>(table)
                    ->Find(*static_cast<const Key*>(key)) != nullptr;
            };
            made.InsertDefault = [](void* table, const void* key) noexcept -> bool {
                if (key == nullptr)
                {
                    return false;
                }
                try
                {
                    return static_cast<TableType*>(table)
                        ->TryAdd(*static_cast<const Key*>(key), Value{});
                }
                catch (...)
                {
                    return false;
                }
            };
            made.RemoveKey = [](void* table, const void* key) noexcept -> bool {
                if (key == nullptr)
                {
                    return false;
                }
                return static_cast<TableType*>(table)->Remove(*static_cast<const Key*>(key));
            };
            made.FindValue = [](void* table, const void* key) noexcept -> void* {
                if (key == nullptr)
                {
                    return nullptr;
                }
                return static_cast<TableType*>(table)->Find(*static_cast<const Key*>(key));
            };
            made.CreateKey = []() noexcept -> void* {
                // **타입이 지워진 쪽은 키를 스택에 만들 수 없다.** 글자에서 키를
                // 되살리는 역직렬화가 이 길을 쓴다. 할당이 일어나므로 프레임
                // 루프에서 쓰지 않는다.
                //
                // 처음에는 이것이 `CreateValue` 자리에 들어가 있었다 - 값을 만들라고
                // 부르면 키 크기의 객체가 나왔고, 테스트도 그 이름으로 키를 만들고
                // 있어서 드러나지 않았다. 표를 읽는 걸음(D-86)이 `CreateKey` 를 부르다 찾았다.
                //
                // `new` 를 직접 쓰는 이유: 소유를 `void*` 로 넘겨야 하는데 `OwnerPtr` 는
                // 소유를 놓는 길이 없다. 짝인 `DestroyKey` 가 같은 타입으로 지운다.
                try
                {
                    return new Key{};
                }
                catch (...)
                {
                    return nullptr;
                }
            };
            made.DestroyKey = [](void* key) noexcept {
                delete static_cast<Key*>(key);
            };
            made.CreateValue = []() noexcept -> void* {
                try
                {
                    return new Value{};
                }
                catch (...)
                {
                    return nullptr;
                }
            };
            made.DestroyValue = [](void* value) noexcept {
                delete static_cast<Value*>(value);
            };
            made.Clear = [](void* table) noexcept {
                static_cast<TableType*>(table)->Clear();
            };
            return made;
        }();
        return ops;
    }

    // 컨테이너의 설명자를 만든다. 이름은 `Array<원소이름>` 처럼 원소에서 짓는다 -
    // 그래야 저장 파일과 오류 메시지에서 무엇의 배열인지 보인다.
    template <typename T>
    const TypeDescriptor& ArrayTypeDescriptorOf()
    {
        static const TypeDescriptor descriptor = [] {
            const TypeDescriptor& element = TypeDescriptorOf<T>::Get();
            String name = "Array<";
            name += NameTable::Get().Resolve(element.typeName) != nullptr
                ? NameTable::Get().Resolve(element.typeName) : "?";
            name += ">";

            TypeDescriptor made;
            made.typeName = NameTable::Get().Intern(name.c_str());
            made.size = static_cast<std::uint32_t>(sizeof(Array<T>));
            made.alignment = static_cast<std::uint32_t>(alignof(Array<T>));
            made.triviallyCopyable = false;
            made.arrayOps = &ArrayOpsOf<T>();
            made.element = &element;
            return made;
        }();
        return descriptor;
    }

    template <typename Key, typename Value>
    const TypeDescriptor& TableTypeDescriptorOf()
    {
        static const TypeDescriptor descriptor = [] {
            const TypeDescriptor& keyType = TypeDescriptorOf<Key>::Get();
            const TypeDescriptor& valueType = TypeDescriptorOf<Value>::Get();
            String name = "Table<";
            name += NameTable::Get().Resolve(keyType.typeName) != nullptr
                ? NameTable::Get().Resolve(keyType.typeName) : "?";
            name += ", ";
            name += NameTable::Get().Resolve(valueType.typeName) != nullptr
                ? NameTable::Get().Resolve(valueType.typeName) : "?";
            name += ">";

            TypeDescriptor made;
            made.typeName = NameTable::Get().Intern(name.c_str());
            made.size = static_cast<std::uint32_t>(sizeof(Table<Key, Value>));
            made.alignment = static_cast<std::uint32_t>(alignof(Table<Key, Value>));
            made.triviallyCopyable = false;
            made.tableOps = &TableOpsOf<Key, Value>();
            made.key = &keyType;
            made.value = &valueType;
            return made;
        }();
        return descriptor;
    }

    // 필드로 바로 쓸 수 있게 한다. `JBRO_FIELD(Array<float>, points)` 가 그대로 선다.
    template <typename T>
    struct TypeDescriptorOf<Array<T>>
    {
        static const TypeDescriptor& Get()
        {
            return ArrayTypeDescriptorOf<T>();
        }
    };

    template <typename Key, typename Value>
    struct TypeDescriptorOf<Table<Key, Value>>
    {
        static const TypeDescriptor& Get()
        {
            return TableTypeDescriptorOf<Key, Value>();
        }
    };
}
