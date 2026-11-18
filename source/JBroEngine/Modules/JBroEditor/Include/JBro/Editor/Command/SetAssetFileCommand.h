#pragma once

#include <JBro/Editor/EditorCommand.h>

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Bool.h>

namespace JBro
{
    class AssetSystem;
    class IPlatform;

    // 에셋 파일 하나가 어디 있고 누구를 다시 읽어야 하는지다. 커맨드는 포인터가 아니라 경로와 아이디로 대상을 가리킨다(D-72).
    // 그 파일은 `.jmeta`(임포트 옵션, D-120)이거나 값을 본문에 든 에셋 파일(`.jmat`, material-plan 4 단계)이다 - 하는 일이 같다.
    struct AssetFileTarget
    {
        IPlatform* platform = nullptr;
        AssetSystem* assets = nullptr;
        String path;
        // 그 파일의 주인(텍스처 등)과, 이미지면 함께 선 스프라이트. 로드돼 있는 것만 in-place 재로드된다.
        AssetId id;
        AssetId spriteId;
        // 편집 기록에 보이는 이름이다(영어, 기록의 칸 이름).
        const char* name = "Edit Import Options";
        // 파일을 쓴 뒤 부른다(실행·되돌리기·다시 하기 모두). 모델은 하위 목록이 바뀔 수 있어 에디터가 레지스트리를 다시 훑게 한다(D-391).
        void (*afterWrite)(void* user) = nullptr;
        void* afterWriteUser = nullptr;
    };

    // 에셋 파일 하나를 되돌릴 수 있게 고친다 - 임포트 옵션(D-120)과 재질 파일(material-plan 4 단계).
    //
    // **되살릴 값은 파일의 글자 전체다.** 블록 하나만 뜨면 나머지 키를 잃을 길이 생기고, 파일 전체를 뜨면 무엇을
    // 고쳤든 되돌리기가 그대로 돌려놓는다. 실행은 새 글자를 쓰고 로드된 에셋을 `ReloadInPlace` 한다 - 핸들이 살아
    // 캔버스의 스프라이트·재질이 그 자리에서 새 값을 본다(asset-plan §2.7).
    //
    // 드래그는 한 덩어리다: 같은 파일에 대한 편집은 새 글자만 흡수한다.
    //
    // 처음 이름은 `SetAssetMetaCommand` 였다. 재질 파일을 같은 길로 고치게 되면서(같은 일을 하는 커맨드를 둘 두지 않으려고) 이름을 넓혔다.
    class SetAssetFileCommand final : public EditorCommand
    {
    public:
        SetAssetFileCommand(const AssetFileTarget& target, String oldText, String newText);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

        const String& GetPath() const;

    private:
        Bool Write(const String& text);

        AssetFileTarget m_target;
        String m_oldText;
        String m_newText;
    };
}
