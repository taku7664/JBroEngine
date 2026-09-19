#pragma once

#include <JBro/Editor/EditorCommand.h>

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class AssetSystem;
    class IPlatform;

    // 메타 하나가 어디 있고 누구를 다시 읽어야 하는지다. 커맨드는 포인터가 아니라 경로와 아이디로 대상을 가리킨다(D-72).
    struct AssetMetaTarget
    {
        IPlatform* platform = nullptr;
        AssetSystem* assets = nullptr;
        String metaPath;
        // 메타의 주인(텍스처 등)과, 이미지면 함께 선 스프라이트. 로드돼 있는 것만 in-place 재로드된다.
        AssetId id;
        AssetId spriteId;
    };

    // 에셋의 임포트 옵션을 되돌릴 수 있게 고친다(D-120).
    //
    // **되살릴 값은 메타 파일의 글자 전체다.** 옵션 블록만 뜨면 나머지 키를 잃을 길이 생기고, 파일 전체를 뜨면 무엇을
    // 고쳤든 되돌리기가 그대로 돌려놓는다. 실행은 새 글자를 쓰고 로드된 에셋을 `ReloadInPlace` 한다 - 핸들이 살아
    // 캔버스의 스프라이트가 그 자리에서 새 칸을 본다(asset-plan §2.7).
    //
    // 드래그는 한 덩어리다: 같은 메타에 대한 편집은 새 글자만 흡수한다.
    class SetAssetMetaCommand final : public EditorCommand
    {
    public:
        SetAssetMetaCommand(const AssetMetaTarget& target, String oldText, String newText);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;
        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

        const String& GetMetaPath() const;

    private:
        bool Write(const String& text);

        AssetMetaTarget m_target;
        String m_oldText;
        String m_newText;
    };
}
