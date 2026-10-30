#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/AspectRatioFitter2D.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/ListView2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Tween2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

// 레이아웃 컨테이너(D-352 6 단계): 자식을 칸에 놓는다.
namespace
{
    using namespace JBro;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool At(const Component::Transform2D* place, JBro::Float x, JBro::Float y)
    {
        return place != nullptr && std::fabs(place->position.x - x) < 0.001f && std::fabs(place->position.y - y) < 0.001f;
    }

    // 100 x 40 칸, 간격 4. 세로면 아래로, 가로면 오른쪽으로, 격자는 두 열로 늘어선다. 원점은 블록의 왼쪽 위다.
    void TestChildrenLineUpInCells()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* list = canvas->CreateObject("list");
        canvas->AttachComponent<Component::Transform2D>(list);
        auto* layout = canvas->AttachComponent<Component::Layout2D>(list);
        Object::GameObject* items[3] = {};
        Component::Transform2D* places[3] = {};
        for (JBro::Int32 index = 0; index < 3; ++index)
        {
            items[index] = canvas->CreateObject("item");
            items[index]->SetParent(list);
            places[index] = canvas->AttachComponent<Component::Transform2D>(items[index]);
            places[index]->position = {999.0f, 999.0f};
        }
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        tick();
        Check(At(places[0], 50.0f, -20.0f) && At(places[1], 50.0f, -64.0f) && At(places[2], 50.0f, -108.0f), "a vertical layout stacks the children downward from the top-left");
        Check(layout->contentWidth == 100.0f && layout->contentHeight == 128.0f, "and reports the block size");
        layout->mode = Component::LayoutMode::Horizontal;
        tick();
        Check(At(places[0], 50.0f, -20.0f) && At(places[1], 154.0f, -20.0f) && At(places[2], 258.0f, -20.0f), "a horizontal layout lines them up to the right");
        Check(layout->contentWidth == 308.0f && layout->contentHeight == 40.0f, "with the block as wide as the row");
        layout->mode = Component::LayoutMode::Grid;
        layout->columns = 2;
        tick();
        Check(At(places[0], 50.0f, -20.0f) && At(places[1], 154.0f, -20.0f) && At(places[2], 50.0f, -64.0f), "a grid wraps after the column count");
        Check(layout->contentWidth == 204.0f && layout->contentHeight == 84.0f, "and is two cells wide and two rows tall");
        // 피벗 (0.5, 0.5) 면 블록의 가운데가 원점이다.
        layout->pivot = {0.5f, 0.5f};
        tick();
        Check(At(places[0], -52.0f, 22.0f), "a centered pivot moves the whole block");
        // 여백은 블록 안쪽이다.
        layout->pivot = {0.0f, 1.0f};
        layout->padding = {10.0f, 10.0f};
        tick();
        Check(At(places[0], 60.0f, -30.0f) && layout->contentWidth == 224.0f, "padding pushes the cells in and widens the block");
        // 꺼진 자식은 칸을 차지하지 않는다.
        layout->padding = {};
        items[1]->SetActive(false);
        tick();
        Check(At(places[2], 154.0f, -20.0f), "an inactive child gives its cell to the next one");
        framework.Shutdown();
    }

    // **자식 크기에 맞추기와 내용 크기 맞추기**(D-355). 스프라이트 30 x 10, 버튼 50 x 20, 아무것도 없는 자식은 칸(100 x 40)이다.
    void TestFittedChildrenAndContentSize()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* list = canvas->CreateObject("list");
        canvas->AttachComponent<Component::Transform2D>(list);
        auto* layout = canvas->AttachComponent<Component::Layout2D>(list);
        layout->fitChildren = true;
        Object::GameObject* picture = canvas->CreateObject("picture");
        picture->SetParent(list);
        auto* picturePlace = canvas->AttachComponent<Component::Transform2D>(picture);
        auto* sprite = canvas->AttachComponent<Component::SpriteRenderer2D>(picture);
        sprite->sizeMode = Component::SpriteSizeMode::Custom;
        sprite->size = {30.0f, 10.0f};
        Object::GameObject* press = canvas->CreateObject("press");
        press->SetParent(list);
        auto* pressPlace = canvas->AttachComponent<Component::Transform2D>(press);
        canvas->AttachComponent<Component::Button2D>(press)->size = {50.0f, 20.0f};
        Object::GameObject* blank = canvas->CreateObject("blank");
        blank->SetParent(list);
        auto* blankPlace = canvas->AttachComponent<Component::Transform2D>(blank);
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        tick();
        Check(At(picturePlace, 50.0f, -5.0f) && At(pressPlace, 50.0f, -24.0f) && At(blankPlace, 50.0f, -58.0f),
            "a fitted vertical layout gives each child its own height and centers it in the widest column");
        Check(layout->contentWidth == 100.0f && layout->contentHeight == 78.0f, "and the block is the widest child by the sum of heights and gaps");
        layout->mode = Component::LayoutMode::Horizontal;
        tick();
        Check(At(picturePlace, 15.0f, -20.0f) && At(pressPlace, 59.0f, -20.0f) && At(blankPlace, 138.0f, -20.0f), "a fitted horizontal layout gives each child its own width");
        Check(layout->contentWidth == 188.0f && layout->contentHeight == 40.0f, "with the block as wide as the sum and as tall as the tallest");
        layout->mode = Component::LayoutMode::Grid;
        layout->columns = 2;
        tick();
        Check(At(picturePlace, 50.0f, -20.0f) && At(pressPlace, 154.0f, -20.0f) && At(blankPlace, 50.0f, -64.0f), "a fitted grid uses the largest child as its one cell");
        // 아무것도 안 보이는 자식의 스프라이트(에셋 크기, 풀리지 않음)는 칸 크기다.
        auto* blankSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(blank);
        layout->mode = Component::LayoutMode::Vertical;
        tick();
        Check(At(blankPlace, 50.0f, -58.0f), "an unresolved asset-sized sprite falls back to the cell size");
        blankSprite->size = {std::numeric_limits<float>::quiet_NaN(), 5.0f};
        blankSprite->sizeMode = Component::SpriteSizeMode::Custom;
        tick();
        Check(At(blankPlace, 50.0f, -58.0f), "a NaN custom size falls back to the cell size too");

        // 내용 크기 맞추기: 레이아웃 블록에 여백을 두른 크기가 같은 오브젝트의 스프라이트·마스크·버튼에 간다.
        auto* panel = canvas->AttachComponent<Component::SpriteRenderer2D>(list);
        auto* mask = canvas->AttachComponent<Component::Mask2D>(list);
        auto* fitter = canvas->AttachComponent<Component::ContentSizeFitter2D>(list);
        fitter->padding = {5.0f, 5.0f};
        tick();
        Check(panel->sizeMode == Component::SpriteSizeMode::Custom && panel->size.x == 110.0f && panel->size.y == 88.0f, "the fitter sizes the sprite to the block plus padding");
        Check(mask->size.x == 110.0f && mask->size.y == 88.0f && fitter->contentWidth == 110.0f && fitter->contentHeight == 88.0f, "and the mask, and reports the size");
        fitter->fitWidth = false;
        fitter->minSize = {0.0f, 300.0f};
        panel->size.x = 7.0f;
        tick();
        Check(panel->size.x == 7.0f && panel->size.y == 300.0f, "a disabled axis is left alone and minSize wins");
        fitter->padding = {std::numeric_limits<float>::quiet_NaN(), 0.0f};
        tick();
        Check(panel->size.y == 300.0f, "a NaN padding changes nothing");
        framework.Shutdown();
    }

    // 목록이 항목을 묶을 때 센다(D-357). 핸들 대신 시험이 꽂아 준 컴포넌트 포인터로 읽는다.
    class ListProbe final : public GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Layout2DTests::ListProbe";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        void OnListItem() override
        {
            ++binds;
            lastIndex = list != nullptr ? list->boundIndex : Int32(-99);
            lastItem = list != nullptr ? list->boundItem.GetInstanceId() : InvalidInstanceId;
        }

        // 가변 크기(D-360): 짝수 항목은 `evenSize`, 홀수는 `oddSize`. `leaveAlone` 이면 쓰지 않는다(기본 = itemSize).
        void OnListMeasure() override
        {
            ++measures;
            if (list == nullptr || leaveAlone)
            {
                return;
            }
            list->measuredSize = list->boundIndex % 2 == 0 ? evenSize : oddSize;
        }

        Component::ListView2D* list = nullptr;
        JBro::Int32 binds = 0;
        JBro::Int32 measures = 0;
        JBro::Int32 lastIndex = -1;
        InstanceId lastItem = InvalidInstanceId;
        JBro::Float evenSize = 10.0f;
        JBro::Float oddSize = 30.0f;
        JBro::Bool leaveAlone = false;
    };

    // **가상화 목록**(D-357). 창(마스크 100 x 60)에 20 x 100 의 항목 20 개, 풀 다섯. 내용은 위가 맞춰져(-170) 있다.
    void TestListViewsBindPoolItems()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* window = canvas->CreateObject("window");
        canvas->AttachComponent<Component::Transform2D>(window);
        auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
        mask->size = {100.0f, 60.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        auto* list = canvas->AttachComponent<Component::ListView2D>(window);
        list->count = 20;
        list->itemSize = {100.0f, 20.0f};
        list->spacing = 0.0f;
        auto* probe = canvas->AttachScript<ListProbe>(window);
        probe->list = list;
        Object::GameObject* content = canvas->CreateObject("content");
        content->SetParent(window);
        auto* contentPlace = canvas->AttachComponent<Component::Transform2D>(content);
        contentPlace->position = {0.0f, -170.0f};
        scroll->content = content->GetScriptHandle();
        Object::GameObject* pool[5] = {};
        Component::Transform2D* places[5] = {};
        for (JBro::Int32 index = 0; index < 5; ++index)
        {
            pool[index] = canvas->CreateObject("item");
            pool[index]->SetParent(content);
            places[index] = canvas->AttachComponent<Component::Transform2D>(pool[index]);
        }
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        tick();
        Check(scroll->contentSize.x == 100.0f && scroll->contentSize.y == 400.0f, "the list writes the content size");
        Check(list->firstVisible == 0 && list->visibleCount == 4, "with the top aligned the first four items are in view");
        Check(pool[0]->IsActiveSelf() && pool[3]->IsActiveSelf() && false == pool[4]->IsActiveSelf(), "four pool objects are on and the fifth is off");
        Check(At(places[0], 0.0f, 190.0f) && At(places[3], 0.0f, 130.0f), "items sit from the top of the content downward");
        Check(probe->binds == 4 && probe->lastIndex == 3 && probe->lastItem == pool[3]->GetInstanceId(), "each bound item told the script once");
        tick();
        Check(probe->binds == 4, "nothing rebinds while nothing moves");
        contentPlace->position.y = -130.0f;
        tick();
        Check(list->firstVisible == 1 && list->visibleCount == 5, "scrolling down shows items one to five");
        Check(pool[0]->IsActiveSelf() && At(places[0], 0.0f, 90.0f) && probe->binds == 6 && probe->lastIndex == 4 && probe->lastItem == pool[4]->GetInstanceId(),
            "item five reuses the first pool object, item four fills the fifth, and the script hears about both in pool order");
        contentPlace->position.y = 170.0f;
        tick();
        Check(list->firstVisible == 16 && list->visibleCount == 4 && false == pool[0]->IsActiveSelf() && pool[1]->IsActiveSelf() && pool[4]->IsActiveSelf(),
            "at the bottom the last four show in pool slots one to four (16 % 5 = 1)");
        list->count = 2;
        tick();
        Check(scroll->contentSize.y == 40.0f && list->visibleCount <= 2, "shrinking the count shrinks the content and the view");
        list->count = 0;
        tick();
        Check(list->visibleCount == 0 && list->firstVisible == -1 && false == pool[0]->IsActiveSelf(), "an empty list hides every pool object");
        list->count = 20;
        list->itemSize = {std::numeric_limits<float>::quiet_NaN(), 20.0f};
        tick();
        Check(list->visibleCount == 0, "a NaN item size binds nothing");
        list->itemSize = {100.0f, 20.0f};
        scroll->content = window->GetScriptHandle();
        tick();
        Check(list->visibleCount == 0, "a content that is the window itself is left alone");
        framework.Shutdown();
    }

    // **가변 크기 목록**(D-360). 창(마스크 100 x 60)에 항목 여섯(짝수 10, 홀수 30, 간격 0 → 끝 10·40·50·80·90·120), 풀 넷. 내용은 위가 맞춰져(-30) 있다.
    void TestListItemsCanVaryInSize()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* window = canvas->CreateObject("window");
        canvas->AttachComponent<Component::Transform2D>(window);
        auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
        mask->size = {100.0f, 60.0f};
        auto* scroll = canvas->AttachComponent<Component::ScrollView2D>(window);
        auto* list = canvas->AttachComponent<Component::ListView2D>(window);
        list->count = 6;
        list->itemSize = {100.0f, 20.0f};
        list->spacing = 0.0f;
        list->variableSize = true;
        auto* probe = canvas->AttachScript<ListProbe>(window);
        probe->list = list;
        Object::GameObject* content = canvas->CreateObject("content");
        content->SetParent(window);
        auto* contentPlace = canvas->AttachComponent<Component::Transform2D>(content);
        contentPlace->position = {0.0f, -30.0f};
        scroll->content = content->GetScriptHandle();
        Object::GameObject* pool[4] = {};
        Component::Transform2D* places[4] = {};
        for (JBro::Int32 index = 0; index < 4; ++index)
        {
            pool[index] = canvas->CreateObject("item");
            pool[index]->SetParent(content);
            places[index] = canvas->AttachComponent<Component::Transform2D>(pool[index]);
        }
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        tick();
        Check(probe->measures == 6 && list->measuredCount == 6 && false == list->remeasure, "the first update measures every item once");
        Check(scroll->contentSize.y == 120.0f, "the content is the sum of the measured sizes");
        Check(list->firstVisible == 0 && list->visibleCount == 4, "with the top aligned items zero to three are in view");
        Check(At(places[0], 0.0f, 55.0f) && At(places[1], 0.0f, 35.0f) && At(places[2], 0.0f, 15.0f) && At(places[3], 0.0f, -5.0f), "each item sits at the middle of its own span");
        Check(probe->binds == 4, "the four visible items were bound");
        tick();
        Check(probe->measures == 6 && probe->binds == 4, "nothing is measured or bound again while nothing changes");
        contentPlace->position.y = 30.0f;
        tick();
        Check(list->firstVisible == 3 && list->visibleCount == 3, "scrolled to the bottom items three to five are in view");
        Check(At(places[3], 0.0f, -5.0f) && At(places[0], 0.0f, -25.0f) && At(places[1], 0.0f, -45.0f), "items four and five reuse pool slots zero and one at their own spans");
        // 개수가 바뀌면 다시 재고, `remeasure` 를 세우면 다시 잰다. 수가 아니거나 음수인 길이는 itemSize 다.
        list->count = 4;
        tick();
        Check(probe->measures == 10 && scroll->contentSize.y == 80.0f, "changing the count measures again and the content shrinks");
        probe->evenSize = 5.0f;
        probe->oddSize = 15.0f;
        list->remeasure = true;
        tick();
        Check(probe->measures == 14 && false == list->remeasure && scroll->contentSize.y == 40.0f, "remeasure runs the hook again and the content follows");
        probe->evenSize = std::numeric_limits<float>::quiet_NaN();
        probe->oddSize = -5.0f;
        list->remeasure = true;
        tick();
        Check(scroll->contentSize.y == 80.0f, "a NaN or negative measured size falls back to the item size");
        probe->leaveAlone = true;
        list->remeasure = true;
        tick();
        Check(scroll->contentSize.y == 80.0f, "a hook that writes nothing leaves the item size");
        // 고정 크기로 돌아가면 재지 않고 itemSize 다. 다시 켜면 다시 잰다.
        list->variableSize = false;
        probe->measures = 0;
        tick();
        Check(probe->measures == 0 && scroll->contentSize.y == 80.0f, "fixed size uses the item size without measuring");
        list->variableSize = true;
        tick();
        Check(probe->measures == 4, "turning variable size back on measures again");
        list->count = 0;
        tick();
        Check(list->visibleCount == 0 && scroll->contentSize.y == 0.0f, "an empty variable list has no content");
        framework.Shutdown();
    }

    Object::GameObject* window_like_object_without_tween(Canvas* canvas)
    {
        Object::GameObject* plain = canvas->CreateObject("plain");
        canvas->AttachComponent<Component::Transform2D>(plain);
        return plain;
    }

    // **트윈**(D-357). 자리·알파가 시간에 따라 간다. 사슬은 D-360.
    void TestTweensMoveOverTime()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* mover = canvas->CreateObject("mover");
        auto* place = canvas->AttachComponent<Component::Transform2D>(mover);
        auto* sprite = canvas->AttachComponent<Component::SpriteRenderer2D>(mover);
        auto* tween = canvas->AttachComponent<Component::Tween2D>(mover);
        tween->toVector = {60.0f, 0.0f};
        tween->duration = 1.0f;
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        const auto near = [](JBro::Float a, JBro::Float b) { return std::fabs(a - b) < 0.01f; };
        tick();
        Check(tween->playing && near(place->position.x, 1.0f), "playOnStart starts on the first update and moves one sixtieth");
        for (JBro::Int32 frame = 0; frame < 29; ++frame)
        {
            tick();
        }
        Check(near(place->position.x, 30.0f) && near(tween->time, 0.5f), "half a second in it is halfway");
        for (JBro::Int32 frame = 0; frame < 29; ++frame)
        {
            tick();
        }
        Check(tween->playing && false == tween->finished, "one frame before the end it is still playing");
        tick();
        Check(near(place->position.x, 60.0f) && false == tween->playing && tween->finished, "at the end it stops and reports finished for that frame");
        tick();
        Check(false == tween->finished && near(place->position.x, 60.0f), "finished lasts one frame and the value stays");
        tween->loop = Component::TweenLoop::PingPong;
        tween->time = 0.0f;
        tween->playing = true;
        for (JBro::Int32 frame = 0; frame < 90; ++frame)
        {
            tick();
        }
        Check(tween->playing && near(place->position.x, 30.0f), "ping-pong comes back halfway after a second and a half");
        tween->target = Component::TweenTarget::Alpha;
        tween->loop = Component::TweenLoop::Once;
        tween->fromValue = 1.0f;
        tween->toValue = 0.0f;
        tween->time = 0.0f;
        tween->playing = true;
        for (JBro::Int32 frame = 0; frame < 30; ++frame)
        {
            tick();
        }
        Check(near(sprite->tint.A, 0.5f), "an alpha tween fades the sprite");
        tween->ease = EaseKind::QuadIn;
        tween->time = 0.0f;
        tween->playing = true;
        for (JBro::Int32 frame = 0; frame < 30; ++frame)
        {
            tick();
        }
        Check(near(sprite->tint.A, 0.75f), "quad-in is slow at the start");
        tween->duration = std::numeric_limits<float>::quiet_NaN();
        tween->time = 0.0f;
        tween->playing = true;
        tick();
        Check(near(sprite->tint.A, 0.0f) && false == tween->playing, "a NaN duration ends at once");
        tween->target = Component::TweenTarget::Position;
        tween->toVector = {std::numeric_limits<float>::quiet_NaN(), 0.0f};
        tween->duration = 1.0f;
        tween->time = 0.0f;
        tween->playing = true;
        tick();
        Check(near(place->position.x, 30.0f), "a NaN target leaves the position where the ping-pong left it");
        // 사슬(D-360): 끝에 닿으면 `next` 의 트윈이 처음부터 돈다. 자기 자신은 무시하고, 트윈이 없는 오브젝트는 아무것도 아니며, 고리는 돈다.
        Object::GameObject* follower = canvas->CreateObject("follower");
        auto* followerPlace = canvas->AttachComponent<Component::Transform2D>(follower);
        auto* followerTween = canvas->AttachComponent<Component::Tween2D>(follower);
        followerTween->playOnStart = false;
        followerTween->toVector = {0.0f, 40.0f};
        followerTween->duration = 0.5f;
        // 풀이 늘어나 옮겨졌을 수 있다 - 처음 것의 포인터를 다시 받는다.
        place = canvas->FindComponentRaw<Component::Transform2D>(mover);
        tween = canvas->FindComponentRaw<Component::Tween2D>(mover);
        tween->next = follower->GetScriptHandle();
        tween->toVector = {60.0f, 0.0f};
        tween->duration = 1.0f;
        tween->ease = EaseKind::Linear;
        tween->time = 0.0f;
        tween->playing = true;
        for (JBro::Int32 frame = 0; frame < 59; ++frame)
        {
            tick();
        }
        Check(false == followerTween->playing && near(followerPlace->position.y, 0.0f), "the next tween waits until the first ends");
        tick();
        Check(tween->finished && followerTween->playing && followerTween->time <= 1.0f / 60.0f + 0.001f, "when the first ends the next starts from the beginning");
        for (JBro::Int32 frame = 0; frame < 30; ++frame)
        {
            tick();
        }
        Check(near(followerPlace->position.y, 40.0f) && false == followerTween->playing, "the chained tween runs its own course");
        followerTween->next = mover->GetScriptHandle();
        followerTween->time = 0.0f;
        followerTween->playing = true;
        place->position.x = 0.0f;
        for (JBro::Int32 frame = 0; frame < 29; ++frame)
        {
            tick();
        }
        Check(false == tween->playing && near(place->position.x, 0.0f), "before the follower ends the first tween is still stopped");
        for (JBro::Int32 frame = 0; frame < 2; ++frame)
        {
            tick();
        }
        // 풀 차례에 따라 되돌아온 트윈이 같은 프레임에 한 번 더 갈 수 있다 - 한두 프레임(1 또는 2)이다.
        Check(tween->playing && place->position.x > 0.5f && place->position.x < 2.5f, "a chain can loop back and restart the first tween");
        tween->next = mover->GetScriptHandle();
        tween->time = 0.0f;
        for (JBro::Int32 frame = 0; frame < 61; ++frame)
        {
            tick();
        }
        Check(false == tween->playing && near(place->position.x, 60.0f), "a tween that names itself as next just stops");
        Object::GameObject* plain = window_like_object_without_tween(canvas);
        place = canvas->FindComponentRaw<Component::Transform2D>(mover);
        tween = canvas->FindComponentRaw<Component::Tween2D>(mover);
        tween->next = plain->GetScriptHandle();
        tween->time = 0.0f;
        tween->playing = true;
        for (JBro::Int32 frame = 0; frame < 61; ++frame)
        {
            tick();
        }
        Check(false == tween->playing && tween->finished == false, "a next without a tween is nothing");
        framework.Shutdown();
    }

    // **가로세로비 맞추기**(D-358). 폭이 주인이면 높이가 폭/비율, 높이가 주인이면 폭이 높이 x 비율이다.
    void TestAspectRatioFittersKeepTheRatio()
    {
        Framework2D framework;
        FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework must initialize without a renderer");
        Canvas* canvas = framework.GetCanvas();
        Object::GameObject* frame = canvas->CreateObject("frame");
        canvas->AttachComponent<Component::Transform2D>(frame);
        auto* sprite = canvas->AttachComponent<Component::SpriteRenderer2D>(frame);
        sprite->sizeMode = Component::SpriteSizeMode::Custom;
        sprite->size = {100.0f, 30.0f};
        auto* mask = canvas->AttachComponent<Component::Mask2D>(frame);
        mask->size = {100.0f, 30.0f};
        auto* fitter = canvas->AttachComponent<Component::AspectRatioFitter2D>(frame);
        fitter->aspect = 2.0f;
        const auto tick = [&]() { JBro::Testing::Tick(framework, 1.0f / 60.0f); };
        tick();
        Check(sprite->size.x == 100.0f && sprite->size.y == 50.0f && mask->size.y == 50.0f, "width controls height: the height becomes width over aspect");
        fitter->mode = Component::AspectMode::HeightControlsWidth;
        mask->size = {100.0f, 30.0f};
        sprite->size = {100.0f, 30.0f};
        tick();
        Check(sprite->size.x == 60.0f && sprite->size.y == 30.0f && mask->size.x == 60.0f, "height controls width: the width becomes height times aspect");
        fitter->aspect = 0.0f;
        sprite->size = {100.0f, 30.0f};
        tick();
        Check(sprite->size.x == 100.0f && sprite->size.y == 30.0f, "a zero aspect changes nothing");
        fitter->aspect = std::numeric_limits<float>::quiet_NaN();
        tick();
        Check(sprite->size.x == 100.0f, "a NaN aspect changes nothing");
        fitter->aspect = 2.0f;
        sprite->sizeMode = Component::SpriteSizeMode::FromSprite;
        sprite->size = {100.0f, 30.0f};
        tick();
        Check(sprite->size.y == 30.0f && mask->size.x == 60.0f, "an asset-sized sprite is left alone while the mask still follows");
        framework.Shutdown();
    }
}

JBro::Int32 RunLayout2DTests()
{
    TestChildrenLineUpInCells();
    TestFittedChildrenAndContentSize();
    TestListViewsBindPoolItems();
    TestTweensMoveOverTime();
    TestAspectRatioFittersKeepTheRatio();
    TestListItemsCanVaryInSize();
    std::cout << "Layout2D tests passed." << std::endl;
    return 0;
}
