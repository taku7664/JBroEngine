#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::String Save(JBro::Canvas& canvas)
    {
        JBro::String text;
        JBro::CanvasFileError error;
        if (false == JBro::WriteCanvasText(canvas, text, error))
        {
            std::cout << "  save failed: " << error.message.c_str()
                << " (object " << error.objectName.c_str()
                << ", type " << error.typeName.c_str()
                << ", field " << error.fieldName.c_str() << ")\n";
            Check(false, "a canvas built by hand must save");
        }
        return text;
    }

    // 저장한 글자를 다시 문서로 읽는다. 저장이 제 형식을 지켰는지 보는 가장 곧은 방법이다.
    void Reopen(const JBro::String& text, JBro::YamlDocument& document)
    {
        JBro::YamlError error;
        if (false == document.Parse(text.c_str(), text.size(), error))
        {
            std::cout << "written:\n" << text.c_str();
            std::cout << "  reread failed at line " << error.line
                << ": " << error.message.c_str() << '\n';
            Check(false, "what the canvas writer produced must read back");
        }
    }

    // 빌트인 컴포넌트만으로는 밟지 못하는 길이 셋 있다. 여기서 일부러 만든다.
    //
    //   · 구조체 **안의** 필드가 저장에서 빠지는 경우
    //   · 프로퍼티를 등록하지 않은 컴포넌트를 저장하려는 경우
    //   · 코덱이 내놓는 글자가 스택 버퍼보다 긴 경우
    struct PartlySaved
    {
        float kept = 0.0f;
        float dropped = 0.0f;
    };

    // 128자 버퍼보다 긴 글자를 내놓는다. 문자열 필드가 생기면 실제로 밟게 될 길이다.
    struct LongText
    {
        int unused = 0;
    };

    constexpr std::size_t LongTextLength = 300;

}

namespace JBro
{
    // 구조체 안의 한 필드만 저장에서 뺀다. 맵으로 적히는 구조체라 나열이 아니다.
    template <>
    struct TypeDescriptorOf<PartlySaved>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&PartlySaved::kept>(),
                MakeFieldEntry<&PartlySaved::dropped>(Attribute::NoSerialize()),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<PartlySaved>("Test::PartlySaved", fields.Get());
            return descriptor;
        }
    };

    // 언제나 같은 긴 글자를 내놓는다. 저장이 버퍼를 늘려 다시 묻는지 보는 용도다.
    template <>
    struct TypeDescriptorOf<LongText>
    {
        static const TypeDescriptor& Get()
        {
            static const ValueCodec codec = []
            {
                ValueCodec result;
                result.ToText = [](
                    const void*, char* buffer, std::size_t capacity, std::size_t& required) noexcept -> bool
                {
                    required = LongTextLength;
                    if (buffer == nullptr || capacity < LongTextLength)
                    {
                        return false;
                    }
                    for (std::size_t i = 0; i < LongTextLength; ++i)
                    {
                        buffer[i] = 'x';
                    }
                    return true;
                };
                result.FromText = [](void*, const char*, std::size_t) noexcept { return true; };
                result.Equals = [](const void*, const void*) noexcept { return true; };
                result.Assign = [](void*, const void*) noexcept {};
                return result;
            }();

            static const TypeDescriptor descriptor = []
            {
                TypeDescriptor built;
                built.typeName = NameTable::Get().Intern("Test::LongText");
                built.size = static_cast<std::uint32_t>(sizeof(LongText));
                built.alignment = static_cast<std::uint32_t>(alignof(LongText));
                built.triviallyCopyable = true;
                built.codec = &codec;
                return built;
            }();
            return descriptor;
        }
    };
}

namespace
{
    class Registered final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TestRegistered";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Registered)

        JBRO_FIELD(PartlySaved, partly);
        JBRO_FIELD(LongText, long_);
    };

    // 등록하지 않는다. 저장이 이것을 만나면 멈춰야 한다.
    class Unregistered final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TestUnregistered";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }
    };

    std::uint32_t FindComponent(
        const JBro::YamlDocument& document,
        std::uint32_t object,
        const char* typeName)
    {
        const std::uint32_t components = document.Find(object, "Components");
        for (std::size_t i = 0; i < document.GetCount(components); ++i)
        {
            const std::uint32_t component = document.GetElement(components, i);
            JBro::String type;
            if (document.FindScalar(component, "Type", type) && type == typeName)
            {
                return component;
            }
        }
        return JBro::YamlDocument::InvalidNode;
    }

    void TestACanvasWithOneObjectSaves()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        JBro::GameObject* object = canvas.CreateObject("Player");
        Check(object != nullptr, "the object must be created");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the transform must attach");
        transform->position = { 1.5f, -2.25f };
        transform->rotation = 0.75f;

        JBro::YamlDocument document;
        const JBro::String text = Save(canvas);
        Reopen(text, document);

        // 파일이 실제로 어떤 모양인지 여기에 그대로 적어 둔다. 필드를 더하거나
        // 표기를 바꾸면 이 단언이 먼저 운다 — 저장 형식이 조용히 바뀌지 않게 한다.
        const char* const expected =
            "Version: 1\n"
            "Layers:\n"
            "  - Id: 0\n"
            "    Name: Default\n"
            "    Visible: true\n"
            "Objects:\n"
            "  - Name: Player\n"
            "    Active: true\n"
            "    ParentIndex: -1\n"
            "    LayerId: 0\n"
            "    Components:\n"
            "      - Type: Component::Transform2D\n"
            "        IsEnabled: true\n"
            "        position:\n"
            "          - 1.5\n"
            "          - -2.25\n"
            "        rotation: 0.75\n"
            "        scale:\n"
            "          - 1\n"
            "          - 1\n";
        if (text != expected)
        {
            std::cout << "written:" << std::endl << text.c_str()
                << "expected:" << std::endl << expected;
            Check(false, "the saved shape must be the one that was agreed");
        }

        const std::uint32_t root = document.GetRoot();
        std::int64_t version = 0;
        Check(document.FindInt(root, "Version", version) && version == 1,
            "a saved canvas must say what it is");

        const std::uint32_t objects = document.Find(root, "Objects");
        Check(document.GetCount(objects) == 1, "the one object must be there");

        const std::uint32_t saved = document.GetElement(objects, 0);
        JBro::String name;
        Check(document.FindScalar(saved, "Name", name) && name == "Player",
            "the object's name must survive");
        std::int64_t parent = 0;
        Check(document.FindInt(saved, "ParentIndex", parent) && parent == -1,
            "an object with no parent hangs from nothing");

        const std::uint32_t component = FindComponent(document, saved, "Component::Transform2D");
        Check(component != JBro::YamlDocument::InvalidNode,
            "the component must be saved under its own type name");

        // 좌표는 이름 없이 나열된다. 씬 파일에서 가장 흔한 값이라 그 모양을 고정한다.
        const std::uint32_t position = document.Find(component, "position");
        Check(document.GetKind(position) == JBro::YamlKind::Sequence,
            "a position is written as a plain list, the way the old engine writes it");
        Check(document.GetCount(position) == 2, "with one entry per axis");
        Check(std::strcmp(document.GetText(document.GetElement(position, 0)), "1.5") == 0,
            "the x must come first and come back exactly");
        Check(std::strcmp(document.GetText(document.GetElement(position, 1)), "-2.25") == 0,
            "the y must come second and keep its sign");

        float rotation = 0.0f;
        Check(document.FindFloat(component, "rotation", rotation) && rotation == 0.75f,
            "a scalar field is written under its own name");
    }

    void TestWhatIsNotSavedStaysOut()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        JBro::GameObject* object = canvas.CreateObject("Thing");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        transform->worldValid = true;
        transform->worldPosition = { 9.0f, 9.0f };

        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        sprite->spriteId.value = 42;
        sprite->sprite = { 3, 7 };

        auto* body = canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        body->linearVelocity = { 5.0f, 5.0f };
        body->mass = 2.0f;

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);

        // 월드 캐시는 저작 값에서 다시 계산된다. 파일에 두 벌을 만들지 않는다.
        const std::uint32_t written = FindComponent(document, saved, "Component::Transform2D");
        Check(document.Find(written, "position") != JBro::YamlDocument::InvalidNode,
            "the authored value must be there");
        Check(document.Find(written, "worldPosition") == JBro::YamlDocument::InvalidNode,
            "the world cache must not be written");
        Check(document.Find(written, "worldValid") == JBro::YamlDocument::InvalidNode,
            "the world cache must not be written");

        // 에셋은 영속 식별자만 나간다. 핸들은 이번 실행에서의 자리다.
        const std::uint32_t renderer = FindComponent(document, saved, "Component::SpriteRenderer2D");
        std::int64_t id = 0;
        Check(document.FindInt(renderer, "spriteId", id) && id == 42,
            "the persistent id is what a scene remembers");
        Check(document.Find(renderer, "sprite") == JBro::YamlDocument::InvalidNode,
            "the runtime handle must not be written");

        // 시뮬레이션이 다시 쓰는 값도 나가지 않는다.
        const std::uint32_t rigid = FindComponent(document, saved, "Component::Rigidbody2D");
        Check(document.Find(rigid, "linearVelocity") == JBro::YamlDocument::InvalidNode,
            "a simulated value must not be restored from a file");
        float mass = 0.0f;
        Check(document.FindFloat(rigid, "mass", mass) && mass == 2.0f,
            "an authored value must still be saved");
    }

    void TestAParentAlwaysComesBeforeItsChild()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        // 일부러 자식을 먼저 만든다. 풀 순서로 적으면 자식이 앞에 오고
        // ParentIndex 가 자기 뒤를 가리키게 된다.
        JBro::GameObject* child = canvas.CreateObject("Child");
        JBro::GameObject* parent = canvas.CreateObject("Parent");
        JBro::GameObject* grandchild = canvas.CreateObject("Grandchild");
        child->SetParent(parent);
        grandchild->SetParent(child);

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t objects = document.Find(document.GetRoot(), "Objects");
        Check(document.GetCount(objects) == 3, "every object must be saved");

        for (std::size_t i = 0; i < document.GetCount(objects); ++i)
        {
            const std::uint32_t object = document.GetElement(objects, i);
            std::int64_t parentIndex = 0;
            Check(document.FindInt(object, "ParentIndex", parentIndex),
                "every object must say where it hangs");
            Check(parentIndex == -1 || static_cast<std::size_t>(parentIndex) < i,
                "a parent must come before its child, so one pass can rebuild the tree");
        }

        // 관계 자체가 맞는지도 본다. 순서만 맞고 가리키는 곳이 틀릴 수 있다.
        JBro::String name;
        const std::uint32_t first = document.GetElement(objects, 0);
        Check(document.FindScalar(first, "Name", name) && name == "Parent",
            "the root must be written first");
        std::int64_t index = 0;
        Check(document.FindInt(document.GetElement(objects, 1), "ParentIndex", index) && index == 0,
            "the child must point at the root");
        Check(document.FindInt(document.GetElement(objects, 2), "ParentIndex", index) && index == 1,
            "the grandchild must point at the child");
    }

    void TestEveryComponentTypeIsSavedUnderItsName()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Everything");
        canvas.AttachComponent<JBro::Component::Transform2D>(object);
        canvas.AttachComponent<JBro::Component::Camera2D>(object);
        canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        canvas.AttachComponent<JBro::Component::Collider2D>(object);

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);
        Check(document.GetCount(document.Find(saved, "Components")) == 5,
            "every attached component must reach the file");

        const char* const types[] =
        {
            "Component::Transform2D",
            "Component::Camera2D",
            "Component::SpriteRenderer2D",
            "Component::Rigidbody2D",
            "Component::Collider2D",
        };
        for (const char* type : types)
        {
            Check(FindComponent(document, saved, type) != JBro::YamlDocument::InvalidNode,
                "each component must be findable by its own type name");
        }

        // enum 은 숫자가 아니라 이름으로 적힌다.
        const std::uint32_t camera = FindComponent(document, saved, "Component::Camera2D");
        JBro::String projection;
        Check(document.FindScalar(camera, "projection", projection)
            && projection == "Orthographic",
            "an enum field must be saved as its name");

        // 색도 좌표처럼 이름 없이 나열된다.
        const std::uint32_t clear = document.Find(camera, "clearColor");
        Check(document.GetKind(clear) == JBro::YamlKind::Sequence, "a color is a plain list");
        Check(document.GetCount(clear) == 4, "with one entry per channel");
    }

    void TestLayersAreSaved()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Layer& extra = canvas.CreateLayer("Background");
        extra.SetVisible(false);

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t layers = document.Find(document.GetRoot(), "Layers");
        Check(document.GetKind(layers) == JBro::YamlKind::Sequence, "layers come out as a list");
        Check(document.GetCount(layers) >= 2, "the default layer and the new one must both be there");

        bool foundHidden = false;
        for (std::size_t i = 0; i < document.GetCount(layers); ++i)
        {
            const std::uint32_t layer = document.GetElement(layers, i);
            JBro::String name;
            bool visible = true;
            if (document.FindScalar(layer, "Name", name) && name == "Background")
            {
                Check(document.FindBool(layer, "Visible", visible), "a layer must say if it shows");
                Check(visible == false, "a hidden layer must be saved as hidden");
                foundHidden = true;
            }
        }
        Check(foundHidden, "the layer that was created must be in the file");
    }

    void TestAFieldInsideAStructCanOptOut()
    {
        JBro::RegisterBuiltinProperties<Registered>();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Fixture");
        auto* component = canvas.AttachComponent<Registered>(object);
        component->partly.kept = 1.25f;
        component->partly.dropped = 9.0f;

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);
        const std::uint32_t written =
            FindComponent(document, saved, "Component::TestRegistered");
        Check(written != JBro::YamlDocument::InvalidNode, "the component must be saved");

        const std::uint32_t partly = document.Find(written, "partly");
        Check(document.GetKind(partly) == JBro::YamlKind::Map,
            "a struct that is not a plain list comes out with named members");
        float kept = 0.0f;
        Check(document.FindFloat(partly, "kept", kept) && kept == 1.25f,
            "the member that saves must be there");
        Check(document.Find(partly, "dropped") == JBro::YamlDocument::InvalidNode,
            "a member inside a struct must be able to stay out of the file too");
    }

    void TestAValueLongerThanTheBufferStillGetsWritten()
    {
        JBro::RegisterBuiltinProperties<Registered>();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Fixture");
        canvas.AttachComponent<Registered>(object);

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);
        const std::uint32_t written =
            FindComponent(document, saved, "Component::TestRegistered");

        // 스택 버퍼는 128자다. 그보다 긴 값을 만나면 필요한 만큼 잡고 다시 물어야 하고,
        // 그 길을 밟지 않으면 값이 잘린 채로 저장된다.
        JBro::String text;
        Check(document.FindScalar(written, "long_", text), "the long value must be written");
        Check(text.size() == LongTextLength,
            "a value longer than the buffer must come out whole, not cut short");
    }

    void TestAnUnregisteredComponentStopsTheSave()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("Fixture");
        canvas.AttachComponent<Unregistered>(object);

        // 조용히 빠뜨리면 씬이 컴포넌트 하나를 잃은 채로 저장되고 아무도 모른다.
        JBro::String text("not touched");
        JBro::CanvasFileError error;
        Check(false == JBro::WriteCanvasText(canvas, text, error),
            "a component whose properties were never registered must stop the save");
        Check(false == error.message.empty(), "the refusal must say what went wrong");
        Check(error.typeName == "Component::TestUnregistered",
            "and which type it was");
        Check(error.objectName == "Fixture", "and which object it was on");
        Check(text == "not touched", "a refused save must not hand back half a file");
    }

    void TestAnEmptyCanvasIsStillAValidFile()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const std::uint32_t objects = document.Find(document.GetRoot(), "Objects");
        Check(objects != JBro::YamlDocument::InvalidNode,
            "an empty canvas must still say it has no objects");
        Check(document.GetKind(objects) == JBro::YamlKind::Sequence,
            "and say it as an empty list, not by leaving the key out");
        Check(document.GetCount(objects) == 0, "with nothing in it");

        // 오브젝트는 있고 컴포넌트가 없는 경우도 같다.
        canvas.CreateObject("Bare");
        JBro::YamlDocument second;
        Reopen(Save(canvas), second);
        const std::uint32_t bare =
            second.GetElement(second.Find(second.GetRoot(), "Objects"), 0);
        Check(second.GetKind(second.Find(bare, "Components")) == JBro::YamlKind::Sequence,
            "an object with no components must still carry an empty list");
    }
}

int RunCanvasFileTests()
{
    TestACanvasWithOneObjectSaves();
    TestWhatIsNotSavedStaysOut();
    TestAParentAlwaysComesBeforeItsChild();
    TestEveryComponentTypeIsSavedUnderItsName();
    TestLayersAreSaved();
    TestAFieldInsideAStructCanOptOut();
    TestAValueLongerThanTheBufferStillGetsWritten();
    TestAnUnregisteredComponentStopsTheSave();
    TestAnEmptyCanvasIsStillAValidFile();
    std::cout << "Canvas file tests passed.\n";
    return 0;
}
