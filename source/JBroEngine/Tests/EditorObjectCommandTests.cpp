#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

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

        JBro::SetPropertyCommand command(
            component->SafeFromThis(), typeId, path, before, JBro::String("1.5"));
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

    // **가지는 잎사귀가 아니다.** `position` 은 필드를 가진 타입이라 코덱이 없다 -
    // 여기에 글자를 쓰려 들면 쓸 방법이 없는데도 있다고 답하게 된다.
    void TestABranchIsNotALeaf()
    {
        RegisterOnce();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Probe");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::ComponentBase* component = transform;
        const JBro::ComponentTypeId typeId = component->GetTypeId();

        const std::uint32_t position = FieldIndex(TransformTable(), "position");
        const JBro::SetPropertyCommand::Path branch = PathTo(position);

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
        JBro::ComponentBase* firstComponent = firstTransform;
        JBro::ComponentBase* secondComponent = secondTransform;
        const JBro::ComponentTypeId typeId = firstComponent->GetTypeId();

        const std::uint32_t rotation = FieldIndex(TransformTable(), "rotation");
        const std::uint32_t position = FieldIndex(TransformTable(), "position");
        const JBro::SetPropertyCommand::Path rotationPath = PathTo(rotation);
        const JBro::SetPropertyCommand::Path positionY = PathTo(position, 1);

        // 같은 잎사귀: 합친다. 그리고 **처음 값은 앞쪽 것을 지킨다** - 드래그
        // 전체가 한 번에 되돌아가야 한다.
        firstTransform->rotation = 1.0f;
        JBro::SetPropertyCommand held(firstComponent->SafeFromThis(), typeId, rotationPath,
            JBro::String("1"), JBro::String("2"));
        Check(held.Execute(), "the first frame of the drag must apply");
        JBro::SetPropertyCommand next(firstComponent->SafeFromThis(), typeId, rotationPath,
            JBro::String("2"), JBro::String("3"));
        Check(next.Execute(), "and the second");
        Check(held.TryMerge(next), "the same leaf during a drag must merge");
        held.Undo();
        Check(NearlyEqual(firstTransform->rotation, 1.0f),
            "undoing the merged drag must reach back to before it started");

        // 다른 잎사귀: 합치지 않는다.
        JBro::SetPropertyCommand onRotation(firstComponent->SafeFromThis(), typeId,
            rotationPath, JBro::String("1"), JBro::String("2"));
        JBro::SetPropertyCommand onPosition(firstComponent->SafeFromThis(), typeId,
            positionY, JBro::String("0"), JBro::String("5"));
        Check(false == onRotation.TryMerge(onPosition),
            "a different field must not be folded into this one");

        // 다른 컴포넌트: 같은 필드라도 합치지 않는다.
        JBro::SetPropertyCommand onOther(secondComponent->SafeFromThis(), typeId,
            rotationPath, JBro::String("1"), JBro::String("2"));
        Check(false == onRotation.TryMerge(onOther),
            "the same field on another object must not be folded in either");
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
}

int RunEditorObjectCommandTests()
{
    TestObjectNumbersAreStableAndNeverInvented();
    TestPathsOfDifferentDepthAreDifferentPaths();
    TestAPropertyGoesThereAndComesBack();
    TestABranchIsNotALeaf();
    TestMergeOnlyJoinsTheSameLeafOfTheSameComponent();
    TestDeletingIsRefusedWhenAValueCannotBeSaved();
    TestDeletingNothingIsRefused();
    TestRestoringBringsBackWhatWasSwitchedOff();
    std::cout << "Editor object command tests passed.\n";
    return 0;
}
