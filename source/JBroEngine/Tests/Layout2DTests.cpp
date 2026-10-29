#include "TestClock.h"
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
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
}

JBro::Int32 RunLayout2DTests()
{
    TestChildrenLineUpInCells();
    std::cout << "Layout2D tests passed." << std::endl;
    return 0;
}
