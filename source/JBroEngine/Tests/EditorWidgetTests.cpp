#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
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

// 공용 위젯 계층이다(ProjectRule §11.1).
//
// **렌더러 없이 잰다.** ImGui 는 컨텍스트와 글꼴 아틀라스만 있으면 프레임을
// 돌 수 있고, 위젯이 스스로 무너지는 곳은 거의 다 그 안에서 드러난다 -
// Id 스택이 어긋났는가, 표를 제대로 닫았는가, 콜백이 몇 번 불렸는가.

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

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
            for (int frame = 0; frame < 2; ++frame)
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

        const int colorsBefore = ImGui::GetCurrentContext()->ColorStack.Size;
        const int varsBefore = ImGui::GetCurrentContext()->StyleVarStack.Size;
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
        int labels = 0;
        int fields = 0;
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

        int drawn = 0;
        int added = 0;
        int removed = -1;
        int movedFrom = -1;
        int movedTo = -1;
        const bool changed = JBro::Widget::ListVirtual("##probe", 3,
            [&](int) -> bool { ++drawn; ImGui::TextUnformatted("row"); return false; },
            [&]() { ++added; },
            [&](int index) { removed = index; },
            [&](int from, int to) { movedFrom = from; movedTo = to; });

        Check(drawn == 3, "every element must be drawn once");
        Check(false == changed, "nothing was touched, so nothing changed");
        Check(added == 0 && removed == -1, "and no callback fired on its own");
        Check(movedFrom == -1 && movedTo == -1, "including the move one");

        // 읽기 전용이면 삭제 단추도 추가 줄도 그리지 않는다. 그려 놓고 눌러도
        // 아무 일이 없으면 고장인지 잠긴 것인지 알 수 없다.
        drawn = 0;
        JBro::Widget::ListVirtual("##readonly", 2,
            [&](int) -> bool { ++drawn; ImGui::TextUnformatted("row"); return false; },
            [&]() { ++added; },
            [&](int) {},
            [&](int, int) {},
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

        float top[3] = {};
        float bottom[3] = {};
        for (int frame = 0; frame < 3; ++frame)
        {
            stage.Begin();
            JBro::Widget::ListVirtual("##rows", 3,
                [&](int index) -> bool {
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
                [](int) {},
                [](int, int) {});
            stage.End();
        }

        const float gapAfterShort = top[1] - bottom[0];
        const float gapAfterTall = top[2] - bottom[1];
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

        JBro::Array<int> items;
        items.Add(10);
        items.Add(20);
        items.Add(30);
        items.Add(40);

        // 목록 위젯을 거치지 않고 덮개가 만드는 옮기기만 따로 확인한다.
        // 위젯은 보정된 **원소 번호**를 넘긴다고 약속한다.
        auto move = [&](int fromIndex, int toIndex) {
            int moved = items[static_cast<std::size_t>(fromIndex)];
            if (fromIndex < toIndex)
            {
                for (int at = fromIndex; at < toIndex; ++at)
                {
                    items[static_cast<std::size_t>(at)] =
                        items[static_cast<std::size_t>(at) + 1];
                }
            }
            else
            {
                for (int at = fromIndex; at > toIndex; --at)
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
        bool tall = true;
        float top[3] = {};
        float bottom[3] = {};
        const auto frame = [&]() {
            stage.Begin();
            JBro::Widget::ListVirtual("##cover", 3,
                [&](int index) -> bool {
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
                [](int) {},
                [](int, int) {});
            stage.End();
        };
        for (int at = 0; at < 3; ++at)
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
        int rowValue = 1;
        const ImGuiID rowBody = ImHashStr("##row_body", 0,
            ImHashData(&rowValue, sizeof(rowValue), body->ID));
        const auto hoveredAt = [&](float y) {
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
        const float tallHeight = bottom[1] - top[1];
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
        int movedFrom = -1;
        int movedTo = -1;
        bool changedInAnyFrame = false;
        const auto frame = [&]() {
            stage.Begin();
            const bool changed = JBro::Widget::ListVirtual("##drag", 3,
                [&](int) -> bool { ImGui::TextUnformatted("row"); return false; },
                [&]() {},
                [&](int) {},
                [&](int from, int to) { movedFrom = from; movedTo = to; });
            changedInAnyFrame = changedInAnyFrame || changed;
            stage.End();
        };
        const auto moveMouse = [&](float x, float y) {
            ImGui::GetIO().AddMousePosEvent(x, y);
            frame();
        };
        const auto press = [&](bool down) {
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
        const auto pushedId = [](ImGuiID seed, int value) {
            return ImHashData(&value, sizeof(value), seed);
        };
        const auto labelId = [](ImGuiID seed, const char* label) {
            return ImHashStr(label, 0, seed);
        };
        // `x` 에서 위아래로 훑어 `target` 이 가리켜지는 y 를 찾는다.
        const auto findY = [&](ImGuiID target, float x, float& y) {
            for (float at = body->Pos.y; at < body->Pos.y + body->Size.y; at += 1.0f)
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
        const auto dragTo = [&](float fromX, float fromY, float toX, float toY) {
            moveMouse(fromX, fromY);
            press(true);
            constexpr int Steps = 10;
            for (int step = 1; step <= Steps; ++step)
            {
                moveMouse(fromX + (toX - fromX) * step / Steps,
                    fromY + (toY - fromY) * step / Steps);
            }
            frame();
            press(false);
            frame();
        };
        const float handleX = body->Pos.x + 5.0f;
        const float middleX = body->Pos.x + body->Size.x * 0.5f;
        float rowY = 0.0f;
        float slotY = 0.0f;

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
        const bool opened = JBro::Widget::TreeBegin("##node",
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
        const bool changed = JBro::Widget::TextField("##probe", text).Draw();
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
        int current = -1;
        int changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        bool triggerKnown = false;
        const auto frame = [&]() {
            stage.Begin();
            const bool changed = JBro::Widget::FilterCombo("##probe", items, current)
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
    }

    // 빈 목록은 열려도 아무것도 고르지 않고 무너지지 않는다.
    void TestAnEmptyFilterComboDrawsAndChangesNothing()
    {
        Stage stage;
        int current = -1;
        const JBro::ArrayView<const char* const> none;
        stage.Begin();
        const bool changed = JBro::Widget::FilterCombo("##empty", none, current)
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
        table.ToIndex = [](const void* value) noexcept -> std::int32_t {
            return *static_cast<const std::int32_t*>(value);
        };
        table.FromIndex = [](void* value, std::int32_t index) noexcept {
            *static_cast<std::int32_t*>(value) = index;
        };
        std::int32_t value = 0;
        int changedFrames = 0;
        ImVec2 triggerMin;
        ImVec2 triggerMax;
        bool triggerKnown = false;
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

        // 둘째 항목("Linear")의 Id 는 팝업 창 → PushID(1) → 이름이다.
        int itemIndex = 1;
        const ImGuiID linear = ImHashStr(names[1], 0,
            ImHashData(&itemIndex, sizeof(itemIndex), popup->ID));
        bool found = false;
        for (float at = popup->Pos.y; at < popup->Pos.y + popup->Size.y; at += 1.0f)
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
}

int RunEditorWidgetTests()
{
    TestScopesUnwindThemselves();
    TestTheFormLayoutOpensAndClosesCleanly();
    TestTheListAsksItsCallbacksForEverything();
    TestAListRowIsAsTallAsWhatItDraws();
    TestTheArrayWrapperMovesElementsCorrectly();
    TestARowsBackgroundCoversEverythingItDraws();
    TestDroppingARowMovesItOnceAndDroppingBelowItselfChangesNothing();
    TestTheTreeHandsBackItsRowAndContent();
    TestTheTextFieldLeavesUntouchedValuesAlone();
    TestTheFilterComboPicksByTypingAndEnter();
    TestAnEmptyFilterComboDrawsAndChangesNothing();
    TestTheEnumComboChangesTheValueWhenAnItemIsClicked();
    TestSeverityColoursDiffer();
    std::cout << "Editor widget tests passed.\n";
    return 0;
}
