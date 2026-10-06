#include <JBro/Core/Log.h>
#include <JBro/Editor/ComponentMenuTable.h>

#include <imgui.h>

#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 컴포넌트별 우클릭 항목의 표(D-220, todo "에디터 공용 기반" 8 번).
//
// 표의 판단(받기·거절·쌓는 순서·떼기·그리는 중 막기)을 잰다. 그리기는 렌더러 없는 ImGui 창 하나 안에서 한다 -
// 구분선이 들어갔는지는 훅이 불릴 때의 커서 높이로 잰다(구분선만 높이를 차지하게 훅은 아무것도 그리지 않는다).

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

    class QuietLog
    {
    public:
        QuietLog()
            : m_previous(JBro::Log::GetEchoToConsole())
        {
            JBro::Log::SetEchoToConsole(false);
        }
        ~QuietLog()
        {
            JBro::Log::SetEchoToConsole(m_previous);
        }
        QuietLog(const QuietLog&) = delete;
        QuietLog& operator=(const QuietLog&) = delete;

    private:
        JBro::Bool m_previous = true;
    };

    using JBro::ComponentMenuContext;
    using JBro::ComponentMenuTable;
    using JBro::ComponentTypeId;

    constexpr ComponentTypeId TypeA = 101;
    constexpr ComponentTypeId TypeB = 202;

    // 훅이 불린 기록이다. 훅은 함수 포인터라 상태를 `user` 로 받는다.
    struct Call
    {
        JBro::Int32 tag = 0;
        JBro::UInt32 ordinal = 0;
        JBro::Float cursorY = 0.0f;
    };

    struct Recorder
    {
        Call calls[16];
        JBro::Int32 count = 0;
        // 참이면 훅이 거짓을 돌려준다(대상을 지운 것처럼).
        JBro::Bool failTag2 = false;
        // 훅 안에서 표를 바꾸려 해 본다.
        ComponentMenuTable* table = nullptr;
        JBro::Bool registerResult = true;
        JBro::UInt32 unregisterResult = 99;
        JBro::Bool drawingSeen = false;
    };

    struct Slot
    {
        Recorder* recorder = nullptr;
        JBro::Int32 tag = 0;
    };

    void Record(const ComponentMenuContext& context)
    {
        Slot* slot = static_cast<Slot*>(context.user);
        Recorder& recorder = *slot->recorder;
        if (recorder.count < 16)
        {
            Call& call = recorder.calls[recorder.count];
            call.tag = slot->tag;
            call.ordinal = context.address.ordinal;
            call.cursorY = ImGui::GetCurrentContext() != nullptr ? ImGui::GetCursorPosY() : 0.0f;
        }
        ++recorder.count;
    }

    JBro::Bool DrawOk(const ComponentMenuContext& context)
    {
        Record(context);
        Slot* slot = static_cast<Slot*>(context.user);
        return false == (slot->recorder->failTag2 && slot->tag == 2);
    }

    // 함수가 다르면 같은 등록자가 같은 타입에 여럿 걸 수 있다.
    JBro::Bool DrawOther(const ComponentMenuContext& context)
    {
        Record(context);
        return true;
    }

    JBro::Bool DrawMeddling(const ComponentMenuContext& context)
    {
        Record(context);
        Recorder& recorder = *static_cast<Slot*>(context.user)->recorder;
        recorder.drawingSeen = recorder.table->IsDrawing();
        recorder.registerResult = recorder.table->Register(TypeB, &DrawOther, &recorder);
        recorder.unregisterResult = recorder.table->Unregister(&recorder);
        return true;
    }

    ComponentMenuContext ContextFor(ComponentTypeId typeId, JBro::UInt32 ordinal = 0)
    {
        ComponentMenuContext context;
        context.address.typeId = typeId;
        context.address.ordinal = ordinal;
        return context;
    }

    // 렌더러 없는 ImGui 창 하나 안에서 그린다.
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

        JBro::Bool Draw(ComponentMenuTable& table, const ComponentMenuContext& context, JBro::Bool separatorFirst = false)
        {
            ImGui::NewFrame();
            ImGui::Begin("Menu");
            cursorBefore = ImGui::GetCursorPosY();
            const JBro::Bool result = table.DrawItems(context, separatorFirst);
            cursorAfter = ImGui::GetCursorPosY();
            ImGui::End();
            ImGui::Render();
            return result;
        }

        JBro::Float cursorBefore = 0.0f;
        JBro::Float cursorAfter = 0.0f;

    private:
        ImGuiContext* m_context = nullptr;
    };

    void TestBadEntriesAreRefused()
    {
        const QuietLog quiet;
        ComponentMenuTable table;
        JBro::Int32 owner = 0;
        Check(false == table.Register(JBro::InvalidComponentTypeId, &DrawOther, &owner),
            "an entry without a type must be refused");
        Check(false == table.Register(TypeA, nullptr, &owner), "an entry without a function must be refused");
        Check(false == table.Register(TypeA, &DrawOther, nullptr), "an entry without an owner must be refused");
        Check(false == table.Has(TypeA), "refused entries must not be kept");

        Check(table.Register(TypeA, &DrawOther, &owner), "a whole entry must be taken");
        Check(false == table.Register(TypeA, &DrawOther, &owner),
            "the same type, function and owner twice would show one item twice");
        Check(table.Register(TypeA, &DrawOk, &owner), "another function of the same owner is a different item");
        JBro::Int32 other = 0;
        Check(table.Register(TypeA, &DrawOther, &other), "the same function of another owner is a different item");
        Check(table.Count(TypeA) == 3, "three distinct entries must be kept");
    }

    void TestItemsDrawInRegistrationOrderWithTheirOwnUser()
    {
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        JBro::Int32 ownerA = 0;
        JBro::Int32 ownerB = 0;
        Slot slot1{ &recorder, 1 };
        Slot slot2{ &recorder, 2 };
        Slot slot3{ &recorder, 3 };
        Slot slotOtherType{ &recorder, 9 };
        Check(table.Register(TypeA, &DrawOk, &ownerA, &slot1), "first entry");
        Check(table.Register(TypeB, &DrawOk, &ownerA, &slotOtherType), "entry of another type");
        Check(table.Register(TypeA, &DrawOk, &ownerB, &slot2), "second entry");
        Check(table.Register(TypeA, &DrawOther, &ownerB, &slot3), "third entry");

        Check(table.Has(TypeA) && table.Has(TypeB), "both types have entries");
        Check(false == table.Has(303), "a type nobody registered has no entries");
        Check(table.Count(TypeA) == 3 && table.Count(TypeB) == 1, "entries are counted per type");

        Check(stage.Draw(table, ContextFor(TypeA, 1)), "drawing without a failing hook is true");
        Check(recorder.count == 3, "only the entries of that type are drawn");
        Check(recorder.calls[0].tag == 1 && recorder.calls[1].tag == 2 && recorder.calls[2].tag == 3,
            "entries are drawn in the order they were registered");
        Check(recorder.calls[0].ordinal == 1 && recorder.calls[2].ordinal == 1,
            "every hook gets the address it was drawn for");
    }

    void TestASeparatorGoesWhereTheOwnerChanges()
    {
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        JBro::Int32 ownerA = 0;
        JBro::Int32 ownerB = 0;
        Slot slot1{ &recorder, 1 };
        Slot slot2{ &recorder, 2 };
        Slot slot3{ &recorder, 3 };
        Slot slot4{ &recorder, 4 };
        table.Register(TypeA, &DrawOk, &ownerA, &slot1);
        table.Register(TypeA, &DrawOther, &ownerA, &slot2);
        table.Register(TypeA, &DrawOk, &ownerB, &slot3);
        table.Register(TypeA, &DrawOther, &ownerB, &slot4);

        stage.Draw(table, ContextFor(TypeA));
        Check(recorder.count == 4, "all four entries are drawn");
        Check(recorder.calls[1].cursorY == recorder.calls[0].cursorY,
            "no separator between two items of the same owner");
        Check(recorder.calls[2].cursorY > recorder.calls[1].cursorY,
            "a separator where the owner changes");
        Check(recorder.calls[3].cursorY == recorder.calls[2].cursorY,
            "no separator after the change until the owner changes again");
    }

    // 앞 구분선은 항목을 그릴 때만 선다. 항목이 없는 타입에서 그으면 빈 구분선이 앞의 것과 겹친다.
    void TestALeadingSeparatorOnlyComesWithItems()
    {
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        JBro::Int32 owner = 0;
        Slot slot1{ &recorder, 1 };
        table.Register(TypeA, &DrawOk, &owner, &slot1);

        stage.Draw(table, ContextFor(TypeA));
        const JBro::Float plain = recorder.calls[0].cursorY;
        stage.Draw(table, ContextFor(TypeA), true);
        Check(recorder.count == 2 && recorder.calls[1].cursorY > plain,
            "asked for, a separator goes in front of the first item");

        stage.Draw(table, ContextFor(TypeB), true);
        Check(recorder.count == 2, "a type without entries draws nothing");
        Check(stage.cursorAfter == stage.cursorBefore, "not even the leading separator");
    }

    void TestAFailingHookStopsTheRest()
    {
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        recorder.failTag2 = true;
        JBro::Int32 owner = 0;
        JBro::Int32 other = 0;
        Slot slot1{ &recorder, 1 };
        Slot slot2{ &recorder, 2 };
        Slot slot3{ &recorder, 3 };
        table.Register(TypeA, &DrawOther, &owner, &slot1);
        // 둘째가 거짓을 돌려준다.
        table.Register(TypeA, &DrawOk, &owner, &slot2);
        table.Register(TypeA, &DrawOk, &other, &slot3);

        Check(false == stage.Draw(table, ContextFor(TypeA)),
            "a hook that may have removed its target makes the whole draw false");
        Check(recorder.count == 2, "hooks after the failing one must not run on a target that may be gone");
    }

    void TestUnregisterTakesOnlyThatOwnerAndKeepsTheOrder()
    {
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        JBro::Int32 ownerA = 0;
        JBro::Int32 ownerB = 0;
        Slot slot1{ &recorder, 1 };
        Slot slot2{ &recorder, 2 };
        Slot slot3{ &recorder, 3 };
        Slot slot4{ &recorder, 4 };
        // 떼어 낼 등록자를 맨 앞에 둔다. 끝의 것을 그 자리로 옮겨 지우면 남은 순서가 뒤집힌다.
        table.Register(TypeA, &DrawOk, &ownerB, &slot2);
        table.Register(TypeA, &DrawOk, &ownerA, &slot1);
        table.Register(TypeB, &DrawOther, &ownerB, &slot3);
        table.Register(TypeA, &DrawOther, &ownerA, &slot4);

        Check(table.Unregister(&ownerB) == 2, "both entries of that owner are taken, on every type");
        Check(false == table.Has(TypeB), "a type left without entries has no line");
        Check(table.Count(TypeA) == 2, "the other owner's entries stay");
        Check(table.Unregister(&ownerB) == 0, "unregistering twice takes nothing");

        stage.Draw(table, ContextFor(TypeA));
        Check(recorder.count == 2 && recorder.calls[0].tag == 1 && recorder.calls[1].tag == 4,
            "what is left keeps its registration order");
    }

    void TestTheTableCannotChangeWhileDrawing()
    {
        const QuietLog quiet;
        Stage stage;
        ComponentMenuTable table;
        Recorder recorder;
        recorder.table = &table;
        Slot slot1{ &recorder, 1 };
        Check(table.Register(TypeA, &DrawMeddling, &recorder, &slot1), "the meddling entry is taken");
        Check(false == table.IsDrawing(), "not drawing before the draw");

        stage.Draw(table, ContextFor(TypeA));
        Check(recorder.drawingSeen, "the table must know it is drawing while a hook runs");
        Check(false == recorder.registerResult, "registering from inside a hook must be refused");
        Check(recorder.unregisterResult == 0, "unregistering from inside a hook must be refused");
        Check(table.Count(TypeA) == 1 && false == table.Has(TypeB), "the table is as it was");
        Check(false == table.IsDrawing(), "not drawing after the draw");

        Check(table.Register(TypeB, &DrawOther, &recorder), "registering works again after the draw");
        Check(table.Unregister(&recorder) == 2, "unregistering works again after the draw");
    }
}

JBro::Int32 RunComponentMenuTableTests()
{
    TestBadEntriesAreRefused();
    TestItemsDrawInRegistrationOrderWithTheirOwnUser();
    TestASeparatorGoesWhereTheOwnerChanges();
    TestALeadingSeparatorOnlyComesWithItems();
    TestAFailingHookStopsTheRest();
    TestUnregisterTakesOnlyThatOwnerAndKeepsTheOrder();
    TestTheTableCannotChangeWhileDrawing();
    std::cout << "Component menu table tests passed.\n";
    return 0;
}
