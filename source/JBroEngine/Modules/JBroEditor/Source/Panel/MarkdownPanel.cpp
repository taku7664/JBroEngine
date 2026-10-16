#include "MarkdownPanel.h"
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Task/TaskManager.h>
#include <JBro/Types/ValueMath.h>
#include <algorithm>

namespace JBro
{
    namespace
    {
        constexpr std::size_t MaximumDocumentBytes = 32 * 1024 * 1024;
        constexpr std::size_t MaximumImageBytes = 64 * 1024 * 1024;
        String LocalPath(const String& document, const String& target)
        {
            String decoded = Markdown::DecodeTarget(target);
            if (decoded.empty() || decoded.StartsWith("//") || decoded.StartsWith("\\\\")
                || (decoded.find(':') != String::npos && !(decoded.size() > 2 && decoded[1] == ':')))
            {
                return {};
            }
            if (!(decoded.size() > 2 && decoded[1] == ':') && decoded[0] != '/' && decoded[0] != '\\')
            {
                decoded = EditorPaths::JoinPath(EditorPaths::FolderOf(document.c_str()).c_str(), decoded.c_str());
            }
            return Code::LanguageClient::NormalizePath(decoded);
        }
        Bool SupportedImage(const String& path)
        {
            String extension = path.Substr(path.find_last_of('.') == String::npos ? path.size() : path.find_last_of('.') + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp" || extension == "tga";
        }
        enum class ContentRequirement { None, Text, Selection };
        template<ContentRequirement Requirement>
        const char* WhyPanelActionBlocked(const EditorActionContext& context)
        {
            if (context.panel == nullptr)
            {
                return Loc::TextOr(LocKeys::MarkdownOpen, "Open a Markdown document first.");
            }
            if (Widget::IsTextInputActive())
            {
                return Loc::TextOr(LocKeys::CodeEditorFinishInput, "Finish editing the active input field first.");
            }
            auto& view = static_cast<MarkdownPanel*>(context.panel)->GetView();
            if constexpr (Requirement == ContentRequirement::Text)
            {
                if (view.GetDocument().text.empty())
                {
                    return Loc::TextOr(LocKeys::CodeEditorEmptyDocument, "The document is empty.");
                }
            }
            if constexpr (Requirement == ContentRequirement::Selection)
            {
                if (!view.HasSelection())
                {
                    return Loc::TextOr(LocKeys::CodeEditorNoSelection, "No text is selected.");
                }
            }
            return nullptr;
        }
        template<ContentRequirement Requirement = ContentRequirement::None>
        void RegisterPanelAction(const char* name, const char* key, const char* fallback, const EditorShortcutBinding& chord,
            Bool (*execute)(EditorActionContext&), UInt32 menu)
        {
            EditorActionInfo action;
            action.name = name;
            action.labelKey = key;
            action.fallbackLabel = fallback;
            action.categoryKey = LocKeys::MarkdownTitle;
            action.panelType = MarkdownPanel::TypeName;
            action.primary = chord;
            action.menus = menu;
            action.placedByEditor = true;
            action.duringGame = true;
            action.Execute = execute;
            action.CanExecute = [](const EditorActionContext& context) -> Bool {
                return WhyPanelActionBlocked<Requirement>(context) == nullptr;
            };
            action.WhyBlocked = WhyPanelActionBlocked<Requirement>;
            EditorActionRegistry::Get().Register(action);
        }
    }
    class MarkdownPanel::LoadTask final : public Task
    {
    public:
        struct File
        {
            OwnerPtr<IFileStream> owner;
            IFileStream* stream = nullptr;
            String target;
            Array<std::byte> bytes;
            DecodedImage decoded;
            Bool valid = false;
        };
        LoadTask(MarkdownPanel& panel, UInt64 version, Bool images)
            : Task(String(Loc::TextOr(LocKeys::MarkdownTitle, "Documents")) + ": " + panel.m_title), panel(&panel), version(version), images(images) {}
        void Prepare(IPlatform& platform, const String& path, const String& target = {})
        {
            File file;
            file.owner = platform.OpenFileStream(path.c_str());
            file.stream = file.owner.Get();
            file.target = target;
            files.Add(std::move(file));
        }
        void Run() override
        {
            for (auto& file : files)
            {
                if (IsCancelRequested())
                {
                    return;
                }
                if (!file.stream)
                {
                    continue;
                }
                const auto size = file.stream->GetSize();
                if (size < 0 || static_cast<std::size_t>(size.Get()) > (images ? MaximumImageBytes : MaximumDocumentBytes))
                {
                    continue;
                }
                file.bytes.Resize(static_cast<std::size_t>(size.Get()));
                std::size_t at = 0;
                while (at < file.bytes.Size() && !IsCancelRequested())
                {
                    const auto read = file.stream->Read(file.bytes.Data() + at, Min(file.bytes.Size() - at, std::size_t(1024 * 1024)));
                    if (read == 0)
                    {
                        break;
                    }
                    at += read;
                }
                if (at != file.bytes.Size() || IsCancelRequested())
                {
                    continue;
                }
                if (images)
                {
                    file.valid = DecodeImage(JArrayView<std::byte>(file.bytes.Data(), static_cast<UInt32>(file.bytes.Size())), file.decoded);
                }
                else
                {
                    const String source = file.bytes.IsEmpty() ? String() : String(reinterpret_cast<const char*>(file.bytes.Data()), file.bytes.Size());
                    file.valid = Markdown::Parse(source, document);
                }
            }
            if (!images && (files.IsEmpty() || !files[0].valid))
            {
                FailSubTask(0, failure.c_str());
            }
        }
        void OnFinished(const TaskResult& result) override
        {
            for (auto& file : files)
            {
                file.stream = nullptr;
                file.owner.Reset();
            }
            panel->Adopt(*this, result);
            // Completed task history must not retain file handles or decoded/document buffers.
            files.Clear();
            files.Shrink();
            document = {};
        }
        String failure = Loc::TextOr(LocKeys::MarkdownReadFailed, "The document could not be read.");
        MarkdownPanel* panel;
        UInt64 version;
        Bool images;
        Array<File> files;
        Markdown::Document document;
    };
    Bool MarkdownPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        Widget::MarkdownCallbacks callbacks;
        callbacks.user = this;
        callbacks.openLink = [](void* user, const String& target) { static_cast<MarkdownPanel*>(user)->OpenLink(target); };
        callbacks.image = [](void* user, const String& target) {
            auto& panel = *static_cast<MarkdownPanel*>(user);
            for (const auto& image : panel.m_images)
            {
                if (image.target == target)
                {
                    return image.image;
                }
            }
            Widget::MarkdownImage missing;
            missing.message = Loc::TextOr(LocKeys::MarkdownImageUnavailable, "Local image is unavailable or unsupported.");
            return missing;
        };
        m_view.SetCallbacks(callbacks);
        return true;
    }
    void MarkdownPanel::CancelTasks()
    {
        if (auto* manager = m_editor->GetTaskManager())
        {
            for (const auto id : m_tasks)
            {
                if (auto* group = manager->FindGroup(id))
                {
                    group->RequestCancel();
                }
            }
        }
    }
    void MarkdownPanel::OnDestroy()
    {
        ++m_version;
        CancelTasks();
        if (auto* manager = m_editor->GetTaskManager())
        {
            for (const auto id : m_tasks)
            {
                manager->Wait(id);
            }
        }
        m_tasks.Clear();
        ClearImages();
        m_view.SetCallbacks({});
    }
    void MarkdownPanel::ClearImages()
    {
        for (const auto& entry : m_images)
        {
            if (entry.image.texture.IsValid())
            {
                m_editor->RetireUiTexture(entry.image.texture);
            }
        }
        m_images.Clear();
    }
    Bool MarkdownPanel::Open(const String& path, const String& anchor)
    {
        m_path = Code::LanguageClient::NormalizePath(path);
        m_title = EditorPaths::LeafOfPath(m_path);
        m_anchor = anchor;
        Reload();
        Focus(anchor);
        return true;
    }
    void MarkdownPanel::Focus(const String& anchor)
    {
        Array<EditorPanel*> siblings;
        m_editor->FindPanels(TypeName, siblings);
        for (auto* sibling : siblings)
        {
            if (sibling != this)
            {
                sibling->TakeFocusRequest();
            }
        }
        if (!anchor.empty())
        {
            if (m_loading)
            {
                m_anchor = anchor;
            }
            else
            {
                m_view.GoToAnchor(anchor);
            }
        }
        RequestFocus();
    }
    void MarkdownPanel::Reload()
    {
        auto* manager = m_editor->GetTaskManager();
        if (!manager)
        {
            m_error = Loc::TextOr(LocKeys::MarkdownReadFailed, "The document could not be read. The last valid contents are retained.");
            return;
        }
        ++m_version;
        CancelTasks();
        m_editor->GetPlatform().ReadFileWriteTime(m_path.c_str(), m_writeTime);
        auto task = MakeOwnerPtr<LoadTask>(*this, m_version, false);
        task->Prepare(m_editor->GetPlatform(), m_path);
        auto group = MakeOwnerPtr<TaskGroup>(m_title);
        group->Add(std::move(task));
        m_tasks.Add(manager->Submit(std::move(group)));
        m_loading = true;
        m_error.clear();
    }
    void MarkdownPanel::Adopt(LoadTask& task, const TaskResult& result)
    {
        if (task.version != m_version || result.state == TaskState::Canceled)
        {
            return;
        }
        if (!task.images)
        {
            m_loading = false;
            if (result.state != TaskState::Completed || task.files.IsEmpty() || !task.files[0].valid)
            {
                m_error = Loc::TextOr(LocKeys::MarkdownReadFailed, "The document could not be read. The last valid contents are retained.");
                return;
            }
            ClearImages();
            m_view.SetDocument(std::move(task.document));
            if (!m_anchor.empty())
            {
                m_view.GoToAnchor(m_anchor);
                m_anchor.clear();
            }
            LoadImages();
            return;
        }
        auto* renderer = m_editor->GetRenderer();
        auto* device = renderer ? renderer->GetDevice() : nullptr;
        for (auto& file : task.files)
        {
            for (auto& entry : m_images)
            {
                if (entry.target != file.target || !file.valid || !device)
                {
                    continue;
                }
                TextureDesc desc;
                desc.extent = {file.decoded.width, file.decoded.height};
                desc.format = TextureFormat::RGBA8Unorm;
                desc.usage = TextureUsage::Sampled;
                const auto texture = device->CreateTexture(desc);
                if (!texture.IsValid())
                {
                    continue;
                }
                if (!device->WriteTexture(texture, 0, JArrayView<std::byte>(file.decoded.pixels.Data(), static_cast<UInt32>(file.decoded.pixels.Size()))))
                {
                    device->DestroyTexture(texture);
                    continue;
                }
                entry.image.texture = texture;
                entry.image.width = file.decoded.width;
                entry.image.height = file.decoded.height;
                entry.image.message = entry.path;
            }
        }
        m_view.InvalidateLayout();
    }
    void MarkdownPanel::LoadImages()
    {
        auto task = MakeOwnerPtr<LoadTask>(*this, m_version, true);
        for (const auto& block : m_view.GetDocument().blocks)
        {
            for (const auto& run : block.runs)
            {
                if (!run.image || m_images.FindBy([&](const ImageEntry& entry) { return entry.target == run.target; }))
                {
                    continue;
                }
                ImageEntry entry;
                entry.target = run.target;
                entry.path = LocalPath(m_path, run.target);
                entry.image.message = Loc::TextOr(LocKeys::MarkdownImageUnavailable, "Local image is unavailable or unsupported.");
                if (!entry.path.empty() && SupportedImage(entry.path))
                {
                    task->Prepare(m_editor->GetPlatform(), entry.path, entry.target);
                }
                m_images.Add(std::move(entry));
            }
        }
        if (!task->files.IsEmpty())
        {
            auto group = MakeOwnerPtr<TaskGroup>(m_title);
            group->Add(std::move(task));
            m_tasks.Add(m_editor->GetTaskManager()->Submit(std::move(group)));
        }
    }
    void MarkdownPanel::OnUpdate(Float)
    {
        if (auto* manager = m_editor->GetTaskManager())
        {
            m_tasks.RemoveAll([&](TaskGroupId id) {
                const auto* group = manager->FindGroup(id);
                return group == nullptr || group->IsFinished();
            });
        }
        const Bool newlyVisible = IsVisible() && !m_wasVisible;
        const Bool newlyFocused = IsFocused() && !m_wasFocused;
        if ((newlyVisible || newlyFocused) && !m_loading)
        {
            UInt64 writeTime = 0;
            if (!m_editor->GetPlatform().ReadFileWriteTime(m_path.c_str(), writeTime) || writeTime != m_writeTime)
            {
                Reload();
            }
        }
        m_wasFocused = IsFocused();
        m_wasVisible = IsVisible();
    }
    void MarkdownPanel::OpenLink(const String& target)
    {
        if (Markdown::IsExternalLink(target))
        {
            m_editor->GetPlatform().OpenPathWithShell(target.c_str());
            return;
        }
        const auto hash = target.find('#');
        const auto path = LocalPath(m_path, target.Substr(0, hash));
        String extension = path.Substr(path.find_last_of('.') == String::npos ? path.size() : path.find_last_of('.'));
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!path.empty() && extension == ".md")
        {
            m_editor->OpenMarkdownFile(path, hash == String::npos ? String() : Markdown::DecodeTarget(target.Substr(hash + 1)));
        }
        else
        {
            m_error = Loc::TextOr(LocKeys::MarkdownUnsupportedLink, "This link scheme is not supported.");
        }
    }
    void MarkdownPanel::OnDraw()
    {
        if (m_loading)
        {
            Widget::HintText(Loc::TextOr(LocKeys::MarkdownLoading, "Reading document..."));
        }
        if (!m_error.empty())
        {
            Widget::SeverityTextF(Widget::Severity::Error, "%s", m_error.c_str());
        }
        m_view.Render("document");
    }
    void MarkdownPanel::OnMenuBar()
    {
        EditorActionContext context;
        context.editor = m_editor;
        context.panel = this;
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            EditorActionUi::DrawItem("markdown.open", context);
            EditorActionUi::DrawItem("markdown.reload", context);
            EditorActionUi::DrawItem("markdown.close", context);
            Widget::EndMenu();
        }
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            EditorActionUi::DrawItem("markdown.copy", context);
            EditorActionUi::DrawItem("markdown.select_all", context);
            EditorActionUi::DrawItem("markdown.find", context);
            Widget::EndMenu();
        }
    }
    void MarkdownPanel::RegisterActions()
    {
        EditorActionInfo open;
        open.name = "markdown.open";
        open.labelKey = LocKeys::MarkdownOpen;
        open.fallbackLabel = "Open Markdown document...";
        open.categoryKey = LocKeys::MarkdownTitle;
        open.menus = EditorActionMenu::File;
        open.placedByEditor = true;
        open.icon = Icons::FolderOpen;
        open.Execute = [](EditorActionContext& context) -> Bool {
            PathBrowseRequest request;
            request.folder = false;
            request.relative = false;
            request.filterName = "Markdown";
            request.filterPattern = "*.md";
            request.user = context.editor;
            request.deliver = [](void* user, const String& path) { static_cast<EditorApplication*>(user)->OpenMarkdownFile(path); };
            context.editor->RequestBrowsePath(request);
            return true;
        };
        EditorActionRegistry::Get().Register(open);
        RegisterPanelAction("markdown.reload", LocKeys::MarkdownReload, "Reload from disk", {}, [](EditorActionContext& context) -> Bool {
            static_cast<MarkdownPanel*>(context.panel)->Reload();
            return true;
        }, EditorActionMenu::File);
        RegisterPanelAction<ContentRequirement::Selection>("markdown.copy", LocKeys::MenuCopy, "Copy", {ImGuiKey_C, true}, [](EditorActionContext& context) -> Bool {
            static_cast<MarkdownPanel*>(context.panel)->m_view.Copy();
            return true;
        }, EditorActionMenu::Edit);
        RegisterPanelAction<ContentRequirement::Text>("markdown.select_all", LocKeys::MenuSelectAll, "Select all", {ImGuiKey_A, true}, [](EditorActionContext& context) -> Bool {
            static_cast<MarkdownPanel*>(context.panel)->m_view.SelectAll();
            return true;
        }, EditorActionMenu::Edit);
        RegisterPanelAction("markdown.find", LocKeys::MarkdownFind, "Find in document", {ImGuiKey_F, true}, [](EditorActionContext& context) -> Bool {
            static_cast<MarkdownPanel*>(context.panel)->m_view.ShowFind();
            return true;
        }, EditorActionMenu::Edit);
        RegisterPanelAction("markdown.close", LocKeys::CodeEditorClose, "Close", {}, [](EditorActionContext& context) -> Bool {
            context.editor->ClosePanel(*context.panel);
            return true;
        }, EditorActionMenu::File);
    }
}
