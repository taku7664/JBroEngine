#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Framework3D/BuiltinComponentProperties3D.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/BuiltinComponentTypes3D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/GameObject.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace
{
    void Check(JBro::Bool condition, const char* message)
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
        JBro::Float kept = 0.0f;
        JBro::Float dropped = 0.0f;
    };

    // 128자 버퍼보다 긴 글자를 내놓는다. 문자열 필드가 생기면 실제로 밟게 될 길이다.
    struct LongText
    {
        JBro::Int32 unused = 0;
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
                    const void*, char* buffer, std::size_t capacity, std::size_t& required) noexcept -> Bool
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
                result.FromText = [](void*, const char*, std::size_t) noexcept -> JBro::Bool { return true; };
                result.Equals = [](const void*, const void*) noexcept -> JBro::Bool { return true; };
                result.Assign = [](void*, const void*) noexcept {};
                return result;
            }();

            static const TypeDescriptor descriptor = []
            {
                TypeDescriptor built;
                built.typeName = NameTable::Get().Intern("Test::LongText");
                built.size = static_cast<JBro::UInt32>(sizeof(LongText));
                built.alignment = static_cast<JBro::UInt32>(alignof(LongText));
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

    using ListedCounts = JBro::Table<JBro::String, JBro::Int32>;
    using ListedColors = JBro::Array<JBro::Color>;

    // 컨테이너를 든 컴포넌트다. 빌트인 컴포넌트에는 아직 컨테이너 필드가 없어서
    // 여기서 만든다 - 캔버스 파일이 배열과 표를 저장했다 여는지 본다(D-86).
    class Listed final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TestListed";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Listed)

        JBRO_FIELD(ListedColors, colors);
        JBRO_FIELD(ListedCounts, counts);
    };

    JBro::UInt32 FindComponent(
        const JBro::YamlDocument& document,
        JBro::UInt32 object,
        const char* typeName)
    {
        const JBro::UInt32 components = document.Find(object, "Components");
        for (std::size_t i = 0; i < document.GetCount(components); ++i)
        {
            const JBro::UInt32 component = document.GetElement(components, i);
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
        transform->SetRotationRadian(JBro::Radian(0.75f));

        JBro::YamlDocument document;
        const JBro::String text = Save(canvas);
        Reopen(text, document);

        // 파일이 실제로 어떤 모양인지 여기에 그대로 적어 둔다. 필드를 더하거나
        // 표기를 바꾸면 이 단언이 먼저 운다 — 저장 형식이 조용히 바뀌지 않게 한다.
        const char* const expected =
            "Version: 1\n"
            // 배경색은 캔버스의 값이다(D-186). 적힐지 않은 옛 파일은 기본값으로 열린다.
            "BackgroundColor:\n"
            "  R: 0.1\n"
            "  G: 0.11\n"
            "  B: 0.13\n"
            "  A: 1\n"
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
            "          - 1\n"
            "        anchor:\n"
            "          - 0.5\n"
            "          - 0.5\n";
        if (text != expected)
        {
            std::cout << "written:" << std::endl << text.c_str()
                << "expected:" << std::endl << expected;
            Check(false, "the saved shape must be the one that was agreed");
        }

        const JBro::UInt32 root = document.GetRoot();
        JBro::Int64 version = 0;
        Check(document.FindInt(root, "Version", version) && version == 1,
            "a saved canvas must say what it is");

        const JBro::UInt32 objects = document.Find(root, "Objects");
        Check(document.GetCount(objects) == 1, "the one object must be there");

        const JBro::UInt32 saved = document.GetElement(objects, 0);
        JBro::String name;
        Check(document.FindScalar(saved, "Name", name) && name == "Player",
            "the object's name must survive");
        JBro::Int64 parent = 0;
        Check(document.FindInt(saved, "ParentIndex", parent) && parent == -1,
            "an object with no parent hangs from nothing");

        const JBro::UInt32 component = FindComponent(document, saved, "Component::Transform2D");
        Check(component != JBro::YamlDocument::InvalidNode,
            "the component must be saved under its own type name");

        // 좌표는 이름 없이 나열된다. 씬 파일에서 가장 흔한 값이라 그 모양을 고정한다.
        const JBro::UInt32 position = document.Find(component, "position");
        Check(document.GetKind(position) == JBro::YamlKind::Sequence,
            "a position is written as a plain list, the way the old engine writes it");
        Check(document.GetCount(position) == 2, "with one entry per axis");
        Check(std::strcmp(document.GetText(document.GetElement(position, 0)), "1.5") == 0,
            "the x must come first and come back exactly");
        Check(std::strcmp(document.GetText(document.GetElement(position, 1)), "-2.25") == 0,
            "the y must come second and keep its sign");

        JBro::Float rotation = 0.0f;
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
        sprite->spriteId = JBro::Uuid::FromName("sprite/42");
        sprite->sprite = { 3, 7 };

        auto* body = canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        body->linearVelocity = { 5.0f, 5.0f };
        body->mass = 2.0f;

        JBro::YamlDocument document;
        Reopen(Save(canvas), document);
        const JBro::UInt32 saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);

        // 월드 캐시는 저작 값에서 다시 계산된다. 파일에 두 벌을 만들지 않는다.
        const JBro::UInt32 written = FindComponent(document, saved, "Component::Transform2D");
        Check(document.Find(written, "position") != JBro::YamlDocument::InvalidNode,
            "the authored value must be there");
        Check(document.Find(written, "worldPosition") == JBro::YamlDocument::InvalidNode,
            "the world cache must not be written");
        Check(document.Find(written, "worldValid") == JBro::YamlDocument::InvalidNode,
            "the world cache must not be written");

        // 에셋은 영속 식별자만 나간다. 핸들은 이번 실행에서의 자리다.
        const JBro::UInt32 renderer = FindComponent(document, saved, "Component::SpriteRenderer2D");
        JBro::String idText;
        char expected[JBro::Uuid::TextCapacity];
        Check(JBro::Uuid::FromName("sprite/42").ToText(expected, sizeof(expected)), "the id has a text form");
        Check(document.FindScalar(renderer, "spriteId", idText) && idText == expected,
            "the persistent id is what a scene remembers, as 32 hex digits");
        Check(document.Find(renderer, "sprite") == JBro::YamlDocument::InvalidNode,
            "the runtime handle must not be written");

        // 시뮬레이션이 다시 쓰는 값도 나가지 않는다.
        const JBro::UInt32 rigid = FindComponent(document, saved, "Component::Rigidbody2D");
        Check(document.Find(rigid, "linearVelocity") == JBro::YamlDocument::InvalidNode,
            "a simulated value must not be restored from a file");
        JBro::Float mass = 0.0f;
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
        const JBro::UInt32 objects = document.Find(document.GetRoot(), "Objects");
        Check(document.GetCount(objects) == 3, "every object must be saved");

        for (std::size_t i = 0; i < document.GetCount(objects); ++i)
        {
            const JBro::UInt32 object = document.GetElement(objects, i);
            JBro::Int64 parentIndex = 0;
            Check(document.FindInt(object, "ParentIndex", parentIndex),
                "every object must say where it hangs");
            Check(parentIndex == -1 || static_cast<std::size_t>(parentIndex) < i,
                "a parent must come before its child, so one pass can rebuild the tree");
        }

        // 관계 자체가 맞는지도 본다. 순서만 맞고 가리키는 곳이 틀릴 수 있다.
        JBro::String name;
        const JBro::UInt32 first = document.GetElement(objects, 0);
        Check(document.FindScalar(first, "Name", name) && name == "Parent",
            "the root must be written first");
        JBro::Int64 index = 0;
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
        const JBro::UInt32 saved =
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
        const JBro::UInt32 camera = FindComponent(document, saved, "Component::Camera2D");
        JBro::String projection;
        Check(document.FindScalar(camera, "projection", projection)
            && projection == "Orthographic",
            "an enum field must be saved as its name");

        // 색도 좌표처럼 이름 없이 나열된다.
        const JBro::UInt32 clear = document.Find(camera, "clearColor");
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
        const JBro::UInt32 layers = document.Find(document.GetRoot(), "Layers");
        Check(document.GetKind(layers) == JBro::YamlKind::Sequence, "layers come out as a list");
        Check(document.GetCount(layers) >= 2, "the default layer and the new one must both be there");

        JBro::Bool foundHidden = false;
        for (std::size_t i = 0; i < document.GetCount(layers); ++i)
        {
            const JBro::UInt32 layer = document.GetElement(layers, i);
            JBro::String name;
            JBro::Bool visible = true;
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
        const JBro::UInt32 saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);
        const JBro::UInt32 written =
            FindComponent(document, saved, "Component::TestRegistered");
        Check(written != JBro::YamlDocument::InvalidNode, "the component must be saved");

        const JBro::UInt32 partly = document.Find(written, "partly");
        Check(document.GetKind(partly) == JBro::YamlKind::Map,
            "a struct that is not a plain list comes out with named members");
        JBro::Float kept = 0.0f;
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
        const JBro::UInt32 saved =
            document.GetElement(document.Find(document.GetRoot(), "Objects"), 0);
        const JBro::UInt32 written =
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
        const JBro::UInt32 objects = document.Find(document.GetRoot(), "Objects");
        Check(objects != JBro::YamlDocument::InvalidNode,
            "an empty canvas must still say it has no objects");
        Check(document.GetKind(objects) == JBro::YamlKind::Sequence,
            "and say it as an empty list, not by leaving the key out");
        Check(document.GetCount(objects) == 0, "with nothing in it");

        // 오브젝트는 있고 컴포넌트가 없는 경우도 같다.
        canvas.CreateObject("Bare");
        JBro::YamlDocument second;
        Reopen(Save(canvas), second);
        const JBro::UInt32 bare =
            second.GetElement(second.Find(second.GetRoot(), "Objects"), 0);
        Check(second.GetKind(second.Find(bare, "Components")) == JBro::YamlKind::Sequence,
            "an object with no components must still carry an empty list");
    }

    // -----------------------------------------------------------------------
    // 읽기
    // -----------------------------------------------------------------------

    JBro::Bool Load(JBro::Canvas& canvas, const JBro::String& text, JBro::CanvasFileError& error);
    void LoadOrFail(JBro::Canvas& canvas, const JBro::String& text);

    // **에디터에서 감춘 오브젝트는 파일에 남고, 게임으로 묶을 때는 빠진다**(D-163, 사용자 결정 2026-09-22).
    void TestEditorHiddenIsSavedButNotPacked()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* hidden = canvas.CreateObject("Hidden");
        canvas.CreateObject("Shown");
        hidden->SetEditorHidden(true);
        Check(hidden->IsEditorHidden() && (hidden->GetFlags() & JBro::ObjectFlagEditorHidden) != 0,
            "the flag lives in the object's flags");

        const JBro::String saved = Save(canvas);
        Check(saved.find("Flags: 1") != JBro::String::npos, "the editor's save writes the flag");
        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, saved);
        JBro::Int32 hiddenCount = 0;
        reopened.ForEachObject([&](JBro::GameObject& object)
        {
            if (object.IsEditorHidden())
            {
                ++hiddenCount;
                Check(std::strcmp(object.GetName(), "Hidden") == 0, "and it comes back on the same object");
            }
        });
        Check(hiddenCount == 1, "exactly one object comes back hidden");

        JBro::String packed;
        JBro::CanvasFileError error;
        Check(JBro::WriteCanvasText(canvas, packed, error, JBro::CanvasWriteMode::Package), "packing writes the canvas");
        Check(packed.find("Flags") == JBro::String::npos, "and leaves the editor-only flag out");
    }

    JBro::Bool Load(JBro::Canvas& canvas, const JBro::String& text, JBro::CanvasFileError& error)
    {
        return JBro::ReadCanvasText(canvas, text.c_str(), text.size(), error);
    }

    void LoadOrFail(JBro::Canvas& canvas, const JBro::String& text)
    {
        JBro::CanvasFileError error;
        if (false == Load(canvas, text, error))
        {
            std::cout << "  load failed: " << error.message.c_str()
                << " (object " << error.objectName.c_str()
                << ", type " << error.typeName.c_str()
                << ", field " << error.fieldName.c_str() << ")" << std::endl;
            std::cout << "text:" << std::endl << text.c_str();
            Check(false, "a file this engine wrote must read back");
        }
    }

    // **컨테이너가 저장했다 열어도 그대로다.** 처음에는 컨테이너를 만나면 저장이
    // 멈췄다 - 필드도 코덱도 없는 값이라 적을 방법이 없다고 보았다.
    void TestContainersMakeTheRoundTrip()
    {
        JBro::RegisterBuiltinProperties<Listed>();
        JBro::RegisterComponentType<Listed>();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Holder");
            auto* listed = canvas.AttachComponent<Listed>(object);
            Check(listed != nullptr, "the listed component must attach");
            listed->colors.Add(JBro::Color{1.0f, 0.5f, 0.25f, 1.0f});
            listed->colors.Add(JBro::Color{0.0f, 0.0f, 1.0f, 0.5f});
            listed->counts.TryAdd(JBro::String("gold"), 12);
            listed->counts.TryAdd(JBro::String("arrows"), 30);
            text = Save(canvas);
        }

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        LoadOrFail(canvas, text);
        JBro::GameObject* object = nullptr;
        canvas.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        Check(object != nullptr, "the holder must come back");
        auto* listed = object->GetComponent<Listed>().Get();
        Check(listed != nullptr, "with its listed component");
        Check(listed->colors.Size() == 2, "both colors must come back");
        Check(listed->colors[1].B > 0.99f && listed->colors[1].A > 0.49f
                && listed->colors[1].A < 0.51f,
            "with their members in place");
        Check(listed->counts.Size() == 2, "both counts must come back");
        const JBro::Int32* arrows = listed->counts.Find(JBro::String("arrows"));
        Check(arrows != nullptr && *arrows == 30, "each under its own key");
    }

    void TestWhatWasSavedComesBack()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Player");
            auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
            transform->position = { 1.5f, -2.25f };
            transform->SetRotationRadian(JBro::Radian(0.75f));
            transform->scale = { 3.0f, 0.5f };

            auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(object);
            camera->projection = JBro::Component::CameraProjection2D::PixelPerfect;
            camera->orthographicSize = 12.5f;
            camera->clearColor = { 0.25f, 0.5f, 0.75f, 1.0f };
            camera->primary = true;

            auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
            sprite->spriteId = JBro::Uuid{1234567890123456789ull, 0xFEDCBA9876543210ull};
            sprite->flip = JBro::Component::SpriteFlip::Vertical;
            sprite->renderOrder = -7;
            sprite->visible = false;
            sprite->tint = { 1.0f, 0.0f, 0.5f, 0.25f };

            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);

        Check(reopened.GetObjectCount() == 1, "the object must come back");

        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        Check(object != nullptr, "the object must be reachable");
        Check(std::strcmp(object->GetName(), "Player") == 0, "its name must come back");

        auto* transform = reopened.FindComponentRaw<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the transform must be attached by name");
        Check(transform->position.x == 1.5f && transform->position.y == -2.25f,
            "a position must come back exactly");
        Check(transform->GetRotationRadian() == 0.75f, "a scalar must come back exactly");
        Check(transform->scale.x == 3.0f && transform->scale.y == 0.5f,
            "every member of a packed value must land on its own member");

        auto* camera = reopened.FindComponentRaw<JBro::Component::Camera2D>(object);
        Check(camera != nullptr, "the camera must be attached by name");
        Check(camera->projection == JBro::Component::CameraProjection2D::PixelPerfect,
            "an enum must come back as the value its name stood for");
        Check(camera->orthographicSize == 12.5f, "a float must come back exactly");
        Check(camera->clearColor.R == 0.25f && camera->clearColor.G == 0.5f
            && camera->clearColor.B == 0.75f && camera->clearColor.A == 1.0f,
            "every channel of a color must land on its own channel");
        Check(camera->primary, "a bool must come back");

        auto* sprite = reopened.FindComponentRaw<JBro::Component::SpriteRenderer2D>(object);
        Check(sprite != nullptr, "the sprite renderer must be attached by name");
        Check(sprite->spriteId == JBro::Uuid{1234567890123456789ull, 0xFEDCBA9876543210ull},
            "an asset id must survive whole, both words, not rounded through a float");
        Check(sprite->flip == JBro::Component::SpriteFlip::Vertical, "an enum must come back");
        Check(sprite->renderOrder == -7, "a negative whole number must come back");
        Check(false == sprite->visible, "false must come back as false");
        Check(sprite->tint.A == 0.25f, "the last channel must not be dropped");

        // 두 번 저장하면 글자가 같아야 한다. 다르면 읽기와 쓰기 중 한쪽이 값을 바꾸고 있다.
        const JBro::String again = Save(reopened);
        if (again != text)
        {
            std::cout << "first:" << std::endl << text.c_str()
                << "second:" << std::endl << again.c_str();
            Check(false, "saving what was loaded must produce the same file");
        }
    }

    void TestTheTreeComesBackStanding()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* parent = canvas.CreateObject("Parent");
            JBro::GameObject* child = canvas.CreateObject("Child");
            JBro::GameObject* grandchild = canvas.CreateObject("Grandchild");
            JBro::GameObject* loner = canvas.CreateObject("Loner");
            child->SetParent(parent);
            grandchild->SetParent(child);
            loner->SetActive(false);
            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);
        Check(reopened.GetObjectCount() == 4, "every object must come back");

        JBro::GameObject* parent = nullptr;
        JBro::GameObject* child = nullptr;
        JBro::GameObject* grandchild = nullptr;
        JBro::GameObject* loner = nullptr;
        reopened.ForEachObject([&](JBro::GameObject& found)
        {
            const char* tag = found.GetName();
            if (std::strcmp(tag, "Parent") == 0) { parent = &found; }
            if (std::strcmp(tag, "Child") == 0) { child = &found; }
            if (std::strcmp(tag, "Grandchild") == 0) { grandchild = &found; }
            if (std::strcmp(tag, "Loner") == 0) { loner = &found; }
        });
        Check(parent != nullptr && child != nullptr && grandchild != nullptr && loner != nullptr,
            "every object must be findable by name");

        Check(child->GetParent() == parent, "the child must hang from the parent again");
        Check(grandchild->GetParent() == child, "and the grandchild from the child");
        Check(parent->GetParent() == nullptr, "the root must stay a root");
        Check(loner->GetParent() == nullptr, "an object with no parent must stay that way");

        Check(false == loner->IsActiveSelf(), "an object saved inactive must come back inactive");
        Check(parent->IsActiveSelf(), "an object saved active must come back active");
    }

    void TestAnInactiveObjectDoesNotDisableItsComponents()
    {
        // IsActiveComponent 는 오브젝트 활성까지 합친 값이다. 그것을 저장하면
        // 꺼진 오브젝트를 저장했다 열 때 컴포넌트가 영구히 꺼진다.
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Sleeping");
            canvas.AttachComponent<JBro::Component::Transform2D>(object);
            object->SetActive(false);
            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);

        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        auto* transform = reopened.FindComponentRaw<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the component must come back");
        Check(transform->IsEnabled(),
            "a component on a sleeping object must not come back switched off");

        object->SetActive(true);
        Check(transform->IsActiveComponent(),
            "waking the object must bring its component back");
    }

    void TestAComponentSwitchedOffStaysOff()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Thing");
            auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
            transform->SetEnabled(false);
            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);
        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        auto* transform = reopened.FindComponentRaw<JBro::Component::Transform2D>(object);
        Check(false == transform->IsEnabled(),
            "a component switched off by hand must come back off");
    }

    void TestLayersComeBackWithoutPilingUp()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::Layer& background = canvas.CreateLayer("Background");
            background.SetVisible(false);
            JBro::GameObject* object = canvas.CreateObject("OnBackground");
            canvas.SetObjectLayer(object, background.GetId());
            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);

        // 캔버스는 기본 레이어를 하나 들고 시작한다. 읽으면서 또 만들면 쓰지 않는 레이어가
        // 하나씩 쌓인다.
        Check(reopened.GetLayerCount() == 2, "the layers must not pile up on top of the default one");

        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        JBro::Layer* layer = object->GetLayer();
        Check(layer != nullptr, "the object must sit on a layer");
        Check(std::strcmp(layer->GetName(), "Background") == 0,
            "it must sit on the layer it was saved on, not on whatever came first");
        Check(false == layer->IsVisible(), "a hidden layer must come back hidden");
    }

    // **화면 레이어가 파일을 오간다**(D-237). 공간과 맞춤 방식을 적고, 기본값(월드·FixedHeight)이면 적지 않는다. 앵커도 오간다.
    void TestScreenLayersComeBack()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::Layer& ui = canvas.CreateLayer("UI");
            ui.SetSpace(JBro::LayerSpace::Screen);
            ui.SetScaleMode(JBro::ScreenScaleMode::Contain);
            JBro::GameObject* object = canvas.CreateObject("Badge");
            canvas.SetObjectLayer(object, ui.GetId());
            canvas.AttachComponent<JBro::Component::Transform2D>(object)->anchor = {1.0f, 0.25f};
            text = Save(canvas);
        }
        Check(text.find("Space: Screen") != JBro::String::npos && text.find("ScaleMode: Contain") != JBro::String::npos,
            "a screen layer writes its space and scale mode");
        Check(text.find("Space: World") == JBro::String::npos && text.find("ScaleMode: FixedHeight") == JBro::String::npos,
            "a world layer and the default scale mode write nothing");

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);
        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        JBro::Layer* layer = object->GetLayer();
        Check(layer != nullptr && layer->GetSpace() == JBro::LayerSpace::Screen && layer->GetScaleMode() == JBro::ScreenScaleMode::Contain,
            "the screen layer comes back with its scale mode");
        const auto* transform = reopened.FindComponentRaw<JBro::Component::Transform2D>(object);
        Check(transform != nullptr && transform->anchor.x == 1.0f && transform->anchor.y == 0.25f, "and the anchor with it");
        Check(reopened.FindLayer(reopened.GetDefaultLayer())->GetSpace() == JBro::LayerSpace::World, "the default layer stays in the world");

        // 모르는 공간은 추측하지 않고 거절한다.
        JBro::String broken = text;
        const std::size_t at = broken.find("Space: Screen");
        broken.replace(at, 13, "Space: Overlay");
        JBro::Canvas refused(JBro::CreateDefaultAllocator());
        JBro::CanvasFileError error;
        Check(false == JBro::ReadCanvasText(refused, broken.c_str(), broken.size(), error), "an unknown layer space is refused");
    }

    // **레이어의 블렌드와 불투명도가 저장된다**(D-279). 키는 기존 엔진의 `Blend`·`Opacity` 이고, 기본값이면 적지 않는다 -
    // 블렌드가 없던 옛 캔버스는 저장해도 그대로다.
    void TestLayerBlendAndOpacityComeBack()
    {
        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::Layer& glow = canvas.CreateLayer("Glow");
            glow.SetBlend(JBro::LayerBlend::Additive);
            JBro::Layer& fog = canvas.CreateLayer("Fog");
            fog.SetOpacity(0.25f);
            fog.SetParallax(0.5f);
            // 빛을 받지 않는 레이어는 `Lit: false` 다(D-291).
            fog.SetLit(false);
            // 아래 그림을 읽는 블렌드도 이름으로 적힌다(D-283).
            canvas.CreateLayer("Tint").SetBlend(JBro::LayerBlend::ColorDodge);
            text = Save(canvas);
        }
        Check(text.find("Blend: Additive") != JBro::String::npos && text.find("Opacity: 0.25") != JBro::String::npos,
            "a blended layer writes its blend and a faded one its opacity");
        Check(text.find("Blend: Normal") == JBro::String::npos && text.find("Opacity: 1") == JBro::String::npos,
            "the defaults write nothing");

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);
        Check(reopened.GetLayerCount() == 4, "all four layers come back");
        Check(text.find("Blend: ColorDodge") != JBro::String::npos && reopened.GetLayerAt(3)->GetBlend() == JBro::LayerBlend::ColorDodge,
            "a backdrop blend is written by name and comes back");
        const JBro::Layer* base = reopened.GetLayerAt(0);
        const JBro::Layer* glow = reopened.GetLayerAt(1);
        const JBro::Layer* fog = reopened.GetLayerAt(2);
        Check(base->GetBlend() == JBro::LayerBlend::Normal && base->GetOpacity() == 1.0f, "the default layer stays plain");
        Check(glow->GetBlend() == JBro::LayerBlend::Additive && glow->GetOpacity() == 1.0f, "the glow comes back additive");
        Check(fog->GetBlend() == JBro::LayerBlend::Normal && fog->GetOpacity() == 0.25f, "the fog comes back at a quarter");
        Check(text.find("Parallax: 0.5") != JBro::String::npos && text.find("Parallax: 1") == JBro::String::npos
                && fog->GetParallax() == 0.5f && base->GetParallax() == 1.0f,
            "a parallax layer writes its factor and the default writes nothing (D-286)");
        Check(text.find("Lit: false") != JBro::String::npos && text.find("Lit: true") == JBro::String::npos
                && false == fog->IsLit() && base->IsLit() && glow->IsLit(),
            "a layer that is not lit writes so and comes back unlit, and lit layers write nothing (D-291)");
        Check(Save(reopened) == text, "and saving it again writes the same bytes");

        // 모르는 블렌드는 추측하지 않고 거절한다.
        JBro::String broken = text;
        const std::size_t at = broken.find("Blend: Additive");
        broken.replace(at, 15, "Blend: Plasma");
        JBro::Canvas refused(JBro::CreateDefaultAllocator());
        JBro::CanvasFileError error;
        Check(false == JBro::ReadCanvasText(refused, broken.c_str(), broken.size(), error), "an unknown layer blend is refused");
    }

    // **레이어 에셋**(D-287, 기존 `LayerSerializer`). 레이어 하나와 그 오브젝트를 적고, 다른 캔버스의 맨 위에 새 레이어로 읽어 넣는다. 레이어 노드는
    // 캔버스 파일과 같은 함수로 쓰여 블렌드·패럴랙스가 같이 가고, 레이어 안의 참조는 새 오브젝트끼리 이어지며, 레이어 밖을 가리키던 참조는 비어 온다.
    // 같은 파일을 두 번 넣으면 서로 다른 두 벌이다. 읽다 실패하면 캔버스는 그대로다. 캔버스 파일은 원본 에셋을 적고 되읽는다.
    void TestLayerAssetsCarryALayerAndItsObjects()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();
        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* outside = canvas.CreateObject("Outside");
            canvas.AttachComponent<JBro::Component::Transform2D>(outside);
            JBro::Layer& props = canvas.CreateLayer("Props");
            props.SetBlend(JBro::LayerBlend::Screen);
            props.SetParallax(0.5f);
            props.SetLit(false);
            JBro::GameObject* crate = canvas.CreateObject("Crate");
            JBro::GameObject* lid = canvas.CreateObject("Lid");
            canvas.SetObjectLayer(crate, props.GetId());
            lid->SetParent(crate);
            canvas.SetObjectLayer(crate, props.GetId());
            canvas.AttachComponent<JBro::Component::Transform2D>(crate)->position = {3.0f, 4.0f};
            canvas.AttachComponent<JBro::Component::Transform2D>(lid);
            canvas.AttachComponent<JBro::Component::DistanceJoint2D>(lid)->connectedObject = crate->GetScriptHandle();
            canvas.AttachComponent<JBro::Component::DistanceJoint2D>(crate)->connectedObject = outside->GetScriptHandle();
            JBro::CanvasFileError error;
            Check(JBro::WriteLayerText(canvas, props.GetId(), text, error), "a layer writes as a layer asset");
        }
        Check(text.find("Blend: Screen") != JBro::String::npos && text.find("Parallax: 0.5") != JBro::String::npos
                && text.find("Name: Crate") != JBro::String::npos && text.find("Name: Outside") == JBro::String::npos
                && text.find("LayerId") == JBro::String::npos && text.find("SourceAsset") == JBro::String::npos,
            "the file holds the layer node and only that layer's objects, with no layer ids and no link to itself");

        JBro::Canvas target(JBro::CreateDefaultAllocator());
        target.CreateObject("Existing");
        JBro::LayerId first = JBro::InvalidLayerId;
        JBro::CanvasFileError error;
        Check(JBro::ReadLayerText(target, text.c_str(), text.size(), first, error), "the layer asset reads into a canvas that has things");
        Check(target.GetLayerCount() == 2 && target.GetLayerAt(1)->GetId() == first, "it lands on top as a new layer");
        const JBro::Layer* landed = target.FindLayer(first);
        Check(false == landed->IsLit(), "a layer asset carries whether the layer is lit (D-291)");
        Check(std::strcmp(landed->GetName(), "Props") == 0 && landed->GetBlend() == JBro::LayerBlend::Screen && landed->GetParallax() == 0.5f
                && landed->GetSourceAsset().IsNull(),
            "with the layer's own values and no source until the caller links one");
        JBro::GameObject* crate = nullptr;
        JBro::GameObject* lid = nullptr;
        target.ForEachObject([&](JBro::GameObject& object) {
            if (std::strcmp(object.GetName(), "Crate") == 0)
            {
                crate = &object;
            }
            if (std::strcmp(object.GetName(), "Lid") == 0)
            {
                lid = &object;
            }
        });
        Check(crate != nullptr && lid != nullptr && lid->GetParent() == crate && crate->GetLayerId() == first && lid->GetLayerId() == first,
            "the objects come back with their parent and on the new layer");
        const auto* lidJoint = target.FindComponentRaw<JBro::Component::DistanceJoint2D>(lid);
        const auto* crateJoint = target.FindComponentRaw<JBro::Component::DistanceJoint2D>(crate);
        Check(lidJoint != nullptr && lidJoint->connectedObject.GetInstanceId() == crate->GetInstanceId(),
            "a reference inside the layer joins the new objects");
        Check(crateJoint != nullptr && false == crateJoint->connectedObject.IsValid(), "a reference out of the layer comes back empty");

        JBro::LayerId second = JBro::InvalidLayerId;
        Check(JBro::ReadLayerText(target, text.c_str(), text.size(), second, error) && second != first && target.GetObjectCount() == 5,
            "the same asset goes in twice as two separate copies");

        // 읽지 못하는 컴포넌트가 있으면 들어온 것을 다 거둔다.
        JBro::String broken = text;
        const std::size_t at = broken.find("Component::DistanceJoint2D");
        broken.replace(at, 26, "Component::NoSuchComponent");
        JBro::LayerId refused = JBro::InvalidLayerId;
        Check(false == JBro::ReadLayerText(target, broken.c_str(), broken.size(), refused, error) && refused == JBro::InvalidLayerId,
            "a layer asset that does not read is refused");
        target.FlushPendingDestroy();
        Check(target.GetLayerCount() == 3 && target.GetObjectCount() == 5, "and leaves the canvas as it was");

        // 캔버스 파일은 원본 에셋을 적고 되읽는다. 레이어 에셋은 적지 않는다(위).
        JBro::Uuid source;
        source.high = 0x1234;
        source.low = 0x5678;
        target.FindLayer(first)->SetSourceAsset(source);
        const JBro::String saved = Save(target);
        Check(saved.find("SourceAsset: ") != JBro::String::npos, "the canvas file writes where a layer came from");
        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, saved);
        Check(reopened.GetLayerAt(1)->GetSourceAsset() == source && reopened.GetLayerAt(2)->GetSourceAsset().IsNull(),
            "and reads it back on that layer only");
    }

    void TestTwoTypesCannotShareAName()
    {
        JBro::Component::RegisterBuiltinComponentTypes2D();
        const std::size_t before = JBro::ComponentRegistry::Get().GetCount();

        // 조용히 덮으면 씬 파일이 가리키는 이름에 어느 타입이 붙는지 알 수 없다.
        Check(false == JBro::RegisterComponentType<JBro::Component::Transform2D>(),
            "registering a name that is already taken must be refused");
        Check(JBro::ComponentRegistry::Get().GetCount() == before,
            "a refused registration must leave the table as it was");

        // 거절당한 뒤에도 원래 것이 그대로 붙어야 한다.
        const JBro::ComponentTypeInfo* info =
            JBro::ComponentRegistry::Get().Find("Component::Transform2D");
        Check(info != nullptr && info->Attach != nullptr,
            "the type that got there first must still be the one that attaches");
    }

    void TestReadingRefusesRatherThanGuessing()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();
        JBro::CanvasFileError error;

        // 파일에 있는데 코드에 없는 필드. 조용히 버리면 그 씬이 들고 있던 값이 사라진다.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text(
                "Version: 1\n"
                "Layers:\n"
                "  - Id: 0\n"
                "    Name: Default\n"
                "    Visible: true\n"
                "Objects:\n"
                "  - Name: A\n"
                "    Active: true\n"
                "    ParentIndex: -1\n"
                "    LayerId: 0\n"
                "    Components:\n"
                "      - Type: Component::Transform2D\n"
                "        IsEnabled: true\n"
                "        gonePropertyFromAnOlderEngine: 3\n");
            Check(false == Load(canvas, text, error),
                "a field the engine no longer knows must stop the read");
            Check(error.fieldName == "gonePropertyFromAnOlderEngine", "and name that field");
        }

        // 코드에 있는데 파일에 없는 필드는 실패가 아니다. 기본값으로 둔다.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text(
                "Version: 1\n"
                "Layers:\n"
                "  - Id: 0\n"
                "    Name: Default\n"
                "    Visible: true\n"
                "Objects:\n"
                "  - Name: A\n"
                "    Active: true\n"
                "    ParentIndex: -1\n"
                "    LayerId: 0\n"
                "    Components:\n"
                "      - Type: Component::Transform2D\n"
                "        IsEnabled: true\n"
                "        rotation: 2\n");
            LoadOrFail(canvas, text);
            JBro::GameObject* object = nullptr;
            canvas.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
            auto* transform = canvas.FindComponentRaw<JBro::Component::Transform2D>(object);
            Check(transform->GetRotationRadian() == 2.0f, "what the file did say must be read");
            Check(transform->scale.x == 1.0f && transform->scale.y == 1.0f,
                "what it did not say must keep the value the code gives it");
        }

        // 이 엔진에 없는 컴포넌트.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text(
                "Version: 1\n"
                "Layers:\n"
                "  - Id: 0\n"
                "    Name: Default\n"
                "    Visible: true\n"
                "Objects:\n"
                "  - Name: A\n"
                "    Active: true\n"
                "    ParentIndex: -1\n"
                "    LayerId: 0\n"
                "    Components:\n"
                "      - Type: Component::NoSuchProbeComponent\n"
                "        IsEnabled: true\n");
            Check(false == Load(canvas, text, error),
                "a component this engine does not have must stop the read");
            Check(error.typeName == "Component::NoSuchProbeComponent", "and name that type");
        }

        // 나열의 개수가 맞지 않는 경우. 순서가 전부이므로 어느 자리가 어느 축인지 알 수 없다.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text(
                "Version: 1\n"
                "Layers:\n"
                "  - Id: 0\n"
                "    Name: Default\n"
                "    Visible: true\n"
                "Objects:\n"
                "  - Name: A\n"
                "    Active: true\n"
                "    ParentIndex: -1\n"
                "    LayerId: 0\n"
                "    Components:\n"
                "      - Type: Component::Transform2D\n"
                "        IsEnabled: true\n"
                "        position:\n"
                "          - 1\n"
                "          - 2\n"
                "          - 3\n");
            Check(false == Load(canvas, text, error),
                "a list with the wrong number of entries must stop the read");
        }

        // 숫자 자리에 글자가 있는 경우. 기본값으로 대신하면 씬이 조용히 달라진다.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text(
                "Version: 1\n"
                "Layers:\n"
                "  - Id: 0\n"
                "    Name: Default\n"
                "    Visible: true\n"
                "Objects:\n"
                "  - Name: A\n"
                "    Active: true\n"
                "    ParentIndex: -1\n"
                "    LayerId: 0\n"
                "    Components:\n"
                "      - Type: Component::Transform2D\n"
                "        IsEnabled: true\n"
                "        rotation: sideways\n");
            Check(false == Load(canvas, text, error),
                "a value that cannot be read must stop the read");
            Check(error.fieldName == "rotation", "and name that field");
        }

        // 버전이 다른 파일.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::String text("Version: 99\nObjects:\n  []\n");
            Check(false == Load(canvas, text, error),
                "a file from another version of the format must stop the read");
        }

        // 이미 내용이 있는 캔버스. 섞으면 무엇이 파일에서 온 것인지 알 수 없다.
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            canvas.CreateObject("Already here");
            JBro::String text("Version: 1\nObjects:\n  []\n");
            Check(false == Load(canvas, text, error),
                "reading into a canvas that already holds something must be refused");
        }
    }

    // **폴리곤 콜라이더의 꼭짓점과 표면·거르기 값이 저장했다 열어도 그대로다**(D-199). 내장 컴포넌트의 첫 배열 필드다.
    void TestAPolygonColliderMakesTheRoundTrip()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Component::RegisterBuiltinComponentTypes2D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Cup");
            canvas.AttachComponent<JBro::Component::Transform2D>(object);
            auto* collider = canvas.AttachComponent<JBro::Component::Collider2D>(object);
            collider->shape = JBro::Component::ColliderShape2D::Polygon;
            collider->points = { { 0.0f, 0.0f }, { 3.0f, 0.0f }, { 3.0f, 3.0f }, { 2.0f, 3.0f }, { 2.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 3.0f }, { 0.0f, 3.0f } };
            collider->friction = 0.25f;
            collider->restitution = 0.5f;
            collider->layer = JBro::UInt32(0x4u);
            collider->mask = JBro::UInt32(0xFFFFFFF0u);
            text = Save(canvas);
        }

        JBro::Canvas loaded(JBro::CreateDefaultAllocator());
        LoadOrFail(loaded, text);
        JBro::Int32 found = 0;
        loaded.ForEach<JBro::Component::Collider2D>([&found](JBro::Component::Collider2D& collider)
        {
            ++found;
            Check(collider.shape == JBro::Component::ColliderShape2D::Polygon, "the shape comes back as a polygon");
            Check(collider.points.Size() == 8, "with all eight corners of the U");
            Check(collider.points[4].x == 2.0f && collider.points[4].y == 1.0f, "in their order");
            Check(collider.friction == 0.25f && collider.restitution == 0.5f, "and the surface values");
            Check(collider.layer.bits == 0x4u && collider.mask.bits == 0xFFFFFFF0u, "and the collision filter");
        });
        Check(found == 1, "exactly one collider comes back");
    }

    void TestA3DSceneMakesTheRoundTripToo()
    {
        JBro::Component::RegisterBuiltinComponentProperties3D();
        JBro::Component::RegisterBuiltinComponentTypes3D();

        JBro::String text;
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("Prop");
            auto* transform = canvas.AttachComponent<JBro::Component::Transform3D>(object);
            transform->position = { 1.0f, -2.5f, 0.25f };
            transform->rotation = { 0.1f, 0.2f, 0.3f, 0.9f };
            transform->scale = { 2.0f, 2.0f, 2.0f };

            auto* camera = canvas.AttachComponent<JBro::Component::Camera3D>(object);
            camera->verticalFieldOfView = 75.5f;

            auto* mesh = canvas.AttachComponent<JBro::Component::MeshRenderer3D>(object);
            mesh->meshId = JBro::Uuid::FromName("mesh/77");
            mesh->mesh = { 4, 4 };

            auto* body = canvas.AttachComponent<JBro::Component::Rigidbody3D>(object);
            body->velocity = { 9.0f, 9.0f, 9.0f };
            body->mass = 3.5f;

            text = Save(canvas);
        }

        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        LoadOrFail(reopened, text);

        JBro::GameObject* object = nullptr;
        reopened.ForEachObject([&object](JBro::GameObject& found) { object = &found; });
        Check(object != nullptr, "the object must come back");

        auto* transform = reopened.FindComponentRaw<JBro::Component::Transform3D>(object);
        Check(transform != nullptr, "a 3D component must be attachable by name too");
        Check(transform->position.x == 1.0f && transform->position.y == -2.5f
            && transform->position.z == 0.25f,
            "all three axes must land on their own members");
        Check(transform->rotation.x == 0.1f && transform->rotation.y == 0.2f
            && transform->rotation.z == 0.3f && transform->rotation.w == 0.9f,
            "a rotation must come back component for component");

        auto* camera = reopened.FindComponentRaw<JBro::Component::Camera3D>(object);
        Check(camera != nullptr && camera->verticalFieldOfView == 75.5f,
            "a 3D camera must come back");

        auto* mesh = reopened.FindComponentRaw<JBro::Component::MeshRenderer3D>(object);
        Check(mesh != nullptr && mesh->meshId == JBro::Uuid::FromName("mesh/77"),
            "the persistent asset id must come back");
        Check(mesh->mesh.index == 0 && mesh->mesh.generation == 0,
            "the runtime handle must not come back from a file; it is resolved, not saved");

        auto* body = reopened.FindComponentRaw<JBro::Component::Rigidbody3D>(object);
        Check(body != nullptr && body->mass == 3.5f, "an authored value must come back");
        Check(body->velocity.x == 0.0f && body->velocity.y == 0.0f && body->velocity.z == 0.0f,
            "a simulated value must not be restored from a file");

        const JBro::String again = Save(reopened);
        if (again != text)
        {
            std::cout << "first:" << std::endl << text.c_str()
                << "second:" << std::endl << again.c_str();
            Check(false, "saving what was loaded must produce the same file");
        }
    }
}

namespace
{
    // **태그는 있을 때만 `Tag:` 로 적히고 그대로 돌아온다**(D-297). 빈 글자는 태그가 없는 것이다.
    void TestATagIsSavedOnlyWhenSet()
    {
        JBro::Component::RegisterBuiltinComponentProperties2D();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* enemy = canvas.CreateObject("Goblin");
        JBro::GameObject* plain = canvas.CreateObject("Rock");
        Check(enemy != nullptr && plain != nullptr, "the objects must be created");
        enemy->SetTag("Enemy");
        Check(enemy->CompareTag("Enemy") && false == enemy->CompareTag("Player") && std::strcmp(enemy->GetTag(), "Enemy") == 0,
            "a set tag compares and reads back");
        Check(plain->GetTagId() == JBro::InvalidNameId && plain->CompareTag(""), "an object starts with no tag");
        plain->SetTag("Temp");
        plain->SetTag("");
        Check(plain->GetTagId() == JBro::InvalidNameId, "an empty tag clears it");

        const JBro::String text = Save(canvas);
        Check(text.find("Tag: Enemy") != JBro::String::npos && text.find("Tag:") == text.rfind("Tag:"),
            "only the tagged object writes a tag");
        JBro::Canvas reopened(JBro::CreateDefaultAllocator());
        JBro::CanvasFileError error;
        Check(Load(reopened, text, error), "the canvas must read back");
        JBro::Bool found = false;
        reopened.ForEachObject([&](JBro::GameObject& object)
        {
            if (std::strcmp(object.GetName(), "Goblin") == 0)
            {
                found = object.CompareTag("Enemy");
            }
            else
            {
                Check(object.GetTagId() == JBro::InvalidNameId, "the untagged object stays untagged");
            }
        });
        Check(found, "the tag comes back on its object");
    }
}

JBro::Int32 RunCanvasFileTests()
{
    TestATagIsSavedOnlyWhenSet();
    TestACanvasWithOneObjectSaves();
    TestWhatIsNotSavedStaysOut();
    TestAParentAlwaysComesBeforeItsChild();
    TestEveryComponentTypeIsSavedUnderItsName();
    TestLayersAreSaved();
    TestAFieldInsideAStructCanOptOut();
    TestAValueLongerThanTheBufferStillGetsWritten();
    TestAnUnregisteredComponentStopsTheSave();
    TestAnEmptyCanvasIsStillAValidFile();
    TestContainersMakeTheRoundTrip();
    TestWhatWasSavedComesBack();
    TestTheTreeComesBackStanding();
    TestAnInactiveObjectDoesNotDisableItsComponents();
    TestAComponentSwitchedOffStaysOff();
    TestLayersComeBackWithoutPilingUp();
    TestScreenLayersComeBack();
    TestLayerBlendAndOpacityComeBack();
    TestLayerAssetsCarryALayerAndItsObjects();
    TestTwoTypesCannotShareAName();
    TestReadingRefusesRatherThanGuessing();
    TestAPolygonColliderMakesTheRoundTrip();
    TestA3DSceneMakesTheRoundTripToo();
    TestEditorHiddenIsSavedButNotPacked();
    std::cout << "Canvas file tests passed.\n";
    return 0;
}
