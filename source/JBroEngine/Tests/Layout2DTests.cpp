#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
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
}

JBro::Int32 RunLayout2DTests()
{
    TestChildrenLineUpInCells();
    TestFittedChildrenAndContentSize();
    std::cout << "Layout2D tests passed." << std::endl;
    return 0;
}
