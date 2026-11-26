#pragma once

#include "Camera/CameraModifier.h"
#include "CameraModifier_Photo.generated.h"

// 필름 카메라 한 대의 조작 값. 조리개·셔터는 실제 카메라처럼 정해진 눈금에서만 고른다.
struct FPhotoCameraState
{
	static constexpr float ApertureStops[] = { 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f };
	// 초 단위. 뷰파인더 눈금(15 ~ 16000)과 같은 순서
	static constexpr float ShutterStops[] = { 1.0f / 15, 1.0f / 30, 1.0f / 60, 1.0f / 125, 1.0f / 250, 1.0f / 500,
		1.0f / 1000, 1.0f / 2000, 1.0f / 4000, 1.0f / 8000, 1.0f / 16000 };
	static constexpr int32 NumApertureStops = sizeof(ApertureStops) / sizeof(float);
	static constexpr int32 NumShutterStops = sizeof(ShutterStops) / sizeof(float);

	static constexpr float MinFocusDistance = 0.2f;   // 꽃·소품을 가까이 찍을 수 있게 LC-A(0.8m)보다 줄였다
	static constexpr float MaxFocusDistance = 100.0f; // 이보다 멀면 무한대로 본다
	// 35mm 필름 한 컷은 36x24mm(3:2), 하프 프레임은 그 반인 18x24mm(3:4 세로). 뷰파인더 프레임이 화면 높이의 이만큼을 차지한다
	static constexpr float FilmHeight = 24.0f;
	static constexpr float FilmAspect = 1.5f;
	static constexpr float HalfFrameAspect = 0.75f;
	static constexpr float FrameHeightOnScreen = 0.8f;
	static constexpr float MinFocalLength = 24.0f;
	static constexpr float MaxFocalLength = 135.0f;

	// 처음 들었을 때 조리개를 최대로 열어 배경을 날리고, 아주 빠른 셔터로 밝기를 잡는다
	int32 ApertureIndex = 0;      // f/1.4
	int32 ShutterIndex = 9;       // 1/8000
	float ISO = 100.0f;
	float FocusDistance = 5.0f;   // m
	float FocalLength = 35.0f;    // mm
	bool bHalfFrame = false;      // 켜면 한 컷을 반으로 나눈 세로 프레임으로 찍는다
	bool bAutoFocus = true;       // 켜면 화면 중앙 물체에 초점을 맞춘다
	bool bAutoExposure = true;  // 켜면 셔터·ISO 대신 노출계가 밝기를 맞춘다. 조리개는 DOF에만 남는다

	float GetFstop() const { return ApertureStops[ApertureIndex]; }
	float GetShutterSeconds() const { return ShutterStops[ShutterIndex]; }
	float GetFilmAspect() const { return bHalfFrame ? HalfFrameAspect : FilmAspect; }
};

UCLASS()
// 사진 모드 동안 렌즈(화각)와 DOF 값을 POV에 덮어쓴다.
// 상태를 카메라 컴포넌트가 아니라 PCM 쪽에 두어서, 어떤 뷰 타깃을 보든 같은 카메라로 찍는다.
class UCameraModifier_Photo : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCameraModifier_Photo();

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	bool IsPhotoModeActive() const { return !IsDisabled() && !IsPendingDisable(); }
	void TogglePhotoMode();

	void StepAperture(int32 Delta);
	void StepShutter(int32 Delta);
	// 휠 한 칸(=120)을 Notches 1로 받는다
	void AdjustFocus(float Notches);
	void ToggleAutoFocus() { State.bAutoFocus = !State.bAutoFocus; }
	void ToggleAutoExposure() { State.bAutoExposure = !State.bAutoExposure; }
	void ToggleHalfFrame() { State.bHalfFrame = !State.bHalfFrame; }
	void AdjustZoom(float DeltaMm);
	// 셔터를 끊는다. 실제 저장은 FPhotoCapture가 다음 화면에서 한다
	void TriggerShutter();

	// 자유 카메라 입력. Move는 (앞, 오른쪽, 위) -1~1 축 값이고 이번 프레임 값으로 덮어쓴다.
	// Yaw/Pitch는 도 단위 회전량이라 여러 번 불려도 쌓인다
	void AddFreeCameraInput(const FVector& Move, float YawDelta, float PitchDelta, bool bFast);

	// 마지막 셔터 뒤로 지난 시간. 뷰파인더가 셔터 막이 닫히는 연출에 쓴다
	float GetTimeSinceShutter() const { return TimeSinceShutter; }
	int32 GetShotCount() const { return ShotCount; }

	const FPhotoCameraState& GetState() const { return State; }

private:
	// 화면 중앙으로 레이를 쏴서 맞은 물체까지의 거리(m)를 돌려준다
	float TraceFocusDistance(const FMinimalViewInfo& POV) const;
	// 지금 조리개·ISO에서 이 EV100이 나오는 셔터 눈금 중 가장 가까운 것
	int32 FindShutterIndexForEV100(float EV100) const;

	FPhotoCameraState State;
	// 레이캐스트로 잰 목표 초점. State.FocusDistance가 이 값을 따라간다 (Step 2)
	float AutoFocusTarget = 5.0f;
	float TimeSinceShutter = 1000.0f;
	int32 ShotCount = 0;

	// 사진 모드에 들어간 순간의 POV에서 출발하는 자유 카메라 (캐릭터의 ControlRotation과는 따로 논다)
	FVector FreeCamLocation;
	FRotator FreeCamRotation;
	bool bFreeCamValid = false;
	FVector PendingMove;
	float PendingYaw = 0.0f;
	float PendingPitch = 0.0f;
	bool bPendingFast = false;
};
