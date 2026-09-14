#pragma once

#include <JBro/Editor/Command/ComponentSnapshot.h>
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
        // 나무를 **평평하게** 편다. 자식이 자기 안에 자식 배열을 들면 타입이
        // 자기 자신을 품게 되어 크기를 잴 수 없다 - 캔버스 파일도 같은 이유로
        // 오브젝트를 한 줄로 늘어놓고 부모를 인덱스로 가리킨다.
        struct ObjectSnapshot
        {
            EditorObjectId id = InvalidEditorObjectId;
            String name;
            bool active = true;
            // 이 배열 안에서의 부모 위치다. -1 이면 지운 나무의 뿌리다.
            std::int64_t parentIndex = -1;
            Array<ComponentSnapshot> components;
        };

        bool Capture(GameObject& object, std::int64_t parentIndex);
        bool Restore();
        bool DestroyTracked();

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_parentId = InvalidEditorObjectId;
        // **나무 전체를 떴는가.** 스냅샷이 비었는지만 보아서는 모자란다 - 뿌리는
        // 떴는데 자식 하나에서 막히면 배열은 비어 있지 않고, 그대로 지우면 그
        // 자식이 돌아올 곳이 없다. 성공했다고 말하면서 잃는 것이 가장 나쁘다.
        bool m_captured = false;
        // 0번이 지운 나무의 뿌리다. 뒤로 갈수록 깊어지므로, 되살릴 때 앞에서부터
        // 만들면 부모가 늘 먼저 있다.
        Array<ObjectSnapshot> m_objects;
    };
}
