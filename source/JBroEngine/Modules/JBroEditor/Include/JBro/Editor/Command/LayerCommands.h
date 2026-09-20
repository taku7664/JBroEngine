#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class Canvas;

    // 레이어를 고치는 커맨드들이다(D-135).
    //
    // 엔진에는 레이어가 있고 `.jcanvas` 도 레이어를 적는데, **에디터에는 레이어를 다루는
    // 길이 하나도 없었다** - 만들 수도, 이름을 바꿀 수도, 숨길 수도, 오브젝트를 옮길 수도
    // 없었다. 기존 엔진의 계층 창은 레이어를 머리로 두고 그 아래에 오브젝트를 묶어 보였다.
    //
    // **레이어 아이디는 되살아나지 않는다.** `Canvas::CreateLayer` 가 늘 새 번호를 주므로,
    // 지운 레이어를 되돌리면 같은 이름·같은 자리·같은 오브젝트를 가진 **새 번호**의 레이어가
    // 선다. 화면에서 보이는 것은 같고, 아이디를 들고 있던 쪽(오브젝트)은 이 커맨드가 다시 잇는다.

    class CreateLayerCommand final : public EditorCommand
    {
    public:
        CreateLayerCommand(Canvas& canvas, const char* name);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 만들어진 레이어다. 만든 뒤 그것을 고르는 쪽이 쓴다. 아직 만들지 않았으면 무효값이다.
        LayerId GetLayerId() const { return m_layerId; }

    private:
        Canvas* m_canvas = nullptr;
        String m_name;
        LayerId m_layerId = InvalidLayerId;
        // 되돌렸다 다시 하면 자리가 바뀌지 않게, 만들어진 자리를 들고 있는다.
        std::size_t m_index = 0;
    };

    class DeleteLayerCommand final : public EditorCommand
    {
    public:
        DeleteLayerCommand(Canvas& canvas, EditorObjectRegistry& registry, LayerId layer);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        // 지우기 전의 레이어와, 그 레이어에 있던 오브젝트들이다.
        // **되살릴 값을 먼저 뜨지 못하면 지우지 않는다**(§11.5).
        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        LayerId m_layerId = InvalidLayerId;
        String m_name;
        std::size_t m_index = 0;
        bool m_visible = true;
        Array<EditorObjectId> m_objects;
        bool m_captured = false;
    };

    class MoveLayerCommand final : public EditorCommand
    {
    public:
        MoveLayerCommand(Canvas& canvas, LayerId layer, std::size_t newIndex);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        std::size_t m_from = 0;
        std::size_t m_to = 0;
        bool m_captured = false;
    };

    class RenameLayerCommand final : public EditorCommand
    {
    public:
        RenameLayerCommand(Canvas& canvas, LayerId layer, const char* name);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 이름 칸에 타자를 치는 동안 프레임마다 커맨드가 생긴다. 같은 레이어면 합친다.
        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        String m_before;
        String m_after;
        bool m_captured = false;
    };

    class SetLayerVisibleCommand final : public EditorCommand
    {
    public:
        SetLayerVisibleCommand(Canvas& canvas, LayerId layer, bool visible);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        bool m_before = true;
        bool m_after = true;
        bool m_captured = false;
    };

    // 오브젝트를 다른 레이어로 옮긴다. **자식도 함께 간다** - 자식은 부모의 레이어를
    // 따른다는 것이 기존 엔진의 불변식이고, 하나만 옮기면 부모와 자식이 다른 칸에 놓인다.
    class SetObjectLayerCommand final : public EditorCommand
    {
    public:
        SetObjectLayerCommand(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            EditorObjectId objectId,
            LayerId layer);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        struct Placement
        {
            EditorObjectId objectId = InvalidEditorObjectId;
            LayerId layer = InvalidLayerId;
        };

        bool Apply(LayerId layer);

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        LayerId m_after = InvalidLayerId;
        // 부분 트리의 오브젝트마다 원래 레이어. 되돌리면 저마다 제 레이어로 간다.
        Array<Placement> m_before;
        bool m_captured = false;
    };
}
