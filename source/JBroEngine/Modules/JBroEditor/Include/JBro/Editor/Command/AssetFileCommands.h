#pragma once

#include <JBro/Editor/EditorCommand.h>

#include <JBro/Types/String.h>

namespace JBro
{
    class EditorApplication;

    // **에셋 파일을 만지는 일도 되돌릴 수 있다**(D-191, 기존 `EditorFileCommands`).
    //
    // 그전까지 에셋 브라우저의 지우기·이름 바꾸기·옮기기·폴더 만들기는 디스크를 곧장
    // 고쳤다. 잘못 지운 파일은 그것으로 끝이었고, 우리 규칙(§11 "되살릴 값을 먼저 뜨지
    // 못했으면 지우지도 떼지도 않는다")도 지켜지지 않고 있었다.
    //
    // **지운 것은 프로젝트 안 숨김 폴더(`.jbrotrash`)로 옮긴다.** 같은 볼륨이라 옮기기가
    // 즉시 끝나고 중간에 실패할 자리가 없다 - 임시 폴더가 다른 드라이브면 큰 파일이
    // 통째로 복사되고 그 도중에 실패할 수 있다. 커맨드가 되돌리기 스택에서 밀려나
    // 사라질 때 그 자리를 치운다. 프로젝트를 열 때 남아 있는 것도 치운다(비정상 종료).
    //
    // **캔버스를 더럽히지 않는다.** 되돌리기는 같은 Ctrl+Z 하나지만, 저장 여부는
    // 문서마다 따로 센다(`EditorCommandManager::AssetDatabase`). 파일 이름을 바꿨다고
    // 캔버스가 "저장 안 됨" 이 되면 저장할 것이 없는데도 저장을 누르게 된다.
    //
    // **`.jmeta` 는 늘 함께 간다.** 두고 오면 그 에셋의 아이디가 사라지고, 그것을
    // 가리키던 컴포넌트의 참조가 전부 풀린다(D-139).

    // 폴더를 만든다. 되돌리면 그 폴더를 휴지통으로 옮긴다 - 지우지 않는 것은
    // 그 사이에 누가 파일을 넣었을 수 있기 때문이다.
    class CreateAssetFolderCommand final : public EditorCommand
    {
    public:
        CreateAssetFolderCommand(EditorApplication& editor, String relativePath);
        ~CreateAssetFolderCommand() override;

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        EditorApplication* m_editor = nullptr;
        String m_relativePath;
        String m_trashPath;
    };

    // 옮기거나 이름을 바꾼다. 둘은 같은 일이다 - 대상 상대경로가 다를 뿐이다.
    class MoveAssetCommand final : public EditorCommand
    {
    public:
        MoveAssetCommand(EditorApplication& editor, String fromRelative, String toRelative);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        const String& GetTarget() const;

    private:
        bool MoveTo(const String& fromRelative, const String& toRelative);

        EditorApplication* m_editor = nullptr;
        String m_from;
        String m_to;
    };

    // 지운다. 되돌리면 휴지통에서 돌아온다.
    class DeleteAssetCommand final : public EditorCommand
    {
    public:
        DeleteAssetCommand(EditorApplication& editor, String relativePath);
        ~DeleteAssetCommand() override;

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        EditorApplication* m_editor = nullptr;
        String m_relativePath;
        String m_trashPath;
    };
}
