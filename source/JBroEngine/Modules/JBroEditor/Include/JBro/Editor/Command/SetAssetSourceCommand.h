#pragma once

#include <JBro/Editor/EditorCommand.h>

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class AssetSystem;
    class IPlatform;

    // 에셋 본문 파일 하나가 어디 있고 누구를 다시 읽어야 하는지다. 커맨드는 포인터가 아니라 경로와 아이디로 대상을 가리킨다(D-72).
    struct AssetSourceTarget
    {
        IPlatform* platform = nullptr;
        AssetSystem* assets = nullptr;
        String path;
        AssetId id;
    };

    // **에셋 본문을 되돌릴 수 있게 고친다**(D-344, 인스펙터의 후처리 프로필). `SetAssetMetaCommand` 와 같은 모양이다 - 되살릴 값은 파일의
    // 글자 전체이고, 실행은 새 글자를 쓰고 로드된 에셋을 `ReloadInPlace` 한다(핸들이 살아 그 프로필을 쓰는 카메라가 다음 프레임에 새 값을 본다).
    // 드래그는 한 덩어리다: 같은 파일에 대한 편집은 끄는 동안 새 글자만 흡수한다.
    class SetAssetSourceCommand final : public EditorCommand
    {
    public:
        // `name` 은 정적 글자다(실행 취소 메뉴에 보인다).
        SetAssetSourceCommand(const AssetSourceTarget& target, String oldText, String newText, const char* name);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Bool Write(const String& text);

        AssetSourceTarget m_target;
        String m_oldText;
        String m_newText;
        const char* m_name = nullptr;
    };
}
