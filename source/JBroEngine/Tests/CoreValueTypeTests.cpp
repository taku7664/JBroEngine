#include <JBro/RHI/RHI.h>
#include <JBro/Types/Types.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// Core 의 공용 값 타입들(D-249): 크기·비트 묶음·사각형 연산·안전 영역·프레임 생존 표시.
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

    JBro::Bool Near(JBro::Float left, JBro::Float right)
    {
        return std::fabs(left - right) < 0.0001f;
    }

    // ── Size ────────────────────────────────────────────────────────────────

    // **높이가 0 이면 가로세로비는 0 이다.** 나누어 무한을 만들면 그 값이 레이아웃을 타고
    // 번져 어디서 터졌는지 못 찾는다.
    void TestSizeDoesNotDivideByZero()
    {
        Check(Near(JBro::Size(1920.0f, 1080.0f).AspectRatio(), 16.0f / 9.0f),
            "a normal size reports its aspect ratio");
        Check(JBro::Size(3.0f, 0.0f).AspectRatio() == 0.0f,
            "a zero height reports 0, not infinity");
    }

    // 넓이나 높이 하나만 0 이어도 비어 있다. 그려지지도 재어지지도 않기 때문이다.
    void TestSizeIsEmptyWhenEitherSideIsGone()
    {
        Check(JBro::Size(0.0f, 5.0f).IsEmpty(), "no width means empty");
        Check(JBro::Size(5.0f, 0.0f).IsEmpty(), "no height means empty");
        Check(JBro::Size(-1.0f, 5.0f).IsEmpty(), "a negative side is empty too");
        Check(false == JBro::Size(1.0f, 1.0f).IsEmpty(), "a real size is not empty");
    }

    // `Extent2D` 는 이제 `SizeU` 다(D-249). 이름만 다르고 같은 타입이라야 한다 -
    // 두 벌로 갈라지면 RHI 바깥이 다시 `width`·`height` 를 낱개로 들고 다니게 된다.
    void TestExtentIsTheSameTypeAsSizeU()
    {
        static_assert(std::is_same_v<JBro::Extent2D, JBro::SizeU>);
        const JBro::Extent2D extent{ 256u, 128u };
        Check(extent.Area() == 32768u, "an extent can answer its own area");
        Check(extent == JBro::SizeU(256u, 128u), "an extent equals the size it is");
    }

    // ── BitFlag ─────────────────────────────────────────────────────────────

    // **`HasAll` 과 `HasAny` 는 다른 질문이다.** 이 타입을 둔 까닭이 그것이고, 생 정수로 쓸 때
    // 손으로 고르다 틀리던 자리다.
    void TestHasAllIsNotHasAny()
    {
        JBro::BitFlag flags;
        flags.Add(0x1u | 0x4u);
        Check(flags.HasAll(0x1u | 0x4u), "it has both of the bits it was given");
        Check(false == flags.HasAll(0x1u | 0x8u), "but not a bit it was never given");
        Check(flags.HasAny(0x2u | 0x4u), "it has one of these two");
        Check(false == flags.HasAny(0x2u | 0x8u), "and neither of those two");
        Check(flags.HasNone(0x8u), "having none is the opposite of having any");
    }

    void TestFlagsCanBeTurnedOnAndOff()
    {
        JBro::BitFlag flags(0x1u | 0x4u);
        flags.Remove(0x4u);
        Check(flags.Get() == 0x1u, "removing clears only the bits it names");
        flags.SetTo(0x8u, true);
        Check(flags.Get() == 0x9u, "setting to true adds");
        flags.SetTo(0x1u, false);
        Check(flags.Get() == 0x8u, "setting to false removes");
        flags.Toggle(0x8u);
        Check(flags.IsEmpty(), "toggling the last bit empties it");
        Check(false == static_cast<bool>(flags), "and an empty flag is false");
    }

    // ── Rect ────────────────────────────────────────────────────────────────

    // **점 하나짜리 사각형은 뒤집힌 것이 아니다.** 물리의 점 질의가 `Rect{ point, point }` 라서,
    // 맞닿은 것을 안 겹친 것으로 세면 그 질의가 아무것도 못 맞힌다.
    void TestATouchingRectCounts()
    {
        const JBro::Rect box{ JBro::Vector2{0.0f, 0.0f}, JBro::Vector2{10.0f, 10.0f} };
        const JBro::Rect point = JBro::MakeRectFromPoint(JBro::Vector2{3.0f, 3.0f});
        Check(box.Intersects(point), "a point inside the box hits it");
        Check(point.Contains(JBro::Vector2{3.0f, 3.0f}), "a zero-area rect still contains its own point");
        const JBro::Rect edge{ JBro::Vector2{10.0f, 0.0f}, JBro::Vector2{20.0f, 10.0f} };
        Check(box.Intersects(edge), "two rects that share an edge overlap");
    }

    // 겹치지 않으면 뒤집힌 사각형이 나온다. 부른 쪽이 `IsEmpty` 로 확인한다.
    void TestAMissedIntersectionComesBackEmpty()
    {
        const JBro::Rect left{ JBro::Vector2{0.0f, 0.0f}, JBro::Vector2{10.0f, 10.0f} };
        const JBro::Rect right{ JBro::Vector2{5.0f, 5.0f}, JBro::Vector2{20.0f, 20.0f} };
        const JBro::Rect hit = JBro::IntersectRect(left, right);
        Check(false == hit.IsEmpty(), "an overlap is not empty");
        Check(Near(hit.min.x, 5.0f) && Near(hit.max.x, 10.0f), "and it is the shared part");

        const JBro::Rect far{ JBro::Vector2{50.0f, 50.0f}, JBro::Vector2{60.0f, 60.0f} };
        Check(JBro::IntersectRect(left, far).IsEmpty(), "no overlap comes back empty");
        Check(false == left.Intersects(far), "and the two do not intersect");
    }

    // **한 점이 NaN 이라고 상자 전체가 NaN 이 되면 안 된다.** 그러면 그 물체가 모든 질의에
    // 걸리거나 아무 질의에도 안 걸린다. 물리의 경계 상자가 전부터 `fmin`/`fmax` 로 쌓아 온 까닭이다.
    void TestOneBadPointDoesNotPoisonTheBounds()
    {
        const JBro::Float nan = std::nanf("");
        JBro::Rect bounds = JBro::MakeRectFromPoint(JBro::Vector2{1.0f, 1.0f});
        bounds = JBro::UnionRect(bounds, JBro::Vector2{nan, 4.0f});
        Check(false == std::isnan(bounds.min.x), "the x side survived the bad point");
        Check(Near(bounds.max.y, 4.0f), "and the good axis still grew");
    }

    void TestRectsGrowAndMove()
    {
        const JBro::Rect box{ JBro::Vector2{0.0f, 0.0f}, JBro::Vector2{10.0f, 10.0f} };
        Check(Near(JBro::ExpandRect(box, 2.0f).Width(), 14.0f), "expanding grows both sides");
        Check(Near(JBro::OffsetRect(box, JBro::Vector2{3.0f, 0.0f}).min.x, 3.0f), "offsetting moves it");
        Check(box.Contains(JBro::Rect{ JBro::Vector2{1.0f, 1.0f}, JBro::Vector2{9.0f, 9.0f} }),
            "a box contains a smaller box inside it");
        Check(Near(box.GetSize().width, 10.0f) && Near(box.Center().x, 5.0f),
            "and it can say its own size and middle");

        const JBro::Rect fromCenter = JBro::MakeRectFromCenter(JBro::Vector2{0.0f, 0.0f}, JBro::Size(4.0f, 6.0f));
        Check(Near(fromCenter.min.x, -2.0f) && Near(fromCenter.max.y, 3.0f),
            "a rect built from a centre is centred on it");
    }

    // ── SafeAreaInsets ──────────────────────────────────────────────────────

    // **데스크톱은 전부 0 이다.** 0 이면 계산을 통째로 건너뛰므로 기본값이 그래야 한다.
    void TestNoInsetsByDefault()
    {
        const JBro::SafeAreaInsets none;
        Check(false == none.IsAny(), "a fresh inset set is empty");
        JBro::SafeAreaInsets notch;
        notch.top = 44.0f;
        Check(notch.IsAny(), "one filled side is enough to matter");
    }

    // ── Vector4 ─────────────────────────────────────────────────────────────

    // **`Quaternion` 과 배치는 같아도 기본값이 다르다**(D-250). 사원수는 회전이라 `w` 가 1 이고,
    // 벡터는 숫자 넷이라 0 이다. 한 타입으로 겸했다면 둘 중 하나가 반드시 틀린다.
    void TestAVectorIsNotAQuaternion()
    {
        const JBro::Vector4 vector;
        const JBro::Quaternion rotation;
        Check(vector.w == 0.0f, "a fresh vector is all zeroes");
        Check(rotation.w == 1.0f, "a fresh quaternion is the identity");
        static_assert(sizeof(JBro::Vector4) == sizeof(JBro::Quaternion));
    }

    // 동차 좌표에서 점과 방향을 가르는 것은 `w` 다. 평행이동이 붙느냐 마느냐가 갈린다.
    void TestPointsAndDirectionsDifferByW()
    {
        const JBro::Vector3 source{ 1.0f, 2.0f, 3.0f };
        Check(JBro::MakePoint(source).w == 1.0f, "a point carries w = 1");
        Check(JBro::MakeDirection(source).w == 0.0f, "a direction carries w = 0");
    }

    // **`w` 가 0 이면 나누지 않는다.** 무한히 먼 점이라 나눌 수 없고, 여기서 무한을 만들면
    // 그 값이 뒤로 번져 어디서 터졌는지 못 찾는다.
    void TestThePerspectiveDivideDoesNotMakeInfinities()
    {
        const JBro::Vector3 halved = JBro::ToVector3(JBro::Vector4{ 4.0f, 6.0f, 8.0f, 2.0f });
        Check(Near(halved.x, 2.0f) && Near(halved.z, 4.0f), "a real w divides through");

        const JBro::Vector3 atInfinity = JBro::ToVector3(JBro::Vector4{ 4.0f, 6.0f, 8.0f, 0.0f });
        Check(std::isfinite(atInfinity.x), "a zero w leaves the value finite");
        Check(Near(atInfinity.x, 4.0f), "and hands back what it was given");
    }

    void TestVector4Arithmetic()
    {
        const JBro::Vector4 left{ 1.0f, 2.0f, 3.0f, 4.0f };
        const JBro::Vector4 right{ 5.0f, 6.0f, 7.0f, 8.0f };
        Check(Near(JBro::Dot(left, right), 70.0f), "the dot product spans all four parts");
        Check(Near(JBro::Add(left, right).w, 12.0f), "adding reaches w");
        Check(Near(JBro::Length(JBro::Vector4{ 0.0f, 0.0f, 0.0f, 3.0f }), 3.0f), "so does the length");
        // 길이 0 을 정규화하면 나눌 수가 없다. 0 을 돌려준다 - NaN 을 흘리지 않는다.
        Check(JBro::Normalize(JBro::Vector4{}).x == 0.0f, "normalizing nothing gives nothing, not NaN");
        Check(Near(JBro::Length(JBro::Normalize(left)), 1.0f), "and a real vector normalizes to 1");
    }

    // ── FrameLiveness ───────────────────────────────────────────────────────

    struct Cached
    {
        JBro::UInt64 lastSeenFrame = 0;
        JBro::Int32 payload = 0;
    };

    // 이번 프레임에 본 것만 남는다. **도는 중에 지우지 않는다** - `Table` 은 지우면 자리를
    // 다시 놓으므로, 도는 중에 지우면 아직 보지 않은 항목을 건너뛴다.
    void TestOnlyWhatWasSeenThisFrameSurvives()
    {
        JBro::Table<JBro::UInt32, Cached> cache;
        JBro::Array<JBro::UInt32> scratch;

        const JBro::UInt64 frame = 7;
        for (JBro::UInt32 id = 0; id < 8; ++id)
        {
            Cached& entry = cache.FindOrAdd(id);
            entry.payload = static_cast<JBro::Int32>(id);
            // 짝수만 이번 프레임에 봤다.
            entry.lastSeenFrame = (id % 2 == 0) ? frame : frame - 1;
        }

        JBro::RemoveStaleEntries(cache, frame, scratch,
            [](const Cached& entry) { return entry.lastSeenFrame; });

        Check(cache.Size() == 4, "the four that were seen are still there");
        for (JBro::UInt32 id = 0; id < 8; ++id)
        {
            const JBro::Bool present = cache.Find(id) != nullptr;
            Check(present == (id % 2 == 0), "and exactly the seen ones remain");
        }
    }

    // 아무도 못 본 프레임이면 전부 지워진다. 비는 것이 맞는 답이다.
    void TestNothingSeenClearsTheCache()
    {
        JBro::Table<JBro::UInt32, Cached> cache;
        JBro::Array<JBro::UInt32> scratch;
        cache.FindOrAdd(1u).lastSeenFrame = 3;
        cache.FindOrAdd(2u).lastSeenFrame = 3;

        JBro::RemoveStaleEntries(cache, 4, scratch,
            [](const Cached& entry) { return entry.lastSeenFrame; });
        Check(cache.Size() == 0, "a frame that saw nothing empties the cache");
    }
}

JBro::Int32 RunCoreValueTypeTests()
{
    TestSizeDoesNotDivideByZero();
    TestSizeIsEmptyWhenEitherSideIsGone();
    TestExtentIsTheSameTypeAsSizeU();
    TestHasAllIsNotHasAny();
    TestFlagsCanBeTurnedOnAndOff();
    TestATouchingRectCounts();
    TestAMissedIntersectionComesBackEmpty();
    TestOneBadPointDoesNotPoisonTheBounds();
    TestRectsGrowAndMove();
    TestNoInsetsByDefault();
    TestAVectorIsNotAQuaternion();
    TestPointsAndDirectionsDifferByW();
    TestThePerspectiveDivideDoesNotMakeInfinities();
    TestVector4Arithmetic();
    TestOnlyWhatWasSeenThisFrameSurvives();
    TestNothingSeenClearsTheCache();
    std::cout << "Core value type tests passed.\n";
    return 0;
}
