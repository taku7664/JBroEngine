#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
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
        // `defaultComponent` 는 만들자마자 붙일 컴포넌트의 등록 이름이다(D-158). 에디터는 프레임워크의
        // 트랜스폼(`Component::Transform2D`·`Transform3D`)을 준다 - 트랜스폼이 없는 오브젝트는 캔버스 뷰에
        // 보이지도 않고 옮길 수도 없다. 기존 엔진은 트랜스폼이 오브젝트의 멤버라 늘 있었다.
        // `position` 은 널이 아니면 만들자마자 트랜스폼의 `position` 에 앞에서부터 써 넣는
        // 세 값이다(D-168, 기존 `DrawAddObjectMenu` 의 `spawnWorldPos`). `Vec2` 면 둘만 쓴다 -
        // 커맨드는 차원을 모른다. `layer` 는 놓을 레이어이고, 없는 번호면 캔버스 기본 레이어다.
        CreateObjectCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            const char* name,
            EditorObjectId parentId,
            const char* defaultComponent = nullptr,
            const float* position = nullptr,
            LayerId layer = InvalidLayerId);

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
        String m_defaultComponent;
        EditorObjectId m_parentId = InvalidEditorObjectId;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        float m_position[3] = {};
        bool m_hasPosition = false;
        LayerId m_layer = InvalidLayerId;
    };

    // 오브젝트의 이름을 바꾼다(D-142).
    //
    // 이름은 태그다(D-51). 문자열은 `NameTable` 에만 있고 오브젝트는 번호만 든다.
    //
    // **글자를 치는 동안 커맨드가 하나여야 한다.** 한 글자마다 한 칸씩 쌓이면 되돌리기가
    // 글자 수만큼 필요해진다 - 그래서 같은 오브젝트를 잇달아 고치는 것끼리는 합친다.
    class RenameObjectCommand final : public EditorCommand
    {
    public:
        RenameObjectCommand(EditorObjectRegistry& registry, EditorObjectId objectId,
            const char* name);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;
        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

    private:
        void Apply(const String& name);

        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        String m_before;
        String m_after;
        bool m_captured = false;
    };

    // **오브젝트마다 켜고 끄는 값 하나를 바꾸는 커맨드의 몸이다**(D-163). 활성과 에디터 숨김이 같은 모양이라
    // 한 벌로 둔다 - 되살릴 값을 먼저 뜨는 것, 바뀌는 것이 없으면 쌓지 않는 것, 오브젝트마다 제 값으로 되돌리는 것.
    // 여럿을 한 번에 바꿔도 커맨드는 하나다.
    class ObjectToggleCommand : public EditorCommand
    {
    public:
        using Getter = bool (*)(const GameObject& object);
        using Setter = void (*)(GameObject& object, bool value);

        bool Execute() override;
        void Undo() override;
        void Redo() override;

    protected:
        ObjectToggleCommand(EditorObjectRegistry& registry,
            const Array<EditorObjectId>& objects, bool after, Getter getter, Setter setter);

    private:
        void Apply(bool value);

        EditorObjectRegistry* m_registry = nullptr;
        Getter m_getter = nullptr;
        Setter m_setter = nullptr;
        Array<EditorObjectId> m_objects;
        // 오브젝트마다 바꾸기 전의 값이다. 다 같은 값이라고 볼 수 없다 - 여럿을 골랐을 때
        // 하나는 켜져 있고 하나는 꺼져 있을 수 있고, 되돌리면 각자 제 값으로 가야 한다.
        Array<std::uint8_t> m_before;
        bool m_after = true;
    };

    // 오브젝트의 활성 상태를 바꾼다(D-142).
    //
    // 예전에는 인스펙터가 `SetActive` 를 그대로 불렀다. 화면에서는 같아 보이지만
    // **되돌릴 수 없었다** - 에디터의 편집은 커맨드로만 한다(§11.5).
    class SetObjectActiveCommand final : public ObjectToggleCommand
    {
    public:
        SetObjectActiveCommand(EditorObjectRegistry& registry,
            const Array<EditorObjectId>& objects, bool active);
        const char* GetName() const override;
    };

    // 오브젝트를 **캔버스 뷰에서만** 감추거나 보인다(D-163, 기존 레이어 창의 눈 표시). 캔버스 파일에 남는다.
    class SetObjectEditorHiddenCommand final : public ObjectToggleCommand
    {
    public:
        SetObjectEditorHiddenCommand(EditorObjectRegistry& registry,
            const Array<EditorObjectId>& objects, bool hidden);
        const char* GetName() const override;
    };

    // 컴포넌트의 사용 여부를 바꾼다(D-142). 이쪽도 예전에는 커맨드 없이 바로 바꿨다.
    class SetComponentEnabledCommand final : public EditorCommand
    {
    public:
        SetComponentEnabledCommand(EditorObjectRegistry& registry,
            const ComponentAddress& address, bool enabled);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        void Apply(bool enabled);

        EditorObjectRegistry* m_registry = nullptr;
        ComponentAddress m_address;
        bool m_before = true;
        bool m_after = true;
        bool m_captured = false;
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
