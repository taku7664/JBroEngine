#include <JBro/Editor/NodeGraph/NodeGraphView.h>
#include <JBro/NodeGraph/MathDomain.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// 노드 그래프 화면 모델의 시험(D-308, node-graph-plan §3 의 2 단계). 화면 없이 판정과 손짓을 잰다.
namespace
{
    using namespace JBro::NodeGraphView;
    using JBro::Vector2;
    using JBro::NodeGraph::PinRef;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(float value, float expected)
    {
        return std::fabs(value - expected) < 0.01f;
    }

    struct Scene
    {
        JBro::OwnerPtr<JBro::NodeGraph::Domain> domain = JBro::NodeGraph::MakeMathDomain();
        Graph graph;
        Style style;
        Camera camera;
        NodeId constant;
        NodeId add;
        NodeId output;

        Scene()
        {
            camera.origin = Vector2{100.0f, 50.0f};
            constant = graph.AddNode(*domain, "Float", Vector2{0.0f, 0.0f});
            add = graph.AddNode(*domain, "Add", Vector2{300.0f, 0.0f});
            output = graph.AddNode(*domain, "Output", Vector2{600.0f, 0.0f});
            graph.Connect(*domain, PinRef{add, 0}, PinRef{output, 0});
        }

        Vector2 Screen(Vector2 graphPoint) const
        {
            return ToScreen(camera, graphPoint);
        }
    };

    Input Move(Vector2 mouse)
    {
        Input input;
        input.mouse = mouse;
        input.hovered = true;
        return input;
    }

    Input Press(Vector2 mouse)
    {
        Input input = Move(mouse);
        input.leftPressed = true;
        input.leftDown = true;
        return input;
    }

    Input Hold(Vector2 mouse)
    {
        Input input = Move(mouse);
        input.leftDown = true;
        return input;
    }

    Input Release(Vector2 mouse)
    {
        Input input = Move(mouse);
        input.leftReleased = true;
        return input;
    }

    // 누르고, 중간을 지나 끌고, 놓는다. 마지막 프레임의 사건을 낸다.
    Events Drag(Scene& scene, Interaction& interaction, Vector2 from, Vector2 to, JBro::Bool alt = false)
    {
        Events events;
        Input press = Press(from);
        press.alt = alt;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, press, events);
        const Vector2 middle{(from.x.Get() + to.x.Get()) * 0.5f, (from.y.Get() + to.y.Get()) * 0.5f};
        Events ignored;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Hold(middle), ignored);
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Hold(to), ignored);
        Events released;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(to), released);
        released.selectionChanged = released.selectionChanged || events.selectionChanged;
        return released;
    }

    void TestCamera()
    {
        Scene scene;
        SetCamera(scene.camera, Vector2{10.0f, -20.0f}, 2.0f);
        const Vector2 point{37.0f, 12.0f};
        const Vector2 screen = ToScreen(scene.camera, point);
        Check(Near(screen.x, 100.0f + (37.0f - 10.0f) * 2.0f) && Near(screen.y, 50.0f + (12.0f + 20.0f) * 2.0f), "screen = origin + (graph - pan) x zoom");
        const Vector2 back = ToGraph(scene.camera, screen);
        Check(Near(back.x, 37.0f) && Near(back.y, 12.0f), "and back");

        // 휠은 가려는 카메라만 바꾼다. 그리는 카메라는 따라가며, 다 따라가면 마우스 아래 그래프 자리가 그대로다.
        const Vector2 anchor{400.0f, 300.0f};
        const Vector2 under = ToGraph(scene.camera, anchor);
        ZoomAt(scene.camera, scene.style, anchor, 0.5f);
        Check(Near(scene.camera.zoom, 2.0f) && Near(scene.camera.goalZoom, 0.5f), "zooming moves the goal, not the drawn camera");
        // 따라가는 도중에 한 번 더 돌려도 같은 점을 붙잡는다 - 기준점을 가려는 카메라로 잰다.
        // 다 간 뒤만이 아니라 **가는 내내** 마우스 아래 점이 그대로다(캔버스 뷰처럼 크기를 따라간다).
        Follow(scene.camera, 0.016f);
        const Vector2 between = ToGraph(scene.camera, anchor);
        Check(Near(between.x, under.x) && Near(between.y, under.y), "half way there, the point under the mouse has not slid");
        ZoomAt(scene.camera, scene.style, anchor, 0.4f);
        for (int frame = 0; frame < 400; ++frame)
        {
            Follow(scene.camera, 0.016f);
            const Vector2 moving = ToGraph(scene.camera, anchor);
            Check(Near(moving.x, under.x) && Near(moving.y, under.y), "every frame of the follow keeps the point under the mouse");
        }
        const Vector2 after = ToGraph(scene.camera, anchor);
        Check(Near(scene.camera.zoom, 0.4f) && Near(after.x, under.x) && Near(after.y, under.y),
            "once it settles, the graph point under the mouse is where it was");
        ZoomAt(scene.camera, scene.style, anchor, 100.0f);
        Check(Near(scene.camera.goalZoom, scene.style.maxZoom), "zoom is clamped");
    }

    void TestFollowIsSmoothAndFrameRateFree()
    {
        Camera a;
        SetCamera(a, Vector2{0.0f, 0.0f}, 1.0f);
        a.goalPan = Vector2{100.0f, 0.0f};
        Camera b = a;
        Follow(a, 0.05f);
        Check(a.pan.x.Get() > 0.0f && a.pan.x.Get() < 100.0f, "one frame moves part of the way, not all of it");
        for (int frame = 0; frame < 5; ++frame)
        {
            Follow(b, 0.01f);
        }
        Check(Near(a.pan.x, b.pan.x), "five frames of 10 ms land where one of 50 ms does");
        for (int frame = 0; frame < 200; ++frame)
        {
            Follow(a, 0.016f);
        }
        Check(a.pan.x.Get() == 100.0f && a.zoom.Get() == 1.0f, "close enough, it snaps onto the goal");
    }

    void TestPick()
    {
        Scene scene;
        const JBro::NodeGraph::Node* add = scene.graph.FindNode(scene.add);
        Hit hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, scene.Screen(InputPosition(*add, 1, scene.style)));
        Check(hit.kind == HitKind::Input && hit.node.Get() == scene.add.Get() && hit.pin == 1, "a pin on the node's edge is a pin, not the node");
        hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, scene.Screen(OutputPosition(*add, 0, scene.style)));
        Check(hit.kind == HitKind::Output && hit.node.Get() == scene.add.Get(), "outputs are on the right edge");
        hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, scene.Screen(Vector2{360.0f, 10.0f}));
        Check(hit.kind == HitKind::Node && hit.node.Get() == scene.add.Get(), "the header is the node");

        // 링크의 가운데. 두 끝이 같은 높이라 곡선이 그 줄을 지난다.
        const JBro::NodeGraph::Node* output = scene.graph.FindNode(scene.output);
        const Vector2 a = scene.Screen(OutputPosition(*add, 0, scene.style));
        const Vector2 b = scene.Screen(InputPosition(*output, 0, scene.style));
        hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, Vector2{(a.x.Get() + b.x.Get()) * 0.5f, a.y.Get() + 3.0f});
        Check(hit.kind == HitKind::Link && hit.link.Get() == scene.graph.GetLinks()[0].id.Get(), "a point near the curve is the link");
        hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, Vector2{(a.x.Get() + b.x.Get()) * 0.5f, a.y.Get() + 40.0f});
        Check(hit.kind == HitKind::None, "far from everything is nothing");

        // 겹친 노드는 뒤에 더한 것이 위다.
        const NodeId over = scene.graph.AddNode(*scene.domain, "Float", Vector2{320.0f, 5.0f});
        hit = Pick(scene.graph, *scene.domain, scene.style, scene.camera, scene.Screen(Vector2{360.0f, 12.0f}));
        Check(hit.kind == HitKind::Node && hit.node.Get() == over.Get(), "the node added last is on top");
    }

    void TestMoveIsOneEventOnRelease()
    {
        Scene scene;
        scene.camera.zoom = 2.0f;
        Interaction interaction;
        const Vector2 from = scene.Screen(Vector2{360.0f, 10.0f});
        const Vector2 to{from.x.Get() + 40.0f, from.y.Get() + 20.0f};

        Events events;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Press(from), events);
        Check(events.selectionChanged && interaction.IsSelected(scene.add) && interaction.GetSelection().Size() == 1, "pressing a node selects it");
        Events during;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Hold(to), during);
        Check(!during.moved && interaction.GetGesture() == Gesture::MovingNodes, "dragging previews, it does not commit");
        Check(Near(interaction.GetMoveOffset().x, 20.0f) && Near(interaction.GetMoveOffset().y, 10.0f), "the preview offset is in graph units");
        Events released;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(to), released);
        Check(released.moved && Near(released.moveDelta.x, 20.0f) && Near(released.moveDelta.y, 10.0f), "releasing reports the move once");
        Check(interaction.GetGesture() == Gesture::Idle && Near(interaction.GetMoveOffset().x, 0.0f), "and the preview ends");

        // 끌지 않고 누르기만 하면 옮기지 않는다.
        const Events click = Drag(scene, interaction, from, Vector2{from.x.Get() + 1.0f, from.y.Get()});
        Check(!click.moved, "a click under the drag threshold is not a move");
    }

    void TestLinking()
    {
        Scene scene;
        Interaction interaction;
        const JBro::NodeGraph::Node* constant = scene.graph.FindNode(scene.constant);
        const JBro::NodeGraph::Node* add = scene.graph.FindNode(scene.add);
        const Vector2 out = scene.Screen(OutputPosition(*constant, 0, scene.style));
        const Vector2 in = scene.Screen(InputPosition(*add, 1, scene.style));

        Events events = Drag(scene, interaction, out, in);
        Check(events.connect && events.connectFrom == (PinRef{scene.constant, 0}) && events.connectTo == (PinRef{scene.add, 1}),
            "dragging from an output to an input asks to connect them");

        events = Drag(scene, interaction, in, out);
        Check(events.connect && events.connectFrom == (PinRef{scene.constant, 0}) && events.connectTo == (PinRef{scene.add, 1}),
            "dragging from the input end gives the same link");

        const Vector2 empty{out.x.Get(), out.y.Get() + 300.0f};
        events = Drag(scene, interaction, out, empty);
        Check(!events.connect && events.linkDropped && events.droppedFromOutput && events.droppedPin == (PinRef{scene.constant, 0}),
            "dropping on nothing offers to add a node there");

        // 잇는 중에 이을 수 없는 핀은 밝히지 않는다 - 모델의 규칙(타입·순환)을 그대로 쓴다.
        Events ignored;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Press(scene.Screen(OutputPosition(*scene.graph.FindNode(scene.add), 0, scene.style))), ignored);
        Check(interaction.GetGesture() == Gesture::Linking, "pressing an output starts a link");
        const Hit constantInput{HitKind::Input, scene.constant, 0, JBro::NodeGraph::InvalidId};
        Check(!interaction.CanLinkTo(scene.graph, *scene.domain, constantInput), "a vector4 output cannot feed a float input");
        const Hit selfInput{HitKind::Input, scene.add, 0, JBro::NodeGraph::InvalidId};
        Check(!interaction.CanLinkTo(scene.graph, *scene.domain, selfInput), "nor its own node");
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(empty), ignored);
    }

    void TestBoxSelectAndModifiers()
    {
        Scene scene;
        Interaction interaction;
        // 빈 곳에서 시작해 앞의 두 노드를 덮는다.
        const Events boxed = Drag(scene, interaction, scene.Screen(Vector2{-20.0f, -20.0f}), scene.Screen(Vector2{320.0f, 60.0f}));
        Check(boxed.selectionChanged && interaction.GetSelection().Size() == 2 && interaction.IsSelected(scene.constant) && interaction.IsSelected(scene.add),
            "a box from empty space selects the nodes it touches");

        // 여럿을 고른 채 하나를 누르고 놓으면 그것만 남는다.
        Drag(scene, interaction, scene.Screen(Vector2{20.0f, 10.0f}), scene.Screen(Vector2{20.0f, 10.0f}));
        Check(interaction.GetSelection().Size() == 1 && interaction.IsSelected(scene.constant), "clicking one of several keeps just it");

        Events events;
        Input press = Press(scene.Screen(Vector2{620.0f, 10.0f}));
        press.ctrl = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, press, events);
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(press.mouse), events);
        Check(interaction.GetSelection().Size() == 2 && interaction.IsSelected(scene.output), "ctrl adds to the selection");

        const Events cleared = Drag(scene, interaction, scene.Screen(Vector2{200.0f, 400.0f}), scene.Screen(Vector2{200.0f, 400.0f}));
        Check(cleared.selectionChanged && interaction.GetSelection().IsEmpty(), "clicking empty space clears the selection");

        scene.graph.RemoveNode(scene.constant);
        const NodeId kept[] = {scene.constant, scene.add};
        interaction.Select({kept, 2});
        interaction.Prune(scene.graph);
        Check(interaction.GetSelection().Size() == 1 && interaction.IsSelected(scene.add), "pruning drops nodes that are gone");
    }

    void TestRightButtonAndAlt()
    {
        Scene scene;
        Interaction interaction;
        const Vector2 at = scene.Screen(Vector2{360.0f, 10.0f});

        Events events;
        Input press = Move(at);
        press.rightPressed = true;
        press.rightDown = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, press, events);
        Input release = Move(at);
        release.rightReleased = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, release, events);
        Check(events.contextMenu && events.contextHit.kind == HitKind::Node && events.contextHit.node.Get() == scene.add.Get(),
            "a right click opens the menu for what is under it");

        const Vector2 panBefore = scene.camera.goalPan;
        Events panEvents;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, press, panEvents);
        Input drag = Move(Vector2{at.x.Get() + 30.0f, at.y.Get()});
        drag.rightDown = true;
        drag.mouseDelta = Vector2{30.0f, 0.0f};
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, drag, panEvents);
        Input dragRelease = Move(drag.mouse);
        dragRelease.rightReleased = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, dragRelease, panEvents);
        Check(!panEvents.contextMenu && Near(scene.camera.goalPan.x, panBefore.x.Get() - 30.0f), "a right drag pans the goal and opens no menu");

        const JBro::NodeGraph::Node* add = scene.graph.FindNode(scene.add);
        const JBro::NodeGraph::Node* output = scene.graph.FindNode(scene.output);
        const Vector2 a = scene.Screen(OutputPosition(*add, 0, scene.style));
        const Vector2 b = scene.Screen(InputPosition(*output, 0, scene.style));
        const Vector2 onLink{(a.x.Get() + b.x.Get()) * 0.5f, a.y.Get()};
        Events cut;
        Input altPress = Press(onLink);
        altPress.alt = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, altPress, cut);
        Check(cut.disconnect && cut.disconnectLink.Get() == scene.graph.GetLinks()[0].id.Get(), "alt-clicking a link breaks it");
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(onLink), cut);

        Events cutInput;
        Input altInput = Press(b);
        altInput.alt = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, altInput, cutInput);
        Check(cutInput.disconnect && cutInput.disconnectLink.Get() == scene.graph.GetLinks()[0].id.Get(), "alt-clicking a linked input breaks its link");
    }

    void TestFrame()
    {
        Scene scene;
        Frame(scene.camera, scene.style, scene.graph, *scene.domain, Vector2{1600.0f, 900.0f}, {}, true);
        const Vector2 center = ToScreen(scene.camera, Vector2{(0.0f + 600.0f + scene.style.nodeWidth.Get()) * 0.5f, 0.0f});
        Check(Near(center.x, 100.0f + 800.0f), "framing centers the nodes across");
        Check(scene.camera.zoom.Get() <= 1.0f, "and never zooms past one to fit a small graph");
        Frame(scene.camera, scene.style, scene.graph, *scene.domain, Vector2{300.0f, 200.0f});
        Check(scene.camera.goalZoom.Get() < 0.5f && scene.camera.zoom.Get() > 0.5f, "framing glides: it sets the goal and the drawn camera follows");

        // 고른 노드만 맞추면 그 노드가 가운데다(캔버스 뷰의 맞추기).
        const NodeId only[] = {scene.output};
        Frame(scene.camera, scene.style, scene.graph, *scene.domain, Vector2{1600.0f, 900.0f}, {only, 1}, true);
        const Vector2 outputCenter = ToScreen(scene.camera, Vector2{600.0f + scene.style.nodeWidth.Get() * 0.5f, 0.0f});
        Check(Near(outputCenter.x, 100.0f + 800.0f), "framing the selection centers just those nodes");
    }

    void TestDoubleClickNode()
    {
        Scene scene;
        Interaction interaction;
        Events events;
        Input press = Press(scene.Screen(Vector2{360.0f, 10.0f}));
        press.leftDoubleClicked = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, press, events);
        Check(events.nodeDoubleClicked && events.doubleClickedNode.Get() == scene.add.Get(), "double-clicking a node reports it");
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, Release(press.mouse), events);
        Events empty;
        Input emptyPress = Press(scene.Screen(Vector2{200.0f, 400.0f}));
        emptyPress.leftDoubleClicked = true;
        interaction.Update(scene.graph, *scene.domain, scene.style, scene.camera, emptyPress, empty);
        Check(!empty.nodeDoubleClicked, "double-clicking empty space does not");
    }
}

JBro::Int32 RunNodeGraphViewTests()
{
    TestCamera();
    TestFollowIsSmoothAndFrameRateFree();
    TestPick();
    TestMoveIsOneEventOnRelease();
    TestLinking();
    TestBoxSelectAndModifiers();
    TestRightButtonAndAlt();
    TestFrame();
    TestDoubleClickNode();
    std::cout << "Node graph view tests passed.\n";
    return 0;
}
