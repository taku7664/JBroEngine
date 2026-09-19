#include <JBro/Editor/Command/SetAssetMetaCommand.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Platform/Platform.h>

#include <utility>

namespace JBro
{
    SetAssetMetaCommand::SetAssetMetaCommand(const AssetMetaTarget& target, String oldText, String newText)
        : m_target(target)
        , m_oldText(std::move(oldText))
        , m_newText(std::move(newText))
    {
    }

    const char* SetAssetMetaCommand::GetName() const
    {
        return "Edit Import Options";
    }

    bool SetAssetMetaCommand::Execute()
    {
        if (m_target.platform == nullptr || m_target.metaPath.empty() || m_oldText == m_newText)
        {
            return false;
        }
        return Write(m_newText);
    }

    void SetAssetMetaCommand::Undo()
    {
        Write(m_oldText);
    }

    void SetAssetMetaCommand::Redo()
    {
        Write(m_newText);
    }

    bool SetAssetMetaCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const SetAssetMetaCommand*>(&newer);
        return other != nullptr && other->m_target.metaPath == m_target.metaPath;
    }

    bool SetAssetMetaCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        m_newText = static_cast<const SetAssetMetaCommand&>(newer).m_newText;
        return true;
    }

    const String& SetAssetMetaCommand::GetMetaPath() const
    {
        return m_target.metaPath;
    }

    bool SetAssetMetaCommand::Write(const String& text)
    {
        JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<std::uint32_t>(text.size());
        if (false == m_target.platform->WriteWholeFile(m_target.metaPath.c_str(), view))
        {
            return false;
        }
        // 로드돼 있지 않으면 거짓이고 그것은 실패가 아니다 - 다음 로드가 새 옵션으로 시작한다.
        if (m_target.assets != nullptr)
        {
            if (false == m_target.id.IsNull())
            {
                m_target.assets->ReloadInPlace(m_target.id);
            }
            if (false == m_target.spriteId.IsNull())
            {
                m_target.assets->ReloadInPlace(m_target.spriteId);
            }
        }
        return true;
    }
}
