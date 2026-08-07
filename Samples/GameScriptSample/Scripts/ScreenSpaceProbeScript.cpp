#include "pch.h"
#include "Scripts/ScreenSpaceProbeScript.h"

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Canvas/CanvasViewProjection.h"
#include "GameFramework/Component/Camera2D.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	std::string Format(float value)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "%.4f", value);
		return std::string(buffer);
	}

	void ProbeLog(const std::string& message)
	{
		if (Script.Debug)
		{
			Script.Debug->Log("[ScreenSpaceProbe] " + message);
		}
	}

	Vector2 WorldPositionOf(const CGameObject& object)
	{
		const Matrix3x2& matrix = object.GetWorld().Matrix;
		return Vector2(matrix.Dx, matrix.Dy);
	}
}

void CScreenSpaceProbeScript::Check(bool condition, const char* label)
{
	if (condition)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label);
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label);
}

void CScreenSpaceProbeScript::CheckNear(float actual, float expected, float tolerance, const char* label)
{
	if (std::abs(actual - expected) <= tolerance)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   " + Format(actual));
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label + "   expected " + Format(expected)
		+ " (+-" + Format(tolerance) + "), got " + Format(actual));
}

void CScreenSpaceProbeScript::CheckNearVector(const Vector2& actual, const Vector2& expected, float tolerance, const char* label)
{
	if (std::abs(actual.x - expected.x) <= tolerance && std::abs(actual.y - expected.y) <= tolerance)
	{
		++m_passCount;
		ProbeLog(std::string("PASS  ") + label + "   (" + Format(actual.x) + ", " + Format(actual.y) + ")");
		return;
	}
	++m_failCount;
	ProbeLog(std::string("FAIL  ") + label
		+ "   expected (" + Format(expected.x) + ", " + Format(expected.y) + ")"
		+ ", got (" + Format(actual.x) + ", " + Format(actual.y) + ")");
}

void CScreenSpaceProbeScript::CheckExtents(float actualHalfW, float actualHalfH,
                                           float expectedHalfW, float expectedHalfH, const char* label)
{
	CheckNearVector(Vector2(actualHalfW, actualHalfH), Vector2(expectedHalfW, expectedHalfH), 0.001f, label);
}

void CScreenSpaceProbeScript::TearDown()
{
	SafePtr<CGameCanvas> canvas = GetCanvas();
	if (false == canvas.IsValid())
	{
		return;
	}
	for (SafePtr<CGameObject>& objectRef : m_spawned)
	{
		if (CGameObject* object = objectRef.TryGet())
		{
			canvas->DestroyGameObject(object);
		}
	}
	m_spawned.clear();
	if (CGameLayer* layer = m_screenLayer.TryGet())
	{
		canvas->DestroyLayer(layer);
	}
	m_screenLayer = nullptr;
}

void CScreenSpaceProbeScript::OnStart()
{
	if (m_started)
	{
		return;
	}
	m_started = true;
	StartCoroutine(RunProbe());
}

Coroutine CScreenSpaceProbeScript::RunProbe()
{
	ProbeLog("START");

	SafePtr<CGameCanvas> canvas = GetCanvas();

	// 렌더 크기와 화면 공간 기준값은 호스트가 **렌더 경로에서** 주입한다(Application 의
	// ConfigureRuntimeViewCamera / 에디터 게임뷰). OnStart 는 그보다 먼저 도는 첫 프레임이라
	// 그때 읽으면 전부 0 이다. 한 프레임 넘기고 본다.
	co_await Wait::Frames(1);

	const float renderWidth  = canvas->GetLastRenderWidth();
	const float renderHeight = canvas->GetLastRenderHeight();
	const float referenceWidth  = canvas->GetScreenReferenceWidth();
	const float referenceHeight = canvas->GetScreenReferenceHeight();
	const float pixelsPerUnit   = canvas->GetScreenPixelsPerUnit();

	Check(renderWidth >= 1.0f && renderHeight >= 1.0f, "render surface size is known");
	Check(pixelsPerUnit > 0.0f, "pixels per unit was injected");
	Check(referenceWidth >= 1.0f && referenceHeight >= 1.0f, "reference resolution was injected");
	if (renderWidth < 1.0f || renderHeight < 1.0f || pixelsPerUnit <= 0.0f)
	{
		ProbeLog("ABORT — no render surface; the host never injected screen-space reference values.");
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		co_return;
	}

	// ── 1) 스케일 모드 계산 ──────────────────────────────────────────────────
	// 창을 리사이즈할 수 없으니 가상의 렉트 크기를 넣어 계산만 본다.
	// 기대값은 구현을 부르지 않고 손으로 세운다 — 자기 자신과 비교하면 아무것도 검증되지 않는다.
	const ScreenSpaceReference reference =
		ScreenSpaceReference::FromResolution(referenceWidth, referenceHeight, pixelsPerUnit);
	const float refHalfW = referenceWidth  / pixelsPerUnit * 0.5f;
	const float refHalfH = referenceHeight / pixelsPerUnit * 0.5f;
	CheckNearVector(Vector2(reference.HalfWidth, reference.HalfHeight),
		Vector2(refHalfW, refHalfH), 0.001f, "reference rect derives from resolution / ppu");

	{
		// (a) 기준과 같은 종횡비 — 네 모드가 전부 같은 값이어야 한다.
		const float rectW = referenceWidth;
		const float rectH = referenceHeight;
		float halfW = 0.0f;
		float halfH = 0.0f;

		ComputeScreenSpaceExtents(EScreenScaleMode::FixedHeight, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH, "reference aspect / fixed height");
		ComputeScreenSpaceExtents(EScreenScaleMode::FixedWidth, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH, "reference aspect / fixed width");
		ComputeScreenSpaceExtents(EScreenScaleMode::Contain, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH, "reference aspect / contain");
		ComputeScreenSpaceExtents(EScreenScaleMode::ConstantPixel, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH, "reference aspect / constant pixel");
	}

	// 기대식을 **기준 렉트의 종횡비 대비**로 세운다. 프로젝트 해상도가 가로형이냐 세로형이냐에
	// 따라 "정사각 표면"이 기준보다 넓을 수도 좁을 수도 있어서, 절대 모양을 가정하면 프로젝트가
	// 바뀌는 순간 프로브가 거짓 실패를 낸다(실제로 여기서 한 번 걸렸다).
	const float referenceAspect = refHalfW / refHalfH;

	{
		// (b) 기준보다 좁은 표면(종횡비 절반). 손 계산:
		//     FixedHeight  → halfH=refHalfH 유지, halfW=refHalfW/2 → 가로가 잘린다.
		//     FixedWidth   → halfW=refHalfW 유지, halfH=refHalfH*2 → 세로가 남는다.
		//     Contain      → max(refHalfH, refHalfH*2)=refHalfH*2 → FixedWidth 와 같아진다.
		const float rectH = referenceHeight;
		const float rectW = rectH * referenceAspect * 0.5f;
		float halfW = 0.0f;
		float halfH = 0.0f;

		ComputeScreenSpaceExtents(EScreenScaleMode::FixedHeight, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW * 0.5f, refHalfH, "narrow surface / fixed height keeps height, halves width");
		Check(halfW < refHalfW - 0.001f, "narrow surface / fixed height clips the reference width");

		ComputeScreenSpaceExtents(EScreenScaleMode::FixedWidth, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH * 2.0f, "narrow surface / fixed width keeps width");

		ComputeScreenSpaceExtents(EScreenScaleMode::Contain, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW, refHalfH * 2.0f, "narrow surface / contain lands on fixed width");
		Check(halfW >= refHalfW - 0.001f && halfH >= refHalfH - 0.001f,
			"narrow surface / contain never clips the reference rect");

		ComputeScreenSpaceExtents(EScreenScaleMode::ConstantPixel, reference, rectW, rectH, halfW, halfH);
		CheckNear(halfH, rectH / pixelsPerUnit * 0.5f, 0.001f, "narrow surface / constant pixel keeps unit size");
	}

	{
		// (c) 기준보다 넓은 표면(종횡비 2배) — 이번엔 Contain 이 FixedHeight 쪽으로 붙는다.
		const float rectH = referenceHeight;
		const float rectW = rectH * referenceAspect * 2.0f;
		float halfW = 0.0f;
		float halfH = 0.0f;

		ComputeScreenSpaceExtents(EScreenScaleMode::Contain, reference, rectW, rectH, halfW, halfH);
		CheckExtents(halfW, halfH, refHalfW * 2.0f, refHalfH, "wide surface / contain lands on fixed height");
		Check(halfW > refHalfW + 0.001f, "wide surface / contain gains width (no clipping)");

		ComputeScreenSpaceExtents(EScreenScaleMode::FixedWidth, reference, rectW, rectH, halfW, halfH);
		Check(halfH < refHalfH - 0.001f, "wide surface / fixed width clips the reference height");
	}

	// ── 2) 화면 공간 레이어와 오브젝트 ───────────────────────────────────────
	CGameLayer* screenLayer = canvas->CreateLayer("ScreenSpaceProbe.UI");
	Check(nullptr != screenLayer, "screen layer created");
	if (nullptr == screenLayer)
	{
		ProbeLog("RESULT " + std::to_string(m_passCount) + "/"
			+ std::to_string(m_passCount + m_failCount) + " PASS");
		co_return;
	}
	m_screenLayer = screenLayer->SafeFromThis();
	screenLayer->Space = ELayerSpace::Screen;
	screenLayer->ScaleMode = EScreenScaleMode::FixedHeight;

	auto spawnOnScreenLayer = [&](const char* name, const Vector2& anchor, const Vector2& offset) -> CGameObject*
	{
		CGameObject* object = canvas->CreateGameObject(name);
		if (nullptr == object)
		{
			return nullptr;
		}
		object->Local.Anchor = anchor;
		object->Local.Position = offset;
		canvas->MoveObjectToLayer(*object, *screenLayer);
		m_spawned.push_back(object->SafeFromThis());
		return object;
	};

	CGameObject* centered   = spawnOnScreenLayer("UI.Centered",    Vector2(0.5f, 0.5f), Vector2(0.0f, 0.0f));
	CGameObject* bottomLeft = spawnOnScreenLayer("UI.BottomLeft",  Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f));
	CGameObject* topRight   = spawnOnScreenLayer("UI.TopRight",    Vector2(1.0f, 1.0f), Vector2(-1.0f, -0.5f));

	// 앵커된 루트의 자식 — 부모를 따라와야 한다(자식에 앵커를 또 적용하면 이중이다).
	CGameObject* child = canvas->CreateGameObject("UI.TopRight.Child");
	child->Local.Position = Vector2(0.25f, 0.25f);
	child->SetParent(*topRight);
	m_spawned.push_back(child->SafeFromThis());

	// 대조군 — 월드 레이어에 그대로 둔다.
	CGameObject* worldMarker = canvas->CreateGameObject("World.Marker");
	worldMarker->Local.Position = Vector2(0.0f, 0.0f);
	m_spawned.push_back(worldMarker->SafeFromThis());

	// 시스템은 스크립트보다 나중에 돈다 — 월드 행렬이 채워지려면 한 프레임 넘겨야 한다.
	co_await Wait::Frames(1);

	float screenHalfW = 0.0f;
	float screenHalfH = 0.0f;
	ComputeScreenSpaceExtents(EScreenScaleMode::FixedHeight, reference,
		renderWidth, renderHeight, screenHalfW, screenHalfH);

	CheckNearVector(WorldPositionOf(*centered), Vector2(0.0f, 0.0f), 0.001f,
		"center anchor resolves to the origin");
	CheckNearVector(WorldPositionOf(*bottomLeft), Vector2(-screenHalfW, -screenHalfH), 0.001f,
		"bottom-left anchor resolves to the rect corner");
	CheckNearVector(WorldPositionOf(*topRight), Vector2(screenHalfW - 1.0f, screenHalfH - 0.5f), 0.001f,
		"top-right anchor plus offset");
	CheckNearVector(WorldPositionOf(*child), Vector2(screenHalfW - 0.75f, screenHalfH - 0.25f), 0.001f,
		"child rides the anchored parent exactly once");

	// ── 3) 역투영 — 카메라를 흔들어도 UI 좌표는 불변 ─────────────────────────
	Vector2 uiCenterBefore(0.0f, 0.0f);
	Vector2 worldCenterBefore(0.0f, 0.0f);
	Check(canvas->ScreenToUI(renderWidth * 0.5f, renderHeight * 0.5f, uiCenterBefore),
		"ScreenToUI resolves through the topmost screen layer");
	Check(canvas->ScreenToWorld(renderWidth * 0.5f, renderHeight * 0.5f, worldCenterBefore),
		"ScreenToWorld resolves (control group)");
	CheckNearVector(uiCenterBefore, Vector2(0.0f, 0.0f), 0.01f, "screen center maps to UI origin");

	Vector2 uiCornerBefore(0.0f, 0.0f);
	canvas->ScreenToUI(0.0f, 0.0f, uiCornerBefore);
	CheckNearVector(uiCornerBefore, Vector2(-screenHalfW, screenHalfH), 0.01f,
		"top-left pixel maps to the UI rect corner");

	// 카메라를 옮기고 줌하고 돌린다.
	CGameObject* cameraOwner = nullptr;
	Camera2D* camera = nullptr;
	canvas->ForEach<Camera2D>([&](Camera2D& found)
	{
		if (nullptr != camera)
		{
			return;
		}
		if (CGameObject* owner = found.GetOwner().TryGet())
		{
			camera = &found;
			cameraOwner = owner;
		}
	});
	Check(nullptr != camera, "a camera exists to disturb");

	if (nullptr != camera && nullptr != cameraOwner)
	{
		cameraOwner->Local.Position = cameraOwner->Local.Position + Vector2(37.0f, -21.0f);
		cameraOwner->Local.RotationRadians = Radian(cameraOwner->Local.RotationRadians.Value + 0.6f);
		camera->OrthographicSize = camera->OrthographicSize * 0.5f;

		co_await Wait::Frames(1);

		Vector2 uiCenterAfter(0.0f, 0.0f);
		Vector2 worldCenterAfter(0.0f, 0.0f);
		canvas->ScreenToUI(renderWidth * 0.5f, renderHeight * 0.5f, uiCenterAfter);
		canvas->ScreenToWorld(renderWidth * 0.5f, renderHeight * 0.5f, worldCenterAfter);

		// 대조군 먼저 — 카메라가 실제로 움직였다는 증거가 없으면 아래 검사는 공허하다.
		Check((worldCenterAfter - worldCenterBefore).Length() > 1.0f,
			"control: the camera really moved (world unprojection shifted)");

		CheckNearVector(uiCenterAfter, uiCenterBefore, 0.001f,
			"UI unprojection is unchanged by camera move / zoom / rotation");

		Vector2 uiCornerAfter(0.0f, 0.0f);
		canvas->ScreenToUI(0.0f, 0.0f, uiCornerAfter);
		CheckNearVector(uiCornerAfter, uiCornerBefore, 0.001f,
			"UI rect corner is unchanged by the camera");

		CheckNearVector(WorldPositionOf(*centered), Vector2(0.0f, 0.0f), 0.001f,
			"anchored object stays put while the camera moves");
	}

	// ── 4) 안전영역 — 인셋 0 이면 켜도 결과가 같아야 한다 ────────────────────
	const Vector2 bottomLeftBeforeSafeArea = WorldPositionOf(*bottomLeft);
	screenLayer->AnchorToSafeArea = true;
	co_await Wait::Frames(1);
	CheckNearVector(WorldPositionOf(*bottomLeft), bottomLeftBeforeSafeArea, 0.001f,
		"safe-area anchoring is a no-op while every inset is zero");
	screenLayer->AnchorToSafeArea = false;

	// ── 5) 월드 레이어는 앵커를 무시한다 ─────────────────────────────────────
	worldMarker->Local.Anchor = Vector2(1.0f, 1.0f);
	co_await Wait::Frames(1);
	CheckNearVector(WorldPositionOf(*worldMarker), Vector2(0.0f, 0.0f), 0.001f,
		"world layer ignores Anchor entirely");

	TearDown();

	const int total = m_passCount + m_failCount;
	ProbeLog("RESULT " + std::to_string(m_passCount) + "/" + std::to_string(total) + " PASS");
	if (m_failCount > 0)
	{
		ProbeLog("FAILED " + std::to_string(m_failCount));
	}
	co_return;
}
