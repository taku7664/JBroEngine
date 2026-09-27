#include "InspectorFieldExtras.h"

#include <JBro/Core/StableTypeId.h>
#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/Framework3DSystem/System/Text3DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

namespace JBro
{
    namespace
    {
        // **프레임 고르기**(기존 `DrawSpriteFramePickRow`). 숫자를 외워 치는 대신 시트에서 칸을 눌러 고른다 - 칸이
        // 마흔 장을 넘으면 번호만 보고 원하는 그림을 찾을 길이 없다. 누르면 스프라이트 뷰어가 그 시트로 열리고,
        // 칸을 누르면 `frameIndex` 가 커맨드로 들어간다. 고르는 중에는 이 단추가 "취소" 다.
        void DrawSpriteFramePick(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            const auto& sprite = *static_cast<const Component::SpriteRenderer2D*>(context.component);
            ComponentAddress address;
            if (false == MakeComponentAddress(context.editor->GetObjectIds(), *context.owner, *context.component, address))
            {
                return;
            }
            const bool pending = context.editor->IsSpriteFramePickFor(address);
            const bool hasSprite = false == sprite.spriteId.IsNull();
            // **값 칸에 둔다.** 줄 전체(`FullRow`)는 첫 칸에 그려져, 단추와 안내 글이 라벨 칸을 넓혀 위의 모든 값 칸을
            // 밀어냈다(에셋 칸을 찾는 테스트가 그래서 깨졌다). 기존도 값 쪽 `FrameIndex` 줄 바로 밑이었다.
            layout.Row([]() {}, [&]() {
                {
                    // 시트가 없으면 열어 볼 것도 없다. 고르는 중이면 취소는 되어야 한다.
                    Widget::DisableScope disabled(false == hasSprite && false == pending);
                    if (Widget::Button(pending
                            ? Loc::TextOr(LocKeys::CommonCancel, "Cancel")
                            : Loc::TextOr(LocKeys::InspectorPickFrame, "Pick Frame")))
                    {
                        if (pending)
                        {
                            context.editor->CancelSpriteFramePick();
                        }
                        else
                        {
                            context.editor->BeginSpriteFramePick(sprite.spriteId, address);
                        }
                    }
                }
                // 왜 못 누르는지는 단추의 툴팁이 말한다. 옆에 글로 두면 좁은 값 칸에서 잘렸다(실제 에디터에서 그랬다).
                if (false == hasSprite && false == pending)
                {
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::InspectorPickFrameNoSprite, "set a sprite first"),
                        ImGuiHoveredFlags_AllowWhenDisabled);
                }
            });
        }

        // **폰트가 없으면 지속 경고**(text-plan §4.6). 콘솔 경고는 한 번뿐이라 지나가면 왜 글자가 안 보이는지 알 길이 없다.
        // 판단은 그리는 시스템이 한다 - 기존 엔진은 인스펙터가 "아이디가 비었나" 만 봐서, 아이디는 있는데 파일이 깨진
        // 폰트에는 경고가 없었다.
        void DrawMissingFontRow(Widget::FormLayout& layout)
        {
            layout.Row([]() {}, [&]() {
                Widget::ValidationMessage(Widget::Severity::Warning,
                    Loc::TextOr(LocKeys::InspectorTextNoFont,
                        "No usable font, so this text is not drawn. Set fontId or add a font in Project Settings."))
                    .Wrapped()
                    .Draw();
            });
        }

        void DrawTextFontWarning(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            Canvas* canvas = context.editor->GetCanvas();
            System::Text2DSystem* texts =
                canvas != nullptr ? canvas->GetSystems().FindSystem<System::Text2DSystem>() : nullptr;
            if (texts != nullptr && texts->IsMissingFont(context.component->GetInstanceId()))
            {
                DrawMissingFontRow(layout);
            }
        }

        // 3D 텍스트도 같은 경고다(D-222). 판단은 3D 텍스트 시스템이 한다.
        void DrawText3DFontWarning(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            Canvas* canvas = context.editor->GetCanvas();
            System::Text3DSystem* texts =
                canvas != nullptr ? canvas->GetSystems().FindSystem<System::Text3DSystem>() : nullptr;
            if (texts != nullptr && texts->IsMissingFont(context.component->GetInstanceId()))
            {
                DrawMissingFontRow(layout);
            }
        }

        void DrawHintRow(Widget::FormLayout& layout, Widget::Severity severity, const char* text)
        {
            layout.Row([]() {}, [&]() {
                Widget::ValidationMessage(severity, text).Wrapped().Draw();
            });
        }

        // **투영이 쓰지 않는 필드에 한 줄을 붙인다**(D-239). 인스펙터에는 필드를 조건부로 잠그는 장치가 없다 - 고쳐도 아무 일이 없는 칸에
        // 까닭을 적어 둔다. `orthographicSize` 는 `Orthographic` 만, `pixelsPerUnit` 은 `PixelPerfect` 만 쓴다.
        void DrawCameraSizeHint(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            const auto& camera = *static_cast<const Component::Camera2D*>(context.component);
            if (camera.projection == Component::CameraProjection2D::PixelPerfect)
            {
                DrawHintRow(layout, Widget::Severity::Info, Loc::TextOr(LocKeys::InspectorCameraSizeUnused,
                    "Not used by PixelPerfect. The view is the reference resolution divided by pixelsPerUnit."));
            }
        }

        void DrawCameraPpuHint(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            const auto& camera = *static_cast<const Component::Camera2D*>(context.component);
            if (camera.projection != Component::CameraProjection2D::PixelPerfect)
            {
                DrawHintRow(layout, Widget::Severity::Info,
                    Loc::TextOr(LocKeys::InspectorCameraPpuUnused, "Used only by PixelPerfect."));
            }
        }

        // **게임 카메라가 `PixelPerfect` 인데 스프라이트의 PPU 가 다르면 경고한다**(D-239). 원본 1 픽셀이 화면 픽셀에 맞지 않아
        // 픽셀 아트가 고르지 않게 늘어난다 - 화면에서 알아채기 어렵고 까닭은 더 찾기 어렵다.
        void DrawSpritePpuWarning(Widget::FormLayout& layout, const FieldExtraContext& context)
        {
            const RenderCamera2D* camera = context.editor->GetGameCamera2D();
            AssetSystem* assets = context.editor->GetAssetSystem();
            if (camera == nullptr || camera->projection != Component::CameraProjection2D::PixelPerfect || assets == nullptr)
            {
                return;
            }
            const auto& sprite = *static_cast<const Component::SpriteRenderer2D*>(context.component);
            const SpriteData* data = assets->GetSprite(sprite.sprite);
            if (data == nullptr)
            {
                return;
            }
            const float ppu = data->options.pixelsPerUnit > 0.0f ? data->options.pixelsPerUnit : DefaultPixelsPerUnit;
            if (ppu != camera->pixelsPerUnit)
            {
                DrawHintRow(layout, Widget::Severity::Warning, Loc::TextOr(LocKeys::InspectorSpritePpuMismatch,
                    "This sprite's pixelsPerUnit differs from the PixelPerfect camera, so its pixels do not line up."));
            }
        }

        struct Entry
        {
            ComponentTypeId typeId;
            const char* field;
            FieldExtraDraw draw;
        };

        constexpr Entry Entries[] = {
            {MakeStableTypeId(Component::SpriteRenderer2D::StaticTypeName()), "frameIndex", &DrawSpriteFramePick},
            {MakeStableTypeId(Component::SpriteRenderer2D::StaticTypeName()), "spriteId", &DrawSpritePpuWarning},
            {MakeStableTypeId(Component::Camera2D::StaticTypeName()), "orthographicSize", &DrawCameraSizeHint},
            {MakeStableTypeId(Component::Camera2D::StaticTypeName()), "pixelsPerUnit", &DrawCameraPpuHint},
            {MakeStableTypeId(Component::Text2D::StaticTypeName()), "fontId", &DrawTextFontWarning},
            {MakeStableTypeId(Component::Text3D::StaticTypeName()), "fontId", &DrawText3DFontWarning},
        };
        constexpr std::size_t EntryCount = sizeof(Entries) / sizeof(Entries[0]);
    }

    FieldExtraDraw FindFieldExtra(ComponentTypeId typeId, NameId field)
    {
        // 필드 이름은 처음 한 번만 인턴한다. 그 뒤로는 정수만 견준다.
        static NameId names[EntryCount] = {};
        static bool interned = false;
        if (false == interned)
        {
            for (std::size_t index = 0; index < EntryCount; ++index)
            {
                names[index] = NameTable::Get().Intern(Entries[index].field);
            }
            interned = true;
        }
        for (std::size_t index = 0; index < EntryCount; ++index)
        {
            if (Entries[index].typeId == typeId && names[index] == field)
            {
                return Entries[index].draw;
            }
        }
        return nullptr;
    }
}
