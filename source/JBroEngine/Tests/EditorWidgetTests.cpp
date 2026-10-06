#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/EditorGuideFocus.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/DragDrop.h>
#include <JBro/Editor/Widget/EnumCombo.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Tree.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 공용 위젯 계층이다(ProjectRule §11.1).
//
// **렌더러 없이 잰다.** ImGui 는 컨텍스트와 글꼴 아틀라스만 있으면 프레임을
// 돌 수 있고, 위젯이 스스로 무너지는 곳은 거의 다 그 안에서 드러난다 -
// Id 스택이 어긋났는가, 표를 제대로 닫았는가, 콜백이 몇 번 불렸는가.

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }


    // 숫자 칸이 받는 타입(D-290). 생성자로 값을 받으므로 타입을 적지 않아도 정해진다.
    template<typename T>
    concept CanDragField = requires(T& value) { JBro::Widget::DragField("##probe", value); };

    template<typename T>
    concept CanSliderField = requires(T& value) { JBro::Widget::SliderField("##probe", value, value, value); };

    // **엔진 값 타입만 받는다.** 원시 `float`·`int` 를 넘기면 컴파일이 멈춘다 - 이 단언이 그것을 붙잡는다.
    static_assert(CanDragField<JBro::Float> && CanDragField<JBro::Int32> && CanDragField<JBro::Int64>
        && CanDragField<JBro::UInt32> && CanDragField<JBro::UInt64>, "every engine number type has a number field");
    static_assert(false == CanDragField<float> && false == CanDragField<int> && false == CanDragField<double>
        && false == CanDragField<std::uint32_t>, "a raw number must not reach the number field");
    static_assert(CanSliderField<JBro::Float> && CanSliderField<JBro::Int32>, "the slider takes the same types");
    static_assert(false == CanSliderField<float> && false == CanSliderField<int>, "and refuses raw numbers too");
    // 타입을 적지 않아도 값에서 정해진다.
    static_assert(std::is_same_v<decltype(JBro::Widget::DragField("##probe", std::declval<JBro::Float&>())),
        JBro::Widget::DragField<JBro::Float>>, "the field type comes from the value");

    // 창 하나짜리 무대. 화면에 내보내지 않고 프레임만 돈다.
    class Stage
    {
    public:
        Stage()
        {
            m_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_context);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
        }

        ~Stage()
        {
            ImGui::DestroyContext(m_context);
        }

        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

        void Begin()
        {
            ImGui::NewFrame();
            // **크기를 준다.** 자동 크기 창은 첫 프레임에 거의 0 이라 그 안의
            // 사각형을 재는 것이 뜻이 없다 - 실제 패널은 도크가 크기를 준다.
            ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
            ImGui::Begin("Stage");
        }

        // 배치가 자리 잡을 때까지 빈 프레임을 돌린다.
        void Settle()
        {
            for (JBro::Int32 frame = 0; frame < 2; ++frame)
            {
                Begin();
                End();
            }
        }

        void End()
        {
            ImGui::End();
            ImGui::Render();
        }

        // Id 스택이 프레임 끝에서 제자리인가. 어긋났으면 ImGui 가 단언한다.
        std::size_t IdStackDepth() const
        {
            ImGuiWindow* window = ImGui::FindWindowByName("Stage");
            return window != nullptr
                ? static_cast<std::size_t>(window->IDStack.Size) : 0;
        }

    private:
        ImGuiContext* m_context = nullptr;
    };

    // **스코프는 되돌아 나가도 스택을 되돌린다.** 그것이 존재 이유다.
    void TestScopesUnwindThemselves()
    {
        Stage stage;
        stage.Begin();

        const JBro::Int32 colorsBefore = ImGui::GetCurrentContext()->ColorStack.Size;
        const JBro::Int32 varsBefore = ImGui::GetCurrentContext()->StyleVarStack.Size;
        {
            JBro::Widget::StyleScope style;
            style.PushColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
            style.PushColor(ImGuiCol_Button, ImVec4(0, 1, 0, 1));
            style.PushVar(ImGuiStyleVar_Alpha, 0.5f);
            Check(ImGui::GetCurrentContext()->ColorStack.Size == colorsBefore + 2,
                "pushing must actually push");
        }
        Check(ImGui::GetCurrentContext()->ColorStack.Size == colorsBefore,
            "leaving the scope must pop the colours");
        Check(ImGui::GetCurrentContext()->StyleVarStack.Size == varsBefore,
            "and the style vars");

        // 두 번 빼내도 한 번만 빼낸다. `Pop` 을 부른 뒤 소멸자가 또 빼면
        // 남의 스타일을 벗긴다.
        {
            JBro::Widget::StyleScope style;
            style.PushColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
            style.Pop();
            Check(ImGui::GetCurrentContext()->ColorStack.Size == colorsBefore,
                "an explicit pop takes it off");
        }
        Check(ImGui::GetCurrentContext()->ColorStack.Size == colorsBefore,
            "and the destructor must not take off one more");

        const std::size_t idBefore = stage.IdStackDepth();
        {
            JBro::Widget::IdScope id("probe");
            Check(stage.IdStackDepth() == idBefore + 1, "an id scope pushes one");
        }
        Check(stage.IdStackDepth() == idBefore, "and takes it back");

        stage.End();
    }

    // 줄 배치는 **표를 열고 닫는다.** 열지 못했으면 아무것도 그리지 않아야
    // 하고, 닫을 때 스타일을 표보다 먼저 벗어야 한다.
    void TestTheFormLayoutOpensAndClosesCleanly()
    {
        Stage stage;
        stage.Begin();

        const std::size_t idBefore = stage.IdStackDepth();
        JBro::Int32 labels = 0;
        JBro::Int32 fields = 0;
        {
            JBro::Widget::FormLayout layout("##probe");
            Check(layout.IsOpen(), "the layout must open inside a window");
            layout.Row([&]() { ++labels; ImGui::TextUnformatted("name"); },
                [&]() { ++fields; ImGui::TextUnformatted("value"); });
            layout.Row([&]() { ++labels; ImGui::TextUnformatted("other"); },
                [&]() { ++fields; ImGui::TextUnformatted("value"); });
            layout.FullRow([&]() { ImGui::TextUnformatted("across"); });
        }
        Check(labels == 2 && fields == 2, "each row must draw both halves once");
        Check(stage.IdStackDepth() == idBefore,
            "and the table must leave the id stack where it found it");

        stage.End();
    }

    // 목록은 **저장소를 모른다.** 콜백이 전부이므로 콜백이 몇 번, 어떤 값으로
    // 불리는지가 이 위젯의 계약이다.
    void TestTheListAsksItsCallbacksForEverything()
    {
        Stage stage;
        stage.Begin();

        JBro::Int32 drawn = 0;
        JBro::Int32 added = 0;
        JBro::Int32 removed = -1;
        JBro::Int32 movedFrom = -1;
        JBro::Int32 movedTo = -1;
        const JBro::Bool changed = JBro::Widget::ListVirtual("##probe", 3,
            [&](JBro::Int32) -> JBro::Bool { ++drawn; ImGui::TextUnformatted("row"); return false; },
            [&]() { ++added; },
            [&](JBro::Int32 index) { removed = index; },
            [&](JBro::Int32 from, JBro::Int32 to) { movedFrom = from; movedTo = to; });

        Check(drawn == 3, "every element must be drawn once");
        Check(false == changed, "nothing was touched, so nothing changed");
        Check(added == 0 && removed == -1, "and no callback fired on its own");
        Check(movedFrom == -1 && movedTo == -1, "including the move one");

        // 읽기 전용이면 삭제 단추도 추가 줄도 그리지 않는다. 그려 놓고 눌러도
        // 아무 일이 없으면 고장인지 잠긴 것인지 알 수 없다.
        drawn = 0;
        JBro::Widget::ListVirtual("##readonly", 2,
            [&](JBro::Int32) -> JBro::Bool { ++drawn; ImGui::TextUnformatted("row"); return false; },
            [&]() { ++added; },
            [&](JBro::Int32) {},
            [&](JBro::Int32, JBro::Int32) {},
            JBro::Widget::ListFlagsReadOnly);
        Check(drawn == 2, "a read-only list still shows its elements");
        Check(added == 0, "but must not offer to add one");

        stage.End();
    }

    // **행은 그린 것만큼 높다**(D-89). 필드를 가진 구조체 원소는 한 행에 여러 줄을 그린다.
    // 처음에는 행 높이가 한 줄로 고정되어, 다음 행이 앞 행의 둘째 줄 위에 겹쳐 그려졌다.
    void TestAListRowIsAsTallAsWhatItDraws()
    {
        Stage stage;
        stage.Settle();

        JBro::Float top[3] = {};
        JBro::Float bottom[3] = {};
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            stage.Begin();
            JBro::Widget::ListVirtual("##rows", 3,
                [&](JBro::Int32 index) -> JBro::Bool {
                    top[index] = ImGui::GetCursorScreenPos().y;
                    ImGui::Button("first");
                    if (index == 1)
                    {
                        // 가운데 행만 세 줄이다.
                        ImGui::Button("second");
                        ImGui::Button("third");
                    }
                    bottom[index] = ImGui::GetItemRectMax().y;
                    return false;
                },
                []() {},
                [](JBro::Int32) {},
                [](JBro::Int32, JBro::Int32) {});
            stage.End();
        }

        const JBro::Float gapAfterShort = top[1] - bottom[0];
        const JBro::Float gapAfterTall = top[2] - bottom[1];
        Check(bottom[1] - top[1] > 2.0f * (bottom[0] - top[0]),
            "the test needs a middle row several lines tall");
        Check(top[2] >= bottom[1], "the row after a tall one must start below all of it");
        Check(gapAfterTall == gapAfterShort,
            "and leave the same gap as after a one-line row, no more and no less");
        // **한 줄짜리 행은 전과 같은 자리에 와야 한다.** 틈은 행 간격 1 과 끌어 놓을 자리 3 이다
        // (`List.h`). 행이 자라게 고치기 전후의 에디터 스크린샷이 한 바이트도 다르지 않을 때 잰
        // 값이다 - 모든 행에 같은 만큼 틈이 늘면 위의 비교로는 드러나지 않는다.
        Check(gapAfterShort == 4.0f, "and a one-line row must keep the gap it always had");
    }

    // `Array<T>` 덮개는 목록에 저장소를 이어 준다. 여기서는 **옮기기 셈**이
    // 맞는지를 본다 - 슬롯 번호와 원소 번호가 다르다는 것이 이 위젯의 함정이다.
    void TestTheArrayWrapperMovesElementsCorrectly()
    {
        Stage stage;
        stage.Begin();

        JBro::Array<JBro::Int32> items;
        items.Add(10);
        items.Add(20);
        items.Add(30);
        items.Add(40);

        // 목록 위젯을 거치지 않고 덮개가 만드는 옮기기만 따로 확인한다.
        // 위젯은 보정된 **원소 번호**를 넘긴다고 약속한다.
        auto move = [&](JBro::Int32 fromIndex, JBro::Int32 toIndex) {
            JBro::Int32 moved = items[static_cast<std::size_t>(fromIndex)];
            if (fromIndex < toIndex)
            {
                for (JBro::Int32 at = fromIndex; at < toIndex; ++at)
                {
                    items[static_cast<std::size_t>(at)] =
                        items[static_cast<std::size_t>(at) + 1];
                }
            }
            else
            {
                for (JBro::Int32 at = fromIndex; at > toIndex; --at)
                {
                    items[static_cast<std::size_t>(at)] =
                        items[static_cast<std::size_t>(at) - 1];
                }
            }
            items[static_cast<std::size_t>(toIndex)] = moved;
        };

        move(0, 2);
        Check(items[0] == 20 && items[1] == 30 && items[2] == 10 && items[3] == 40,
            "moving forward must slide the ones in between back");
        move(3, 1);
        Check(items[0] == 20 && items[1] == 40 && items[2] == 30 && items[3] == 10,
            "and moving backward must slide them forward");

        stage.End();
    }

    // **행의 배경과 끌기 자리는 행이 그린 만큼 덮는다**(D-89). 처음에는 한 줄 높이라 펼친
    // 구조체 원소의 둘째 줄부터는 손잡이 자리를 눌러도 아무것도 잡히지 않았다. 높이는 지난
    // 프레임에 잰 것이므로 프레임 둘을 돌린 뒤에 재고, 마디를 접은 뒤에는 다시 줄어야 한다.
    void TestARowsBackgroundCoversEverythingItDraws()
    {
        Stage stage;
        JBro::Bool tall = true;
        JBro::Float top[3] = {};
        JBro::Float bottom[3] = {};
        const auto frame = [&]() {
            stage.Begin();
            JBro::Widget::ListVirtual("##cover", 3,
                [&](JBro::Int32 index) -> JBro::Bool {
                    top[index] = ImGui::GetCursorScreenPos().y;
                    ImGui::Button("first");
                    if (index == 1 && tall)
                    {
                        ImGui::Button("second");
                        ImGui::Button("third");
                    }
                    bottom[index] = ImGui::GetItemRectMax().y;
                    return false;
                },
                []() {},
                [](JBro::Int32) {},
                [](JBro::Int32, JBro::Int32) {});
            stage.End();
        };
        for (JBro::Int32 at = 0; at < 3; ++at)
        {
            frame();
        }
        ImGuiWindow* body = nullptr;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "##list_body") != nullptr)
            {
                body = window;
            }
        }
        Check(body != nullptr, "the list must open its body");
        JBro::Int32 rowValue = 1;
        const ImGuiID rowBody = ImHashStr("##row_body", 0,
            ImHashData(&rowValue, sizeof(rowValue), body->ID));
        const auto hoveredAt = [&](JBro::Float y) {
            ImGui::GetIO().AddMousePosEvent(body->Pos.x + 5.0f, y);
            frame();
            return ImGui::GetHoveredID();
        };
        Check(hoveredAt(top[1] + 2.0f) == rowBody, "the first line of a row must be its handle");
        Check(hoveredAt(bottom[1] - 2.0f) == rowBody,
            "and so must the last line of a row that draws several");
        // 배경이 넘치는 것은 뒤 항목이 가려 마우스로는 보이지 않는다. 배경 높이를 정하는
        // 저장값을 직접 본다 - 내용 높이와 같아야 하고, 접으면 한 줄로 돌아와야 한다.
        const ImGuiID heightKey = ImHashStr("##row_height", 0,
            ImHashData(&rowValue, sizeof(rowValue), body->ID));
        const JBro::Float tallHeight = bottom[1] - top[1];
        Check(body->DC.StateStorage->GetFloat(heightKey, 0.0f) == tallHeight,
            "the remembered row height must be exactly what the row drew");

        // 접으면 다시 한 줄이다. 목록은 줄 간격을 좁힌 제 스타일로 그리므로, 한 줄 높이는
        // 바깥의 `GetFrameHeight()` 가 아니라 한 줄이 된 행이 실제로 그린 높이로 잰다.
        tall = false;
        frame();
        frame();
        Check(bottom[1] - top[1] < tallHeight, "the test needs the row to have shrunk");
        Check(body->DC.StateStorage->GetFloat(heightKey, 0.0f) == bottom[1] - top[1],
            "a row that shrank back to one line must not keep its old height");
        Check(hoveredAt(top[1] + 2.0f) == rowBody, "and its first line is still its handle");
    }

    // **놓는 자리는 "이 원소 앞" 이고, 옮길 것이 없는 손짓은 바뀐 것이 없다**(D-89 ⑤).
    //
    // 인스펙터 테스트는 커맨드가 생기는지로 재므로, 아무것도 옮기지 않은 손짓에 위젯이
    // "바뀌었다" 고 답하는 것은 거기서 보이지 않는다 - 커맨드가 값을 비교해 걸러 준다. 그 답은
    // 이 위젯의 계약이라 여기서 마우스를 흘려 직접 잰다. 렌더러 없이 ImGui 입력 이벤트로 끈다.
    void TestDroppingARowMovesItOnceAndDroppingBelowItselfChangesNothing()
    {
        Stage stage;
        JBro::Int32 movedFrom = -1;
        JBro::Int32 movedTo = -1;
        JBro::Bool changedInAnyFrame = false;
        const auto frame = [&]() {
            stage.Begin();
            const JBro::Bool changed = JBro::Widget::ListVirtual("##drag", 3,
                [&](JBro::Int32) -> JBro::Bool { ImGui::TextUnformatted("row"); return false; },
                [&]() {},
                [&](JBro::Int32) {},
                [&](JBro::Int32 from, JBro::Int32 to) { movedFrom = from; movedTo = to; });
            changedInAnyFrame = changedInAnyFrame || changed;
            stage.End();
        };
        const auto moveMouse = [&](JBro::Float x, JBro::Float y) {
            ImGui::GetIO().AddMousePosEvent(x, y);
            frame();
        };
        const auto press = [&](JBro::Bool down) {
            ImGui::GetIO().AddMouseButtonEvent(0, down);
            frame();
        };
        frame();
        frame();

        ImGuiWindow* body = nullptr;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "##list_body") != nullptr)
            {
                body = window;
            }
        }
        Check(body != nullptr, "the list must open its body");
        const auto pushedId = [](ImGuiID seed, JBro::Int32 value) {
            return ImHashData(&value, sizeof(value), seed);
        };
        const auto labelId = [](ImGuiID seed, const char* label) {
            return ImHashStr(label, 0, seed);
        };
        // `x` 에서 위아래로 훑어 `target` 이 가리켜지는 y 를 찾는다.
        const auto findY = [&](ImGuiID target, JBro::Float x, JBro::Float& y) {
            for (JBro::Float at = body->Pos.y; at < body->Pos.y + body->Size.y; at += 1.0f)
            {
                moveMouse(x, at);
                if (ImGui::GetHoveredID() == target)
                {
                    y = at;
                    return true;
                }
            }
            return false;
        };
        const auto dragTo = [&](JBro::Float fromX, JBro::Float fromY, JBro::Float toX, JBro::Float toY) {
            moveMouse(fromX, fromY);
            press(true);
            constexpr JBro::Int32 Steps = 10;
            for (JBro::Int32 step = 1; step <= Steps; ++step)
            {
                moveMouse(fromX + (toX - fromX) * step / Steps,
                    fromY + (toY - fromY) * step / Steps);
            }
            frame();
            press(false);
            frame();
        };
        const JBro::Float handleX = body->Pos.x + 5.0f;
        const JBro::Float middleX = body->Pos.x + body->Size.x * 0.5f;
        JBro::Float rowY = 0.0f;
        JBro::Float slotY = 0.0f;

        // 둘째 행을 첫 행 위에 놓는다.
        Check(findY(labelId(pushedId(body->ID, 1), "##row_body"), handleX, rowY),
            "the second row must have a handle");
        Check(findY(labelId(pushedId(body->ID, 0), "##slot"), middleX, slotY),
            "there must be a slot above the first row");
        dragTo(handleX, rowY, middleX, slotY);
        Check(movedFrom == 1 && movedTo == 0, "dropping above the first row must move to 0");
        Check(changedInAnyFrame, "and the list must say something changed");

        // 둘째 행을 제 바로 아래 자리에 놓는다. 옮길 것이 없다.
        movedFrom = -1;
        movedTo = -1;
        changedInAnyFrame = false;
        Check(findY(labelId(pushedId(body->ID, 2), "##slot"), middleX, slotY),
            "there must be a slot below the second row");
        dragTo(handleX, rowY, middleX, slotY);
        Check(movedFrom == -1 && movedTo == -1,
            "dropping a row right below itself must not ask to move it");
        Check(false == changedInAnyFrame, "and the list must not say anything changed");

        // 첫 행을 맨 끝 자리에 놓는다. 원본을 먼저 빼므로 목표는 한 칸 당겨진 2 다.
        Check(findY(labelId(pushedId(body->ID, 0), "##row_body"), handleX, rowY),
            "the first row must have a handle");
        Check(findY(labelId(pushedId(body->ID, 3), "##slot"), middleX, slotY),
            "there must be a slot after the last row");
        dragTo(handleX, rowY, middleX, slotY);
        Check(movedFrom == 0 && movedTo == 2,
            "dropping at the end must hand over the corrected element index");
    }

    // 트리는 **줄 사각형과 내용 사각형을 나눠 준다.** 그것이 없으면 이 위젯을
    // 옮길 이유가 없다.
    void TestTheTreeHandsBackItsRowAndContent()
    {
        Stage stage;
        stage.Settle();
        stage.Begin();

        JBro::Widget::TreeDrawContext row;
        // **잎사귀에도 `NoTreePushOnOpen` 이 필요하다.** 열린 것으로 치면서
        // Id 를 밀어 넣으므로, 빼지 않으면 ImGui 가 "TreePop 이 빠졌다" 고
        // 단언한다 - 계층 패널이 자식 없는 줄에 같은 짝을 준다.
        const JBro::Bool opened = JBro::Widget::TreeBegin("##node",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf
                | ImGuiTreeNodeFlags_NoTreePushOnOpen, &row);
        JBro::Widget::TreeEnd();

        Check(row.IsVisible, "a node inside a visible window must be visible");
        Check(row.RowRect.Max.x > row.RowRect.Min.x, "the row must have width");
        Check(row.RowRect.Max.y > row.RowRect.Min.y, "and height");
        // **내용은 화살표 오른쪽에서 시작한다.** 줄 왼쪽에서 시작하면 이름이
        // 화살표 위에 겹친다.
        Check(row.ContentRect.Min.x > row.RowRect.Min.x,
            "the content must start to the right of the arrow");
        Check(row.ContentRect.Min.x < row.RowRect.Max.x,
            "and still be inside the row");
        Check(opened, "a leaf with DefaultOpen reports open");

        stage.End();
    }

    // 글자 칸은 `String` 과 ImGui 버퍼 사이를 옮긴다. 아무도 만지지 않으면
    // 값이 그대로여야 한다 - 프레임마다 다시 써 넣으면 되돌리기가 매 프레임 쌓인다.
    void TestTheTextFieldLeavesUntouchedValuesAlone()
    {
        Stage stage;
        stage.Begin();

        JBro::String text = "hello";
        const JBro::Bool changed = JBro::Widget::TextField("##probe", text).Draw();
        Check(false == changed, "nobody typed, so nothing changed");
        Check(text == JBro::String("hello"), "and the value is untouched");

        JBro::String empty;
        JBro::Widget::SearchBox("##search", empty).Draw();
        Check(empty.size() == 0, "an empty search box stays empty");

        stage.End();
    }

    // 드롭다운 팝업 창을 찾는다. ImGui 는 콤보 팝업을 "##Combo_NN" 으로 연다.
    ImGuiWindow* FindComboPopup()
    {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "##Combo_") != nullptr && window->WasActive)
            {
                return window;
            }
        }
        return nullptr;
    }

    // **검색 드롭다운은 글자 몇 자와 Enter 로 고른다**(D-116). 마우스로 열고, 글자를 넣고,
    // Enter 를 눌러 보이는 첫 항목이 골라지는지 잰다. 다시 열면 검색이 비어 있어야 한다 -
    // 남아 있으면 Enter 가 지난번 항목을 다시 고른다.
    void TestTheFilterComboPicksByTypingAndEnter()
    {
        Stage stage;
        const char* const items[] = { "Transform2D", "SpriteRenderer2D", "Rigidbody2D" };
        JBro::Int32 current = -1;
        JBro::Int32 changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            const JBro::Bool changed = JBro::Widget::FilterCombo("##probe", items, current)
                .EmptyText("(none)")
                .Width(200.0f)
                .Draw();
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            if (changed)
            {
                ++changedFrames;
            }
            stage.End();
        };
        const auto open = [&]() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f,
                (triggerMin.y + triggerMax.y) * 0.5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            // 팝업은 눌린 다음 프레임에 나타나고, 검색 칸은 그 프레임에 활성화된다.
            // 활성화되는 프레임의 글자는 버려지므로 한 프레임 더 돈다.
            frame();
            frame();
            Check(FindComboPopup() != nullptr, "clicking the trigger must open the popup");
        };
        const auto type = [&](const char* text) {
            for (const char* at = text; *at != '\0'; ++at)
            {
                ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(*at));
                frame();
            }
        };
        const auto pressEnter = [&]() {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            // 창의 WasActive 는 다음 NewFrame 에서야 갱신된다. 닫힘을 보려면 한 프레임 더.
            frame();
        };

        stage.Settle();
        frame();
        frame();
        Check(current == -1 && changedFrames == 0, "nothing chosen before anyone touches it");
        Check(triggerMax.x - triggerMin.x > 100.0f, "the trigger must have the width it was given");

        open();
        type("rig");
        pressEnter();
        Check(current == 2, "typing 'rig' and Enter must pick Rigidbody2D");
        Check(changedFrames == 1, "and report the change exactly once");
        Check(FindComboPopup() == nullptr, "Enter must close the popup");

        // 다시 열면 검색이 비어 있다. 첫 항목이 골라진다.
        open();
        pressEnter();
        Check(current == 0, "a reopened combo starts with an empty filter, so Enter picks the first item");
        Check(changedFrames == 2, "that is the second change");

        // 아무것에도 맞지 않는 글자 뒤의 Enter 는 아무것도 고르지 않는다.
        open();
        type("zzz");
        pressEnter();
        Check(current == 0 && changedFrames == 2, "Enter with no visible item changes nothing");
        Check(FindComboPopup() != nullptr, "and the popup stays open for another try");
        // 팝업 밖을 누르면 닫힌다.
        ImGui::GetIO().AddMousePosEvent(triggerMin.x + 5.0f, triggerMax.y + 200.0f);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        frame();
        Check(FindComboPopup() == nullptr, "clicking outside closes the popup");

        // 이미 고른 항목을 다시 골라도 바뀐 것이 없다. 인스펙터가 이 답으로 커맨드를 내므로
        // 여기서 거짓이어야 되돌리기 목록에 빈 걸음이 쌓이지 않는다.
        open();
        type("trans");
        pressEnter();
        Check(current == 0 && changedFrames == 2, "picking the current item again is not a change");
    }

    // **갈래로 묶고, 못 고르는 것은 회색으로 둔다**(D-180, 기존 컴포넌트 갈래 메뉴).
    // 갈래 제목줄이 실제로 자리를 차지하는지, Enter 가 회색 항목을 건너뛰는지 잰다.
    void TestTheFilterComboGroupsItemsAndSkipsTheDisabled()
    {
        Stage stage;
        const char* const items[] = { "Collider2D", "Rigidbody2D", "Camera2D" };
        const char* const groups[] = { "Physics", "Physics", "Rendering" };
        // 첫 항목은 이미 붙어 있어 고를 수 없다.
        const JBro::Bool addable[] = { false, true, true };
        JBro::Int32 plain = -1;
        JBro::Int32 grouped = -1;
        JBro::Float plainHeight = 0.0f;
        JBro::Float groupedHeight = 0.0f;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        JBro::Bool useGroups = false;
        const auto frame = [&]() {
            stage.Begin();
            if (useGroups)
            {
                JBro::Widget::FilterCombo("##grouped", items, grouped)
                    .EmptyText("(none)")
                    .ItemGroups(groups)
                    .ItemEnabled(addable)
                    .DisabledTooltip("already added")
                    .Width(200.0f)
                    .Draw();
            }
            else
            {
                JBro::Widget::FilterCombo("##plain", items, plain)
                    .EmptyText("(none)")
                    .Width(200.0f)
                    .Draw();
            }
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            if (const ImGuiWindow* popup = FindComboPopup())
            {
                JBro::Float& into = useGroups ? groupedHeight : plainHeight;
                into = popup->ContentSize.y;
            }
            stage.End();
        };
        const auto open = [&]() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f,
                (triggerMin.y + triggerMax.y) * 0.5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
            frame();
            Check(FindComboPopup() != nullptr, "clicking the trigger must open the popup");
        };
        const auto pressEnter = [&]() {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            frame();
        };

        stage.Settle();
        frame();
        frame();
        // 갈래 없이 같은 세 항목을 그린 팝업의 높이를 먼저 잰다.
        open();
        pressEnter();
        Check(plain == 0, "without groups Enter picks the very first item");
        Check(plainHeight > 0.0f, "and the plain popup must have been measured");

        useGroups = true;
        triggerKnown = false;
        frame();
        frame();
        open();
        Check(groupedHeight > plainHeight + ImGui::GetTextLineHeight(),
            "two group headings must take space the plain list does not");
        pressEnter();
        Check(grouped == 1,
            "Enter must skip the item that cannot be added and pick the next one");
    }

    // 빈 목록은 열려도 아무것도 고르지 않고 무너지지 않는다.
    void TestAnEmptyFilterComboDrawsAndChangesNothing()
    {
        Stage stage;
        JBro::Int32 current = -1;
        const JBro::ArrayView<const char* const> none;
        stage.Begin();
        const JBro::Bool changed = JBro::Widget::FilterCombo("##empty", none, current)
            .EmptyText("(none)").Draw();
        stage.End();
        Check(false == changed && current == -1, "an empty list changes nothing");

        Check(JBro::Widget::MatchesFilter("SpriteRenderer2D", ""), "an empty filter matches all");
        Check(JBro::Widget::MatchesFilter("SpriteRenderer2D", "render"), "case does not matter");
        Check(false == JBro::Widget::MatchesFilter("SpriteRenderer2D", "mesh"), "a miss is a miss");
        Check(false == JBro::Widget::MatchesFilter(nullptr, "a"), "no text matches nothing");
    }

    // `EnumCombo` 는 같은 몸이다. 이름이 몇 개뿐이면 검색 칸이 없으므로 마우스로 항목을
    // 눌러 값이 바뀌는지 잰다. 이름표의 `FromIndex` 가 실제로 불려야 한다.
    void TestTheEnumComboChangesTheValueWhenAnItemIsClicked()
    {
        Stage stage;
        static const char* const names[] = { "Nearest", "Linear", "Cubic" };
        JBro::EnumNames table;
        table.names = names;
        table.count = 3;
        table.ToIndex = [](const void* value) noexcept -> JBro::Int32 {
            return *static_cast<const std::int32_t*>(value);
        };
        table.FromIndex = [](void* value, JBro::Int32 index) noexcept {
            *static_cast<std::int32_t*>(value) = index;
        };
        JBro::Int32 value = 0;
        JBro::Int32 changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            if (JBro::Widget::EnumCombo("##enum", table, &value, 150.0f))
            {
                ++changedFrames;
            }
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            stage.End();
        };
        ImGuiIO& io = ImGui::GetIO();
        stage.Settle();
        frame();
        frame();
        Check(value == 0 && changedFrames == 0, "untouched, the value stays");

        io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f, (triggerMin.y + triggerMax.y) * 0.5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        ImGuiWindow* popup = FindComboPopup();
        Check(popup != nullptr, "clicking the trigger must open the popup");
        // 이름 셋은 한 화면에 들어간다. 검색 칸이 없으므로 포커스를 받은 글자 칸도 없다.
        Check(ImGui::GetActiveID() == 0, "a short enum shows no search box to focus");

        // 둘째 항목("Linear")의 Id 는 팝업 창 → PushID(1) → 이름이다.
        JBro::Int32 itemIndex = 1;
        const ImGuiID linear = ImHashStr(names[1], 0,
            ImHashData(&itemIndex, sizeof(itemIndex), popup->ID));
        JBro::Bool found = false;
        for (JBro::Float at = popup->Pos.y; at < popup->Pos.y + popup->Size.y; at += 1.0f)
        {
            io.AddMousePosEvent(popup->Pos.x + 10.0f, at);
            frame();
            if (ImGui::GetHoveredID() == linear)
            {
                found = true;
                break;
            }
        }
        Check(found, "the second item must be somewhere in the popup");
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        // 창의 WasActive 는 다음 NewFrame 에서야 갱신된다. 닫힘을 보려면 한 프레임 더.
        frame();
        Check(value == 1, "clicking Linear must write 1 through FromIndex");
        Check(changedFrames == 1, "and report the change once");
        Check(FindComboPopup() == nullptr, "and close the popup");
    }


    // **레이어 칸은 이름으로 켜고 끈다(D-233).** 펼치면 이름 있는 레이어마다 켜기 칸이 있고 누르면 그 비트가 켜지며 팝업은
    // 열린 채다. 이름 없는 비트는 켜져 있을 때만 `#번호` 로 보인다. "없음" 은 모두 끈다.
    void TestTheLayerMaskFieldTogglesNamedBits()
    {
        Stage stage;
        const char* names[32] = {};
        names[0] = "Default";
        names[1] = "Player";
        names[2] = "Enemy";
        JBro::UInt32 mask = (1u << 0) | (1u << 5);
        JBro::Int32 changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            if (JBro::Widget::LayerMaskField("##layers", JBro::ArrayView<const char* const>(names, 32), mask))
            {
                ++changedFrames;
            }
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            stage.End();
        };
        ImGuiIO& io = ImGui::GetIO();
        stage.Settle();
        frame();
        io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f, (triggerMin.y + triggerMax.y) * 0.5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        ImGuiWindow* popup = FindComboPopup();
        Check(popup != nullptr, "clicking the layer field opens its list");
        const auto idOf = [&](JBro::Int32 bit, const char* label) {
            return ImHashStr(label, 0, ImHashData(&bit, sizeof(bit), popup->ID));
        };
        const auto hover = [&](ImGuiID target) {
            for (JBro::Float at = popup->Pos.y; at < popup->Pos.y + popup->Size.y; at += 1.0f)
            {
                io.AddMousePosEvent(popup->Pos.x + 12.0f, at);
                frame();
                if (ImGui::GetHoveredID() == target)
                {
                    return true;
                }
            }
            return false;
        };
        Check(false == hover(idOf(3, "#3")), "an unnamed layer that is off is not listed");
        Check(hover(idOf(5, "#5")), "an unnamed layer that is on is listed by its number");
        Check(hover(idOf(2, "Enemy")), "a named layer is listed by its name");
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        Check(mask == ((1u << 0) | (1u << 2) | (1u << 5)) && changedFrames == 1, "clicking Enemy turns its bit on once");
        Check(FindComboPopup() != nullptr, "and the list stays open for the next one");
        const char* nothing = JBro::Loc::TextOr(JBro::LocKeys::InspectorLayersNothing, "Nothing");
        Check(hover(ImHashStr(nothing, 0, popup->ID)), "the list offers Nothing");
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        Check(mask == 0u && changedFrames == 2, "and Nothing turns every bit off");
    }

    // 그리기 목록 전부에서 이 색의 정점이 있는가. 외곽선은 `DragDropTarget`, 칠하기는 `DragDropTargetBg` 로 그려진다.
    JBro::Bool AnyVertexHasColour(ImU32 colour)
    {
        const ImDrawData* data = ImGui::GetDrawData();
        if (data == nullptr)
        {
            return false;
        }
        for (const ImDrawList* list : data->CmdLists)
        {
            for (const ImDrawVert& vertex : list->VtxBuffer)
            {
                if (vertex.col == colour)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // **받는 자리는 외곽선 없이 칠해지고, 제 종류만 받는다**(D-255).
    //
    // ImGui 의 기본 표시는 받는 자리에 테두리를 두른다. 공용 층은 받는 쪽(`AcceptDrop`)과 끄는 쪽(`BeginDragSource`)
    // 양쪽에서 그것을 끈다 - 끄는 쪽에서 끄는 것은 공용 층을 거치지 않은 받는 자리까지 막기 위해서다. 그래서 받는 자리를
    // 셋 둔다: 공용 층의 것, ImGui 를 직접 부른 것, 다른 종류만 받는 것. 끄는 쪽도 둘이다: 공용 층의 것과 ImGui 를 직접 부른 것 -
    // 끄는 쪽의 깃발이 받는 쪽의 것을 가리므로, 직접 부른 끄는 쪽이 있어야 받는 쪽의 깃발을 따로 잰다.
    void TestADropTargetIsFilledNotOutlinedAndTakesOnlyItsKind()
    {
        Stage stage;
        ImGuiStyle& style = ImGui::GetStyle();
        // 기본 칠하기 색은 투명이라 그려지지 않는다. 둘 다 다른 것과 겹치지 않는 불투명한 색을 준다.
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_DragDropTargetBg] = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
        const ImU32 outline = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        const ImU32 fill = ImGui::GetColorU32(ImGuiCol_DragDropTargetBg);

        ImRect source;
        ImRect rawSource;
        ImRect shared;
        ImRect raw;
        ImRect other;
        JBro::UInt64 received = 0;
        JBro::Int32 deliveries = 0;
        JBro::Bool otherReceived = false;
        const auto frame = [&]() {
            stage.Begin();
            ImGui::Button("source", ImVec2(120.0f, 24.0f));
            source = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (JBro::Widget::BeginDragSource())
            {
                JBro::Widget::SetDragValue(JBro::Widget::DragKind::HierarchyObject, JBro::UInt64(42));
                ImGui::TextUnformatted("dragging");
                JBro::Widget::EndDragSource();
            }
            ImGui::Button("raw source", ImVec2(120.0f, 24.0f));
            rawSource = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (ImGui::BeginDragDropSource())
            {
                const JBro::UInt64 value = 7;
                ImGui::SetDragDropPayload("JBRO_HIERARCHY_MOVE", &value, sizeof(value));
                ImGui::TextUnformatted("dragging");
                ImGui::EndDragDropSource();
            }
            ImGui::Button("shared", ImVec2(120.0f, 24.0f));
            shared = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (JBro::Widget::BeginDropTarget())
            {
                JBro::UInt64 value = 0;
                if (JBro::Widget::AcceptDropValue(JBro::Widget::DragKind::HierarchyObject, value))
                {
                    received = value;
                    ++deliveries;
                }
                JBro::Widget::EndDropTarget();
            }
            ImGui::Button("raw", ImVec2(120.0f, 24.0f));
            raw = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (ImGui::BeginDragDropTarget())
            {
                ImGui::AcceptDragDropPayload("JBRO_HIERARCHY_MOVE");
                ImGui::EndDragDropTarget();
            }
            ImGui::Button("other", ImVec2(120.0f, 24.0f));
            other = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (JBro::Widget::BeginDropTarget())
            {
                JBro::UInt32 layer = 0;
                otherReceived = JBro::Widget::AcceptDropValue(JBro::Widget::DragKind::HierarchyLayer, layer)
                    || otherReceived;
                JBro::Widget::EndDropTarget();
            }
            stage.End();
        };
        ImGuiIO& io = ImGui::GetIO();
        const auto moveTo = [&](const ImRect& rect) {
            const ImVec2 at = rect.GetCenter();
            io.AddMousePosEvent(at.x, at.y);
            frame();
        };
        stage.Settle();
        frame();

        // 끌기로 치려면 문턱만큼 움직여야 한다. 한 번에 넘기지 않고 몇 번에 나눠 간다.
        const auto startDrag = [&](const ImRect& from) {
            moveTo(from);
            io.AddMouseButtonEvent(0, true);
            frame();
            const ImVec2 start = from.GetCenter();
            for (JBro::Int32 step = 1; step <= 5; ++step)
            {
                io.AddMousePosEvent(start.x + step * 4.0f, start.y);
                frame();
            }
        };

        // 먼저 ImGui 를 직접 부른 끄는 쪽이다. 이 꾸러미에는 외곽선을 끄는 깃발이 없다.
        startDrag(rawSource);
        Check(JBro::Widget::IsDragging(JBro::Widget::DragKind::HierarchyObject), "the raw source must have started a drag");
        moveTo(shared);
        moveTo(shared);
        Check(false == AnyVertexHasColour(outline),
            "a shared drop target must not draw an outline even for a source that did not turn it off");
        Check(AnyVertexHasColour(fill), "it is filled instead");
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        Check(deliveries == 1 && received == 7, "the raw source's value is delivered like any other");
        deliveries = 0;
        received = 0;

        startDrag(source);
        Check(JBro::Widget::IsDragging(JBro::Widget::DragKind::HierarchyObject), "the source must have started a drag");
        Check(false == JBro::Widget::IsDragging(JBro::Widget::DragKind::HierarchyLayer),
            "and the drag is of its own kind only");

        // 받는 자리마다 두 프레임을 머문다. 칠하기는 지난 프레임에도 받은 자리에만 된다.
        moveTo(shared);
        moveTo(shared);
        Check(false == AnyVertexHasColour(outline), "a shared drop target must not draw an outline");
        Check(AnyVertexHasColour(fill), "it is filled instead, so it still shows where the drop goes");

        moveTo(raw);
        moveTo(raw);
        Check(false == AnyVertexHasColour(outline),
            "a target that calls ImGui directly must not draw an outline either - the source turns it off");

        moveTo(other);
        moveTo(other);
        Check(false == AnyVertexHasColour(fill), "a target of another kind must not light up");
        Check(false == AnyVertexHasColour(outline), "nor draw an outline");

        moveTo(shared);
        moveTo(shared);
        Check(deliveries == 0, "nothing is received before the button is released");
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        Check(deliveries == 1 && received == 42, "releasing over the target delivers the value once");
        Check(false == otherReceived, "and the target of another kind never received it");
        Check(false == JBro::Widget::IsDraggingAnything(), "the drag is over after the drop");
    }

    // **목록의 행은 제 목록 안에서만 옮겨진다**(D-255). 꾸러미 이름은 전역이라 번호만 실었을 때는 옆 목록의 사이 칸이
    // 그 번호를 제 원소 번호로 받아 엉뚱한 원소를 옮겼다. 인스펙터에는 목록이 나란히, 구조체 원소 안에 겹쳐 선다.
    void TestAListRowDroppedOnAnotherListMovesNothing()
    {
        Stage stage;
        JBro::Int32 movesA = 0;
        JBro::Int32 movesB = 0;
        const auto frame = [&]() {
            stage.Begin();
            JBro::Widget::ListVirtual("##a", 3,
                [&](JBro::Int32) -> JBro::Bool { ImGui::TextUnformatted("row"); return false; },
                [&]() {}, [&](JBro::Int32) {}, [&](JBro::Int32, JBro::Int32) { ++movesA; });
            JBro::Widget::ListVirtual("##b", 3,
                [&](JBro::Int32) -> JBro::Bool { ImGui::TextUnformatted("row"); return false; },
                [&]() {}, [&](JBro::Int32) {}, [&](JBro::Int32, JBro::Int32) { ++movesB; });
            stage.End();
        };
        frame();
        frame();
        ImGuiWindow* bodies[2] = {};
        JBro::Int32 found = 0;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "##list_body") != nullptr && found < 2)
            {
                bodies[found++] = window;
            }
        }
        Check(found == 2 && bodies[0]->Pos.y < bodies[1]->Pos.y, "both lists must open their bodies, one above the other");
        ImGuiIO& io = ImGui::GetIO();
        const auto moveMouse = [&](JBro::Float x, JBro::Float y) {
            io.AddMousePosEvent(x, y);
            frame();
        };
        const auto findY = [&](ImGuiWindow* body, ImGuiID target, JBro::Float x, JBro::Float& y) {
            for (JBro::Float at = body->Pos.y; at < body->Pos.y + body->Size.y; at += 1.0f)
            {
                moveMouse(x, at);
                if (ImGui::GetHoveredID() == target)
                {
                    y = at;
                    return true;
                }
            }
            return false;
        };
        const auto rowId = [](ImGuiWindow* body, JBro::Int32 index, const char* label) {
            return ImHashStr(label, 0, ImHashData(&index, sizeof(index), body->ID));
        };
        const auto dragTo = [&](JBro::Float fromX, JBro::Float fromY, JBro::Float toX, JBro::Float toY) {
            moveMouse(fromX, fromY);
            io.AddMouseButtonEvent(0, true);
            frame();
            constexpr JBro::Int32 Steps = 10;
            for (JBro::Int32 step = 1; step <= Steps; ++step)
            {
                moveMouse(fromX + (toX - fromX) * step / Steps, fromY + (toY - fromY) * step / Steps);
            }
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        };
        const JBro::Float handleX = bodies[0]->Pos.x + 5.0f;
        const JBro::Float middleX = bodies[0]->Pos.x + bodies[0]->Size.x * 0.5f;
        JBro::Float rowY = 0.0f;
        JBro::Float slotY = 0.0f;
        Check(findY(bodies[0], rowId(bodies[0], 1, "##row_body"), handleX, rowY), "list A's second row must have a handle");
        // 끌지 않는 동안 B 의 사이 칸은 받는 자리가 아니라 그냥 빈 단추다. 자리는 그대로 찾을 수 있다.
        Check(findY(bodies[1], rowId(bodies[1], 0, "##slot"), middleX, slotY), "list B must have a slot above its first row");

        dragTo(handleX, rowY, middleX, slotY);
        Check(movesB == 0, "a row from list A dropped on list B must not move anything in B");
        Check(movesA == 0, "nor in A");

        // 같은 목록 안에서는 여전히 옮겨진다 - 막은 것이 옮기기 전부가 아니다.
        Check(findY(bodies[0], rowId(bodies[0], 1, "##row_body"), handleX, rowY), "list A's second row is still there");
        Check(findY(bodies[0], rowId(bodies[0], 0, "##slot"), middleX, slotY), "and so is its first slot");
        dragTo(handleX, rowY, middleX, slotY);
        Check(movesA == 1 && movesB == 0, "a row dropped inside its own list still moves");
    }

    // **오브젝트 칸은 이름으로 고른다(D-233).** 검색해 Enter 로 고르면 그 번호가 되고, 끌어 놓은 것이 없으면 dropped 는 0 이다.
    void TestTheObjectFieldPicksByName()
    {
        Stage stage;
        const char* const names[] = { "None", "Anchor", "Weight" };
        JBro::Int32 chosen = 0;
        JBro::UInt64 dropped = 7;
        JBro::Int32 changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            if (JBro::Widget::ObjectField("##object", names, chosen, JBro::Widget::DragKind::HierarchyObject, dropped))
            {
                ++changedFrames;
            }
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            stage.End();
        };
        ImGuiIO& io = ImGui::GetIO();
        stage.Settle();
        frame();
        Check(dropped == 0, "with nothing dropped the drop slot reads zero");
        io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f, (triggerMin.y + triggerMax.y) * 0.5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        frame();
        Check(FindComboPopup() != nullptr, "clicking the object field opens its list");
        for (const char* at = "Wei"; *at != '\0'; ++at)
        {
            io.AddInputCharacter(static_cast<unsigned int>(*at));
            frame();
        }
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        frame();
        Check(chosen == 2 && changedFrames == 1 && dropped == 0, "typing part of a name and Enter picks that object");
    }

    // **에셋 칸은 이름으로 고르고 아이디를 쓴다**(D-116). 이름을 쳐 Enter 로 고르면 짝 아이디가
    // 들어가고, 비우기 항목을 고르면 빈 아이디다. 목록에 없는 아이디는 바뀌지 않은 채 남는다.
    void TestTheAssetFieldWritesTheIdOfTheChosenName()
    {
        Stage stage;
        const char* const names[] = { "art/hero.png", "art/tiles.png" };
        const JBro::AssetId ids[] = { JBro::Uuid::FromName("hero"), JBro::Uuid::FromName("tiles") };
        JBro::AssetId value;
        JBro::Int32 changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        JBro::Bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            if (JBro::Widget::AssetField("##asset", names, ids, value)
                .NoneText("(none)").MissingText("(missing)").Width(220.0f).Draw())
            {
                ++changedFrames;
            }
            if (false == triggerKnown)
            {
                triggerMin = ImGui::GetItemRectMin();
                triggerMax = ImGui::GetItemRectMax();
                triggerKnown = true;
            }
            stage.End();
        };
        const auto open = [&]() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent((triggerMin.x + triggerMax.x) * 0.5f,
                (triggerMin.y + triggerMax.y) * 0.5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
            frame();
            Check(FindComboPopup() != nullptr, "clicking the asset field must open its popup");
        };
        const auto type = [&](const char* text) {
            for (const char* at = text; *at != '\0'; ++at)
            {
                ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(*at));
                frame();
            }
        };
        const auto pressEnter = [&]() {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            frame();
        };

        stage.Settle();
        frame();
        frame();
        Check(value.IsNull() && changedFrames == 0, "untouched, the id stays empty");

        open();
        type("tiles");
        pressEnter();
        Check(value == ids[1], "typing the name and Enter must write that asset's id");
        Check(changedFrames == 1, "and report one change");

        // 비우기 항목은 맨 위다. 빈 검색에 Enter 면 그것이 골라진다.
        open();
        pressEnter();
        Check(value.IsNull(), "the clear row is first, so Enter on an empty filter empties the id");
        Check(changedFrames == 2, "that is the second change");

        // 목록에 없는 아이디는 그대로 둔다 - 사용자가 고르기 전에는 위젯이 값을 고치지 않는다.
        value = JBro::Uuid::FromName("gone");
        frame();
        frame();
        Check(value == JBro::Uuid::FromName("gone") && changedFrames == 2,
            "an id that is not in the list is shown as missing, not overwritten");
    }

    // **한 줄이 한 이름인 목록**이다(D-189). 아무도 만지지 않으면 값이 그대로여야 하고,
    // 버퍼와 목록 사이를 오갈 때 빈 줄과 `\r` 이 이름으로 둔갑하면 안 된다 -
    // 그 글자가 섞인 패턴은 어떤 파일 이름과도 맞지 않는다.
    void TestTheNameListEditKeepsTheBufferAndTheListInStep()
    {
        Stage stage;
        stage.Begin();

        JBro::String buffer = "*.psd\n*.tmp\n";
        const JBro::Bool changed = JBro::Widget::NameListEdit("##names", buffer);
        Check(false == changed, "nobody typed, so nothing changed");
        Check(buffer == JBro::String("*.psd\n*.tmp\n"), "and the buffer is untouched");

        stage.End();

        JBro::Array<JBro::String> items;
        JBro::Widget::SplitLines(JBro::String("*.psd\r\n\r\n  *.tmp\r\n"), items);
        Check(items.Size() == 2, "a blank line is not a name");
        Check(items[0] == JBro::String("*.psd"), "and the carriage return is not part of one");
        Check(items[1] == JBro::String("  *.tmp"), "leading spaces belong to the name, though");

        // 줄바꿈으로 끝나지 않는 마지막 줄도 이름이다. 사람이 엔터를 치기 전에
        // 저장을 누르면 그 줄이 사라지면 안 된다.
        JBro::Array<JBro::String> tail;
        JBro::Widget::SplitLines(JBro::String("a\nb"), tail);
        Check(tail.Size() == 2 && tail[1] == JBro::String("b"),
            "a last line without a newline is still a name");

        JBro::String joined;
        JBro::Widget::JoinLines(items, joined);
        Check(joined == JBro::String("*.psd\n  *.tmp\n"), "joining puts one name on one line");

        JBro::Array<JBro::String> again;
        JBro::Widget::SplitLines(joined, again);
        Check(again.Size() == items.Size(), "and the round trip keeps the count");

        JBro::Array<JBro::String> nothing;
        nothing.Add(JBro::String("x"));
        JBro::Widget::SplitLines(JBro::String(""), nothing);
        Check(nothing.IsEmpty(), "an empty buffer means no names at all");
    }

    // 이 단추가 방금 칠한 색들을 모은다. 정점 색을 그대로 읽는다 - 테마를 밀고 당기는
    // 것은 그리는 순간에만 서 있어서, 부른 뒤에 스타일을 물어보면 이미 원래대로다.
    void CollectButtonColors(const ImDrawList* list, JBro::Int32 fromVertex,
        JBro::Array<unsigned int>& out)
    {
        out.Clear();
        for (JBro::Int32 at = fromVertex; at < list->VtxBuffer.Size; ++at)
        {
            const unsigned int color = list->VtxBuffer[at].col;
            JBro::Bool seen = false;
            for (std::size_t index = 0; index < out.Size(); ++index)
            {
                seen = seen || out[index] == color;
            }
            if (false == seen)
            {
                out.Add(color);
            }
        }
    }

    JBro::Bool HasColor(const JBro::Array<unsigned int>& colors, unsigned int color)
    {
        for (std::size_t index = 0; index < colors.Size(); ++index)
        {
            if (colors[index] == color)
            {
                return true;
            }
        }
        return false;
    }

    // **되돌릴 수 없는 단추는 다르게 생겨야 한다**(D-190, 기존 `ImActionButton`).
    // 지우기가 그만두기와 똑같이 생기면 손이 먼저 움직인다. 무게가 `Info` 인 것은
    // 테마의 단추 그대로여야 한다 - 모든 단추가 물들면 무게가 뜻을 잃는다.
    void TestAWeightedButtonLooksDifferentFromAPlainOne()
    {
        Stage stage;
        stage.Begin();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const unsigned int themeButton = ImGui::GetColorU32(ImGuiCol_Button);

        JBro::Int32 mark = list->VtxBuffer.Size;
        JBro::Widget::ActionButton("plain", JBro::Widget::Severity::Info);
        JBro::Array<unsigned int> plain;
        CollectButtonColors(list, mark, plain);

        mark = list->VtxBuffer.Size;
        JBro::Widget::ActionButton("danger", JBro::Widget::Severity::Error);
        JBro::Array<unsigned int> danger;
        CollectButtonColors(list, mark, danger);

        mark = list->VtxBuffer.Size;
        JBro::Widget::ActionButton("go", JBro::Widget::Severity::Success);
        JBro::Array<unsigned int> success;
        CollectButtonColors(list, mark, success);

        Check(HasColor(plain, themeButton), "a plain button keeps the theme's own colour");
        Check(false == HasColor(danger, themeButton),
            "an irreversible one must not look like the theme's button");
        Check(false == HasColor(success, themeButton), "nor must the confirming one");
        Check(false == HasColor(danger, success[0]) || danger.Size() != success.Size(),
            "and the two weights must not be the same colour either");

        stage.End();
        Check(stage.IdStackDepth() == 1, "and the id stack comes back");

        // **잠긴 단추는 눌러도 눌리지 않는다.** 같은 자리를 같은 손짓으로 두 번 누른다 -
        // 한 번은 잠근 채로, 한 번은 풀고서. 잠갔을 때만 답이 없어야 한다.
        JBro::Bool locked = true;
        JBro::Bool pressed = false;
        ImVec2 where(0.0f, 0.0f);
        const auto frame = [&]() {
            stage.Begin();
            where = ImGui::GetCursorScreenPos();
            pressed = JBro::Widget::ActionButton("locked", JBro::Widget::Severity::Error,
                false == locked, "this is why");
            stage.End();
        };
        frame();
        const auto clickAt = [&](ImVec2 at) {
            ImGui::GetIO().AddMousePosEvent(at.x + 8.0f, at.y + 6.0f);
            frame();
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
            return pressed;
        };
        Check(false == clickAt(where), "a locked button does not answer a press");
        locked = false;
        Check(clickAt(where), "and the same press on the same spot lands once it is open");
    }

    // 무게마다 색이 달라야 한다. 같으면 경고와 오류를 눈으로 가릴 수 없다.
    void TestSeverityColoursDiffer()
    {
        const ImVec4 info = JBro::Widget::SeverityColor(JBro::Widget::Severity::Info);
        const ImVec4 warning = JBro::Widget::SeverityColor(JBro::Widget::Severity::Warning);
        const ImVec4 error = JBro::Widget::SeverityColor(JBro::Widget::Severity::Error);
        const ImVec4 success = JBro::Widget::SeverityColor(JBro::Widget::Severity::Success);

        auto same = [](const ImVec4& left, const ImVec4& right) {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        };
        Check(false == same(info, warning), "info and warning must look different");
        Check(false == same(warning, error), "warning and error too");
        Check(false == same(error, success), "and error and success");

        Check(JBro::Widget::IsEmptyText(nullptr), "nothing is empty text");
        Check(JBro::Widget::IsEmptyText(""), "and so is the empty string");
        Check(false == JBro::Widget::IsEmptyText("x"), "but a letter is not");

        const ImVec4 faded = JBro::Widget::WithAlpha(error, 0.25f);
        Check(faded.w > 0.24f && faded.w < 0.26f, "alpha must be replaced");
        Check(faded.x == error.x, "and the colour left alone");
    }

    // **숫자 칸의 ▲▼ 는 칸 안 오른쪽 끝에 위아래로 쌓인다.** 칸 옆에 단추가 붙지 않으므로 칸이 준 폭을
    // 다 쓰고, 위를 누르면 한 칸 오르고 아래를 누르면 내려가며 범위에서 멈춘다. 그린 뒤 마지막 항목은 칸이다.
    void TestTheNumberFieldSpinsWithStackedArrowsInsideIt()
    {
        Stage stage;
        JBro::EditorTheme::ApplyLayout();
        ImGuiIO& io = ImGui::GetIO();
        JBro::Int32 value = 9;
        JBro::Int32 changedFrames = 0;
        ImVec2 fieldMin;
        ImVec2 fieldMax;
        ImGuiID lastItem = 0;
        const auto frame = [&]() {
            stage.Begin();
            if (JBro::Widget::DragField("##count", value).Range(0, 10).Width(160.0f)())
            {
                ++changedFrames;
            }
            fieldMin = ImGui::GetItemRectMin();
            fieldMax = ImGui::GetItemRectMax();
            lastItem = ImGui::GetItemID();
            ImGui::PushID("##count");
            const ImGuiID drag = ImGui::GetID("##drag");
            ImGui::PopID();
            Check(lastItem == drag, "the last item after the arrows must still be the field");
            stage.End();
        };
        stage.Settle();
        frame();
        Check(fieldMax.x - fieldMin.x > 159.0f && fieldMax.x - fieldMin.x < 161.0f,
            "the arrows sit inside the field, so the field keeps the whole width");

        const auto click = [&](JBro::Float x, JBro::Float y) {
            io.AddMousePosEvent(x, y);
            frame();
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const JBro::Float arrowX = fieldMax.x - 3.0f;
        const JBro::Float upY = fieldMin.y + 3.0f;
        const JBro::Float downY = fieldMax.y - 3.0f;

        click(arrowX, upY);
        Check(value == 10 && changedFrames == 1, "the upper arrow must add one step");
        click(arrowX, upY);
        Check(value == 10, "and stop at the top of the range");
        click(arrowX, downY);
        Check(value == 9, "the lower arrow must take one step away");

        // 칸 한가운데를 눌렀다 떼는 것은 끌기의 일이다. 움직이지 않았으니 값은 그대로다.
        const JBro::Int32 before = changedFrames;
        click((fieldMin.x + fieldMax.x) * 0.5f, (fieldMin.y + fieldMax.y) * 0.5f);
        Check(value == 9 && changedFrames == before, "a click on the middle of the field is not an arrow");
    }

    // **실수·부호 없는 정수·64 비트 칸도 같은 ▲▼ 다**(D-290). 실수는 `Step` 만큼, 부호 없는 값은 0 아래로 감기지 않고,
    // 64 비트 정수는 32 비트를 넘는 값도 그대로 한 칸씩 움직인다. 슬라이더도 엔진 타입을 바로 잡는다.
    void TestEveryNumberTypeSpinsTheSameWay()
    {
        Stage stage;
        JBro::EditorTheme::ApplyLayout();
        ImGuiIO& io = ImGui::GetIO();
        JBro::Float speed = 1.0f;
        JBro::UInt32 count = 0;
        JBro::Int64 big = 5000000000ll;
        JBro::UInt32 sliderValue = 3;
        ImVec2 rects[3][2];
        const auto frame = [&]() {
            stage.Begin();
            JBro::Widget::DragField("##speed", speed).Step(0.25f).Width(160.0f)();
            rects[0][0] = ImGui::GetItemRectMin();
            rects[0][1] = ImGui::GetItemRectMax();
            JBro::Widget::DragField("##count", count).Width(160.0f)();
            rects[1][0] = ImGui::GetItemRectMin();
            rects[1][1] = ImGui::GetItemRectMax();
            JBro::Widget::DragField("##big", big).Width(160.0f)();
            rects[2][0] = ImGui::GetItemRectMin();
            rects[2][1] = ImGui::GetItemRectMax();
            JBro::Widget::SliderField("##slider", sliderValue, 0u, 10u).Width(160.0f)();
            stage.End();
        };
        stage.Settle();
        frame();
        const auto click = [&](JBro::Float x, JBro::Float y) {
            io.AddMousePosEvent(x, y);
            frame();
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const auto up = [&](int which) { click(rects[which][1].x - 3.0f, rects[which][0].y + 3.0f); };
        const auto down = [&](int which) { click(rects[which][1].x - 3.0f, rects[which][1].y - 3.0f); };

        up(0);
        Check(speed > 1.24f && speed < 1.26f, "a float field steps by its Step");
        down(1);
        Check(count == 0u, "an unsigned field does not wrap below zero");
        up(1);
        Check(count == 1u, "and steps up by one");
        up(2);
        Check(big == 5000000001ll, "a 64-bit field keeps values past 32 bits");
        Check(sliderValue == 3u, "an untouched slider keeps its engine value");
    }

    // **툴팁은 마우스가 멈춘 뒤 잠시 있다가 뜬다.** 지나가기만 해도 뜨면 패널 위를 움직일 때마다
    // 말풍선이 따라다닌다. 회색 항목의 까닭도 같은 길을 지난다.
    void TestATooltipWaitsForTheMouseToRest()
    {
        Stage stage;
        JBro::EditorTheme::ApplyLayout();
        ImGuiIO& io = ImGui::GetIO();

        ImVec2 plainMin;
        ImVec2 plainMax;
        ImVec2 grayMin;
        ImVec2 grayMax;
        auto frame = [&]() {
            stage.Begin();
            ImGui::Button("plain", ImVec2(120.0f, 24.0f));
            plainMin = ImGui::GetItemRectMin();
            plainMax = ImGui::GetItemRectMax();
            JBro::Widget::HoveredTooltip("plain tip");
            ImGui::BeginDisabled();
            ImGui::Button("gray", ImVec2(120.0f, 24.0f));
            ImGui::EndDisabled();
            grayMin = ImGui::GetItemRectMin();
            grayMax = ImGui::GetItemRectMax();
            JBro::Widget::DisabledReason(true, "gray reason");
            stage.End();
            ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
            return tooltip != nullptr && tooltip->Active;
        };
        stage.Settle();
        frame();

        auto restOn = [&](const ImVec2& min, const ImVec2& max, const char* early, const char* late) {
            // 툴팁이 막 떴던 자리에서 곧장 옆 항목으로 가면 ImGui 는 기다림을 이어 쓴다(도구 막대를 훑을 때
            // 바로바로 뜨게). 그 몫이 지워지도록 1 초 비켜 있는다.
            io.AddMousePosEvent(5.0f, 590.0f);
            for (JBro::Int32 step = 0; step < 60; ++step)
            {
                frame();
            }
            io.AddMousePosEvent((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            // 0.3 초까지는 뜨지 않는다.
            for (JBro::Int32 step = 0; step < 18; ++step)
            {
                Check(false == frame(), early);
            }
            JBro::Bool shown = false;
            for (JBro::Int32 step = 0; step < 30 && false == shown; ++step)
            {
                shown = frame();
            }
            Check(shown, late);
        };
        restOn(plainMin, plainMax, "a tooltip must not appear the moment the mouse arrives",
            "a tooltip must appear once the mouse has rested on the item");
        restOn(grayMin, grayMax, "the reason a gray item is locked must wait like any other tooltip",
            "and appear once the mouse has rested on the gray item");
    }
}


namespace
{
    // **가이드는 목록 끝에 반쯤 걸친 칸도 다 보이게 굴린다**(D-291). 높이 100 의 목록 칸 아래 경계에 걸친 단추를 가이드가 가리키면, 몇 프레임 뒤
    // 그 단추가 칸 안에 다 들어와 있어야 한다. 전에는 조금이라도 보이면 굴리지 않아 구멍이 칸 밖에 걸렸다(컴포넌트 목록이 두 칸 길어지자 가이드 시험이 실패했다).
    void TestTheGuideScrollsAHalfHiddenTargetIntoView()
    {
        Stage stage;
        const JBro::GuideFocusTarget target = JBro::GuideFocusTargets::Action("probe.half_hidden");
        JBro::EditorGuideFocus focus;
        JBro::GuideFocusPath path;
        Check(path.Push(target), "the path takes the button");
        Check(focus.Begin(path), "the guide begins on the button");
        JBro::Widget::SetGuideFocus(&focus);
        ImVec2 itemMin;
        ImVec2 itemMax;
        ImRect listClip;
        const auto frame = [&]() {
            stage.Begin();
            ImGui::BeginChild("##list", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
            ImGui::Dummy(ImVec2(10.0f, 80.0f));
            JBro::Widget::SetNextItemTarget(target);
            JBro::Widget::Button("half hidden");
            itemMin = ImGui::GetItemRectMin();
            itemMax = ImGui::GetItemRectMax();
            ImGui::Dummy(ImVec2(10.0f, 300.0f));
            listClip = ImGui::GetCurrentWindow()->InnerClipRect;
            ImGui::EndChild();
            stage.End();
        };
        frame();
        Check(itemMin.y < listClip.Max.y && itemMax.y > listClip.Max.y, "the button starts half hidden at the bottom of the list");
        for (JBro::Int32 at = 0; at < 3; ++at)
        {
            frame();
        }
        JBro::Widget::SetGuideFocus(nullptr);
        Check(itemMin.y >= listClip.Min.y && itemMax.y <= listClip.Max.y, "the guide scrolls it wholly into the list");
    }
}

JBro::Int32 RunEditorWidgetTests()
{
    TestTheGuideScrollsAHalfHiddenTargetIntoView();
    TestScopesUnwindThemselves();
    TestTheFormLayoutOpensAndClosesCleanly();
    TestTheListAsksItsCallbacksForEverything();
    TestAListRowIsAsTallAsWhatItDraws();
    TestTheArrayWrapperMovesElementsCorrectly();
    TestARowsBackgroundCoversEverythingItDraws();
    TestDroppingARowMovesItOnceAndDroppingBelowItselfChangesNothing();
    TestAListRowDroppedOnAnotherListMovesNothing();
    TestADropTargetIsFilledNotOutlinedAndTakesOnlyItsKind();
    TestTheTreeHandsBackItsRowAndContent();
    TestTheTextFieldLeavesUntouchedValuesAlone();
    TestTheFilterComboPicksByTypingAndEnter();
    TestTheFilterComboGroupsItemsAndSkipsTheDisabled();
    TestAnEmptyFilterComboDrawsAndChangesNothing();
    TestTheLayerMaskFieldTogglesNamedBits();
    TestTheObjectFieldPicksByName();
    TestTheEnumComboChangesTheValueWhenAnItemIsClicked();
    TestTheAssetFieldWritesTheIdOfTheChosenName();
    TestTheNameListEditKeepsTheBufferAndTheListInStep();
    TestAWeightedButtonLooksDifferentFromAPlainOne();
    TestSeverityColoursDiffer();
    TestATooltipWaitsForTheMouseToRest();
    TestTheNumberFieldSpinsWithStackedArrowsInsideIt();
    TestEveryNumberTypeSpinsTheSameWay();
    std::cout << "Editor widget tests passed.\n";
    return 0;
}
