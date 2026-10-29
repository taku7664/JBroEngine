#include <JBro/Editor/Gizmo/ButtonRectModel.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// 캔버스 뷰 버튼 사각형 편집의 모델 테스트(D-351 1 단계). 화면 없이 오브젝트 로컬 숫자로 잰다.
namespace
{
    using JBro::Vector2;
    using JBro::ButtonRectModel::Handle;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float got, JBro::Float want)
    {
        return std::fabs(got - want) < 0.001f;
    }

    JBro::Bool Near(Vector2 got, Vector2 want)
    {
        return Near(got.x, want.x) && Near(got.y, want.y);
    }

    void TestTheHandlesSitOnTheRectangle()
    {
        const Vector2 offset{ 10.0f, -4.0f };
        const Vector2 size{ 40.0f, 20.0f };
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, size, Handle::Left), { -10.0f, -4.0f }), "the left handle is on the left edge's middle");
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, size, Handle::Right), { 30.0f, -4.0f }), "the right handle on the right edge");
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, size, Handle::Bottom), { 10.0f, -14.0f }), "the bottom handle on the bottom edge");
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, size, Handle::Top), { 10.0f, 6.0f }), "the top handle on the top edge");
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, size, Handle::Center), offset), "the center handle is the offset");
        // 음수 크기는 절댓값이다 - 판정(`HitTest`)과 같다.
        Check(Near(JBro::ButtonRectModel::HandleLocal(offset, { -40.0f, 20.0f }, Handle::Right), { 30.0f, -4.0f }), "a negative size is its absolute value");
        Vector2 corners[4];
        JBro::ButtonRectModel::Corners(offset, size, corners);
        Check(Near(corners[0], { -10.0f, -14.0f }) && Near(corners[2], { 30.0f, 6.0f }), "the corners enclose the rectangle");
    }

    void TestDraggingAnEdgeMovesOnlyThatEdge()
    {
        const Vector2 offset{ 0.0f, 0.0f };
        const Vector2 size{ 40.0f, 20.0f };
        Vector2 newOffset;
        Vector2 newSize;
        // 오른쪽 변을 10 밀면 폭 50, 가운데는 5 로 간다. 왼쪽 변(-20)은 그대로다.
        JBro::ButtonRectModel::Drag(Handle::Right, offset, size, { 20.0f, 0.0f }, { 30.0f, 0.0f }, newOffset, newSize);
        Check(Near(newSize, { 50.0f, 20.0f }) && Near(newOffset, { 5.0f, 0.0f }), "pulling the right edge out widens the rectangle and keeps the left edge");
        // 잡은 자리가 손잡이에서 어긋나도 간 만큼만 움직인다.
        JBro::ButtonRectModel::Drag(Handle::Right, offset, size, { 22.0f, 1.0f }, { 32.0f, 3.0f }, newOffset, newSize);
        Check(Near(newSize, { 50.0f, 20.0f }) && Near(newOffset, { 5.0f, 0.0f }), "an off-center grab moves the edge by the drag only");
        // 변은 맞은편을 넘지 않는다.
        JBro::ButtonRectModel::Drag(Handle::Left, offset, size, { -20.0f, 0.0f }, { 60.0f, 0.0f }, newOffset, newSize);
        Check(Near(newSize, { 0.0f, 20.0f }) && Near(newOffset, { 20.0f, 0.0f }), "an edge dragged past the opposite edge stops there");
        JBro::ButtonRectModel::Drag(Handle::Top, offset, size, { 0.0f, 10.0f }, { 0.0f, 4.0f }, newOffset, newSize);
        Check(Near(newSize, { 40.0f, 14.0f }) && Near(newOffset, { 0.0f, -3.0f }), "lowering the top edge shrinks the height and keeps the bottom edge");
        JBro::ButtonRectModel::Drag(Handle::Bottom, offset, size, { 0.0f, -10.0f }, { 0.0f, -20.0f }, newOffset, newSize);
        Check(Near(newSize, { 40.0f, 30.0f }) && Near(newOffset, { 0.0f, -5.0f }), "pulling the bottom edge down grows the height downward");
        // 가운데는 통째로 옮긴다.
        JBro::ButtonRectModel::Drag(Handle::Center, offset, size, { 1.0f, 1.0f }, { 4.0f, -2.0f }, newOffset, newSize);
        Check(Near(newSize, size) && Near(newOffset, { 3.0f, -3.0f }), "the center handle moves the rectangle without resizing it");
    }

    void TestFittingToASprite()
    {
        Vector2 size;
        Vector2 offset;
        Check(JBro::ButtonRectModel::FitToSprite({ 64.0f, 32.0f }, { 0.5f, 0.5f }, size, offset) && Near(size, { 64.0f, 32.0f }) && Near(offset, {}),
            "a centered sprite fits with no offset");
        // 피벗이 왼쪽 아래면 그림은 원점에서 오른쪽 위로 뻗는다 - 사각형의 가운데는 (32, 16) 이다.
        Check(JBro::ButtonRectModel::FitToSprite({ 64.0f, 32.0f }, { 0.0f, 0.0f }, size, offset) && Near(offset, { 32.0f, 16.0f }),
            "a bottom-left pivot moves the rectangle up and right");
        // 뒤집은 그림(음수 크기)도 크기는 절댓값이고 자리는 뒤집힌 쪽이다.
        Check(JBro::ButtonRectModel::FitToSprite({ -64.0f, 32.0f }, { 0.0f, 0.5f }, size, offset) && Near(size, { 64.0f, 32.0f }) && Near(offset, { -32.0f, 0.0f }),
            "a flipped sprite keeps the absolute size and mirrors the offset");
        size = { 1.0f, 1.0f };
        Check(false == JBro::ButtonRectModel::FitToSprite({ 0.0f, 32.0f }, { 0.5f, 0.5f }, size, offset) && Near(size, { 1.0f, 1.0f }),
            "a sprite without width cannot be fitted and leaves the values alone");
    }

    void TestTheHandleIdsAreDistinct()
    {
        const char* left = JBro::ButtonRectModel::HandleId(Handle::Left);
        const char* center = JBro::ButtonRectModel::HandleId(Handle::Center);
        Check(left[0] == '#' && left[1] == '#' && std::string(left) != center, "every handle has its own hidden id");
    }
}

JBro::Int32 RunButtonRectModelTests()
{
    TestTheHandlesSitOnTheRectangle();
    TestDraggingAnEdgeMovesOnlyThatEdge();
    TestFittingToASprite();
    TestTheHandleIdsAreDistinct();
    std::cout << "ButtonRectModel tests passed." << std::endl;
    return 0;
}
