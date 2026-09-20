#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentSnapshot.h>
#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Field.h>

#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

// 글자로 쓰기를 거부하는 값이다. 스냅샷이 이런 값을 만나면 **캡처가 실패해야** 한다 -
// 처음에는 그 값만 조용히 빼고 성공이라 말했다(D-76 과 같은 구멍).
namespace
{
    struct Stubborn
    {
        int unused = 0;
    };

    // 그 값을 **구조체 안에** 품는다. 캡처는 필드를 타고 내려가므로, 안쪽에서 난
    // 실패를 바깥이 버리면 뿌리에서만 막는 검사를 빠져나간다.
    struct Burrow
    {
        float depth = 0.0f;
        Stubborn stubborn;
    };

    // 길은 네 칸까지만 담는다(`SetPropertyCommand::MaxDepth`). 한 겹씩 싸서 넷째 칸과
    // 다섯째 칸에 잎사귀를 둔다.
    struct Den
    {
        float warmth = 0.0f;
    };

    struct Warren
    {
        Den den;
    };

    struct Tunnel
    {
        Warren warren;
    };

    struct Shaft
    {
        Tunnel tunnel;
    };

    // 필드의 종류가 섞인 원소다. 한 줄 숫자 묶음으로 읽히지 않으므로 필드마다 따로 고친다.
    // 안에 구조체(`Den`)와 배열이 하나씩 있다.
    struct Beacon
    {
        float range = 0.0f;
        bool lit = false;
        JBro::Color tint{};
        Den den;
        JBro::Array<float> pulses;
    };

    template <auto Member, typename Owner>
    const JBro::TypeDescriptor& SingleFieldStruct(const char* typeName)
    {
        static const JBro::FieldEntry entries[] =
        {
            JBro::MakeFieldEntry<Member>(),
        };
        static const JBro::StaticPropertyTable<1> fields { entries };
        static const JBro::TypeDescriptor descriptor =
            JBro::MakeStructTypeDescriptor<Owner>(typeName, fields.Get());
        return descriptor;
    }
}

namespace JBro
{
    template <>
    struct TypeDescriptorOf<Stubborn>
    {
        static const TypeDescriptor& Get()
        {
            static const ValueCodec codec = []
            {
                ValueCodec result;
                result.ToText = [](const void*, char*, std::size_t, std::size_t& required) noexcept
                {
                    required = 0;
                    return false;
                };
                result.FromText = [](void*, const char*, std::size_t) noexcept { return true; };
                result.Equals = [](const void*, const void*) noexcept { return true; };
                result.Assign = [](void*, const void*) noexcept {};
                return result;
            }();
            static const TypeDescriptor descriptor = []
            {
                TypeDescriptor built;
                built.typeName = NameTable::Get().Intern("Test::Stubborn");
                built.size = static_cast<std::uint32_t>(sizeof(Stubborn));
                built.alignment = static_cast<std::uint32_t>(alignof(Stubborn));
                built.triviallyCopyable = true;
                built.codec = &codec;
                return built;
            }();
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Burrow>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Burrow::depth>(),
                MakeFieldEntry<&Burrow::stubborn>(),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Burrow>("Test::Burrow", fields.Get());
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Den>
    {
        static const TypeDescriptor& Get()
        {
            return SingleFieldStruct<&Den::warmth, Den>("Test::Den");
        }
    };

    template <>
    struct TypeDescriptorOf<Warren>
    {
        static const TypeDescriptor& Get()
        {
            return SingleFieldStruct<&Warren::den, Warren>("Test::Warren");
        }
    };

    template <>
    struct TypeDescriptorOf<Tunnel>
    {
        static const TypeDescriptor& Get()
        {
            return SingleFieldStruct<&Tunnel::warren, Tunnel>("Test::Tunnel");
        }
    };

    template <>
    struct TypeDescriptorOf<Shaft>
    {
        static const TypeDescriptor& Get()
        {
            return SingleFieldStruct<&Shaft::tunnel, Shaft>("Test::Shaft");
        }
    };

    template <>
    struct TypeDescriptorOf<Beacon>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Beacon::range>(),
                MakeFieldEntry<&Beacon::lit>(),
                MakeFieldEntry<&Beacon::tint>(),
                MakeFieldEntry<&Beacon::den>(),
                MakeFieldEntry<&Beacon::pulses>(),
            };
            static const StaticPropertyTable<5> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Beacon>("Test::Beacon", fields.Get());
            return descriptor;
        }
    };
}

// 에디터 커맨드의 **속**을 본다. `EditorCommandTests` 는 스택의 계약만 보고,
// `EditorApplicationTests` 는 디바이스가 있어야 도는 큰 길을 본다 - 그 둘 사이에
// 프로퍼티 길·오브젝트 번호·삭제 스냅샷이 아무에게도 안 보이는 채로 남아 있었다.
// 뮤테이션 서른여섯 개를 돌려 보고 나서야 알았다.

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // 프로퍼티를 등록하지 않은 컴포넌트다. **스크립트가 실제로 이렇다** -
    // `Canvas::AttachScript` 가 붙인 것도 `GetComponents()` 에 들어오는데,
    // 리플렉션 표는 없다.
    class UnreflectedProbe final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::UnreflectedProbe";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }
    };

    void RegisterOnce()
    {
        Check(JBro::Component::RegisterBuiltinComponentProperties2D(),
            "the builtin 2D properties must register");
        Check(JBro::Component::RegisterBuiltinComponentTypes2D(),
            "the builtin 2D component types must register");
    }

    std::uint32_t FieldIndex(const JBro::PropertyTable& table, const char* name)
    {
        for (std::uint32_t index = 0; index < table.count; ++index)
        {
            const char* found =
                JBro::NameTable::Get().Resolve(table.properties[index].name);
            if (found != nullptr && std::strcmp(found, name) == 0)
            {
                return index;
            }
        }
        Check(false, "the field this test names must be in the table");
        return 0;
    }

    const JBro::PropertyTable& TransformTable()
    {
        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup(JBro::Component::Transform2D::StaticTypeName());
        Check(table != nullptr, "Transform2D must have registered its properties");
        return *table;
    }

    JBro::SetPropertyCommand::Path PathTo(std::uint32_t first)
    {
        JBro::SetPropertyCommand::Path path;
        path.indices[0] = first;
        path.depth = 1;
        return path;
    }

    JBro::SetPropertyCommand::Path PathTo(std::uint32_t first, std::uint32_t second)
    {
        JBro::SetPropertyCommand::Path path = PathTo(first);
        path.indices[1] = second;
        path.depth = 2;
        return path;
    }

    JBro::ComponentAddress AddressOf(JBro::EditorObjectRegistry& ids,
        JBro::GameObject& object, const JBro::ComponentBase& component)
    {
        JBro::ComponentAddress address;
        Check(JBro::MakeComponentAddress(ids, object, component, address),
            "a component on its own object must have an address");
        return address;
    }

    bool NearlyEqual(float value, float expected)
    {
        const float delta = value - expected;
        return delta > -0.0001f && delta < 0.0001f;
    }

    // ── 오브젝트 번호 ────────────────────────────────────────────────────

    // 번호는 **같은 것에 같은 번호**, **모르는 번호는 짓지 않기**, **지워도 다시
    // 세지 않기** 셋을 지켜야 커맨드가 옛 오브젝트를 다시 찾을 수 있다(D-72).
    void TestObjectNumbersAreStableAndNeverInvented()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;

        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");

        const JBro::EditorObjectId firstId = ids.Track(first);
        Check(firstId != JBro::InvalidEditorObjectId, "a real object must get a number");
        Check(ids.Track(first) == firstId,
            "asking twice about the same object must give the same number");
        Check(ids.GetCount() == 1, "and must not make a second entry for it");
        Check(ids.Track(second) != firstId, "a different object must get a different one");

        Check(ids.Track(nullptr) == JBro::InvalidEditorObjectId,
            "nothing has no number");
        Check(ids.Resolve(JBro::InvalidEditorObjectId) == nullptr,
            "and the invalid number finds nothing");
        Check(ids.Resolve(firstId) == first, "a number must find what it was given");

        // **모르는 번호에는 걸지 않는다.** 걸어 주면 그 번호를 들고 있던 커맨드가
        // 엉뚱한 오브젝트를 찾아내 거기에 편집을 쏟는다.
        const JBro::EditorObjectId strangerId = firstId + 5000;
        const std::size_t before = ids.GetCount();
        Check(false == ids.Rebind(strangerId, second),
            "rebinding a number nobody issued must be refused");
        Check(ids.GetCount() == before, "and must not quietly add an entry");
        Check(ids.Resolve(strangerId) == nullptr, "that number must still find nothing");
        Check(false == ids.Rebind(firstId, nullptr), "and nothing cannot be bound");

        // 지운 뒤에도 번호는 이어서 센다. 1 부터 다시 세면, 지우기 전에 발급한
        // 번호를 들고 있는 커맨드가 그 번호로 전혀 다른 오브젝트를 집는다.
        ids.Clear();
        Check(ids.GetCount() == 0, "clearing must empty the table");
        JBro::GameObject* third = canvas.CreateObject("Third");
        const JBro::EditorObjectId thirdId = ids.Track(third);
        Check(thirdId != firstId, "numbers must not be handed out a second time");
    }

    // ── 프로퍼티 커맨드 ──────────────────────────────────────────────────

    // 길은 인덱스의 나열이다. **깊이가 다르면 다른 길이다** - 앞부분이 같다고
    // 같은 잎사귀일 수 없다. `position` 과 `position.x` 는 다른 것이다.
    void TestPathsOfDifferentDepthAreDifferentPaths()
    {
        const JBro::SetPropertyCommand::Path shallow = PathTo(0);
        const JBro::SetPropertyCommand::Path deep = PathTo(0, 0);
        Check(false == shallow.Equals(deep), "a branch is not the leaf under it");
        Check(false == deep.Equals(shallow), "and that holds both ways");
        Check(shallow.Equals(PathTo(0)), "the same path is the same path");
        Check(false == PathTo(0, 1).Equals(PathTo(0, 2)),
            "siblings under one branch are different paths");
    }

    void TestAPropertyGoesThereAndComesBack()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Probe");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the probe must have a transform");
        JBro::ComponentBase* component = transform;
        const JBro::ComponentTypeId typeId = component->GetTypeId();

        const std::uint32_t rotation = FieldIndex(TransformTable(), "rotation");
        const JBro::SetPropertyCommand::Path path = PathTo(rotation);

        transform->rotation = 0.5f;
        JBro::String before;
        Check(JBro::SetPropertyCommand::ReadValue(*component, typeId, path, before),
            "reading a leaf must work");

        JBro::EditorObjectRegistry ids;
        JBro::SetPropertyCommand command(
            ids, AddressOf(ids, *object, *component), path, before, JBro::String("1.5"));
        Check(command.Execute(), "the edit must go through");
        Check(NearlyEqual(transform->rotation, 1.5f), "and must have happened");
        command.Undo();
        Check(NearlyEqual(transform->rotation, 0.5f), "undo must put the old value back");
        command.Redo();
        Check(NearlyEqual(transform->rotation, 1.5f), "and redo must do it again");

        // 한 칸 더 내려간 잎사귀도 같은 길이다.
        const std::uint32_t position = FieldIndex(TransformTable(), "position");
        const JBro::SetPropertyCommand::Path deep = PathTo(position, 1);
        transform->position = {0.0f, 2.0f};
        JBro::String nested;
        Check(JBro::SetPropertyCommand::ReadValue(*component, typeId, deep, nested),
            "a nested leaf must read too");
        Check(JBro::SetPropertyCommand::ApplyValue(
                *component, typeId, deep, JBro::String("7.25")),
            "and must be writable");
        Check(NearlyEqual(transform->position.y, 7.25f),
            "the write must land on y, not on x");
        Check(NearlyEqual(transform->position.x, 0.0f), "and must leave x alone");
    }

    // **가지는 잎사귀가 아니다.** `world` 는 필드를 가진 타입이고 한 줄에 담기지 않아
    // 필드를 타고 내려가 그린다 - 여기에 글자를 쓰려 들면 쓸 방법이 없는데도 있다고 답하게 된다.
    // (`position` 은 처음에 이 자리의 예였다. 한 줄 숫자 묶음은 이제 잎사귀다 - D-89.)
    void TestABranchIsNotALeaf()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Probe");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::ComponentBase* component = transform;
        const JBro::ComponentTypeId typeId = component->GetTypeId();

        const std::uint32_t world = FieldIndex(TransformTable(), "world");
        const JBro::SetPropertyCommand::Path branch = PathTo(world);

        void* address = nullptr;
        const JBro::TypeDescriptor* type = nullptr;
        Check(false == JBro::SetPropertyCommand::ResolveLeaf(
                *component, typeId, branch, address, type),
            "a type with fields is not a leaf and must be refused");

        JBro::String text;
        Check(false == JBro::SetPropertyCommand::ReadValue(*component, typeId, branch, text),
            "so there is no value to read from it");
        Check(false == JBro::SetPropertyCommand::ApplyValue(
                *component, typeId, branch, JBro::String("1 2")),
            "and nothing to write into it");

        // 표 밖을 가리키는 길, 빈 길, 너무 깊은 길도 같다.
        JBro::SetPropertyCommand::Path outside = PathTo(9999);
        Check(false == JBro::SetPropertyCommand::ResolveLeaf(
                *component, typeId, outside, address, type),
            "an index past the end of the table must be refused");
        JBro::SetPropertyCommand::Path empty;
        Check(false == JBro::SetPropertyCommand::ResolveLeaf(
                *component, typeId, empty, address, type),
            "a path of no steps points at nothing");
    }

    // **한 줄 숫자 묶음은 한 값이다**(D-89). 인스펙터가 `position` 을 한 줄에 그리므로 커맨드도
    // 한 값으로 든다. 처음에는 가지로 거절해, 인스펙터가 커밋하지 못하고 위젯이 쓴 값이
    // 커맨드 없이 남았다 - 되돌릴 수 없었고 여럿 골라도 주된 것만 움직였다.
    void TestAOneLineRunIsOneValue()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Probe");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::ComponentBase* component = transform;
        const JBro::ComponentTypeId typeId = component->GetTypeId();
        transform->position = JBro::Vec2{1.5f, -2.0f};

        const JBro::SetPropertyCommand::Path position =
            PathTo(FieldIndex(TransformTable(), "position"));
        void* address = nullptr;
        const JBro::TypeDescriptor* type = nullptr;
        Check(JBro::SetPropertyCommand::ResolveLeaf(*component, typeId, position, address, type),
            "a run drawn on one line must be a leaf");
        Check(address == &transform->position, "at the address of the whole run");

        JBro::String before;
        Check(JBro::SetPropertyCommand::ReadValue(*component, typeId, position, before),
            "its value must read as text");
        transform->position = JBro::Vec2{9.0f, 9.0f};
        Check(JBro::SetPropertyCommand::ApplyValue(*component, typeId, position, before),
            "and that text must write back");
        Check(NearlyEqual(transform->position.x, 1.5f) && NearlyEqual(transform->position.y, -2.0f),
            "bringing back both members");

        // 칸으로도 내려갈 수 있다. 스냅샷이 칸마다 뜨는 길이다.
        Check(JBro::SetPropertyCommand::ResolveLeaf(*component, typeId, PathTo(
                FieldIndex(TransformTable(), "position"), 1), address, type)
                && address == &transform->position.y,
            "and a member of the run is still a leaf of its own");

        // **반만 읽히는 글자는 반만 쓰지 않는다.** 첫 칸을 읽고 둘째에서 막히면 x 만 바뀐다.
        JBro::String broken("Value:\n  - 7\n  - not a number\n");
        Check(false == JBro::SetPropertyCommand::ApplyValue(*component, typeId, position, broken),
            "a text that does not parse into the run must be refused");
        Check(NearlyEqual(transform->position.x, 1.5f), "and leave the run as it was");
    }

    // **드래그 병합은 같은 컴포넌트의 같은 잎사귀일 때만이다.** 아니면 그 사이
    // 편집이 되돌리기에서 사라진다.
    void TestMergeOnlyJoinsTheSameLeafOfTheSameComponent()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");
        auto* firstTransform = canvas.AttachComponent<JBro::Component::Transform2D>(first);
        auto* secondTransform = canvas.AttachComponent<JBro::Component::Transform2D>(second);
        JBro::EditorObjectRegistry ids;
        const JBro::ComponentAddress firstAddress = AddressOf(ids, *first, *firstTransform);
        const JBro::ComponentAddress secondAddress = AddressOf(ids, *second, *secondTransform);

        const std::uint32_t rotation = FieldIndex(TransformTable(), "rotation");
        const std::uint32_t position = FieldIndex(TransformTable(), "position");
        const JBro::SetPropertyCommand::Path rotationPath = PathTo(rotation);
        const JBro::SetPropertyCommand::Path positionY = PathTo(position, 1);

        // 같은 잎사귀: 합친다. 그리고 **처음 값은 앞쪽 것을 지킨다** - 드래그
        // 전체가 한 번에 되돌아가야 한다.
        firstTransform->rotation = 1.0f;
        JBro::SetPropertyCommand held(ids, firstAddress, rotationPath,
            JBro::String("1"), JBro::String("2"));
        Check(held.Execute(), "the first frame of the drag must apply");
        JBro::SetPropertyCommand next(ids, firstAddress, rotationPath,
            JBro::String("2"), JBro::String("3"));
        Check(next.Execute(), "and the second");
        Check(held.TryMerge(next), "the same leaf during a drag must merge");
        held.Undo();
        Check(NearlyEqual(firstTransform->rotation, 1.0f),
            "undoing the merged drag must reach back to before it started");

        // 다른 잎사귀: 합치지 않는다.
        JBro::SetPropertyCommand onRotation(ids, firstAddress,
            rotationPath, JBro::String("1"), JBro::String("2"));
        JBro::SetPropertyCommand onPosition(ids, firstAddress,
            positionY, JBro::String("0"), JBro::String("5"));
        Check(false == onRotation.TryMerge(onPosition),
            "a different field must not be folded into this one");

        // 다른 컴포넌트: 같은 필드라도 합치지 않는다.
        JBro::SetPropertyCommand onOther(ids, secondAddress,
            rotationPath, JBro::String("1"), JBro::String("2"));
        Check(false == onRotation.TryMerge(onOther),
            "the same field on another object must not be folded in either");

        // 주소는 셋이 다 맞아야 같은 곳이다. 같은 오브젝트의 같은 타입이라도
        // 둘째 것이면, 다른 타입이면 다른 컴포넌트다.
        JBro::ComponentAddress secondOfAKind = firstAddress;
        secondOfAKind.ordinal = 1;
        JBro::SetPropertyCommand onSecondOfAKind(ids, secondOfAKind,
            rotationPath, JBro::String("1"), JBro::String("2"));
        Check(false == onRotation.TryMerge(onSecondOfAKind),
            "the second of a kind on the same object must not be folded in");
        JBro::ComponentAddress otherType = firstAddress;
        otherType.typeId = firstAddress.typeId + 1;
        JBro::SetPropertyCommand onOtherType(ids, otherType,
            rotationPath, JBro::String("1"), JBro::String("2"));
        Check(false == onRotation.TryMerge(onOtherType),
            "and neither must a component of another type");
    }

    // **지웠다 되살린 오브젝트에서도 앞선 편집이 되돌아가야 한다**(D-72).
    //
    // 되살리면 컴포넌트가 새로 만들어져 주소가 달라진다. 편집 커맨드가 옛
    // 컴포넌트를 붙들고 있으면, 삭제를 되돌린 뒤 그 편집을 되돌려도 아무 일도
    // 일어나지 않는다 - 사용자에게는 Ctrl+Z 가 고장 난 것으로 보인다.
    void TestAnEditSurvivesItsObjectBeingDeletedAndRestored()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the subject must have a transform");
        transform->rotation = 0.5f;
        const JBro::EditorObjectId id = ids.Track(object);
        const JBro::SetPropertyCommand::Path path =
            PathTo(FieldIndex(TransformTable(), "rotation"));

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::SetPropertyCommand>(
                ids, AddressOf(ids, *object, *transform), path,
                JBro::String("0.5"), JBro::String("1.5"))),
            "the edit must go through");
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::DeleteObjectCommand>(
                canvas, ids, object)),
            "and so must the delete");
        // 에디터는 프레임 끝에 비운다. 비우지 않으면 옛 컴포넌트가 아직 살아 있어
        // 이 테스트가 재려는 것을 가린다.
        canvas.FlushPendingDestroy();

        Check(commands.Undo(), "undoing the delete must run");
        JBro::GameObject* restored = ids.Resolve(id);
        Check(restored != nullptr, "and bring the object back under its number");
        auto* restoredTransform =
            restored->GetComponent<JBro::Component::Transform2D>().Get();
        Check(restoredTransform != nullptr, "with its transform");
        Check(NearlyEqual(restoredTransform->rotation, 1.5f),
            "holding the edited value");

        Check(commands.Undo(), "undoing the edit must run");
        Check(NearlyEqual(restoredTransform->rotation, 0.5f),
            "and must reach the restored object, not the one that died");
        Check(commands.Redo(), "redoing the edit must run");
        Check(NearlyEqual(restoredTransform->rotation, 1.5f), "and reach it again");
    }

    // ── 삭제 ─────────────────────────────────────────────────────────────

    // **되살릴 수 없는 것은 지우지 않는다.** 스냅샷이 반쪽이면 배열은 비어 있지
    // 않으므로, 비었는지만 보아서는 모자란다(D-72).
    void TestDeletingIsRefusedWhenAValueCannotBeSaved()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;

        JBro::GameObject* parent = canvas.CreateObject("Parent");
        canvas.AttachComponent<JBro::Component::Transform2D>(parent);
        JBro::GameObject* child = canvas.CreateObject("Child");
        child->SetParent(parent);
        // 자식에만 리플렉션 없는 컴포넌트를 붙인다. **뿌리는 멀쩡하므로 스냅샷은
        // 비어 있지 않다** - 여기가 조용히 잃던 자리다.
        Check(canvas.AttachComponent<UnreflectedProbe>(child) != nullptr,
            "the probe must attach");

        const std::size_t before = canvas.GetObjectCount();
        JBro::DeleteObjectCommand command(canvas, ids, parent);
        Check(false == command.Execute(),
            "deleting a tree that cannot be fully captured must be refused");
        canvas.FlushPendingDestroy();
        Check(canvas.GetObjectCount() == before,
            "and must leave every object where it was");

        // 뿌리 자신이 못 뜨는 경우도 같다.
        JBro::GameObject* lone = canvas.CreateObject("Lone");
        canvas.AttachComponent<UnreflectedProbe>(lone);
        const std::size_t now = canvas.GetObjectCount();
        JBro::DeleteObjectCommand onLone(canvas, ids, lone);
        Check(false == onLone.Execute(), "and so must a root that cannot be captured");
        canvas.FlushPendingDestroy();
        Check(canvas.GetObjectCount() == now, "nothing may be lost");
    }

    void TestDeletingNothingIsRefused()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        const std::size_t before = canvas.GetObjectCount();

        JBro::DeleteObjectCommand command(canvas, ids, nullptr);
        Check(false == command.Execute(), "there is nothing to delete");
        canvas.FlushPendingDestroy();
        Check(canvas.GetObjectCount() == before, "so nothing may go away");
    }

    // 되살아난 것은 **꺼져 있던 것까지** 그대로여야 한다. 값만 돌려놓고 켜 버리면
    // 되돌린 것이 아니다.
    void TestRestoringBringsBackWhatWasSwitchedOff()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;

        JBro::GameObject* object = canvas.CreateObject("Sleeping");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        transform->rotation = 0.25f;
        sprite->SetEnabled(false);
        object->SetActive(false);
        const JBro::EditorObjectId id = ids.Track(object);

        JBro::DeleteObjectCommand command(canvas, ids, object);
        Check(command.Execute(), "deleting must go through");
        Check(ids.Resolve(id) == nullptr, "and the object must be gone");

        command.Undo();
        JBro::GameObject* restored = ids.Resolve(id);
        Check(restored != nullptr, "undo must bring it back under the same number");
        Check(false == restored->IsActiveSelf(),
            "an object that was switched off must come back switched off");

        auto* restoredSprite =
            restored->GetComponent<JBro::Component::SpriteRenderer2D>().Get();
        Check(restoredSprite != nullptr, "with its sprite renderer");
        Check(false == restoredSprite->IsEnabled(),
            "and a disabled component must come back disabled");

        auto* restoredTransform =
            restored->GetComponent<JBro::Component::Transform2D>().Get();
        Check(restoredTransform != nullptr, "and its transform");
        Check(NearlyEqual(restoredTransform->rotation, 0.25f), "with the value it had");
    }

    // ── 컴포넌트 붙이기·떼기 ─────────────────────────────────────────────

    JBro::NameId TypeNameOf(const char* name)
    {
        return JBro::NameTable::Get().Intern(name);
    }

    std::size_t CountComponents(const JBro::GameObject& object, JBro::ComponentTypeId typeId)
    {
        std::size_t found = 0;
        const JBro::Array<JBro::ComponentSlot>& components = object.GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            if (components[index].typeId == typeId
                && components[index].reference.TryGet() != nullptr)
            {
                ++found;
            }
        }
        return found;
    }

    // 붙인 것은 되돌리면 떨어지고 다시하면 돌아온다. **뗄 때 풀도 돌려받아야
    // 한다** - 슬롯만 빼면 컴포넌트 자리가 계속 늘어난다.
    void TestAddingAComponentCanBeUndone()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        const JBro::ComponentTypeId spriteType =
            JBro::MakeStableTypeId(JBro::Component::SpriteRenderer2D::StaticTypeName());

        Check(CountComponents(*object, spriteType) == 0, "it starts with none");

        auto command = JBro::MakeOwnerPtr<JBro::AddComponentCommand>(
            canvas, ids, id, TypeNameOf(JBro::Component::SpriteRenderer2D::StaticTypeName()));
        JBro::AddComponentCommand* raw = command.Get();
        Check(commands.Execute(std::move(command)), "adding must go through");
        Check(CountComponents(*object, spriteType) == 1, "and put one on");
        Check(raw->GetComponent() != nullptr, "and hand it back");
        Check(object->GetComponent<JBro::Component::SpriteRenderer2D>().Get() != nullptr,
            "and the object must find it by type");

        Check(commands.Undo(), "undo must run");
        Check(CountComponents(*object, spriteType) == 0, "and take it off");
        Check(object->GetComponent<JBro::Component::SpriteRenderer2D>().Get() == nullptr,
            "the object must not find it any more");

        Check(commands.Redo(), "redo must run");
        Check(CountComponents(*object, spriteType) == 1, "and put it back");

        // 모르는 타입은 붙이지 않는다. 실패한 편집이 스택에 남으면 다음 Ctrl+Z 가
        // 일어나지도 않은 일을 되돌린다.
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::AddComponentCommand>(
                canvas, ids, id, TypeNameOf("Component::NobodyRegisteredThis"))),
            "a type the registry never heard of must be refused");
        Check(commands.GetUndoCount() == 1, "and must not be remembered");
    }

    // 뗀 것을 되돌리면 **값까지** 돌아와야 한다. 껍데기만 다시 붙이면 되돌린 것이
    // 아니라 비슷한 것을 새로 만든 것이다.
    void TestRemovingAComponentBringsBackItsValues()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(sprite != nullptr, "the subject must have a sprite renderer");
        sprite->renderOrder = 23;
        sprite->tint = {0.25f, 0.5f, 0.75f, 1.0f};
        sprite->SetEnabled(false);

        const JBro::ComponentTypeId spriteType = sprite->GetTypeId();
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, sprite)),
            "removing must go through");
        Check(CountComponents(*object, spriteType) == 0, "and take it off");

        Check(commands.Undo(), "undo must run");
        auto* restored = object->GetComponent<JBro::Component::SpriteRenderer2D>().Get();
        Check(restored != nullptr, "and put one back");
        Check(restored->renderOrder == 23, "with the value it had");
        Check(restored->tint.G > 0.49f && restored->tint.G < 0.51f,
            "including the ones inside a struct");
        Check(false == restored->IsEnabled(),
            "and a component that was switched off must come back switched off");

        // 다시 떼고 다시 되살려도 같아야 한다. 되살린 것은 맨 끝에 붙었다가
        // 원래 자리로 옮겨지므로, 다시하기가 옮긴 뒤의 자리를 따라가야 한다.
        Check(commands.Redo(), "redo must run");
        Check(CountComponents(*object, spriteType) == 0, "and take it off again");
        Check(commands.Undo(), "and undo once more");
        Check(object->GetComponent<JBro::Component::SpriteRenderer2D>().Get() != nullptr,
            "bringing it back a second time");
    }

    // 같은 타입이 둘 붙어 있을 때 **가리킨 그것**이 떨어져야 한다.
    void TestRemovingPicksTheRightOneOfTwoOfAKind()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        auto* first = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        auto* second = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        Check(first != nullptr && second != nullptr, "two of a kind must attach");
        first->radius = 1.5f;
        second->radius = 4.5f;

        std::uint32_t ordinal = 99;
        Check(JBro::FindComponentOrdinal(*object, *second, ordinal),
            "the second one must be findable");
        Check(ordinal == 1, "and must be the second of its kind");
        Check(JBro::FindComponentAt(*object, second->GetTypeId(), 1) == second,
            "and that number must find it again");
        Check(JBro::FindComponentAt(*object, second->GetTypeId(), 0) == first,
            "while the first keeps its own");
        Check(JBro::FindComponentAt(*object, second->GetTypeId(), 2) == nullptr,
            "and there is no third");

        const JBro::ComponentTypeId type = second->GetTypeId();
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, second)),
            "removing the second must go through");
        Check(CountComponents(*object, type) == 1, "leaving one");
        auto* survivor = JBro::FindComponentAt(*object, type, 0);
        Check(survivor == first, "and it must be the one we did not point at");
        Check(static_cast<JBro::Component::Collider2D*>(survivor)->radius > 1.4f
                && static_cast<JBro::Component::Collider2D*>(survivor)->radius < 1.6f,
            "with its own value untouched");

        Check(commands.Undo(), "undo must run");
        Check(CountComponents(*object, type) == 2, "and bring the other one back");
    }

    // 컴포넌트 슬롯의 자리를 옮긴다. **넷으로 잰다** - 셋이면 가운데를 빼서 밀어낸
    // 것과 마지막 것을 끌어다 덮은 것이 같은 결과라 둘을 가려내지 못한다(D-84).
    void TestComponentSlotsCanBeRearranged()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Subject");
        JBro::ComponentBase* slots[4] = {};
        for (int index = 0; index < 4; ++index)
        {
            slots[index] = canvas.AttachComponent<JBro::Component::Collider2D>(object);
            Check(slots[index] != nullptr, "four colliders must attach");
        }
        auto expect = [&](const int order[4], const char* message) {
            for (std::size_t at = 0; at < 4; ++at)
            {
                std::size_t found = 99;
                Check(object->FindComponentIndex(slots[order[at]], found) && found == at,
                    message);
            }
        };

        const int start[4] = {0, 1, 2, 3};
        expect(start, "they sit in the order they were attached");

        Check(object->SetComponentIndex(slots[0], 2), "moving the first back must work");
        const int movedBack[4] = {1, 2, 0, 3};
        expect(movedBack, "and slide the ones between forward, leaving the last alone");

        Check(object->SetComponentIndex(slots[3], 1), "moving the last forward must work");
        const int movedForward[4] = {1, 3, 2, 0};
        expect(movedForward, "and push the ones between back");

        Check(object->SetComponentIndex(slots[1], 99), "an index past the end is the end");
        const int toEnd[4] = {3, 2, 0, 1};
        expect(toEnd, "so it lands last");

        JBro::GameObject* stranger = canvas.CreateObject("Stranger");
        auto* foreign = canvas.AttachComponent<JBro::Component::Collider2D>(stranger);
        std::size_t index = 0;
        Check(false == object->FindComponentIndex(foreign, index),
            "a component on another object is not in this one");
        Check(false == object->SetComponentIndex(foreign, 0),
            "and cannot be moved within it");
        Check(false == object->SetComponentIndex(nullptr, 0), "nor can nothing");
    }

    // **슬롯을 옮기는 것도 커맨드다.** 순서가 스크립트 실행 순서라 되돌릴 수 있어야 한다.
    void TestMovingAComponentSlotCanBeUndone()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;
        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        JBro::ComponentBase* slots[3] = {
            canvas.AttachComponent<JBro::Component::Transform2D>(object),
            canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object),
            canvas.AttachComponent<JBro::Component::Collider2D>(object),
        };
        auto expect = [&](const int order[3], const char* message) {
            for (std::size_t at = 0; at < 3; ++at)
            {
                std::size_t found = 99;
                Check(object->FindComponentIndex(slots[order[at]], found) && found == at, message);
            }
        };

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveComponentCommand>(ids, id, 2, 0)),
            "moving the last slot to the front must go through");
        const int moved[3] = {2, 0, 1};
        expect(moved, "and put it first, sliding the others back");
        Check(commands.Undo(), "undo must run");
        const int start[3] = {0, 1, 2};
        expect(start, "and restore the attach order");
        Check(commands.Redo(), "redo must run");
        expect(moved, "and move it again");
        Check(commands.Undo(), "and undo once more");
        expect(start, "back to the start");

        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::MoveComponentCommand>(ids, id, 1, 1)),
            "moving a slot onto itself is refused");
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::MoveComponentCommand>(ids, id, 0, 3)),
            "and so is a slot past the end");
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::MoveComponentCommand>(ids, 12345, 0, 1)),
            "and an object number nobody handed out");
        expect(start, "leaving the order alone");
    }

    // **붙여넣기는 떠 둔 나무를 새 번호로 만들고, 다시 하기는 그 번호를 지킨다.** 지우기의
    // 되돌리기와 같은 스냅샷이다 - 다른 점은 새 번호를 받는다는 것뿐이다.
    void TestPastingBuildsTheTreeAgainUnderNewNumbers()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* parent = canvas.CreateObject("Parent");
        JBro::GameObject* child = canvas.CreateObject("Child");
        child->SetParent(parent);
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(parent);
        transform->position = {3.0f, 4.0f};
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(child);
        sprite->renderOrder = 9;
        const JBro::EditorObjectId sourceId = ids.Track(parent);

        JBro::ObjectTreeSnapshot tree;
        Check(tree.Capture(ids, *parent), "the tree must be captured");
        Check(tree.objects.Size() == 2 && tree.objects[1].parentIndex == 0,
            "flattened with the child pointing at its parent");
        JBro::Array<JBro::ObjectTreeSnapshot> clipboard;
        clipboard.Add(tree);

        const std::size_t before = canvas.GetObjectCount();
        auto command = JBro::MakeOwnerPtr<JBro::PasteObjectsCommand>(
            canvas, ids, clipboard, JBro::InvalidEditorObjectId);
        JBro::PasteObjectsCommand* raw = command.Get();
        Check(commands.Execute(std::move(command)), "pasting must go through");
        Check(canvas.GetObjectCount() == before + 2, "and add the whole tree");
        const JBro::Array<JBro::EditorObjectId> pasted = raw->GetPastedRootIds();
        Check(pasted.Size() == 1 && pasted[0] != JBro::InvalidEditorObjectId && pasted[0] != sourceId,
            "under a number of its own, not the source's");
        JBro::GameObject* copy = ids.Resolve(pasted[0]);
        Check(copy != nullptr && copy != parent && std::strcmp(copy->GetTag(), "Parent") == 0,
            "the pasted root must be a new object with the same name");
        Check(copy->GetParent() == nullptr, "at the canvas root when no parent was given");
        auto* copiedTransform = canvas.FindComponentRaw<JBro::Component::Transform2D>(copy);
        Check(copiedTransform != nullptr && copiedTransform != transform
                && copiedTransform->position.x == 3.0f,
            "with its own component carrying the copied values");
        Check(copy->GetChildren().Size() == 1, "and its child");
        JBro::GameObject* copiedChild = copy->GetChildren()[0].TryGet();
        auto* copiedSprite = canvas.FindComponentRaw<JBro::Component::SpriteRenderer2D>(copiedChild);
        Check(copiedSprite != nullptr && copiedSprite->renderOrder == 9,
            "whose component also carries its values");
        Check(transform->position.x == 3.0f && parent->GetChildren().Size() == 1,
            "leaving the source alone");

        Check(commands.Undo(), "undo must run");
        Check(canvas.GetObjectCount() == before, "and take the pasted tree away");
        Check(ids.Resolve(pasted[0]) == nullptr, "so its number finds nothing");
        Check(commands.Redo(), "redo must run");
        Check(canvas.GetObjectCount() == before + 2, "and bring the tree back");
        Check(ids.Resolve(pasted[0]) != nullptr, "under the same number as the first time");

        // 부모를 주면 그 아래에 붙는다. 부모가 지워졌으면 거절한다.
        const JBro::EditorObjectId childId = ids.Track(child);
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::PasteObjectsCommand>(
                canvas, ids, clipboard, childId)),
            "pasting under a parent must go through");
        Check(child->GetChildren().Size() == 1, "and hang the tree under it");
        Check(commands.Undo(), "undo must run");
        canvas.DestroyObject(child);
        canvas.FlushPendingDestroy();
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::PasteObjectsCommand>(
                canvas, ids, clipboard, childId)),
            "pasting under a parent that is gone is refused");
        JBro::Array<JBro::ObjectTreeSnapshot> empty;
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::PasteObjectsCommand>(
                canvas, ids, empty, JBro::InvalidEditorObjectId)),
            "and so is pasting nothing");
    }

    // **떼었다 되돌린 뒤에도 앞선 편집은 그 컴포넌트로 가야 한다.**
    //
    // 컴포넌트는 (오브젝트 번호, 타입, 같은 타입 중 몇 번째)로 가리킨다. 되돌리기가
    // 원래 자리가 아니라 맨 끝에 다시 붙이면 같은 타입 둘의 차례가 뒤바뀌고,
    // 그 전에 쌓인 편집의 "몇 번째" 가 다른 컴포넌트를 가리키게 된다.
    void TestAnEditBeforeARemovalStillFindsItsComponent()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        // 앞에 다른 타입을 하나 둔다. 떼는 것이 0번 슬롯이면 "원래 자리" 와
        // "언제나 맨 앞" 을 가려내지 못한다.
        Check(canvas.AttachComponent<JBro::Component::Transform2D>(object) != nullptr,
            "the subject must have a transform in front");
        auto* first = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        auto* second = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        Check(first != nullptr && second != nullptr, "two of a kind must attach");
        first->radius = 1.5f;
        second->radius = 4.5f;
        const JBro::ComponentTypeId type = first->GetTypeId();
        std::size_t firstSlot = 0;
        Check(object->FindComponentIndex(first, firstSlot) && firstSlot == 1,
            "the first collider sits behind the transform");

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::Component::Collider2D::StaticTypeName());
        Check(table != nullptr, "Collider2D must have registered its properties");
        const JBro::SetPropertyCommand::Path radius = PathTo(FieldIndex(*table, "radius"));

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::SetPropertyCommand>(
                ids, AddressOf(ids, *object, *first), radius,
                JBro::String("1.5"), JBro::String("2"))),
            "editing the first must go through");
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, first)),
            "and so must removing it");
        canvas.FlushPendingDestroy();

        Check(commands.Undo(), "undoing the removal must run");
        Check(CountComponents(*object, type) == 2, "and bring it back");
        auto* restored = static_cast<JBro::Component::Collider2D*>(
            JBro::FindComponentAt(*object, type, 0));
        Check(restored != nullptr && NearlyEqual(restored->radius, 2.0f),
            "as the first of its kind again, holding the edited value");
        std::size_t restoredSlot = 0;
        Check(object->FindComponentIndex(restored, restoredSlot) && restoredSlot == firstSlot,
            "in the very slot it was taken from");
        Check(commands.Undo(), "undoing the edit must run");

        // 값으로 가려낸다. 되살린 것은 주소가 새것이라 포인터로는 알아볼 수 없다.
        std::size_t untouched = 0;
        std::size_t reverted = 0;
        for (std::uint32_t ordinal = 0; ordinal < 2; ++ordinal)
        {
            auto* collider = static_cast<JBro::Component::Collider2D*>(
                JBro::FindComponentAt(*object, type, ordinal));
            Check(collider != nullptr, "both must still be there");
            if (NearlyEqual(collider->radius, 4.5f))
            {
                ++untouched;
            }
            if (NearlyEqual(collider->radius, 1.5f))
            {
                ++reverted;
            }
        }
        Check(untouched == 1, "the one that was never edited must keep its value");
        Check(reverted == 1, "and the edited one must be back to where it started");
    }

    // **되살릴 수 없는 것은 떼지 않는다**(D-76 과 같은 규칙).
    void TestRemovingIsRefusedWhenTheValuesCannotBeSaved()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Subject");
        const JBro::EditorObjectId id = ids.Track(object);
        auto* probe = canvas.AttachComponent<UnreflectedProbe>(object);
        Check(probe != nullptr, "the probe must attach");

        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, probe)),
            "a component whose values cannot be saved must not be removed");
        Check(CountComponents(*object, probe->GetTypeId()) == 1,
            "and must still be there");
        Check(false == commands.CanUndo(), "and nothing must be on the stack");

        // 가리킨 것이 없을 때도 거절한다.
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, nullptr)),
            "removing nothing must be refused");
    }

    // ── 컨테이너 ─────────────────────────────────────────────────────────

    using StockColors = JBro::Array<JBro::Color>;
    using StockCounts = JBro::Table<JBro::String, std::int32_t>;
    using StockSamples = JBro::Array<float>;

    // 컨테이너를 든 컴포넌트다. 빌트인 컴포넌트에는 아직 컨테이너 필드가 없다(D-86).
    class Stocked final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Stocked";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Stocked)

        JBRO_FIELD(StockColors, colors);
        JBRO_FIELD(StockCounts, counts);
        JBRO_FIELD(StockSamples, samples);
    };

    class Burrowed final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Burrowed";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Burrowed)

        JBRO_FIELD(Burrow, burrow);
    };

    class Obstinate final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Obstinate";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Obstinate)

        JBRO_FIELD(float, speed) = 1.0f;
        JBRO_FIELD(Stubborn, stubborn);
    };

    // 잎사귀가 넷째 칸에 있다. 길이 담을 수 있는 가장 깊은 자리다.
    class Tunneled final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Tunneled";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Tunneled)

        JBRO_FIELD(Tunnel, tunnel);
    };

    // 잎사귀가 다섯째 칸에 있다. 길이 담지 못한다.
    class Shafted final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Shafted";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Shafted)

        JBRO_FIELD(Shaft, shaft);
    };

    // 필드를 가진 구조체 원소의 목록을 든다(D-89).
    class Lighthouse final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Lighthouse";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Lighthouse)

        JBRO_FIELD(JBro::Array<Beacon>, beacons);
    };

    // 원소 200개는 글자로 뜨면 512바이트를 한참 넘는다. 스냅샷은 잎사귀를 그 크기의
    // 고정 버퍼로 읽었다 - 컨테이너를 넣는 순간 잘리거나 빠진다.
    constexpr std::size_t SampleCount = 200;

    Stocked* MakeStocked(JBro::Canvas& canvas, JBro::GameObject* object)
    {
        JBro::RegisterBuiltinProperties<Stocked>();
        JBro::RegisterComponentType<Stocked>();
        auto* stocked = canvas.AttachComponent<Stocked>(object);
        Check(stocked != nullptr, "the stocked component must attach");
        stocked->colors.Add(JBro::Color{1.0f, 0.5f, 0.25f, 1.0f});
        stocked->colors.Add(JBro::Color{0.0f, 0.0f, 1.0f, 0.5f});
        stocked->counts.TryAdd(JBro::String("gold"), 12);
        stocked->counts.TryAdd(JBro::String("arrows"), 30);
        for (std::size_t index = 0; index < SampleCount; ++index)
        {
            stocked->samples.Add(static_cast<float>(index) + 0.125f);
        }
        return stocked;
    }

    void CheckStocked(const Stocked* stocked, const char* what)
    {
        Check(stocked != nullptr, what);
        Check(stocked->colors.Size() == 2 && NearlyEqual(stocked->colors[1].A, 0.5f),
            "the colors must come back whole");
        const std::int32_t* arrows = stocked->counts.Find(JBro::String("arrows"));
        Check(stocked->counts.Size() == 2 && arrows != nullptr && *arrows == 30,
            "the table must come back with each value under its key");
        Check(stocked->samples.Size() == SampleCount
                && NearlyEqual(stocked->samples[SampleCount - 1], 199.125f),
            "and a list longer than any fixed buffer must come back to its last element");
    }

    // **지웠다 되살리면 컨테이너도 돌아온다.** 처음에는 스냅샷이 컨테이너를 조용히
    // 건너뛰어, 되살린 컴포넌트의 배열과 표가 비어 있었고 삭제는 성공했다고 말했다.
    void TestDeletingBringsBackContainers()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Holder");
        const JBro::EditorObjectId id = ids.Track(object);
        MakeStocked(canvas, object);

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::DeleteObjectCommand>(
                canvas, ids, object)),
            "deleting the holder must go through");
        canvas.FlushPendingDestroy();
        Check(commands.Undo(), "and undoing it must run");

        JBro::GameObject* restored = ids.Resolve(id);
        Check(restored != nullptr, "the holder must come back");
        CheckStocked(restored->GetComponent<Stocked>().Get(), "with its stocked component");
    }

    void TestRemovingBringsBackContainers()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Holder");
        const JBro::EditorObjectId id = ids.Track(object);
        Stocked* stocked = MakeStocked(canvas, object);

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, stocked)),
            "removing the stocked component must go through");
        canvas.FlushPendingDestroy();
        Check(object->GetComponent<Stocked>().Get() == nullptr, "and take it off");
        Check(commands.Undo(), "undoing it must run");
        CheckStocked(object->GetComponent<Stocked>().Get(), "and put it back");
    }

    // 컨테이너 하나가 프로퍼티 커맨드의 잎사귀다. **전체의 전과 후**를 든다.
    void TestAContainerIsOneEditableValue()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Holder");
        Stocked* stocked = MakeStocked(canvas, object);
        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup(Stocked::StaticTypeName());
        Check(table != nullptr, "the stocked component must have its table");
        const JBro::SetPropertyCommand::Path colors = PathTo(FieldIndex(*table, "colors"));
        const JBro::ComponentTypeId typeId = stocked->GetTypeId();

        JBro::String before;
        Check(JBro::SetPropertyCommand::ReadValue(*stocked, typeId, colors, before),
            "a container must read as one value");

        stocked->colors.Add(JBro::Color{0.25f, 0.25f, 0.25f, 1.0f});
        JBro::String after;
        Check(JBro::SetPropertyCommand::ReadValue(*stocked, typeId, colors, after),
            "and read again after it grew");
        Check(after != before, "the two readings must differ");
        Check(JBro::SetPropertyCommand::ApplyValue(*stocked, typeId, colors, before),
            "writing the old reading back must work");
        Check(stocked->colors.Size() == 2, "and shrink it back");

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::SetPropertyCommand>(
                ids, AddressOf(ids, *object, *stocked), colors, before, after)),
            "an edit of the whole container must go through");
        Check(stocked->colors.Size() == 3, "and leave three colors");
        Check(commands.Undo(), "undo must run");
        Check(stocked->colors.Size() == 2, "and bring back two");

        // **읽히지 않는 글자는 배열을 반쯤 바꾸지 않는다.** 앞 원소를 읽고 뒤에서
        // 실패하면 배열이 줄어든 채로 남는다 - 되돌리기가 그 상태를 되돌릴 방법이 없다.
        JBro::String broken("Value:");
        broken += "\n  -\n    - 1\n    - 1\n    - 1\n    - 1\n  - not a color\n";
        Check(false == JBro::SetPropertyCommand::ApplyValue(*stocked, typeId, colors, broken),
            "a reading that does not parse into the container must be refused");
        Check(stocked->colors.Size() == 2 && NearlyEqual(stocked->colors[1].A, 0.5f),
            "and leave the container as it was");
    }

    // **떠 둘 수 없는 값이 있으면 지우지 않는다.** 그 값만 빼고 성공이라 말하면
    // 되살린 컴포넌트에서 그 값이 기본값으로 바뀌고 아무도 모른다.
    void TestDeletingIsRefusedWhenAValueRefusesToBeWritten()
    {
        RegisterOnce();
        JBro::RegisterBuiltinProperties<Obstinate>();
        JBro::RegisterComponentType<Obstinate>();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;

        JBro::GameObject* object = canvas.CreateObject("Holder");
        const JBro::EditorObjectId id = ids.Track(object);
        auto* obstinate = canvas.AttachComponent<Obstinate>(object);
        Check(obstinate != nullptr, "the obstinate component must attach");

        JBro::ComponentSnapshot snapshot;
        Check(false == JBro::CaptureComponent(*obstinate, snapshot),
            "a component with a value that will not be written must not count as captured");

        JBro::DeleteObjectCommand command(canvas, ids, object);
        Check(false == command.Execute(), "so deleting its object must be refused");
        canvas.FlushPendingDestroy();
        Check(ids.Resolve(id) == object, "and the object must still be there");

        // 같은 값이 구조체 안에 숨어 있어도 같다.
        JBro::RegisterBuiltinProperties<Burrowed>();
        auto* burrowed = canvas.AttachComponent<Burrowed>(canvas.CreateObject("Deeper"));
        Check(burrowed != nullptr, "the burrowed component must attach");
        JBro::ComponentSnapshot deeper;
        Check(false == JBro::CaptureComponent(*burrowed, deeper),
            "a value that will not be written must fail the capture from inside a struct too");
    }

    // **길이 담지 못하는 깊이의 값이 있으면 떼지 않는다.** 처음에는 스냅샷이 다섯째 칸을
    // 조용히 건너뛰었다 - 떼기는 성공했고, 되돌린 컴포넌트에서 그 값만 기본값이었다.
    // 저장 파일은 깊이 제한 없이 쓰므로, 파일에는 남는 값이 되돌리기에서만 사라진다.
    void TestRemovingIsRefusedWhenAValueIsTooDeepToAddress()
    {
        RegisterOnce();
        JBro::RegisterBuiltinProperties<Tunneled>();
        JBro::RegisterComponentType<Tunneled>();
        JBro::RegisterBuiltinProperties<Shafted>();
        JBro::RegisterComponentType<Shafted>();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* object = canvas.CreateObject("Holder");
        const JBro::EditorObjectId id = ids.Track(object);

        // 넷째 칸은 길이 담는다. 떼었다 되돌리면 값이 돌아와야 한다 - 경계를 한 칸
        // 당겨서 막으면 여기서 드러난다.
        auto* tunneled = canvas.AttachComponent<Tunneled>(object);
        Check(tunneled != nullptr, "the tunneled component must attach");
        tunneled->tunnel.warren.den.warmth = 7.5f;
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, tunneled)),
            "a value four steps down must not stop the removal");
        canvas.FlushPendingDestroy();
        Check(commands.Undo(), "undoing the removal must run");
        const Tunneled* back = object->GetComponent<Tunneled>().Get();
        Check(back != nullptr && NearlyEqual(back->tunnel.warren.den.warmth, 7.5f),
            "and bring back the value four steps down");

        // 다섯째 칸은 길이 담지 못한다. 떠 둘 수 없으니 떼지도 않는다.
        auto* shafted = canvas.AttachComponent<Shafted>(object);
        Check(shafted != nullptr, "the shafted component must attach");
        shafted->shaft.tunnel.warren.den.warmth = 7.5f;
        JBro::ComponentSnapshot snapshot;
        Check(false == JBro::CaptureComponent(*shafted, snapshot),
            "a value five steps down must not count as captured");
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::RemoveComponentCommand>(
                canvas, ids, id, shafted)),
            "so the component holding it must not be removed");
        canvas.FlushPendingDestroy();
        Check(object->GetComponent<Shafted>().Get() == shafted
                && NearlyEqual(shafted->shaft.tunnel.warren.den.warmth, 7.5f),
            "and must still be there with its value");
    }

    // ── 목록 편집 ────────────────────────────────────────────────────────

    JBro::ListEdit SetElement(std::uint32_t index, float delta)
    {
        JBro::ListEdit edit;
        edit.kind = JBro::ListEdit::Kind::SetElement;
        edit.index = index;
        edit.deltaCount = 1;
        edit.delta[0] = delta;
        return edit;
    }

    JBro::ListEdit MoveElement(std::uint32_t from, std::uint32_t to)
    {
        JBro::ListEdit edit;
        edit.kind = JBro::ListEdit::Kind::Move;
        edit.index = from;
        edit.to = to;
        return edit;
    }

    bool FloatsAre(const JBro::Array<float>& values, std::initializer_list<float> expected)
    {
        if (values.Size() != expected.size())
        {
            return false;
        }
        std::size_t at = 0;
        for (float value : expected)
        {
            if (false == NearlyEqual(values[at], value))
            {
                return false;
            }
            ++at;
        }
        return true;
    }

    // 편집 하나가 타입을 모르는 배열에 제대로 닿는다. **넷으로 잰다** - 셋이면 밀어서
    // 끼운 것과 끝의 것을 끌어다 덮은 것이 같은 결과가 된다(D-84).
    void TestAListEditLandsOnOneArray()
    {
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<StockSamples>::Get();
        StockSamples values;
        // **용량을 원소 수에 딱 맞춘다.** 옮기기가 임시 자리로 배열을 한 칸 늘리던 때, 늘기
        // 전에 받아 둔 원소 주소는 저장소가 옮겨 가면 죽었다 - 처음 구현이 그랬다. 지금은
        // 제자리에서 돌리지만(D-89), 옮기기가 다시 자리를 늘리면 여기서 드러난다.
        values.Reserve(4);
        values.Add(1.0f);
        values.Add(2.0f);
        values.Add(3.0f);
        values.Add(4.0f);
        Check(values.Capacity() == 4, "the test needs a full array so that growing moves it");

        Check(JBro::ApplyListEdit(type, &values, SetElement(1, 0.5f)),
            "a numeric element edit must land");
        Check(FloatsAre(values, {1.0f, 2.5f, 3.0f, 4.0f}), "adding its delta to that element only");

        Check(JBro::ApplyListEdit(type, &values, MoveElement(0, 2)), "moving back must land");
        Check(FloatsAre(values, {2.5f, 3.0f, 1.0f, 4.0f}),
            "sliding the ones between forward and leaving the last alone");
        Check(JBro::ApplyListEdit(type, &values, MoveElement(3, 1)), "moving forward must land");
        Check(FloatsAre(values, {2.5f, 4.0f, 3.0f, 1.0f}), "pushing the ones between back");

        JBro::ListEdit remove;
        remove.kind = JBro::ListEdit::Kind::Remove;
        remove.index = 1;
        Check(JBro::ApplyListEdit(type, &values, remove), "removing must land");
        Check(FloatsAre(values, {2.5f, 3.0f, 1.0f}), "keeping the order of what is left");

        JBro::ListEdit add;
        add.kind = JBro::ListEdit::Kind::Add;
        Check(JBro::ApplyListEdit(type, &values, add), "adding must land");
        Check(FloatsAre(values, {2.5f, 3.0f, 1.0f, 0.0f}), "with a default element at the end");

        // 맞지 않는 편집은 거짓이다. 부르는 쪽이 그 대상을 빼야 한다.
        Check(false == JBro::ApplyListEdit(type, &values, SetElement(9, 1.0f)),
            "an element that is not there cannot be edited");
        JBro::ListEdit removeMissing = remove;
        removeMissing.index = 9;
        Check(false == JBro::ApplyListEdit(type, &values, removeMissing),
            "nor removed");
        Check(false == JBro::ApplyListEdit(type, &values, MoveElement(0, 9)),
            "nor moved past the end");
        Check(FloatsAre(values, {2.5f, 3.0f, 1.0f, 0.0f}), "and a refused edit changes nothing");
    }

    // **코덱이 없는 원소도 옮긴다.** 옮기기는 원소 코덱의 `Assign` 을 빌렸는데, 필드로 말하는
    // 타입에는 코덱이 없다 - `Color` 목록을 끌어 놓으면 대상이 전부 빠지고 아무 일도 없었다.
    void TestAListEditMovesElementsThatHaveNoCodec()
    {
        const JBro::TypeDescriptor& colorType = JBro::TypeDescriptorOf<StockColors>::Get();
        Check(colorType.element != nullptr && colorType.element->codec == nullptr,
            "the test needs an element that speaks through fields");
        StockColors colors;
        colors.Add(JBro::Color{0.1f, 0.0f, 0.0f, 1.0f});
        colors.Add(JBro::Color{0.2f, 0.0f, 0.0f, 1.0f});
        colors.Add(JBro::Color{0.3f, 0.0f, 0.0f, 1.0f});
        Check(JBro::ApplyListEdit(colorType, &colors, MoveElement(0, 2)),
            "a color must move like any element");
        Check(NearlyEqual(colors[0].R, 0.2f) && NearlyEqual(colors[1].R, 0.3f)
                && NearlyEqual(colors[2].R, 0.1f),
            "sliding the ones between forward");

        // 필드의 종류가 섞인 구조체다. 한 줄 숫자 묶음으로도 읽히지 않는다.
        using Burrows = JBro::Array<Burrow>;
        const JBro::TypeDescriptor& burrowType = JBro::TypeDescriptorOf<Burrows>::Get();
        Burrows burrows;
        burrows.Add(Burrow{1.0f, Stubborn{10}});
        burrows.Add(Burrow{2.0f, Stubborn{20}});
        burrows.Add(Burrow{3.0f, Stubborn{30}});
        Check(JBro::ApplyListEdit(burrowType, &burrows, MoveElement(2, 0)),
            "a struct with mixed fields must move too");
        Check(NearlyEqual(burrows[0].depth, 3.0f) && burrows[0].stubborn.unused == 30
                && NearlyEqual(burrows[1].depth, 1.0f) && burrows[2].stubborn.unused == 20,
            "carrying every field with it");
        Check(false == JBro::ApplyListEdit(burrowType, &burrows, MoveElement(3, 0)),
            "and a move from past the end is still refused");
    }

    JBro::ListEdit FieldEdit(std::uint32_t index, std::initializer_list<std::uint32_t> fields)
    {
        Check(fields.size() <= JBro::ListEdit::MaxFieldDepth, "the test must name a path that fits");
        JBro::ListEdit edit;
        edit.kind = JBro::ListEdit::Kind::SetElement;
        edit.index = index;
        for (std::uint32_t field : fields)
        {
            edit.fieldPath[edit.fieldDepth] = field;
            ++edit.fieldDepth;
        }
        return edit;
    }

    JBro::ListEdit FieldDelta(
        std::uint32_t index, std::initializer_list<std::uint32_t> fields, float delta)
    {
        JBro::ListEdit edit = FieldEdit(index, fields);
        edit.deltaCount = 1;
        edit.delta[0] = delta;
        return edit;
    }

    // **원소 안의 필드 하나에 닿는다**(D-89). 필드의 종류가 섞인 원소는 한 줄에 그리지 못하므로
    // 필드마다 고치고, 편집은 그 필드까지 내려가는 길을 든다. 숫자는 델타, 나머지는 글자다.
    void TestAListEditReachesAFieldInsideAnElement()
    {
        using Beacons = JBro::Array<Beacon>;
        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<Beacons>::Get();
        const JBro::PropertyTable& fields = *JBro::TypeDescriptorOf<Beacon>::Get().fields;
        const std::uint32_t range = FieldIndex(fields, "range");
        const std::uint32_t lit = FieldIndex(fields, "lit");
        const std::uint32_t tint = FieldIndex(fields, "tint");
        const std::uint32_t den = FieldIndex(fields, "den");
        const std::uint32_t pulses = FieldIndex(fields, "pulses");
        const std::uint32_t warmth =
            FieldIndex(*JBro::TypeDescriptorOf<Den>::Get().fields, "warmth");

        Beacons beacons;
        beacons.Add(Beacon{});
        beacons.Add(Beacon{});
        beacons[1].range = 2.0f;

        Check(JBro::ApplyListEdit(type, &beacons, FieldDelta(1, {range}, 0.5f)),
            "a float field inside an element must take a delta");
        Check(NearlyEqual(beacons[1].range, 2.5f) && NearlyEqual(beacons[0].range, 0.0f),
            "on that element only");

        JBro::ListEdit light = FieldEdit(0, {lit});
        light.text = "true";
        Check(JBro::ApplyListEdit(type, &beacons, light), "a flag inside an element must take its text");
        Check(beacons[0].lit && false == beacons[1].lit, "on that element only");

        JBro::ListEdit tinted = FieldEdit(1, {tint});
        tinted.deltaCount = 4;
        tinted.delta[1] = 0.25f;
        Check(JBro::ApplyListEdit(type, &beacons, tinted),
            "a color inside an element must take four deltas");
        Check(NearlyEqual(beacons[1].tint.G, 0.25f) && NearlyEqual(beacons[1].tint.R, 0.0f),
            "each on its own member");

        Check(JBro::ApplyListEdit(type, &beacons, FieldDelta(1, {den, warmth}, 3.0f)),
            "a field two steps down must be reached");
        Check(NearlyEqual(beacons[1].den.warmth, 3.0f) && NearlyEqual(beacons[0].den.warmth, 0.0f),
            "on that element only");

        // 맞지 않는 길은 거짓이다. 부르는 쪽이 그 대상을 뺀다.
        Check(false == JBro::ApplyListEdit(type, &beacons, FieldDelta(1, {den}, 1.0f)),
            "a struct inside an element is a branch, not a leaf");
        JBro::ListEdit inner = FieldDelta(1, {pulses}, 1.0f);
        Check(false == JBro::ApplyListEdit(type, &beacons, inner),
            "a list inside an element is not edited from here");
        inner.deltaCount = 0;
        inner.text = "- 1";
        Check(false == JBro::ApplyListEdit(type, &beacons, inner), "not even from its text");
        Check(false == JBro::ApplyListEdit(type, &beacons, FieldDelta(1, {9}, 1.0f)),
            "a field that is not there cannot be reached");
        Check(false == JBro::ApplyListEdit(type, &beacons, FieldDelta(1, {range, 0}, 1.0f)),
            "nor can a path go on past a leaf");
        JBro::ListEdit tooDeep = FieldDelta(1, {range}, 1.0f);
        tooDeep.fieldDepth = JBro::ListEdit::MaxFieldDepth + 1;
        Check(false == JBro::ApplyListEdit(type, &beacons, tooDeep),
            "a path longer than an edit can hold must be refused");
        Check(false == JBro::ApplyListEdit(type, &beacons, FieldDelta(2, {range}, 1.0f)),
            "and a field of an element that is not there cannot be reached either");

        Check(NearlyEqual(beacons[1].range, 2.5f) && NearlyEqual(beacons[1].den.warmth, 3.0f)
                && beacons[1].pulses.IsEmpty() && beacons.Size() == 2,
            "refused edits change nothing");
    }

    // 원소 안의 필드 편집도 **고른 대상마다 다시 적용하고 한 되돌리기로 묶는다.** 되돌리기 값은
    // 목록 전체의 글자라서, 고치지 않은 필드(색·안쪽 구조체·안쪽 배열)도 그 글자를 타고 온전히
    // 돌아와야 한다.
    void TestAFieldInsideAnElementReachesEveryChosenTarget()
    {
        RegisterOnce();
        JBro::RegisterBuiltinProperties<Lighthouse>();
        JBro::RegisterComponentType<Lighthouse>();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* first = canvas.CreateObject("Near");
        JBro::GameObject* second = canvas.CreateObject("Far");
        auto* near_ = canvas.AttachComponent<Lighthouse>(first);
        auto* far_ = canvas.AttachComponent<Lighthouse>(second);
        Check(near_ != nullptr && far_ != nullptr, "both lighthouses must attach");

        near_->beacons.Add(Beacon{});
        near_->beacons[0].range = 1.0f;
        near_->beacons[0].tint = JBro::Color{0.5f, 0.25f, 0.125f, 1.0f};
        near_->beacons[0].den.warmth = 4.0f;
        near_->beacons[0].pulses.Add(1.0f);
        near_->beacons[0].pulses.Add(2.0f);
        far_->beacons.Add(Beacon{});
        far_->beacons[0].range = 10.0f;
        far_->beacons[0].lit = true;
        far_->beacons[0].pulses.Add(7.0f);

        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup(Lighthouse::StaticTypeName());
        Check(table != nullptr, "the lighthouse must have its table");
        const JBro::SetPropertyCommand::Path beacons = PathTo(FieldIndex(*table, "beacons"));
        const JBro::PropertyTable& fields = *JBro::TypeDescriptorOf<Beacon>::Get().fields;

        JBro::Array<JBro::ComponentAddress> targets;
        targets.Add(AddressOf(ids, *first, *near_));
        targets.Add(AddressOf(ids, *second, *far_));

        JBro::Array<JBro::ListEdit> edits;
        edits.Add(FieldDelta(0, {FieldIndex(fields, "range")}, 0.5f));
        JBro::ListEdit light = FieldEdit(0, {FieldIndex(fields, "lit")});
        light.text = "true";
        edits.Add(light);

        auto command = JBro::MakeListEditCommand(ids, targets, beacons, edits);
        Check(command->GetCount() == 2, "an edit inside an element must reach both targets");
        Check(NearlyEqual(near_->beacons[0].range, 1.0f) && false == near_->beacons[0].lit,
            "and building the command must leave the values as they were");
        Check(commands.Execute(std::move(command)), "the edit must go through");

        const Beacon& nearBeacon = near_->beacons[0];
        const Beacon& farBeacon = far_->beacons[0];
        Check(NearlyEqual(nearBeacon.range, 1.5f) && nearBeacon.lit,
            "the near beacon moves by the delta and lights up");
        Check(NearlyEqual(farBeacon.range, 10.5f) && farBeacon.lit,
            "the far one moves by the same delta from its own range");
        Check(NearlyEqual(nearBeacon.tint.G, 0.25f) && NearlyEqual(nearBeacon.den.warmth, 4.0f)
                && nearBeacon.pulses.Size() == 2 && NearlyEqual(nearBeacon.pulses[1], 2.0f),
            "and every field that was not edited survives the trip through the text");

        Check(commands.Undo(), "undo must run");
        Check(NearlyEqual(near_->beacons[0].range, 1.0f) && false == near_->beacons[0].lit
                && near_->beacons[0].pulses.Size() == 2,
            "and bring the near beacon back");
        Check(NearlyEqual(far_->beacons[0].range, 10.0f) && far_->beacons[0].lit
                && far_->beacons[0].pulses.Size() == 1,
            "and the far one");
    }

    // 숫자가 아닌 원소는 글자를 그대로 쓰고, 숫자 묶음은 개수가 맞아야 한다.
    void TestAListEditRespectsWhatTheElementIs()
    {
        using Flags = JBro::Array<bool>;
        const JBro::TypeDescriptor& flagType = JBro::TypeDescriptorOf<Flags>::Get();
        Flags flags;
        flags.Add(false);
        flags.Add(false);
        JBro::ListEdit set;
        set.kind = JBro::ListEdit::Kind::SetElement;
        set.index = 1;
        set.text = "true";
        Check(JBro::ApplyListEdit(flagType, &flags, set), "a flag must be set from its text");
        Check(false == flags[0] && flags[1], "on that element only");

        const JBro::TypeDescriptor& colorType = JBro::TypeDescriptorOf<StockColors>::Get();
        StockColors colors;
        colors.Add(JBro::Color{0.5f, 0.5f, 0.5f, 1.0f});
        JBro::ListEdit tint;
        tint.kind = JBro::ListEdit::Kind::SetElement;
        tint.index = 0;
        tint.deltaCount = 4;
        tint.delta[0] = 0.25f;
        tint.delta[3] = -0.5f;
        Check(JBro::ApplyListEdit(colorType, &colors, tint), "a color takes four deltas");
        Check(NearlyEqual(colors[0].R, 0.75f) && NearlyEqual(colors[0].G, 0.5f)
                && NearlyEqual(colors[0].A, 0.5f),
            "each on its own member");
        Check(false == JBro::ApplyListEdit(colorType, &colors, SetElement(0, 1.0f)),
            "and one delta does not fit a color");
    }

    // **고른 대상마다 다시 적용하고, 한 되돌리기로 묶는다.** 원소가 모자란 대상은 빠진다.
    void TestAListEditReachesEveryChosenTarget()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");
        Stocked* long_ = MakeStocked(canvas, first);
        Stocked* short_ = MakeStocked(canvas, second);
        short_->colors.RemoveAt(1);
        short_->samples.Clear();
        short_->samples.Add(10.0f);

        const JBro::PropertyTable* table =
            JBro::PropertyRegistry::Lookup(Stocked::StaticTypeName());
        Check(table != nullptr, "the stocked component must have its table");
        const JBro::SetPropertyCommand::Path samples = PathTo(FieldIndex(*table, "samples"));

        JBro::Array<JBro::ComponentAddress> targets;
        targets.Add(AddressOf(ids, *first, *long_));
        targets.Add(AddressOf(ids, *second, *short_));

        // 둘째 원소에 델타 - 짧은 쪽에는 둘째가 없다.
        JBro::Array<JBro::ListEdit> nudge;
        nudge.Add(SetElement(1, 2.0f));
        auto nudged = JBro::MakeListEditCommand(ids, targets, samples, nudge);
        Check(nudged->GetCount() == 1, "a target without that element must be left out");
        Check(NearlyEqual(long_->samples[1], 1.125f),
            "and building the command must leave the values as they were");
        Check(commands.Execute(std::move(nudged)), "the nudge must go through");
        Check(NearlyEqual(long_->samples[1], 3.125f), "adding the delta to the long list");
        Check(short_->samples.Size() == 1 && NearlyEqual(short_->samples[0], 10.0f),
            "and leaving the short one alone");

        // 첫 원소를 지우고 하나 더한다 - 둘 다에 닿고, 한 되돌리기다.
        JBro::Array<JBro::ListEdit> reshape;
        JBro::ListEdit remove;
        remove.kind = JBro::ListEdit::Kind::Remove;
        remove.index = 0;
        reshape.Add(remove);
        JBro::ListEdit add;
        add.kind = JBro::ListEdit::Kind::Add;
        reshape.Add(add);
        auto reshaped = JBro::MakeListEditCommand(ids, targets, samples, reshape);
        Check(reshaped->GetCount() == 2, "edits every target can take must reach both");
        const std::size_t undoBefore = commands.GetUndoCount();
        Check(commands.Execute(std::move(reshaped)), "the reshape must go through");
        Check(commands.GetUndoCount() == undoBefore + 1, "as one undo");
        Check(long_->samples.Size() == SampleCount && NearlyEqual(long_->samples[0], 3.125f)
                && NearlyEqual(long_->samples[SampleCount - 1], 0.0f),
            "the long list lost its first and gained a default last");
        Check(short_->samples.Size() == 1 && NearlyEqual(short_->samples[0], 0.0f),
            "the short list too");

        Check(commands.Undo(), "undo must run");
        Check(NearlyEqual(long_->samples[0], 0.125f) && NearlyEqual(long_->samples[1], 3.125f),
            "and bring the long list back");
        Check(short_->samples.Size() == 1 && NearlyEqual(short_->samples[0], 10.0f),
            "and the short one");

        // **앞 편집은 되고 뒤 편집이 막히는 대상은 통째로 빠진다.** 짧은 쪽은 하나를
        // 더해도 여섯째 원소가 없다. 반쯤 적용된 결과를 커맨드로 올리면 사용자가 한
        // 적 없는 편집(원소 하나만 늘어난 목록)이 남는다.
        JBro::Array<JBro::ListEdit> growThenNudge;
        growThenNudge.Add(add);
        growThenNudge.Add(SetElement(5, 1.0f));
        auto partly = JBro::MakeListEditCommand(ids, targets, samples, growThenNudge);
        Check(partly->GetCount() == 1, "a target that takes only the first edit must be left out");
        Check(short_->samples.Size() == 1, "and be left as it was while the command is built");
        Check(commands.Execute(std::move(partly)), "the rest must still go through");
        Check(short_->samples.Size() == 1 && NearlyEqual(short_->samples[0], 10.0f),
            "without growing the short list by the half it could take");
        Check(commands.Undo(), "and undo cleanly");

        // 아무것도 바뀌지 않는 편집은 올리지 않는다.
        JBro::Array<JBro::ListEdit> nothing;
        nothing.Add(SetElement(0, 0.0f));
        Check(JBro::MakeListEditCommand(ids, targets, samples, nothing)->GetCount() == 0,
            "an edit that changes nothing must leave nothing to undo");
    }

    // ── 계층 이동 ────────────────────────────────────────────────────────

    // **형제 사이의 차례는 사람이 보는 순서다.** 부모를 바꿔도 남은 형제들의
    // 차례가 흐트러지면 안 된다 - 그래서 `SetParent` 가 순서를 지키며 뺀다.
    void TestChildOrderSurvivesEverything()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("Parent");
        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");
        JBro::GameObject* third = canvas.CreateObject("Third");
        first->SetParent(parent);
        second->SetParent(parent);
        third->SetParent(parent);

        std::size_t index = 99;
        Check(parent->FindChildIndex(second, index) && index == 1,
            "children keep the order they were added in");
        Check(false == parent->FindChildIndex(parent, index),
            "something that is not a child has no index");

        // **넷째가 있어야 밀기와 자리바꿈이 갈린다.**
        //
        // 셋 [a,b,c] 에서 b 를 뺄 때는 마지막을 끌어다 덮어도 [a,c] 가 되어
        // 밀어낸 것과 결과가 같다. 넷 [a,b,c,d] 에서 b 를 빼야 갈린다 -
        // 밀면 [a,c,d], 끌어다 덮으면 [a,d,c] 다.
        JBro::GameObject* fourth = canvas.CreateObject("Fourth");
        fourth->SetParent(parent);

        second->SetParent(nullptr);
        Check(parent->GetChildren().Size() == 3, "one left, three remain");
        Check(parent->FindChildIndex(first, index) && index == 0,
            "the first stays first after a sibling leaves");
        Check(parent->FindChildIndex(third, index) && index == 1,
            "the third moves up by one, rather than the last being swapped in");
        Check(parent->FindChildIndex(fourth, index) && index == 2,
            "and the last stays last");

        // 뒤의 정리는 아래가 이어서 본다. 넷째는 다시 빼 둔다.
        fourth->SetParent(nullptr);

        // 자리 옮기기.
        second->SetParent(parent);
        Check(parent->FindChildIndex(second, index) && index == 2,
            "coming back puts it at the end");
        Check(parent->SetChildIndex(second, 0), "moving it to the front must work");
        Check(parent->FindChildIndex(second, index) && index == 0, "and land there");
        Check(parent->FindChildIndex(first, index) && index == 1,
            "pushing the others back");
        Check(parent->SetChildIndex(second, 99), "an index past the end is the end");
        Check(parent->FindChildIndex(second, index) && index == 2, "so it goes last");
        Check(false == parent->SetChildIndex(nullptr, 0), "nothing cannot be moved");
        Check(false == parent->SetChildIndex(third->GetChildren().IsEmpty()
                ? parent : nullptr, 0),
            "and neither can something that is not a child");
    }

    // 끌어 옮기기를 되돌릴 수 있어야 한다. **부모와 자리가 함께** 돌아온다.
    void TestMovingInTheHierarchyCanBeUndone()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* alpha = canvas.CreateObject("Alpha");
        JBro::GameObject* beta = canvas.CreateObject("Beta");
        JBro::GameObject* moved = canvas.CreateObject("Moved");
        JBro::GameObject* sibling = canvas.CreateObject("Sibling");
        moved->SetParent(alpha);
        sibling->SetParent(alpha);

        const JBro::EditorObjectId movedId = ids.Track(moved);
        const JBro::EditorObjectId betaId = ids.Track(beta);

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, movedId, betaId, 0)),
            "moving under another object must go through");
        Check(moved->GetParent() == beta, "and land there");
        std::size_t index = 99;
        Check(alpha->FindChildIndex(sibling, index) && index == 0,
            "the sibling left behind closes the gap");

        Check(commands.Undo(), "undo must run");
        Check(moved->GetParent() == alpha, "and put it back under its old parent");
        Check(alpha->FindChildIndex(moved, index) && index == 0,
            "at the place it had, not at the end");
        Check(alpha->FindChildIndex(sibling, index) && index == 1,
            "with the sibling back behind it");

        Check(commands.Redo(), "redo must run");
        Check(moved->GetParent() == beta, "and move it again");

        // **0 번이 아닌 자리로도 옮겨져야 한다.** 늘 맨 앞에 꽂는 구현과
        // 구분하려면 0 이 아닌 자리를 한 번은 써야 한다.
        JBro::GameObject* one = canvas.CreateObject("One");
        JBro::GameObject* two = canvas.CreateObject("Two");
        one->SetParent(beta);
        two->SetParent(beta);
        // 지금 beta 의 자식은 [moved, one, two] 다.
        Check(beta->FindChildIndex(moved, index) && index == 0, "moved is first");

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, movedId, betaId, 2)),
            "moving it to the third place must go through");
        Check(beta->FindChildIndex(moved, index) && index == 2,
            "and land at that place, not at the front");
        Check(beta->FindChildIndex(one, index) && index == 0,
            "with the others sliding up");
        Check(beta->FindChildIndex(two, index) && index == 1, "both of them");

        Check(commands.Undo(), "undo must run");
        Check(beta->FindChildIndex(moved, index) && index == 0,
            "and put it back at the front where it was");

        one->SetParent(nullptr);
        two->SetParent(nullptr);

        // **제자리로 옮기는 것은 편집이 아니다.**
        Check(commands.Undo(), "back to the start");
        const std::size_t before = commands.GetUndoCount();
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, movedId, ids.Track(alpha), 0)),
            "moving something to where it already is must be refused");
        Check(commands.GetUndoCount() == before, "and must not be remembered");

        // **자기 밑으로는 못 들어간다.** 들어가면 나무가 고리가 된다.
        const JBro::EditorObjectId siblingId = ids.Track(sibling);
        Check(false == commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, ids.Track(alpha), siblingId, 0)),
            "moving a parent under its own child must be refused");
        Check(sibling->GetParent() == alpha, "and must change nothing");
    }

    // **끌어다 놓은 것이 화면에서 튀면 안 된다.** 부모가 바뀌면 같은 로컬 값이
    // 다른 월드 자리를 뜻하므로, 새 부모 기준으로 로컬을 다시 구한다.
    void TestMovingKeepsTheObjectWhereItLooks()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* anchor = canvas.CreateObject("Anchor");
        auto* anchorTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(anchor);
        JBro::GameObject* floating = canvas.CreateObject("Floating");
        auto* floatingTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(floating);

        // 트랜스폼 시스템이 돌지 않으므로 월드 값을 손으로 세운다. 실제
        // 에디터에서는 매 프레임 그것이 채워진다.
        anchorTransform->position = {10.0f, 0.0f};
        anchorTransform->worldPosition = {10.0f, 0.0f};
        anchorTransform->worldRotation = 0.0f;
        anchorTransform->worldScale = {2.0f, 2.0f};
        anchorTransform->worldValid = true;

        floatingTransform->position = {30.0f, 8.0f};
        floatingTransform->worldPosition = {30.0f, 8.0f};
        floatingTransform->worldRotation = 0.0f;
        floatingTransform->worldScale = {1.0f, 1.0f};
        floatingTransform->worldValid = true;

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, ids.Track(floating), ids.Track(anchor), 0)),
            "moving under the anchor must go through");

        // 부모가 (10,0) 에서 두 배로 늘어나 있으므로, 월드 (30,8) 에 머무르려면
        // 로컬은 ((30-10)/2, (8-0)/2) = (10, 4) 여야 한다.
        Check(NearlyEqual(floatingTransform->position.x, 10.0f),
            "the local x must be what keeps it where it was");
        Check(NearlyEqual(floatingTransform->position.y, 4.0f), "and the local y");
        Check(NearlyEqual(floatingTransform->scale.x, 0.5f),
            "the scale must be divided out of the parent's");

        Check(commands.Undo(), "undo must run");
        Check(NearlyEqual(floatingTransform->position.x, 30.0f),
            "and give the old local values back exactly");
        Check(NearlyEqual(floatingTransform->position.y, 8.0f), "both of them");
        Check(NearlyEqual(floatingTransform->scale.x, 1.0f), "and the scale");
    }

    // 월드 값이 아직 안 선 오브젝트는 **짐작하지 않는다.**
    void TestMovingWithoutWorldValuesLeavesTheLocalAlone()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* anchor = canvas.CreateObject("Anchor");
        canvas.AttachComponent<JBro::Component::Transform2D>(anchor);
        JBro::GameObject* fresh = canvas.CreateObject("Fresh");
        auto* freshTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(fresh);
        freshTransform->position = {5.0f, 6.0f};
        Check(false == freshTransform->worldValid,
            "a transform that has not been through a frame has no world yet");

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, ids.Track(fresh), ids.Track(anchor), 0)),
            "moving it must still work");
        Check(NearlyEqual(freshTransform->position.x, 5.0f),
            "and must leave the local value it had");
        Check(NearlyEqual(freshTransform->position.y, 6.0f), "both parts of it");

        // 트랜스폼이 아예 없는 오브젝트도 옮겨져야 한다.
        JBro::GameObject* bare = canvas.CreateObject("Bare");
        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, ids.Track(bare), ids.Track(anchor), 0)),
            "an object with no transform must move too");
        Check(bare->GetParent() == anchor, "and land under the anchor");
    }

    // 뿌리에도 **보이는 차례**가 있어야 한다(D-128). 풀 순회 순서를 그대로 쓰면
    // 부모를 붙였다 떼는 것만으로 계층의 줄이 뛴다.
    void TestRootsKeepAnOrderOfTheirOwn()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");
        JBro::GameObject* third = canvas.CreateObject("Third");

        JBro::Array<JBro::GameObject*> roots;
        canvas.GetRootObjects(roots);
        Check(roots.Size() == 3, "all three are roots");
        Check(roots[0] == first && roots[1] == second && roots[2] == third,
            "and they come in the order they were made");

        Check(canvas.SetRootIndex(third, 0), "the third one can move to the front");
        canvas.GetRootObjects(roots);
        Check(roots[0] == third && roots[1] == first && roots[2] == second,
            "and the others slide back one place");

        std::size_t index = 99;
        Check(canvas.FindRootIndex(first, index) && index == 1,
            "and the list can say where each one is");

        // **부모가 생기면 뿌리에서 빠지고, 떼면 맨 뒤로 돌아온다.** 그 사이에
        // 남은 뿌리들의 차례는 흐트러지지 않아야 한다.
        first->SetParent(second);
        canvas.GetRootObjects(roots);
        Check(roots.Size() == 2, "the child is no longer a root");
        Check(roots[0] == third && roots[1] == second, "and the rest keep their order");

        first->SetParent(nullptr);
        canvas.GetRootObjects(roots);
        Check(roots.Size() == 3, "and it is a root again");
        Check(roots[2] == first, "coming back at the end, not where it used to be");
        Check(roots[0] == third && roots[1] == second, "with the others untouched");

        // 뿌리가 아닌 것은 자리가 없다.
        first->SetParent(second);
        Check(false == canvas.FindRootIndex(first, index),
            "a child has no place among the roots");
        Check(false == canvas.SetRootIndex(first, 0), "and cannot be moved there");
    }

    // 뿌리끼리 순서를 바꾸는 것과 부모를 떼는 것이 둘 다 커맨드로 되고 되돌려져야 한다.
    // 사용자가 화면에서 짚은 자리다.
    void TestMovingAmongRootsAndOutOfAParentCanBeUndone()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::EditorObjectRegistry ids;
        JBro::EditorCommandManager commands;

        JBro::GameObject* first = canvas.CreateObject("First");
        JBro::GameObject* second = canvas.CreateObject("Second");
        JBro::GameObject* third = canvas.CreateObject("Third");

        JBro::Array<JBro::GameObject*> roots;
        canvas.GetRootObjects(roots);
        Check(roots[0] == first && roots[1] == second && roots[2] == third,
            "they start in the order they were made");

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, ids.Track(third), JBro::InvalidEditorObjectId, 0)),
            "a root can be moved among the roots");
        canvas.GetRootObjects(roots);
        Check(roots[0] == third && roots[1] == first && roots[2] == second,
            "and it lands at the front");

        Check(commands.Undo(), "undo must run");
        canvas.GetRootObjects(roots);
        Check(roots[0] == first && roots[1] == second && roots[2] == third,
            "and put the order back");

        // **부모를 떼는 것**. 뿌리로 올라간 자리까지 정해지고, 되돌리면 부모와
        // 그 안의 자리가 함께 돌아온다.
        JBro::GameObject* child = canvas.CreateObject("Child");
        JBro::GameObject* laterSibling = canvas.CreateObject("LaterSibling");
        child->SetParent(first);
        laterSibling->SetParent(first);
        const JBro::EditorObjectId childId = ids.Track(child);

        Check(commands.Execute(JBro::MakeOwnerPtr<JBro::MoveInHierarchyCommand>(
                canvas, ids, childId, JBro::InvalidEditorObjectId, 1)),
            "a child can be taken out to the roots");
        Check(child->GetParent() == nullptr, "and it has no parent any more");
        canvas.GetRootObjects(roots);
        Check(roots[1] == child, "landing at the place it was dropped, not at the end");

        std::size_t index = 99;
        Check(first->FindChildIndex(laterSibling, index) && index == 0,
            "the sibling left behind closes the gap");

        Check(commands.Undo(), "undo must run");
        Check(child->GetParent() == first, "and put it back under its parent");
        Check(first->FindChildIndex(child, index) && index == 0,
            "at the place it had");
        canvas.GetRootObjects(roots);
        Check(roots.Size() == 3, "and it is not a root any more");

        Check(commands.Redo(), "redo must run");
        Check(child->GetParent() == nullptr, "and take it out again");
        canvas.GetRootObjects(roots);
        Check(roots[1] == child, "to the same place");
    }
}

int RunEditorObjectCommandTests()
{
    TestObjectNumbersAreStableAndNeverInvented();
    TestPathsOfDifferentDepthAreDifferentPaths();
    TestAPropertyGoesThereAndComesBack();
    TestABranchIsNotALeaf();
    TestAOneLineRunIsOneValue();
    TestMergeOnlyJoinsTheSameLeafOfTheSameComponent();
    TestAnEditSurvivesItsObjectBeingDeletedAndRestored();
    TestDeletingIsRefusedWhenAValueCannotBeSaved();
    TestDeletingNothingIsRefused();
    TestRestoringBringsBackWhatWasSwitchedOff();
    TestAddingAComponentCanBeUndone();
    TestRemovingAComponentBringsBackItsValues();
    TestRemovingPicksTheRightOneOfTwoOfAKind();
    TestComponentSlotsCanBeRearranged();
    TestMovingAComponentSlotCanBeUndone();
    TestPastingBuildsTheTreeAgainUnderNewNumbers();
    TestAnEditBeforeARemovalStillFindsItsComponent();
    TestRemovingIsRefusedWhenTheValuesCannotBeSaved();
    TestDeletingBringsBackContainers();
    TestRemovingBringsBackContainers();
    TestAContainerIsOneEditableValue();
    TestDeletingIsRefusedWhenAValueRefusesToBeWritten();
    TestRemovingIsRefusedWhenAValueIsTooDeepToAddress();
    TestAListEditLandsOnOneArray();
    TestAListEditMovesElementsThatHaveNoCodec();
    TestAListEditReachesAFieldInsideAnElement();
    TestAFieldInsideAnElementReachesEveryChosenTarget();
    TestAListEditRespectsWhatTheElementIs();
    TestAListEditReachesEveryChosenTarget();
    TestChildOrderSurvivesEverything();
    TestMovingInTheHierarchyCanBeUndone();
    TestMovingKeepsTheObjectWhereItLooks();
    TestMovingWithoutWorldValuesLeavesTheLocalAlone();
    TestRootsKeepAnOrderOfTheirOwn();
    TestMovingAmongRootsAndOutOfAParentCanBeUndone();
    std::cout << "Editor object command tests passed.\n";
    return 0;
}
