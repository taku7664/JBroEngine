#pragma once

#include <JBro/Editor/Command/ComponentSnapshot.h>
#include <JBro/Editor/Command/ObjectTreeSnapshot.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>

#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class Canvas;
    class GameObject;
    struct PropertyTable;

    // 오브젝트 하나를 만든다. 되돌리면 지우고, 다시 하면 **같은 번호로** 되살린다 -
    // 그 번호를 들고 있는 다른 커맨드들이 계속 그것을 찾아야 한다.
    class CreateObjectCommand final : public EditorCommand
    {
    public:
        CreateObjectCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            const char* name,
            EditorObjectId parentId);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 만들어진 오브젝트의 번호다. 부른 쪽이 선택을 옮기는 데 쓴다.
        EditorObjectId GetObjectId() const;

    private:
        bool Create();

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        String m_name;
        EditorObjectId m_parentId = InvalidEditorObjectId;
        EditorObjectId m_objectId = InvalidEditorObjectId;
    };

    // 오브젝트와 그 아래 전부를 지운다. 되돌리면 스냅샷에서 되살린다.
    //
    // **바이트를 들고 있다가 되돌려 놓을 수는 없다.** 컴포넌트는 풀이 소유하고 그
    // 주소는 되살릴 때 달라진다. 그래서 기존 엔진처럼 값을 글자로 떠 두었다가
    // 컴포넌트를 새로 붙이고 다시 써 넣는다 - 다른 점은 중간에 YAML 이 없다는 것뿐이다.
    class DeleteObjectCommand final : public EditorCommand
    {
    public:
        DeleteObjectCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            GameObject* object);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_parentId = InvalidEditorObjectId;
        // **나무 전체를 떴는가.** 스냅샷이 비었는지만 보아서는 모자란다 - 뿌리는
        // 떴는데 자식 하나에서 막히면 배열은 비어 있지 않고, 그대로 지우면 그
        // 자식이 돌아올 곳이 없다. 성공했다고 말하면서 잃는 것이 가장 나쁘다.
        bool m_captured = false;
        // 나무를 평평하게 편 스냅샷이다(`ObjectTreeSnapshot`). 복사·붙여넣기와 같은 것을 쓴다.
        ObjectTreeSnapshot m_tree;
    };

    // 떠 둔 나무들을 새 오브젝트로 붙여 넣는다(기존 엔진 `CPasteObjectsCommand`). 되돌리면
    // 붙인 것을 지우고, 다시 하면 **같은 번호로** 되살린다 - 붙여 넣은 것을 골라 고친 커맨드가
    // 그 뒤에 쌓여 있다.
    //
    // 클립보드는 글자가 아니라 스냅샷이다. 기존 엔진은 YAML 글자를 시스템 클립보드에 두어
    // 프로세스 사이에서도 붙일 수 있었지만, 지금 캔버스 파일 쓰기는 캔버스 전체 단위라
    // 부분 나무의 글자 왕복이 없다. 에디터 안에서만 붙는다 - 프로세스를 넘는 것은 그 왕복이
    // 생길 때 한다.
    class PasteObjectsCommand final : public EditorCommand
    {
    public:
        PasteObjectsCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            const Array<ObjectTreeSnapshot>& trees,
            EditorObjectId parentId);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 붙여 넣은 뿌리들의 번호다. 부른 쪽이 선택을 옮기는 데 쓴다.
        Array<EditorObjectId> GetPastedRootIds() const;

    private:
        bool Paste();
        void DestroyPasted();

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_parentId = InvalidEditorObjectId;
        Array<ObjectTreeSnapshot> m_trees;
        // 한 번 붙였는가. 첫 실행은 새 번호를 받고, 그 뒤는 그 번호에 다시 건다.
        bool m_pasted = false;
    };
}
