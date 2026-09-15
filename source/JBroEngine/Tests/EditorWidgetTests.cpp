#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
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
    TestTheTreeHandsBackItsRowAndContent();
    TestTheTextFieldLeavesUntouchedValuesAlone();
    TestSeverityColoursDiffer();
    std::cout << "Editor widget tests passed.\n";
    return 0;
}
