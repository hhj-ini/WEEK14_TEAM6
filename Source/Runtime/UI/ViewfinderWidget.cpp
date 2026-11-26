#include "EnginePCH.h"
#include "UI/ViewfinderWidget.h"

#include "Asset/AssetManager.h"
#include "Camera/CameraModifier_Photo.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/PostProcessSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Render/PostProcess/ExposureMeterRenderer.h"
#include "Text/Font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	const FLinearColor MaskColor(0.01f, 0.01f, 0.01f, 1.0f);
	const FLinearColor PlateColor(0.08f, 0.08f, 0.08f, 1.0f);
	const FLinearColor LabelColor(0.82f, 0.82f, 0.80f, 1.0f);
	const FLinearColor FrameColor(0.95f, 0.93f, 0.88f, 0.85f);
	const FLinearColor NeedleColor(1.0f, 0.55f, 0.15f, 1.0f);
	const FLinearColor MeterColor(0.95f, 0.95f, 0.92f, 1.0f);
	const FLinearColor FlagColor(0.85f, 0.10f, 0.08f, 1.0f);
	const FLinearColor PatchColor(0.80f, 0.80f, 0.80f, 0.30f);

	const char* ShutterLabels[] = { "15", "30", "60", "125", "250", "500", "1000", "2000", "4000", "8000", "16000" };
	const char* ApertureLabels[] = { "1.4", "2", "2.8", "4", "5.6", "8", "11", "16", "22" };
	static_assert(sizeof(ShutterLabels) / sizeof(ShutterLabels[0]) == FPhotoCameraState::NumShutterStops);
	static_assert(sizeof(ApertureLabels) / sizeof(ApertureLabels[0]) == FPhotoCameraState::NumApertureStops);

	// 피벗이 눈금판에서 멀수록 바늘이 덜 기운다 (디자인 해상도 픽셀)
	constexpr float NeedlePivotDistance = 700.0f;

	FLinearColor WithOpacity(FLinearColor Color, float Opacity)
	{
		Color.A *= Opacity;
		return Color;
	}

	float LerpF(float A, float B, float T) { return A + (B - A) * T; }

	// 화면 좌표는 Y가 아래로 커지므로 90°가 아래, 270°가 위다
	void AddArc(FUIDrawList& DrawList, const FVector2& Center, float Radius, float StartDeg, float EndDeg,
		float Thickness, const FLinearColor& Color, int32 Segments = 8)
	{
		const float ToRad = 3.14159265f / 180.0f;
		FVector2 Prev = Center + FVector2(cosf(StartDeg * ToRad), sinf(StartDeg * ToRad)) * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Deg = LerpF(StartDeg, EndDeg, static_cast<float>(i) / Segments);
			const FVector2 Next = Center + FVector2(cosf(Deg * ToRad), sinf(Deg * ToRad)) * Radius;
			DrawList.AddLine(Prev, Next, Thickness, Color);
			Prev = Next;
		}
	}

	constexpr float ScaleNearDistance = 0.8f;   // 거리계 왼쪽 끝(m), LC-A 인물 아이콘 위치

	// 초점 링 눈금은 거리의 역수(디옵터)에 거의 비례한다. 그래서 가까운 쪽은 넓고 먼 쪽은 무한대 앞에 몰린다.
	// 눈금은 LC-A처럼 0.8m부터 그리고, 그보다 가까우면 바늘이 왼쪽 끝에 붙는다 (실제 거리는 글자로 보인다)
	float FocusDistanceToScale(float Distance)
	{
		const float Near = 1.0f / ScaleNearDistance;
		const float Far = 1.0f / FPhotoCameraState::MaxFocusDistance;
		return std::clamp((Near - 1.0f / std::max(Distance, 0.01f)) / (Near - Far), 0.0f, 1.0f);
	}

	// LC-A 거리 아이콘. Base는 아이콘 바닥 가운데, K는 크기 배율 (디자인 픽셀 기준 높이 약 20)
	void AddPersonIcon(FUIDrawList& DrawList, const FVector2& Base, float K, const FLinearColor& Color)
	{
		const float Th = 1.8f * K;
		AddArc(DrawList, Base + FVector2(0.0f, -15.0f) * K, 4.5f * K, 0.0f, 360.0f, Th, Color, 12);
		AddArc(DrawList, Base, 8.0f * K, 180.0f, 360.0f, Th, Color, 10);
		DrawList.AddLine(Base + FVector2(-8.0f, 0.0f) * K, Base + FVector2(8.0f, 0.0f) * K, Th, Color);
	}

	void AddMountainIcon(FUIDrawList& DrawList, const FVector2& Base, float K, const FLinearColor& Color)
	{
		const FVector2 P[] = {
			FVector2(-18.0f, 0.0f), FVector2(-7.0f, -17.0f), FVector2(1.0f, -7.0f),
			FVector2(8.0f, -14.0f), FVector2(18.0f, 0.0f) };
		for (int32 i = 0; i + 1 < 5; ++i)
			DrawList.AddLine(Base + P[i] * K, Base + P[i + 1] * K, 1.8f * K, Color);
	}

	// LC-A 거리 눈금: 0.8m 인물, 1.5m 두 사람, 3m 단체, 무한대 풍경
	void AddDistanceIcon(FUIDrawList& DrawList, int32 Zone, const FVector2& Base, float S, const FLinearColor& Color)
	{
		switch (Zone)
		{
		case 0:
			AddPersonIcon(DrawList, Base, S, Color);
			break;
		case 1:
			AddPersonIcon(DrawList, Base + FVector2(-6.0f, 0.0f) * S, 0.85f * S, Color);
			AddPersonIcon(DrawList, Base + FVector2(7.0f, 0.0f) * S, 0.7f * S, Color);
			break;
		case 2:
			for (int32 i = -1; i <= 1; ++i)
				AddPersonIcon(DrawList, Base + FVector2(i * 10.0f, 0.0f) * S, 0.6f * S, Color);
			break;
		default:
			AddMountainIcon(DrawList, Base, S, Color);
			break;
		}
	}

	// 조리개 눈금 칸마다 스톱 수가 달라도 맞도록 N^2의 log2를 눈금 칸 사이에서 보간한다.
	// 눈금 밖은 끝 칸의 기울기로 늘려서 노출계 바늘이 빨간 칸까지 넘어가게 한다
	float ApertureLog2N2ToScale(float Log2N2)
	{
		constexpr int32 Num = FPhotoCameraState::NumApertureStops;
		const auto StopLog = [](int32 i) { const float N = FPhotoCameraState::ApertureStops[i]; return std::log2(N * N); };
		const float Step = 1.0f / (Num - 1);

		if (Log2N2 <= StopLog(0))
			return (Log2N2 - StopLog(0)) / (StopLog(1) - StopLog(0)) * Step;
		for (int32 i = 1; i < Num; ++i)
			if (Log2N2 <= StopLog(i))
				return Step * ((i - 1) + (Log2N2 - StopLog(i - 1)) / (StopLog(i) - StopLog(i - 1)));
		return 1.0f + (Log2N2 - StopLog(Num - 1)) / (StopLog(Num - 1) - StopLog(Num - 2)) * Step;
	}

	constexpr float DistanceZones[] = { ScaleNearDistance, 1.5f, 3.0f, FPhotoCameraState::MaxFocusDistance };
}

void FViewfinderNeedle::Update(float Target, float DeltaTime)
{
	if (!bInitialized)
	{
		Position = Target;
		Velocity = 0.0f;
		bInitialized = true;
		return;
	}

	// 감쇠비를 1보다 작게 두어 실제 계기 바늘처럼 살짝 넘쳤다가 돌아오게 한다
	constexpr float Stiffness = 180.0f;
	constexpr float DampingRatio = 0.55f;
	const float Damping = 2.0f * DampingRatio * std::sqrt(Stiffness);
	// 프레임이 크게 튀면 스프링이 발산하므로 한 번에 진행하는 시간을 제한한다
	const float Dt = std::min(DeltaTime, 1.0f / 30.0f);

	Velocity += (Stiffness * (Target - Position) - Damping * Velocity) * Dt;
	Position += Velocity * Dt;
}

UViewfinderWidget::UViewfinderWidget()
{
	// HUD라서 클릭을 가로채지 않는다
	Visibility = static_cast<int32>(EUIVisibility::HitTestInvisible);
}

void UViewfinderWidget::NativeConstruct()
{
	Font = UAssetManager::GetAssetByPath<UFont>("Engine/Fonts/Pretendard.json");
}

UCameraModifier_Photo* UViewfinderWidget::FindPhotoModifier() const
{
	UWorld* World = GetOwningWorld();
	APlayerController* PC = World ? World->GetPlayerController() : nullptr;
	APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;
	if (!PCM) return nullptr;
	return static_cast<UCameraModifier_Photo*>(PCM->FindCameraModifierByClass(UCameraModifier_Photo::StaticClass()));
}

void UViewfinderWidget::NativeTick(float DeltaTime)
{
	const UCameraModifier_Photo* Photo = FindPhotoModifier();
	if (!Photo) return;

	const FPhotoCameraState& State = Photo->GetState();
	ShutterNeedle.Update(static_cast<float>(State.ShutterIndex) / (FPhotoCameraState::NumShutterStops - 1), DeltaTime);
	ApertureNeedle.Update(static_cast<float>(State.ApertureIndex) / (FPhotoCameraState::NumApertureStops - 1), DeltaTime);
	FocusNeedle.Update(FocusDistanceToScale(State.FocusDistance), DeltaTime);

	float AverageLuminance = 0.0f;
	bMeterValid = Photo->IsPhotoModeActive() && FExposureMeterRenderer::GetAverageLuminance(AverageLuminance);
	if (bMeterValid)
	{
		const float MeteredEV = FPostProcessSettings::GetMeteredEV100(AverageLuminance, FExposureMeterRenderer::GetDisplayGamma());
		const float ShutterSpeed = 1.0f / State.GetShutterSeconds();
		ExposureOffsetEV = MeteredEV - FPostProcessSettings::ComputeEV100(State.GetFstop(), ShutterSpeed, State.ISO);

		// EV100 = log2(N^2 * 셔터분모 * 100 / ISO)를 N^2에 대해 풀고, 조리개 눈금 위치로 바꾼다
		const float Log2N2 = MeteredEV - std::log2(ShutterSpeed * 100.0f / State.ISO);
		const float Pos = ApertureLog2N2ToScale(Log2N2);
		// 눈금을 벗어나면 빨간 칸에 머문다. 너무 멀리 보내면 돌아올 때 오래 걸리므로 조금만 넘긴다
		MeterNeedle.Update(std::clamp(Pos, -0.35f, 1.35f), DeltaTime);
	}
}

void UViewfinderWidget::PaintNeedle(FUIDrawList& DrawList, const FVector2& Pivot, const FVector2& Tip,
	float HalfLength, float Thickness, const FLinearColor& Color) const
{
	const FVector2 D = Tip - Pivot;
	const float Len = std::sqrt(D.X * D.X + D.Y * D.Y);
	if (Len <= 0.0f) return;
	const FVector2 Dir = D * (1.0f / Len);
	DrawList.AddLine(Tip - Dir * HalfLength, Tip + Dir * HalfLength, Thickness, Color);
}

void UViewfinderWidget::Paint(FUIDrawList& DrawList, float ParentOpacity) const
{
	if (!IsVisible()) return;
	const UCameraModifier_Photo* Photo = FindPhotoModifier();
	if (!Photo || Photo->GetAlpha() <= 0.0f) return;

	const float Opacity = ParentOpacity * RenderOpacity * Photo->GetAlpha();
	const FPhotoCameraState& State = Photo->GetState();
	const FUIRect& R = CachedRect;
	const float S = CachedScale;

	// ---------- 프레임과 창 ----------
	// 프레임 크기는 CameraModifier_Photo가 화각을 계산할 때와 같은 비율이어야 찍히는 범위가 맞는다
	const float FrameH = R.H * FPhotoCameraState::FrameHeightOnScreen;
	const float FrameW = FrameH * State.GetFilmAspect();
	const float CX = R.X + R.W * 0.5f;
	const float CY = R.Y + R.H * 0.5f;
	const float FL = CX - FrameW * 0.5f, FR = CX + FrameW * 0.5f;
	const float FT = CY - FrameH * 0.5f, FB = CY + FrameH * 0.5f;

	// 실제 거리계 카메라처럼 프레임선 바깥도 조금 보이게 창을 넓힌다
	const float Pad = 26.0f * S;
	const float WL = FL - Pad, WR = FR + Pad, WT = FT - Pad, WB = FB + Pad;
	// 아래 글자들은 3:2 프레임 폭에 고정해서, 하프 프레임으로 좁아져도 가운데 거리계와 겹치지 않게 한다
	const float BandL = CX - FrameH * FPhotoCameraState::FilmAspect * 0.5f - Pad;
	const float BandR = CX + FrameH * FPhotoCameraState::FilmAspect * 0.5f + Pad;

	const FLinearColor Mask = WithOpacity(MaskColor, Opacity);
	DrawList.AddRect({ R.X, R.Y, R.W, WT - R.Y }, Mask);
	DrawList.AddRect({ R.X, WB, R.W, R.Y + R.H - WB }, Mask);
	DrawList.AddRect({ R.X, WT, WL - R.X, WB - WT }, Mask);
	DrawList.AddRect({ WR, WT, R.X + R.W - WR, WB - WT }, Mask);

	// 모서리가 둥근 프레임선. 변 가운데는 비워 두어 피사체를 가리지 않는다 (LC-A·Elektra 스타일)
	{
		const FLinearColor Line = WithOpacity(FrameColor, Opacity);
		const float Th = 3.0f * S, Rad = 22.0f * S;
		const float SegW = FrameW * 0.3f, SegH = FrameH * 0.3f;

		AddArc(DrawList, FVector2(FL + Rad, FT + Rad), Rad, 180.0f, 270.0f, Th, Line);
		AddArc(DrawList, FVector2(FR - Rad, FT + Rad), Rad, 270.0f, 360.0f, Th, Line);
		AddArc(DrawList, FVector2(FR - Rad, FB - Rad), Rad, 0.0f, 90.0f, Th, Line);
		AddArc(DrawList, FVector2(FL + Rad, FB - Rad), Rad, 90.0f, 180.0f, Th, Line);

		DrawList.AddLine(FVector2(FL + Rad, FT), FVector2(FL + SegW, FT), Th, Line);
		DrawList.AddLine(FVector2(FR - Rad, FT), FVector2(FR - SegW, FT), Th, Line);
		DrawList.AddLine(FVector2(FL + Rad, FB), FVector2(FL + SegW, FB), Th, Line);
		DrawList.AddLine(FVector2(FR - Rad, FB), FVector2(FR - SegW, FB), Th, Line);
		DrawList.AddLine(FVector2(FL, FT + Rad), FVector2(FL, FT + SegH), Th, Line);
		DrawList.AddLine(FVector2(FL, FB - Rad), FVector2(FL, FB - SegH), Th, Line);
		DrawList.AddLine(FVector2(FR, FT + Rad), FVector2(FR, FT + SegH), Th, Line);
		DrawList.AddLine(FVector2(FR, FB - Rad), FVector2(FR, FB - SegH), Th, Line);
	}

	// 가운데 거리계 패치
	{
		const float PW = 84.0f * S, PH = 58.0f * S;
		DrawList.AddRect({ CX - PW * 0.5f, CY - PH * 0.5f, PW, PH }, WithOpacity(PatchColor, Opacity));
	}

	const FLinearColor Label = WithOpacity(LabelColor, Opacity);
	const FLinearColor Needle = WithOpacity(NeedleColor, Opacity);

	// ---------- 위: 셔터 눈금 (35RC) ----------
	{
		// 눈금이 11칸이라 네·다섯 자리 숫자끼리 붙지 않게 거리계보다 넓게 둔다
		const float PW = 960.0f * S, PH = 40.0f * S;
		const float PL = CX - PW * 0.5f;
		const float PCY = R.Y + (WT - R.Y) * 0.5f;
		const float PT = PCY - PH * 0.5f;
		DrawList.AddRect({ PL, PT, PW, PH }, WithOpacity(PlateColor, Opacity));

		const float Inset = 44.0f * S;
		const float X0 = PL + Inset, X1 = PL + PW - Inset;
		for (int32 i = 0; i < FPhotoCameraState::NumShutterStops; ++i)
		{
			const float X = LerpF(X0, X1, static_cast<float>(i) / (FPhotoCameraState::NumShutterStops - 1));
			if (Font)
				DrawList.AddText({ X - 40.0f * S, PT, 80.0f * S, PH }, ShutterLabels[i], *Font, 22.0f * S, Label, 1);
		}

		// 피벗이 아래에 있어서 바늘이 양 끝으로 갈수록 기운다
		const FVector2 Tip(LerpF(X0, X1, ShutterNeedle.Position), PCY);
		const FVector2 Pivot(CX, PCY + NeedlePivotDistance * S);
		PaintNeedle(DrawList, Pivot, Tip, PH * 0.5f + 6.0f * S, 2.5f * S, Needle);
	}

	// ---------- 오른쪽: 조리개 눈금 (QL17) ----------
	{
		const float PW = 64.0f * S, PH = FrameH * 0.72f;
		const float PCX = WR + 60.0f * S;
		const float PL = PCX - PW * 0.5f, PT = CY - PH * 0.5f;
		DrawList.AddRect({ PL, PT, PW, PH }, WithOpacity(PlateColor, Opacity));

		// 위아래 빨간 칸은 노출계(5단계)가 범위를 벗어났을 때 바늘이 가리키는 자리다
		const float FlagH = 26.0f * S;
		const FLinearColor Flag = WithOpacity(FlagColor, Opacity);
		DrawList.AddRect({ PL, PT, PW, FlagH }, Flag);
		DrawList.AddRect({ PL, PT + PH - FlagH, PW, FlagH }, Flag);

		const float Y0 = PT + FlagH + 24.0f * S, Y1 = PT + PH - FlagH - 24.0f * S;
		for (int32 i = 0; i < FPhotoCameraState::NumApertureStops; ++i)
		{
			const float Y = LerpF(Y0, Y1, static_cast<float>(i) / (FPhotoCameraState::NumApertureStops - 1));
			if (Font)
				DrawList.AddText({ PL, Y - 15.0f * S, PW, 30.0f * S }, ApertureLabels[i], *Font, 22.0f * S, Label, 1);
		}

		const FVector2 Pivot(PCX - NeedlePivotDistance * S, CY);
		if (bMeterValid)
		{
			// 노출계 바늘(흰색)에 설정 바늘(주황)을 겹치면 적정 노출이다
			const float MeterY = std::clamp(LerpF(Y0, Y1, MeterNeedle.Position), PT + FlagH * 0.5f, PT + PH - FlagH * 0.5f);
			PaintNeedle(DrawList, Pivot, FVector2(PCX, MeterY), PW * 0.5f + 10.0f * S, 2.0f * S, WithOpacity(MeterColor, Opacity));

			if (Font)
			{
				char Buffer[16];
				// AE일 때는 카메라가 알아서 맞추므로 내 설정과의 차이 대신 모드를 보여 준다
				if (State.bAutoExposure)
					std::snprintf(Buffer, sizeof(Buffer), "AE");
				else
					std::snprintf(Buffer, sizeof(Buffer), "%+.1f EV", ExposureOffsetEV);
				DrawList.AddText({ PL - 30.0f * S, PT + PH + 6.0f * S, PW + 60.0f * S, 28.0f * S }, Buffer, *Font, 18.0f * S, Label, 1);
			}
		}

		const FVector2 Tip(PCX, LerpF(Y0, Y1, ApertureNeedle.Position));
		PaintNeedle(DrawList, Pivot, Tip, PW * 0.5f + 6.0f * S, 2.5f * S, Needle);
	}

	// ---------- 아래: 거리계 (LC-A) ----------
	{
		const float PW = 560.0f * S, PH = 44.0f * S;
		const float PL = CX - PW * 0.5f;
		const float PCY = WB + (R.Y + R.H - WB) * 0.5f;
		const float PT = PCY - PH * 0.5f;
		DrawList.AddRect({ PL, PT, PW, PH }, WithOpacity(PlateColor, Opacity));

		const float Inset = 40.0f * S;
		const float X0 = PL + Inset, X1 = PL + PW - Inset;
		const float IconBase = PT + PH - 10.0f * S;
		for (int32 Zone = 0; Zone < 4; ++Zone)
		{
			const float X = LerpF(X0, X1, FocusDistanceToScale(DistanceZones[Zone]));
			AddDistanceIcon(DrawList, Zone, FVector2(X, IconBase), S, Label);
		}

		// LC-A 바늘은 기울지 않고 눈금판을 세로로 가로지른다
		const float NX = LerpF(X0, X1, FocusNeedle.Position);
		DrawList.AddLine(FVector2(NX, PT - 4.0f * S), FVector2(NX, PT + PH + 4.0f * S), 2.5f * S, Needle);

		if (Font)
		{
			const bool bInfinity = State.FocusDistance >= FPhotoCameraState::MaxFocusDistance * 0.99f;
			char Buffer[16];
			std::snprintf(Buffer, sizeof(Buffer), "%.1fm", State.FocusDistance);
			DrawList.AddText({ PL + PW + 16.0f * S, PT, 120.0f * S, PH }, bInfinity ? FString("INF") : FString(Buffer),
				*Font, 20.0f * S, Label, 0);
			// 휠을 돌리면 MF로 바뀌므로 지금 누가 초점을 잡는지 보여 준다
			DrawList.AddText({ PL - 136.0f * S, PT, 120.0f * S, PH }, State.bAutoFocus ? FString("AF") : FString("MF"),
				*Font, 20.0f * S, Label, 2);
		}
	}

	// ---------- 아래 오른쪽: 필름 카운터 ----------
	if (Font)
	{
		const FString Counter = "No. " + std::to_string(Photo->GetShotCount());
		const float BandH = R.Y + R.H - WB;
		DrawList.AddText({ BandR - 300.0f * S, WB, 300.0f * S, BandH }, Counter, *Font, 20.0f * S, Label, 2);
	}

	// ---------- 셔터 막 ----------
	// 화면 저장은 UI보다 먼저 일어나므로 이 검은 막은 사진에 찍히지 않는다
	{
		constexpr float ClosedTime = 0.05f;
		constexpr float OpenTime = 0.15f;
		const float T = Photo->GetTimeSinceShutter();
		const float Blackout = T < ClosedTime ? 1.0f : std::clamp(1.0f - (T - ClosedTime) / OpenTime, 0.0f, 1.0f);
		if (Blackout > 0.0f)
			DrawList.AddRect({ WL, WT, WR - WL, WB - WT }, WithOpacity(MaskColor, Opacity * Blackout));
	}

	// ---------- 아래 왼쪽: 필름·렌즈 정보 ----------
	if (Font)
	{
		const FString Info = "ISO " + std::to_string(static_cast<int32>(State.ISO)) + "   " +
			std::to_string(static_cast<int32>(std::lround(State.FocalLength))) + "mm" + (State.bHalfFrame ? "   HALF" : "");
		const float BandH = R.Y + R.H - WB;
		DrawList.AddText({ BandL, WB, 300.0f * S, BandH }, Info, *Font, 20.0f * S, Label, 0);
	}
}
