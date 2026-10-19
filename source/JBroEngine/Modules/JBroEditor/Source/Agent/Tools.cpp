#include <JBro/Editor/Agent/Tools.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/EditorNames.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Core/Yaml.h>
#include "../Panel/CodeEditorPanel.h"
#include <nlohmann/json.hpp>
#include <charconv>

namespace JBro::Agent
{
    using Json = nlohmann::json;
    namespace
    {
        ToolResult Ok(const Json& value) { return {true, value.dump(2)}; }
        ToolResult Error(const char* reason) { return {false, reason}; }
        String Get(const Json& value, const char* key) { return value.at(key).get<std::string>(); }
        UInt64 Number(const String& text)
        {
            std::uint64_t value = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? UInt64(value) : UInt64(0);
        }
        String Digits(UInt64 value) { return std::to_string(value.Value); }
        Bool Read(IPlatform& platform, const String& path, String& text)
        {
            Array<std::byte> bytes;
            if (!platform.ReadWholeFile(path.c_str(), bytes) || bytes.Size() > 1024 * 1024)
            {
                return false;
            }
            text = bytes.IsEmpty() ? String() : String(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
            return text.find('\0') == String::npos;
        }
        Bool Within(const String& root, const String& path)
        {
            const String base = Code::LanguageClient::NormalizePath(root);
            const String normalized = Code::LanguageClient::NormalizePath(path);
            return !base.empty() && normalized.size() > base.size() && normalized.compare(0, base.size(), base) == 0
                && normalized[base.size()] == '/';
        }
        String AssetText(AssetId asset)
        {
            char text[Uuid::TextCapacity]{};
            asset.ToText(text, sizeof(text));
            return text;
        }
        Json TargetInfo(const Target& target)
        {
            const char* kind = target.kind == Target::Kind::Asset ? "asset" : target.kind == Target::Kind::Component ? "component" : "object";
            return {{"kind", kind}, {"project", target.project}, {"canvas", target.canvas}, {"name", target.name},
                {"assetId", AssetText(target.asset)}, {"instanceId", Digits(target.instance)},
                {"componentId", Digits(target.component)}, {"revision", target.revision.Value}};
        }
        AttachedRef FindAttached(Object::GameObject& object, UInt64 instance)
        {
            for (const auto& slot : object.GetComponents())
            {
                if (slot.reference != nullptr && slot.reference->GetInstanceId() == instance)
                {
                    return AttachedRef(slot.reference.TryGet());
                }
            }
            for (const auto& slot : object.GetScripts())
            {
                if (slot.reference != nullptr && slot.reference->GetInstanceId() == instance)
                {
                    return AttachedRef(slot.reference.TryGet());
                }
            }
            return {};
        }
        Json TargetInfo(EditorApplication& editor, Object::GameObject& object)
        {
            return {{"kind", "object"}, {"project", editor.GetProjectFilePath()}, {"canvas", editor.GetCanvasPath()},
                {"instanceId", Digits(object.GetInstanceId())}, {"name", object.GetName()}, {"revision", editor.GetAgentContextRevision().Value}};
        }
    }
    struct Tools::State
    {
        struct Snapshot { String path, token, text; UInt64 revision; };
        EditorApplication& editor;
        Array<Snapshot> snapshots;
        Array<Target> attachments;
        UInt64 nextToken = 1;
        explicit State(EditorApplication& value) : editor(value) {}
        String Scope() const
        {
            return editor.GetProjectFilePath() + "|" + editor.GetCanvasPath() + "|" + Digits(editor.GetAgentContextRevision());
        }
        Object::GameObject* Find(UInt64 instance)
        {
            auto* canvas = editor.GetCanvas();
            Object::GameObject* found = nullptr;
            if (canvas != nullptr && instance != 0)
            {
                canvas->ForEachObject([&](Object::GameObject& candidate) {
                    if (candidate.GetInstanceId() == instance)
                    {
                        found = &candidate;
                    }
                });
            }
            return found;
        }
        Object::GameObject* Find(const String& id)
        {
            return Find(Number(id));
        }
        CodeEditorPanel* Document(const String& path)
        {
            Array<EditorPanel*> panels;
            editor.FindPanels(CodeEditorPanel::TypeName, panels);
            for (auto* panel : panels)
            {
                auto* document = static_cast<CodeEditorPanel*>(panel);
                if (document->GetPath() == Code::LanguageClient::NormalizePath(path))
                {
                    return document;
                }
            }
            return nullptr;
        }
        String ScriptPath(const String& relative)
        {
            if (relative.empty() || relative.find('\0') != String::npos || relative.find(':') != String::npos || relative[0] == '/' || relative[0] == '\\')
            {
                return {};
            }
            const String path = EditorPaths::Normalize(EditorPaths::JoinPath(editor.GetScriptRoot().c_str(), relative.c_str()));
            if (!Within(editor.GetScriptRoot(), path) || !editor.GetPlatform().IsPathWithinDirectory(editor.GetScriptRoot().c_str(), path.c_str())
                || !(path.EndsWith(".h") || path.EndsWith(".hpp") || path.EndsWith(".cpp") || path.EndsWith(".jscript")))
            {
                return {};
            }
            return path;
        }
        Json DescribeAsset(const AssetRecord& record)
        {
            const auto& registry = editor.GetAssetRegistry();
            Array<AssetId> children;
            registry.CollectOwned(record.id, children);
            Json owned = Json::array();
            for (AssetId id : children)
            {
                if (const auto* child = registry.Find(id))
                {
                    owned.push_back({{"assetId", AssetText(id)}, {"type", AssetTypeRules::GetTypeName(child->type)}});
                }
            }
            return {{"assetId", AssetText(record.id)}, {"path", record.relativePath},
                {"type", AssetTypeRules::GetTypeName(record.type)}, {"owner", AssetText(record.owner)}, {"owned", owned}};
        }
        Json Describe(Object::GameObject& object)
        {
            Json result = TargetInfo(editor, object);
            result["components"] = Json::array();
            const auto append = [&](AttachedRef attached) {
                if (!attached.IsValid())
                {
                    return;
                }
                Json entry{{"instanceId", Digits(attached.GetInstanceId())},
                    {"type", NameTable::Get().Resolve(attached.GetTypeId())},
                    {"kind", attached.GetKind() == AttachedKind::Script ? "script" : "component"}, {"fields", Json::array()}};
                if (const auto* table = PropertyRegistry::Lookup(attached.GetTypeId()))
                {
                    for (UInt32 i = 0; i < table->count; ++i)
                    {
                        SetPropertyCommand::Path path;
                        path.depth = 1;
                        path.indices[0] = i;
                        String value;
                        if (SetPropertyCommand::ReadValue(attached.GetInstance(), attached.GetTypeId(), path, value))
                        {
                            entry["fields"].push_back({{"name", NameTable::Get().Resolve(table->properties[i].name)}, {"valueYaml", value}});
                        }
                    }
                }
                result["components"].push_back(std::move(entry));
            };
            for (const auto& slot : object.GetComponents())
            {
                append(AttachedRef(slot.reference.TryGet()));
            }
            for (const auto& slot : object.GetScripts())
            {
                append(AttachedRef(slot.reference.TryGet()));
            }
            return result;
        }
    };
    Tools::Tools(EditorApplication& editor) : m_state(MakeOwnerPtr<State>(editor)) {}
    Tools::~Tools() = default;
    Bool Tools::MakeObjectTarget(UInt64 editorObject, Target& target)
    {
        auto& editor = m_state->editor;
        auto* object = editor.GetObjectIds().Resolve(editorObject);
        if (object == nullptr)
        {
            return false;
        }
        target = {};
        target.project = editor.GetProjectFilePath();
        target.canvas = editor.GetCanvasPath();
        target.revision = editor.GetAgentContextRevision();
        target.instance = object->GetInstanceId();
        target.name = object->GetName();
        return true;
    }
    Bool Tools::MakeComponentTarget(UInt64 editorObject, UInt64 instance, Target& target)
    {
        if (!MakeObjectTarget(editorObject, target))
        {
            return false;
        }
        auto* object = m_state->editor.GetObjectIds().Resolve(editorObject);
        const auto attached = FindAttached(*object, instance);
        if (!attached.IsValid())
        {
            return false;
        }
        target.kind = Target::Kind::Component;
        target.component = instance;
        target.name += " / " + String(EditorNames::DisplayTypeName(NameTable::Get().Resolve(attached.GetTypeId())));
        return true;
    }
    Bool Tools::MakeAssetTarget(AssetId asset, Target& target)
    {
        auto& editor = m_state->editor;
        const auto* record = editor.GetAssetRegistry().Find(asset);
        if (record == nullptr)
        {
            return false;
        }
        target = {};
        target.kind = Target::Kind::Asset;
        target.project = editor.GetProjectFilePath();
        target.revision = editor.GetAgentContextRevision();
        target.asset = asset;
        target.name = record->relativePath;
        return true;
    }
    Bool Tools::IsTargetValid(const Target& target)
    {
        auto& editor = m_state->editor;
        if (target.project != editor.GetProjectFilePath() || target.revision != editor.GetAgentContextRevision())
        {
            return false;
        }
        if (target.kind == Target::Kind::Asset)
        {
            return !target.asset.IsNull() && editor.GetAssetRegistry().Find(target.asset) != nullptr;
        }
        if (target.canvas != editor.GetCanvasPath())
        {
            return false;
        }
        auto* object = m_state->Find(target.instance);
        return object != nullptr && (target.kind == Target::Kind::Object || FindAttached(*object, target.component).IsValid());
    }
    String Tools::Context(ArrayView<const Target> attachments)
    {
        m_state->attachments.Clear();
        for (const auto& target : attachments)
        {
            m_state->attachments.Add(target);
        }
        return Context();
    }
    String Tools::Context()
    {
        auto& editor = m_state->editor;
        Json value{{"project", editor.GetProjectFilePath()}, {"canvas", editor.GetCanvasPath()}, {"scope", m_state->Scope()},
            {"selection", Json::array()}, {"selectedAsset", AssetText(editor.GetSelectedAsset())}, {"attachments", Json::array()}};
        for (auto* object : editor.GetSelectedObjects())
        {
            value["selection"].push_back(TargetInfo(editor, *object));
        }
        for (const auto& target : m_state->attachments)
        {
            auto info = TargetInfo(target);
            const Bool valid = IsTargetValid(target);
            info["valid"] = valid.Value;
            if (valid && target.kind == Target::Kind::Asset)
            {
                info["metadata"] = m_state->DescribeAsset(*editor.GetAssetRegistry().Find(target.asset));
            }
            value["attachments"].push_back(std::move(info));
        }
        return value.dump(2);
    }
    ToolResult Tools::Execute(const String& name, const String& arguments)
    {
        auto& state = *m_state;
        auto& editor = state.editor;
        try
        {
            const auto args = Json::parse(arguments);
            if (!args.is_object())
            {
                return Error("Expected an object of tool arguments.");
            }
            if (name == "jbro_context")
            {
                return {true, Context()};
            }
            const Bool needsScope = name == "jbro_asset_read" || name == "jbro_object_read" || name == "jbro_object_create" || name == "jbro_object_delete"
                || name == "jbro_component_add" || name == "jbro_field_set" || name == "jbro_script_read" || name == "jbro_script_write"
                || name == "jbro_script_create" || name == "jbro_script_build" || name == "jbro_guide_validate"
                || name == "jbro_guide_start" || name == "jbro_guide_stop";
            if (needsScope && !args.contains("scope"))
            {
                return Error("A fresh context scope is required for this operation.");
            }
            if (args.contains("scope") && Get(args, "scope") != state.Scope())
            {
                return Error("Project or canvas changed. Refresh context; old references are invalid.");
            }
            if (name == "jbro_asset_read")
            {
                const String id = Get(args, "asset");
                AssetId asset;
                if (!Uuid::Parse(id.c_str(), id.size(), asset))
                {
                    return Error("Invalid asset ID.");
                }
                const auto& registry = editor.GetAssetRegistry();
                const auto* record = registry.Find(asset);
                if (record == nullptr)
                {
                    return Error("This asset no longer exists in this project.");
                }
                return Ok(state.DescribeAsset(*record));
            }
            if (name == "jbro_guide_catalog")
            {
                YamlWriter writer;
                EditorGuides::WriteCatalog(writer);
                return {true, writer.GetText()};
            }
            if (name == "jbro_catalog")
            {
                Json result{{"tools", Json::parse(Definitions())}, {"components", Json::array()}};
                EditorActions::AddComponentList list;
                if (auto* selected = editor.GetSelectedObject())
                {
                    EditorActions::BuildAddComponentList(*selected, list);
                }
                for (std::size_t i = 0; i < list.names.Size(); ++i)
                {
                    result["components"].push_back({{"type", list.names[i]}, {"fullType", NameTable::Get().Resolve(list.typeNames[i])}, {"addable", list.addable[i].Value}});
                }
                return Ok(result);
            }
            if (name == "jbro_object_read")
            {
                auto* object = state.Find(Get(args, "object"));
                return object == nullptr ? Error("Object no longer exists in this canvas.") : Ok(state.Describe(*object));
            }
            if (name == "jbro_guide_validate" || name == "jbro_guide_start")
            {
                const String text = Get(args, "yaml");
                OwnerPtr<LoadedGuide> guide;
                String error;
                if (!EditorGuides::Parse(text.c_str(), text.size(), guide, error))
                {
                    return {false, error};
                }
                if (name == "jbro_guide_validate")
                {
                    return Ok({{"validated", true}});
                }
                if (!editor.StartGuideFromText(text.c_str(), text.size(), error, true))
                {
                    return {false, error};
                }
                return Ok({{"prepared", true}, {"started", false}, {"sceneEdited", false}});
            }
            if (name == "jbro_guide_status")
            {
                const auto& guide = editor.GetGuide();
                return Ok({{"running", guide.IsRunning().Value}, {"step", guide.GetStepIndex().Value},
                    {"prepared", editor.IsAgentGuideReady().Value},
                    {"pausedForChat", editor.IsAgentGuidePaused().Value}});
            }
            if (name == "jbro_guide_stop")
            {
                editor.StopAgentGuide();
                return Ok({{"stopped", true}});
            }
            if (name == "jbro_script_list")
            {
                struct Collection { State& state; Json files = Json::array(); Array<String> directories; } collection{state};
                const Bool listed = editor.GetPlatform().EnumerateDirectory(editor.GetScriptRoot().c_str(), [](const char* relative, Bool directory, void* user) -> Bool {
                    auto& into = *static_cast<Collection*>(user);
                    const auto& root = into.state.editor.GetScriptRoot();
                    const String path = EditorPaths::JoinPath(root.c_str(), relative);
                    if (!Within(root, path) || !into.state.editor.GetPlatform().IsPathWithinDirectory(root.c_str(), path.c_str()))
                    {
                        return false;
                    }
                    if (directory)
                    {
                        for (const auto& previous : into.directories)
                        {
                            if (previous == relative)
                            {
                                return false;
                            }
                        }
                        into.directories.Add(relative);
                    }
                    if (!directory)
                    {
                        const String path(relative);
                        if (path.EndsWith(".h") || path.EndsWith(".hpp") || path.EndsWith(".cpp") || path.EndsWith(".jscript"))
                        {
                            into.files.push_back(path);
                        }
                    }
                    return true;
                }, &collection);
                return listed ? Ok(collection.files) : Error("The project script directory could not be listed.");
            }
            if (name == "jbro_script_read" || name == "jbro_script_write")
            {
                const String path = state.ScriptPath(Get(args, "path"));
                if (path.empty())
                {
                    return Error("Only relative script paths inside this project's script root are accepted.");
                }
                if (!editor.OpenCodeFile(path))
                {
                    return Error("The script could not be opened in the code editor.");
                }
                auto* document = state.Document(path);
                if (document == nullptr)
                {
                    return Error("Script document is unavailable.");
                }
                String disk;
                if (!Read(editor.GetPlatform(), path, disk))
                {
                    return Error("The script could not be read.");
                }
                if (name == "jbro_script_read")
                {
                    const String token = state.Scope() + ":" + Digits(state.nextToken++);
                    state.snapshots.Add({path, token, document->GetBufferText(), document->GetBuffer().GetRevision()});
                    if (state.snapshots.Size() > 64)
                    {
                        state.snapshots.RemoveAt(0);
                    }
                    return Ok({{"text", document->GetBufferText()}, {"token", token}, {"unsaved", document->HasUnsavedChanges().Value}});
                }
                const String token = Get(args, "token");
                State::Snapshot* read = nullptr;
                for (auto& snapshot : state.snapshots)
                {
                    if (snapshot.path == path && snapshot.token == token)
                    {
                        read = &snapshot;
                    }
                }
                if (read == nullptr || read->revision != document->GetBuffer().GetRevision()
                    || read->text != document->GetBufferText() || document->HasUnsavedChanges())
                {
                    return Error("Script changed or has unsaved user edits. No overwrite was performed. Read again after resolving the conflict.");
                }
                const String wanted = Get(args, "text");
                if (wanted.size() > 1024 * 1024 || wanted.find('\0') != String::npos)
                {
                    return Error("Script is too large or contains NUL bytes.");
                }
                if (!document->ApplyAgentText(read->text, wanted))
                {
                    return Error("Script save conflict or invalid text. Inspect the code editor; saving and building are not confirmed.");
                }
                read->token.clear();
                return Ok({{"saved", true}, {"path", path}, {"built", false}, {"runtimeVerified", false}});
            }
            if (name == "jbro_script_reference")
            {
                const String requested = Get(args, "path");
                if (requested.find('\0') != String::npos)
                {
                    return Error("Reference paths cannot contain NUL bytes.");
                }
                String root;
                if (Get(args, "source") == "skill")
                {
                    root = editor.GetAgentSkillsRoot();
                }
                else if (Get(args, "source") == "engine")
                {
                    root = ScriptProject::FindEngineRoot(editor.GetPlatform(), editor.GetPlatform().GetExecutableFolder().c_str());
                }
                else
                {
                    return Error("Reference source must be skill or engine.");
                }
                const String path = EditorPaths::Normalize(EditorPaths::JoinPath(root.c_str(), requested.c_str()));
                if (root.empty() || !Within(root, path) || !editor.GetPlatform().IsPathWithinDirectory(root.c_str(), path.c_str())
                    || !(path.EndsWith(".md") || path.EndsWith(".h") || path.EndsWith(".hpp") || path.EndsWith(".cpp")))
                {
                    return Error("Invalid reference path. Use relative bundled skill/documentation or engine declaration paths.");
                }
                String text;
                return Read(editor.GetPlatform(), path, text) ? ToolResult{true, text} : Error("Reference not available in this engine installation.");
            }
            if (name == "jbro_script_diagnostics")
            {
                Json diagnostics = Json::array();
                for (const auto& item : editor.GetScriptDiagnostics())
                {
                    diagnostics.push_back({{"file", item.file}, {"line", item.line.Value}, {"column", item.column.Value},
                        {"code", item.code}, {"message", item.message}, {"error", item.isError.Value}});
                }
                return Ok({{"state", static_cast<int>(editor.GetScriptBuildState())}, {"diagnostics", diagnostics}, {"runtimeVerified", false}});
            }
            if (name == "jbro_script_build")
            {
                Array<EditorPanel*> panels;
                editor.FindPanels(CodeEditorPanel::TypeName, panels);
                for (auto* panel : panels)
                {
                    if (panel->HasUnsavedChanges())
                    {
                        return Error("A code document has unsaved changes. Resolve them before building; user edits were not saved automatically.");
                    }
                }
                return editor.BuildScripts() ? Ok({{"buildStarted", true}, {"completed", false}, {"next", "jbro_script_diagnostics"}})
                    : Error("Build could not start. Check editor build tools and diagnostics.");
            }
            if (!args.contains("scope"))
            {
                return Error("A fresh context scope is required for mutations.");
            }
            if (editor.GetCommands().IsLocked() || editor.IsSimulationPlaying())
            {
                return Error("Stop game execution or debugging before editing.");
            }
            if (name == "jbro_script_create")
            {
                const String folder = args.value("folder", "");
                const String className = Get(args, "name");
                if (folder.find('\0') != String::npos || className.find('\0') != String::npos)
                {
                    return Error("Script paths and names cannot contain NUL bytes.");
                }
                const String destination = EditorPaths::JoinPath(editor.GetScriptRoot().c_str(), (folder + "/" + className + ".h").c_str());
                const String implementation = EditorPaths::JoinPath(editor.GetScriptRoot().c_str(), (folder + "/" + className + ".cpp").c_str());
                if (!editor.GetPlatform().IsPathWithinDirectory(editor.GetScriptRoot().c_str(), destination.c_str())
                    || !editor.GetPlatform().IsPathWithinDirectory(editor.GetScriptRoot().c_str(), implementation.c_str()))
                {
                    return Error("Script destination resolves outside the project script root.");
                }
                if (editor.CheckScriptName(folder.c_str(), className.c_str()) != ScriptProject::NameProblem::None)
                {
                    return Error("The script class name is invalid or already exists. No project generation was started.");
                }
                String error;
                const String path = editor.CreateScript(folder.c_str(), className.c_str(), {}, error);
                return path.empty() ? ToolResult{false, error} : Ok({{"created", path}, {"built", false}});
            }
            if (name == "jbro_object_create")
            {
                Object::GameObject* parent = nullptr;
                if (args.contains("parent") && !Get(args, "parent").empty())
                {
                    parent = state.Find(Get(args, "parent"));
                    if (parent == nullptr)
                    {
                        return Error("Parent no longer exists.");
                    }
                }
                auto* made = EditorActions::CreateObject(editor, parent);
                return made == nullptr ? Error("Object creation failed.") : Ok(TargetInfo(editor, *made));
            }
            auto* object = state.Find(Get(args, "object"));
            if (object == nullptr)
            {
                return Error("Object no longer exists. Refresh context.");
            }
            if (name == "jbro_object_delete")
            {
                return EditorActions::DeleteObject(editor, *object) ? Ok({{"deleted", true}}) : Error("Object deletion failed.");
            }
            if (name == "jbro_component_add")
            {
                EditorActions::AddComponentList list;
                EditorActions::BuildAddComponentList(*object, list);
                const String requested = Get(args, "type");
                NameId type = InvalidNameId;
                for (std::size_t i = 0; i < list.typeNames.Size(); ++i)
                {
                    if (requested == list.names[i] || requested == NameTable::Get().Resolve(list.typeNames[i]))
                    {
                        if (type != InvalidNameId && type != list.typeNames[i])
                        {
                            return Error("Component name is ambiguous. Use its full type name from the catalog.");
                        }
                        type = list.typeNames[i];
                    }
                }
                return type != InvalidNameId && EditorActions::AddComponent(editor, *object, type)
                    ? Ok(state.Describe(*object)) : Error("Component cannot be attached. Query the live component catalog.");
            }
            if (name == "jbro_field_set")
            {
                const UInt64 component = Number(Get(args, "component"));
                AttachedRef attached;
                for (const auto& slot : object->GetComponents())
                {
                    if (slot.reference != nullptr && slot.reference->GetInstanceId() == component)
                    {
                        attached = AttachedRef(slot.reference.TryGet());
                    }
                }
                for (const auto& slot : object->GetScripts())
                {
                    if (slot.reference != nullptr && slot.reference->GetInstanceId() == component)
                    {
                        attached = AttachedRef(slot.reference.TryGet());
                    }
                }
                SetPropertyCommand::Path path;
                ComponentAddress address;
                String oldValue;
                if (!attached.IsValid() || !MakeAttachedAddress(editor.GetObjectIds(), *object, attached, address)
                    || !SetPropertyCommand::MakeFieldPath(attached.GetTypeId(), Get(args, "field").c_str(), path)
                    || !SetPropertyCommand::ReadValue(attached.GetInstance(), attached.GetTypeId(), path, oldValue))
                {
                    return Error("Component or field no longer exists, or the value cannot be edited.");
                }
                const auto* table = PropertyRegistry::Lookup(attached.GetTypeId());
                const auto* edit = table->properties[path.indices[0].Value].edit;
                if (edit != nullptr && !edit->editable)
                {
                    return Error("Field is read-only.");
                }
                if (Get(args, "expected") != oldValue)
                {
                    return Error("Field value changed. Read the object again before editing.");
                }
                auto command = MakeOwnerPtr<SetPropertyCommand>(editor.GetObjectIds(), address, path, oldValue, Get(args, "value"));
                if (!editor.GetCommands().Execute(std::move(command)))
                {
                    return Error("Field value rejected; command was not recorded.");
                }
                return Ok(state.Describe(*object));
            }
            return Error("Unknown JBro tool.");
        }
        catch (const Json::exception&)
        {
            return Error("Invalid tool arguments. Query jbro_catalog for the current schema.");
        }
    }
    String Tools::Definitions()
    {
        Json tools = Json::array();
        const auto add = [&](const char* name, const char* description, std::initializer_list<const char*> fields) {
            Json properties = Json::object();
            Json required = Json::array();
            for (const auto* field : fields)
            {
                properties[field] = {{"type", "string"}};
                required.push_back(field);
            }
            tools.push_back({{"type", "function"}, {"name", name}, {"description", description},
                {"inputSchema", {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}}}});
        };
        add("jbro_asset_read", "Read metadata of an attached asset by AssetId in the current project. Returns its current relative path, asset type name, owner and owned asset IDs (e.g. paired Sprites); does not load file contents.", {"scope", "asset"});
        add("jbro_context", "Read current project/canvas scope, selection and asset ID. Always refresh before using target IDs.", {});
        add("jbro_catalog", "Read available engine tools and attachable component types.", {});
        add("jbro_object_read", "Read an object's components and field YAML. Object is its decimal InstanceId, not EditorObjectId.", {"scope", "object"});
        add("jbro_object_create", "Create an undoable object. Parent is a decimal InstanceId, or empty for root.", {"scope", "parent"});
        add("jbro_object_delete", "Delete the specified object through an undoable command.", {"scope", "object"});
        add("jbro_component_add", "Attach a component or script using a live catalog type name.", {"scope", "object", "type"});
        add("jbro_field_set", "Set an undoable top-level field. Component is its InstanceId; expected/value are YAML from object_read.", {"scope", "object", "component", "field", "expected", "value"});
        add("jbro_script_list", "List this project's script source files, relative to script root.", {});
        add("jbro_script_read", "Read a source document, obtaining a write token and unsaved status.", {"scope", "path"});
        add("jbro_script_write", "Replace and save a clean source document using its read token; preserves editor undo and detects conflicts.", {"scope", "path", "token", "text"});
        add("jbro_script_create", "Create a C++ script through the engine's template/project generation. Folder is relative, name is a class name.", {"scope", "folder", "name"});
        add("jbro_script_reference", "Read bundled documentation. source=skill: jbro-guide/SKILL.md or jbro-create/SKILL.md; source=engine: paths relative to engine root, e.g. Modules/JBroFramework2D/Include/JBro/Framework2D/Handle/Transform2D.h. Never invent APIs.", {"source", "path"});
        add("jbro_script_build", "Start existing script build after requested functionality changes. Query diagnostics until no longer running.", {"scope"});
        add("jbro_script_diagnostics", "Read script build state (0=Idle, 1=Running, 2=Succeeded, 3=Failed), errors and warnings. Build success is not runtime proof.", {});
        add("jbro_guide_catalog", "Read authoritative YAML actions and arguments supported by this running engine.", {});
        add("jbro_guide_validate", "Validate a newly authored YAML guide without starting it or editing the scene.", {"scope", "yaml"});
        add("jbro_guide_start", "Prepare a validated contextual guide in chat. Only the user can start it with Start guide. Preparation never selects objects or opens panels. Existing guide is preserved on parse failure.", {"scope", "yaml"});
        add("jbro_guide_status", "Read whether the guide is prepared, running or paused, and its current step.", {});
        add("jbro_guide_stop", "Cancel the prepared or active guide without undoing user edits.", {"scope"});
        return tools.dump();
    }
}
