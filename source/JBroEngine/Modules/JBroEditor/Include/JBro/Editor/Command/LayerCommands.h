#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/Command/ObjectTreeSnapshot.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

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
        Bool Execute() override;
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
        Bool Execute() override;
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
        Bool m_visible = true;
        // 되살릴 때 화면 레이어였는지도 같이 뜬다(D-237).
        LayerSpace m_space = LayerSpace::World;
        ScreenScaleMode m_scaleMode = ScreenScaleMode::FixedHeight;
        // 블렌드와 불투명도도 같이 뜬다(D-279). 안 뜨면 되돌린 레이어가 보통 레이어로 돌아온다.
        LayerBlend m_blend = LayerBlend::Normal;
        Float m_opacity = 1.0f;
        Float m_parallax = 1.0f;
        // 원본 에셋 표시도 같이 뜬다(D-287) - 기존 엔진은 레이어 삭제를 되돌리면 이것을 잃었다.
        Uuid m_sourceAsset;
        Array<EditorObjectId> m_objects;
        Bool m_captured = false;
    };

    class MoveLayerCommand final : public EditorCommand
    {
    public:
        MoveLayerCommand(Canvas& canvas, LayerId layer, std::size_t newIndex);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        std::size_t m_from = 0;
        std::size_t m_to = 0;
        Bool m_captured = false;
    };

    class RenameLayerCommand final : public EditorCommand
    {
    public:
        RenameLayerCommand(Canvas& canvas, LayerId layer, const char* name);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 이름 칸에 타자를 치는 동안 프레임마다 커맨드가 생긴다. 같은 레이어면 합친다.
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        String m_before;
        String m_after;
        Bool m_captured = false;
    };

    // **레이어의 공간과 맞춤 방식을 바꾼다**(D-237). 월드↔화면을 오가면 그 레이어 루트의 자리를 함께 옮긴다 - 화면에서 보이던 자리가 그대로 남게
    // (자리는 부르는 쪽이 게임 카메라로 계산해 넘긴다, `EditorApplication::MakeLayerSpaceCommand`). 되돌리면 공간·맞춤·자리가 모두 돌아온다.
    // 대상은 번호로 가리킨다. 옮길 루트의 옛 자리를 뜨지 못했으면 실행하지 않는다.
    class SetLayerSpaceCommand final : public EditorCommand
    {
    public:
        struct RootMove
        {
            EditorObjectId object = 0;
            Float x = 0.0f;
            Float y = 0.0f;
        };

        SetLayerSpaceCommand(Canvas& canvas, EditorObjectRegistry& registry, LayerId layer, LayerSpace space, ScreenScaleMode scaleMode,
            const Array<RootMove>& moves);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        void Apply(Bool after);

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        LayerId m_layerId = InvalidLayerId;
        LayerSpace m_spaceBefore = LayerSpace::World;
        LayerSpace m_spaceAfter = LayerSpace::World;
        ScreenScaleMode m_modeBefore = ScreenScaleMode::FixedHeight;
        ScreenScaleMode m_modeAfter = ScreenScaleMode::FixedHeight;
        Array<RootMove> m_after;
        Array<RootMove> m_before;
        Bool m_captured = false;
    };

    // **레이어의 블렌드와 불투명도를 바꾼다**(D-279). 인스펙터의 블렌드 칸과 불투명도 슬라이더가 낸다. 슬라이더를 끄는 동안
    // 프레임마다 생기므로 같은 레이어면 합친다 - 끌기 하나가 되돌리기 하나다.
    class SetLayerCompositeCommand final : public EditorCommand
    {
    public:
        SetLayerCompositeCommand(Canvas& canvas, LayerId layer, LayerBlend blend, Float opacity);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        void Apply(LayerBlend blend, Float opacity);

        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        LayerBlend m_blendBefore = LayerBlend::Normal;
        LayerBlend m_blendAfter = LayerBlend::Normal;
        Float m_opacityBefore = 1.0f;
        Float m_opacityAfter = 1.0f;
        Bool m_captured = false;
    };

    // **레이어의 패럴랙스 계수를 바꾼다**(D-286). 인스펙터의 끄는 칸이 낸다 - 같은 레이어끼리 합쳐 끌기 하나가 되돌리기 하나다.
    class SetLayerParallaxCommand final : public EditorCommand
    {
    public:
        SetLayerParallaxCommand(Canvas& canvas, LayerId layer, Float factor);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        Float m_before = 1.0f;
        Float m_after = 1.0f;
        Bool m_captured = false;
    };

    // **레이어의 원본 에셋 표시를 바꾼다**(D-287). 레이어를 에셋으로 저장하면 그 레이어가 그 파일에서 온 것으로 표시된다 - 되돌리면 표시만 빠지고
    // 파일은 남는다(파일 일은 에셋 브라우저의 것이다).
    class SetLayerSourceAssetCommand final : public EditorCommand
    {
    public:
        SetLayerSourceAssetCommand(Canvas& canvas, LayerId layer, const Uuid& asset);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        Uuid m_before;
        Uuid m_after;
        Bool m_captured = false;
    };

    // **레이어 에셋을 캔버스 맨 위에 새 레이어로 넣는다**(D-287, 기존 `CAddLayerFromAssetCommand`). 파일의 글자는 만들 때 떠 둔다 - 파일이 그 사이에
    // 바뀌어도 이 커맨드는 같은 것을 넣는다. 처음 실행이 오브젝트를 만든 뒤 그 나무들을 떠 두고, 되돌리면 나무째 지우고 레이어를 뺀다. 다시 하기는
    // 떠 둔 나무를 **같은 번호로** 되살린다 - 뒤의 커맨드가 그 오브젝트를 번호로 가리켜도 이어진다. 레이어는 새 번호로 선다(`DeleteLayerCommand` 와 같다).
    class AddLayerFromAssetCommand final : public EditorCommand
    {
    public:
        AddLayerFromAssetCommand(Canvas& canvas, EditorObjectRegistry& registry, const String& text, const Uuid& asset);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

        // 넣은 레이어다. 넣은 뒤 그것을 고르는 쪽이 쓴다. 실패했거나 되돌린 동안은 무효값이다.
        LayerId GetLayerId() const { return m_layerId; }
        // 읽지 못했을 때의 까닭이다.
        const String& GetError() const { return m_error; }

    private:
        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        String m_text;
        Uuid m_asset;
        LayerId m_layerId = InvalidLayerId;
        String m_error;
        // 처음 실행 뒤에 뜬다 - 레이어의 값(레이어 에셋 글자의 레이어 노드)과 자리, 뿌리 나무들.
        Bool m_captured = false;
        std::size_t m_index = 0;
        String m_name;
        Bool m_visible = true;
        LayerSpace m_space = LayerSpace::World;
        ScreenScaleMode m_scaleMode = ScreenScaleMode::FixedHeight;
        LayerBlend m_blend = LayerBlend::Normal;
        Float m_opacity = 1.0f;
        Float m_parallax = 1.0f;
        Array<ObjectTreeSnapshot> m_trees;
    };

    class SetLayerVisibleCommand final : public EditorCommand
    {
    public:
        SetLayerVisibleCommand(Canvas& canvas, LayerId layer, Bool visible);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        Canvas* m_canvas = nullptr;
        LayerId m_layerId = InvalidLayerId;
        Bool m_before = true;
        Bool m_after = true;
        Bool m_captured = false;
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
        Bool Execute() override;
        void Undo() override;
        void Redo() override;

    private:
        struct Placement
        {
            EditorObjectId objectId = InvalidEditorObjectId;
            LayerId layer = InvalidLayerId;
        };

        Bool Apply(LayerId layer);

        Canvas* m_canvas = nullptr;
        EditorObjectRegistry* m_registry = nullptr;
        EditorObjectId m_objectId = InvalidEditorObjectId;
        LayerId m_after = InvalidLayerId;
        // 부분 트리의 오브젝트마다 원래 레이어. 되돌리면 저마다 제 레이어로 간다.
        Array<Placement> m_before;
        Bool m_captured = false;
    };
}
