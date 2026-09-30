#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// 스크립트 핫 리로드(cpp-script-plan §3.5, D-268)를 본다. 캔버스 쪽 두 걸음(`KeepScriptsAsText`·`ResolveKeptComponents`)은 한 스크립트의
// 두 판을 이 실행 안에서 등록해 보고 - 필드를 지우고, 타입을 바꾸고, 더한 판 - 에디터 쪽은 실제 시험 DLL 로 본다.

namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    bool Contains(const String& text, const char* piece)
    {
        return text.find(piece) != String::npos;
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    fs::path FreshFolder(const fs::path& name)
    {
        const fs::path root = fs::temp_directory_path() / fs::path(u8"JBro핫리로드") / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root, ignored);
        return root;
    }

    // 같은 스크립트의 두 판이다. 이름(`StaticTypeName`)이 같다 - DLL 을 고쳐 다시 빌드한 것과 같다.
    namespace First
    {
        class HotProbe final : public GameScriptBase
        {
            JBRO_REFLECT_BODY(HotProbe)
        public:
            static constexpr const char* StaticTypeName()
            {
                return "Test::HotProbe";
            }
            ComponentTypeId GetTypeId() const override
            {
                return MakeStableTypeId(StaticTypeName());
            }

            JBRO_FIELD(float, Speed) = 1.0f;
            JBRO_FIELD(float, Lives) = 3.0f;
            JBRO_FIELD(float, Gone) = 0.0f;
            JBRO_FIELD(Handle::GameObject, Target);
        };
    }

    namespace Second
    {
        // `Gone` 을 지웠고, `Lives` 를 `int` 로 바꿨고, `Jump` 를 더했다.
        class HotProbe final : public GameScriptBase
        {
            JBRO_REFLECT_BODY(HotProbe)
        public:
            static constexpr const char* StaticTypeName()
            {
                return "Test::HotProbe";
            }
            ComponentTypeId GetTypeId() const override
            {
                return MakeStableTypeId(StaticTypeName());
            }

            JBRO_FIELD(float, Speed) = 1.0f;
            JBRO_FIELD(int, Lives) = 0;
            JBRO_FIELD(Handle::GameObject, Target);
            JBRO_FIELD(float, Jump) = 9.0f;
        };
    }
}

template<>
struct JBro::RefCategoryOf<First::HotProbe>
{
    static constexpr JBro::RefCategory value = JBro::RefCategory::Script;
};
template<>
struct JBro::RefCategoryOf<Second::HotProbe>
{
    static constexpr JBro::RefCategory value = JBro::RefCategory::Script;
};

namespace
{
    // 프로퍼티 표 없이 스크립트 표에만 든 타입이다. 캔버스는 이것을 글자로 뜰 수 없다.
    class BareProbe final : public GameScriptBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Bare";
        }
        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }
    };

    // 호스트 코드가 아는 스크립트다(`AttachScript<T>`). DLL 의 풀에 들지 않는다(D-271).
    class HostProbe final : public GameScriptBase
    {
        JBRO_REFLECT_BODY(HostProbe)
    public:
        JBRO_FIELD(float, Weight) = 1.0f;

        static constexpr const char* StaticTypeName()
        {
            return "Test::HostProbe";
        }
        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }
    };

    // 이 시험만의 스크립트 표다. 다른 시험이 등록한 것을 건드리지 않는다. 끝나면 원래 표로 되돌린다.
    class ScopedScriptTables final
    {
    public:
        ScopedScriptTables()
        {
            ScriptRegistry::Bind(&m_scripts);
            PropertyRegistry::BindScript(&m_properties);
        }
        ~ScopedScriptTables()
        {
            ScriptRegistry::Bind(nullptr);
            PropertyRegistry::BindScript(nullptr);
        }
        // DLL 을 내린 것과 같다. 로더가 내릴 때 하는 일이다.
        void Clear()
        {
            m_scripts.Clear();
            m_properties.Clear();
        }

    private:
        ScriptRegistry m_scripts;
        PropertyRegistry m_properties;
    };

    ComponentBase* AttachBuiltin(Canvas& canvas, Object::GameObject* object, const char* name)
    {
        const ComponentTypeInfo* info = ComponentRegistry::Get().Find(MakeNameId(name));
        Check(info != nullptr, "the builtin is known");
        return info->Attach(canvas, object);
    }

    // 오브젝트의 컴포넌트 차례, `|`, 스크립트 차례를 타입 이름으로 적는다(D-271). 모르는 스크립트는 `?이름` 으로 그 자리에 끼운다.
    String DescribeOrder(Canvas& canvas, Object::GameObject* object)
    {
        String text;
        for (const ComponentSlot& slot : object->GetComponents())
        {
            text += NameTable::Get().Resolve(slot.typeId);
            text += " ";
        }
        text += "| ";
        const Array<ScriptSlot>& scripts = object->GetScripts();
        const Array<UnresolvedScript>* kept = canvas.FindUnresolvedScripts(object);
        std::size_t nextKept = 0;
        for (std::size_t s = 0; s <= scripts.Size(); ++s)
        {
            while (kept != nullptr && nextKept < kept->Size() && ((*kept)[nextKept].position <= s || s == scripts.Size()))
            {
                text += "?";
                text += (*kept)[nextKept].typeName;
                text += " ";
                ++nextKept;
            }
            if (s < scripts.Size())
            {
                text += NameTable::Get().Resolve(scripts[s].typeId);
                text += " ";
            }
        }
        return text;
    }

    template<typename T>
    T* FindScript(Canvas& canvas)
    {
        Array<GameScriptBase*> scripts;
        canvas.CollectScripts(scripts);
        T* first = nullptr;
        for (GameScriptBase* script : scripts)
        {
            if (first == nullptr || script->GetInstanceId() < first->GetInstanceId())
            {
                first = static_cast<T*>(script);
            }
        }
        return first;
    }

    // 떠서 두고, 다른 판을 싣고, 되살린다. 자리·번호·값·`Ref<T>` 가 이어지고, 이어지지 못한 값은 알린다.
    void TestAScriptSurvivesANewVersionOfItself()
    {
        Check(Component::RegisterBuiltinComponentProperties2D() && Component::RegisterBuiltinComponentTypes2D(),
            "the builtin 2D components register");
        ScopedScriptTables tables;
        Check(RegisterScriptType<First::HotProbe>(), "the first version registers");

        Canvas canvas(CreateDefaultAllocator());
        Object::GameObject* target = canvas.CreateObject("Target");
        Object::GameObject* holder = canvas.CreateObject("Holder");
        ComponentBase* transform = AttachBuiltin(canvas, holder, "Component::Transform2D");
        Check(transform != nullptr, "a builtin goes first");
        auto* one = static_cast<First::HotProbe*>(canvas.AttachScript(holder, First::HotProbe::StaticTypeName()));
        Check(AttachBuiltin(canvas, holder, "Component::SpriteRenderer2D") != nullptr,
            "a builtin attached between the two scripts goes to the components");
        // 호스트가 아는 스크립트가 둘 사이에 있다. 떠 두는 동안 이것만 남고, 되살린 것은 이것의 앞뒤 제자리로 온다.
        // 저장하려면 표가 있어야 한다(표 없는 타입은 저장을 멈춘다).
        Check(PropertyRegistry::RegisterScript(NameTable::Get().Intern(HostProbe::StaticTypeName()), GetPropertyTable<HostProbe>()),
            "the host script's table registers");
        Check(canvas.AttachScript<HostProbe>(holder) != nullptr, "a host script sits between the two");
        auto* two = static_cast<First::HotProbe*>(canvas.AttachScript(holder, First::HotProbe::StaticTypeName()));
        Check(one != nullptr && two != nullptr, "two copies of the script attach");
        UnresolvedScript mystery;
        mystery.typeName = "Test::Mystery";
        mystery.text = "Type: Test::Mystery\nIsEnabled: true\nValue: 5\n";
        mystery.position = 3;
        Check(canvas.AddUnresolvedScript(holder, mystery), "a script nobody knows sits at the end");

        one->Speed = 7.25f;
        one->Lives = 2.5f;
        one->Gone = 4.0f;
        one->Target = Internal::GameObjectHandleAccess::FromId(target->GetInstanceId());
        two->SetEnabled(false);
        const InstanceId oneId = one->GetInstanceId();
        const InstanceId twoId = two->GetInstanceId();
        Ref<Second::HotProbe> reference;
        reference.ObjectId = holder->GetInstanceId();
        reference.ComponentId = oneId;
        Check(reference.Get() == reinterpret_cast<Second::HotProbe*>(one), "a reference finds the first script");
        const String before = DescribeOrder(canvas, holder);
        Check(before == "Component::Transform2D Component::SpriteRenderer2D | Test::HotProbe Test::HostProbe Test::HotProbe ?Test::Mystery ", before.c_str());

        std::size_t kept = 0;
        CanvasFileError error;
        Check(KeepScriptsAsText(canvas, kept, error), error.message.c_str());
        Check(kept == 2, "both scripts are set aside");
        Array<GameScriptBase*> scripts;
        canvas.CollectScripts(scripts);
        Check(scripts.Size() == 1, "and taken off the object - only the host's script stays");
        Check(DescribeOrder(canvas, holder) == "Component::Transform2D Component::SpriteRenderer2D | ?Test::HotProbe Test::HostProbe ?Test::HotProbe ?Test::Mystery ",
            "each keeps its place in the script list");
        Check(reference.Get() == nullptr, "a reference finds nothing while the script is away");
        Check(holder->GetComponents()[0].reference.TryGet() == transform, "the builtins are left where they were");

        // 떠 둔 사이에 저장해도 값이 적힌다 - DLL 이 없는 동안 에디터가 저장하는 길이다.
        String saved;
        Check(WriteCanvasText(canvas, saved, error), "a canvas with set-aside scripts saves");
        Check(Contains(saved, "Speed: 7.25"), "with the script's values");

        // 새 판을 싣는다. 로더가 내릴 때처럼 표를 비우고 새로 등록한다.
        tables.Clear();
        Check(RegisterScriptType<Second::HotProbe>(), "the second version registers");
        Array<ScriptResolveNote> notes;
        Check(ResolveKeptScripts(canvas, notes) == 2, "both scripts come back");
        Check(DescribeOrder(canvas, holder) == before, "in the places they had");
        Check(holder->GetComponents()[0].reference.TryGet() == transform, "and the builtin was never touched");

        Second::HotProbe* newOne = nullptr;
        {
            Array<GameScriptBase*> all;
            canvas.CollectScripts(all);
            for (GameScriptBase* script : all)
            {
                if (script->GetInstanceId() == oneId)
                {
                    newOne = static_cast<Second::HotProbe*>(script);
                }
            }
        }
        Check(newOne != nullptr && newOne->GetInstanceId() == oneId, "the first comes back under its own number");
        Check(newOne->Speed == 7.25f, "a field that matches keeps its value");
        Check(newOne->Jump == 9.0f, "a new field starts at its default");
        Check(newOne->Lives == 0, "a field whose value no longer reads goes back to its default");
        Check(Internal::GameObjectHandleAccess::Resolve(newOne->Target) == target, "an object reference points where it did");
        Check(reference.Get() == newOne, "a reference made before finds the new script");
        Array<GameScriptBase*> reborn;
        canvas.CollectScripts(reborn);
        bool secondDisabled = false;
        for (GameScriptBase* script : reborn)
        {
            secondDisabled = secondDisabled || (script->GetInstanceId() == twoId && false == script->IsEnabled());
        }
        Check(secondDisabled, "the second comes back under its number and still switched off");
        Check(reborn.Size() == 3, "beside the host's script");

        std::size_t dropped = 0;
        std::size_t unreadable = 0;
        for (const ScriptResolveNote& note : notes)
        {
            Check(note.objectName == "Holder" && note.typeName == "Test::HotProbe", "each note names the object and the script");
            dropped += note.kind == ScriptResolveNote::Kind::FieldDropped && note.fieldName == "Gone" ? 1 : 0;
            unreadable += note.kind == ScriptResolveNote::Kind::FieldUnreadable && note.fieldName == "Lives" ? 1 : 0;
        }
        Check(notes.Size() == 3 && dropped == 2 && unreadable == 1,
            "the removed field is reported for both, the changed one where it did not read");
        Check(canvas.GetUnresolvedScriptCount() == 1, "the script nobody knows stays as it was");
        tables.Clear();
        canvas.ReleaseModuleScripts();
    }

    // 새 판이 그 타입을 모르면 값을 든 채 남고, 다시 알게 되면 돌아온다. 뜨지 못하는 스크립트가 있으면 아무것도 떼지 않는다.
    void TestAScriptTheNewVersionLacksKeepsItsValues()
    {
        ScopedScriptTables tables;
        Check(RegisterScriptType<First::HotProbe>(), "the first version registers");
        Canvas canvas(CreateDefaultAllocator());
        Object::GameObject* holder = canvas.CreateObject("Holder");
        auto* script = static_cast<First::HotProbe*>(canvas.AttachScript(holder, First::HotProbe::StaticTypeName()));
        script->Speed = 3.5f;

        std::size_t kept = 0;
        CanvasFileError error;
        Check(KeepScriptsAsText(canvas, kept, error) && kept == 1, "the script is set aside");
        tables.Clear();
        Array<ScriptResolveNote> notes;
        Check(ResolveKeptScripts(canvas, notes) == 0 && notes.IsEmpty(), "a library without the type brings nothing back");
        Check(canvas.GetUnresolvedScriptCount() == 1, "and the script waits with its values");
        // 다음 빌드에서 돌아온다.
        Check(RegisterScriptType<First::HotProbe>(), "the type comes back");
        Check(ResolveKeptScripts(canvas, notes) == 1, "and so does the script");
        Check(FindScript<First::HotProbe>(canvas) != nullptr && FindScript<First::HotProbe>(canvas)->Speed == 3.5f,
            "with its value");

        // 표가 없는 스크립트(프로퍼티를 등록하지 않았다)는 뜨지 못한다. 그러면 하나도 떼지 않는다.
        NameTable::Get().Intern(BareProbe::StaticTypeName());
        Check(ScriptRegistry::Get().Register(MakeScriptTypeInfo<BareProbe>()), "a script with no property table registers");
        Check(canvas.AttachScript(holder, "Test::Bare") != nullptr, "and attaches");
        Check(false == KeepScriptsAsText(canvas, kept, error), "a script that cannot be written stops the whole step");
        Array<GameScriptBase*> scripts;
        canvas.CollectScripts(scripts);
        Check(scripts.Size() == 2 && canvas.GetUnresolvedScriptCount() == 0, "and nothing was taken off");
        Check(canvas.ReleaseModuleScripts() == 2, "releasing takes off every script from the library");
        canvas.CollectScripts(scripts);
        Check(scripts.IsEmpty(), "leaving none");
        Check(canvas.AttachScript(holder, First::HotProbe::StaticTypeName()) != nullptr, "and a script attaches again afterwards");

        // **호스트가 아는 타입은 DLL 을 내려도 남는다**(D-271). 그 코드는 DLL 안에 있지 않다 - 풀 표가 따로다.
        auto* host = canvas.AttachScript<HostProbe>(holder);
        Check(host != nullptr && false == canvas.IsModuleScript(host), "a host type attaches outside the library's pools");
        Check(canvas.ReleaseModuleScripts() == 1, "releasing the library takes off only the library's script");
        canvas.CollectScripts(scripts);
        Check(scripts.Size() == 1 && scripts[0] == host && holder->GetScripts().Size() == 1,
            "and leaves the host's script on its object");
        tables.Clear();
        canvas.ReleaseModuleScripts();
    }

    // 파일에서 읽을 때 몰랐던 스크립트는 파일 안 번호로 오브젝트를 가리킨다. 뒤에 타입을 알게 되면 그 파일의 차례로 푼다.
    void TestAScriptReadBeforeItsLibraryPointsAtTheRightObject()
    {
        ScopedScriptTables tables;
        Check(RegisterScriptType<First::HotProbe>(), "the first version registers");
        String text;
        {
            Canvas canvas(CreateDefaultAllocator());
            canvas.CreateObject("Spacer");
            Object::GameObject* target = canvas.CreateObject("Target");
            Object::GameObject* holder = canvas.CreateObject("Holder");
            auto* script = static_cast<First::HotProbe*>(canvas.AttachScript(holder, First::HotProbe::StaticTypeName()));
            script->Target = Internal::GameObjectHandleAccess::FromId(target->GetInstanceId());
            CanvasFileError error;
            Check(WriteCanvasText(canvas, text, error), "the canvas saves");
            canvas.ReleaseModuleScripts();
        }
        tables.Clear();

        Canvas canvas(CreateDefaultAllocator());
        CanvasFileError error;
        Check(ReadCanvasText(canvas, text.c_str(), text.size(), error), "the canvas opens without the library");
        Check(canvas.GetUnresolvedScriptCount() == 1, "holding the script it does not know");
        Check(RegisterScriptType<First::HotProbe>(), "the library arrives");
        Array<ScriptResolveNote> notes;
        Check(ResolveKeptScripts(canvas, notes) == 1 && notes.IsEmpty(), "the script comes back");
        const Object::GameObject* found = Internal::GameObjectHandleAccess::Resolve(FindScript<First::HotProbe>(canvas)->Target);
        Check(found != nullptr && std::strcmp(found->GetTag(), "Target") == 0, "pointing at the object the file named");
        tables.Clear();
        canvas.ReleaseModuleScripts();
    }

    // ── 에디터와 실제 시험 DLL ──────────────────────────────────────────────

    // 되돌리기 기록에 한 칸을 남기는 것뿐인 커맨드다. 기록이 남는지 비는지를 본다.
    class NoteCommand final : public EditorCommand
    {
    public:
        const char* GetName() const override
        {
            return "Note";
        }
        bool Execute() override
        {
            return true;
        }
        void Undo() override
        {
        }
        void Redo() override
        {
        }
    };

    fs::path ProbeLibrary(const wchar_t* name)
    {
        wchar_t executable[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        return fs::path(executable).parent_path() / name;
    }

    PropertyInfo const* FindField(const char* type, const char* field)
    {
        const PropertyTable* table = PropertyRegistry::Lookup(type);
        for (std::uint32_t index = 0; table != nullptr && index < table->count; ++index)
        {
            if (std::strcmp(NameTable::Get().Resolve(table->properties[index].name), field) == 0)
            {
                return &table->properties[index];
            }
        }
        return nullptr;
    }

    GameScriptBase* OnlyScript(Canvas& canvas)
    {
        Array<GameScriptBase*> scripts;
        canvas.CollectScripts(scripts);
        return scripts.Size() == 1 ? scripts[0] : nullptr;
    }

    std::uint32_t ProbeRevision(EditorApplication& editor)
    {
        using GetRevision = std::uint32_t (*)() noexcept;
        const auto get = reinterpret_cast<GetRevision>(editor.GetScriptModule()->GetSymbol("JBroScriptProbe_GetRevision"));
        return get != nullptr ? get() : 0;
    }

    void TestTheEditorReloadsTheLibraryUnderItsScripts()
    {
        const fs::path root = FreshFolder(u8"에디터");
        fs::create_directories(root / "Contents" / "Assets");
        const fs::path library = root / "x64" / "Debug" / "GameScript.dll";
        fs::create_directories(library.parent_path());
        fs::copy_file(ProbeLibrary(L"JBroScriptModuleProbe.dll"), library, fs::copy_options::overwrite_existing);
        const fs::path projectPath = root / "Reloaded.jproject";
        std::ofstream(projectPath, std::ios::binary) <<
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Contents/Assets\n"
            "ScriptSourceDirectory: Contents\n"
            "ScriptOutputLibraryPath: x64/Debug/GameScript.dll\n"
            "Build:\n"
            "  ProductName: Reloaded\n";

        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; reloading scripts in the editor not verified" << std::endl;
            return;
        }
        ProjectFileError error;
        Check(editor.OpenProjectFile(Utf8(projectPath).c_str(), error), "the project opens");
        Check(editor.IsScriptModuleLoaded(), "with the probe library");
        Check(ProbeRevision(editor) == 1, "the first revision of the probe");
        Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the editor has a canvas");
        Object::GameObject* object = canvas->CreateObject("Scripted");
        Check(canvas->AttachScript(object, "ProbeRegisteredScript") != nullptr, "the probe script attaches");
        const PropertyInfo* speed = FindField("ProbeRegisteredScript", "Speed");
        Check(speed != nullptr, "the probe script has its speed field");
        *static_cast<float*>(speed->Address(OnlyScript(*canvas))) = 7.25f;
        InstanceId scriptId = OnlyScript(*canvas)->GetInstanceId();
        const std::uint64_t generation = editor.GetScriptModule()->GetGeneration();

        // 새 판의 DLL 을 같은 자리에 쓰고 다시 싣는다. 섀도 복사본을 실었으므로 원본을 덮을 수 있다.
        Check(editor.GetCommands().Execute(MakeOwnerPtr<NoteCommand>()), "an edit goes into the history");
        fs::copy_file(ProbeLibrary(L"JBroScriptModuleProbeV2.dll"), library, fs::copy_options::overwrite_existing);
        Check(editor.ReloadScripts(), "the editor reloads the library");
        Check(editor.GetCommands().GetUndoCount() == 1, "the same fields keep the undo history");
        Check(ProbeRevision(editor) == 2, "and the new revision is the one loaded");
        Check(editor.GetScriptModule()->GetGeneration() != generation, "as a new generation");
        GameScriptBase* reborn = OnlyScript(*canvas);
        Check(reborn != nullptr && reborn->GetInstanceId() == scriptId, "the script comes back under its own number");
        speed = FindField("ProbeRegisteredScript", "Speed");
        Check(speed != nullptr && *static_cast<const float*>(speed->ConstAddress(reborn)) == 7.25f, "with its value");
        Check(editor.GetNotifications().GetLastLevel() == NotificationLevel::Success, "the reload is announced");

        // **재생 중이면 멈출 때까지 미룬다.**
        Check(editor.StartSimulation(), "play starts");
        const std::uint64_t playing = editor.GetScriptModule()->GetGeneration();
        Check(false == editor.ReloadScripts() && editor.IsScriptReloadPending(), "a reload during play waits");
        Check(editor.GetScriptModule()->GetGeneration() == playing, "and the playing library stays");
        editor.StopSimulation();
        Check(false == editor.IsScriptReloadPending() && editor.GetScriptModule()->GetGeneration() != playing,
            "stopping play loads it");
        Check(OnlyScript(*canvas) != nullptr, "under the restored canvas");
        // 재생을 멈추면 캔버스를 재생 전 글자에서 다시 읽는다 - 번호는 거기서 새로 난다. 여기서부터 그 번호가 이어져야 한다.
        scriptId = OnlyScript(*canvas)->GetInstanceId();

        // **DLL 파일이 바뀌면 저절로 다시 싣는다**(Visual Studio 에서 빌드한 것). 바뀐 시각이 다음 확인에도 같아야 싣는다.
        const std::uint64_t watched = editor.GetScriptModule()->GetGeneration();
        fs::copy_file(ProbeLibrary(L"JBroScriptModuleProbe.dll"), library, fs::copy_options::overwrite_existing);
        fs::last_write_time(library, fs::file_time_type::clock::now());
        // 0.5 초마다 본다 - 프레임마다 0.5 초를 넘기면 프레임마다 한 번 본다. 처음 바뀐 것을 본 확인에서는 싣지 않는다
        // (링커가 아직 쓰고 있을 수 있다). 앞의 틱이 남긴 몫이 있어 첫 틱은 확인을 하나 더 할 수 없다 - 0.5 초에서 몫을 비운다.
        Check(editor.Tick(0.5f), "the editor ticks while the library changes");
        Check(editor.GetScriptModule()->GetGeneration() == watched, "a change seen once is not loaded yet");
        Check(editor.Tick(0.5f), "the editor ticks again");
        Check(editor.GetScriptModule()->GetGeneration() != watched && ProbeRevision(editor) == 1,
            "a library rewritten on disk is loaded again");
        Check(OnlyScript(*canvas) != nullptr && OnlyScript(*canvas)->GetInstanceId() == scriptId, "keeping the script");

        // **스크립트를 뜨지 못하면 DLL 을 내리지 않는다.** 표 없이 등록한 스크립트가 캔버스에 있으면 뜨기가 멈춘다.
        NameTable::Get().Intern(BareProbe::StaticTypeName());
        Check(ScriptRegistry::Get().Register(MakeScriptTypeInfo<BareProbe>()), "a script with no property table registers");
        GameScriptBase* bare = canvas->AttachScript(object, BareProbe::StaticTypeName());
        Check(bare != nullptr, "and attaches in the editor");
        const std::uint64_t unwritable = editor.GetScriptModule()->GetGeneration();
        Check(false == editor.ReloadScripts(), "a reload whose scripts cannot be set aside does not happen");
        Check(editor.GetScriptModule()->GetGeneration() == unwritable, "the library stays");
        Array<GameScriptBase*> stillThere;
        canvas->CollectScripts(stillThere);
        Check(stillThere.Size() == 2, "and so do both scripts");
        Check(canvas->DetachScript(object, bare), "the unwritable script comes off");
        canvas->FlushPendingDestroy();

        // 새 DLL 을 싣지 못하면 스크립트는 값을 든 채 캔버스에 남는다.
        fs::remove(library);
        Check(false == editor.ReloadScripts(), "a missing library does not load");
        Check(OnlyScript(*canvas) == nullptr && canvas->GetUnresolvedScriptCount() == 1, "the script waits in the canvas");
        String saved;
        CanvasFileError canvasError;
        Check(WriteCanvasText(*canvas, saved, canvasError) && Contains(saved, "Speed: 7.25"), "with its value");
        fs::copy_file(ProbeLibrary(L"JBroScriptModuleProbe.dll"), library, fs::copy_options::overwrite_existing);
        Check(editor.ReloadScripts() && OnlyScript(*canvas) != nullptr, "the next good library brings it back");

        // **스크립트가 붙은 채 닫아도 멈추지 않는다.** 캔버스는 DLL 이 내려간 뒤에 사라진다 - 그 전에 스크립트를 떼야 한다.
        editor.CloseProject();
        Check(false == editor.IsScriptModuleLoaded(), "closing unloads the library");
        editor.Shutdown();
    }
}

int RunScriptHotReloadTests()
{
    TestAScriptSurvivesANewVersionOfItself();
    TestAScriptTheNewVersionLacksKeepsItsValues();
    TestAScriptReadBeforeItsLibraryPointsAtTheRightObject();
    TestTheEditorReloadsTheLibraryUnderItsScripts();
    std::cout << "Script hot reload tests passed.\n";
    return 0;
}
