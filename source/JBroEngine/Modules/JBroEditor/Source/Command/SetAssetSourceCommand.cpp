#include <JBro/Editor/Command/SetAssetSourceCommand.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Platform/Platform.h>

#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    SetAssetSourceCommand::SetAssetSourceCommand(const AssetSourceTarget& target, String oldText, String newText, const char* name)
        : m_target(target)
        , m_oldText(std::move(oldText))
        , m_newText(std::move(newText))
        , m_name(name)
    {
    }

    const char* SetAssetSourceCommand::GetName() const
    {
        return m_name != nullptr ? m_name : "Edit Asset";
    }

    Bool SetAssetSourceCommand::Execute()
    {
        if (m_target.platform == nullptr || m_target.path.empty() || m_oldText == m_newText)
        {
            return false;
        }
        return Write(m_newText);
    }

    void SetAssetSourceCommand::Undo()
    {
        Write(m_oldText);
    }

    void SetAssetSourceCommand::Redo()
    {
        Write(m_newText);
    }

    Bool SetAssetSourceCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const SetAssetSourceCommand*>(&newer);
        return other != nullptr && other->m_target.path == m_target.path;
    }

    Bool SetAssetSourceCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        m_newText = static_cast<const SetAssetSourceCommand&>(newer).m_newText;
        return true;
    }

    Bool SetAssetSourceCommand::Write(const String& text)
    {
        JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<JBro::UInt32>(text.size());
        if (false == m_target.platform->WriteWholeFile(m_target.path.c_str(), view))
        {
            return false;
        }
        // 로드돼 있지 않으면 거짓이고 그것은 실패가 아니다 - 다음 로드가 새 글자로 시작한다.
        if (m_target.assets != nullptr && false == m_target.id.IsNull())
        {
            m_target.assets->ReloadInPlace(m_target.id);
        }
        return true;
    }
}
