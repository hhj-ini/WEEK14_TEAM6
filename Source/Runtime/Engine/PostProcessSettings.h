#pragma once

#include "Core/Types.h"
#include "UObject/Class.h"
#include "PostProcessSettings.generated.h"

// 카메라가 화면 후처리에 넘기는 값. 값마다 bOverride_ 플래그가 있어서,
// 켜진 값만 아래(기본값이나 앞서 섞인 값) 위에 덮어쓴다.
USTRUCT()
struct FPostProcessSettings
{
	GENERATED_BODY()

	// ---------- Vignette ----------
	UPROPERTY(EditAnywhere)
	bool bOverride_VignetteIntensity = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float VignetteIntensity = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensVignetteAmount = false;
	// 렌즈 경통 깊이(k). 0이면 자연·광학 비네팅을 둘 다 끈다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=4, Delta=0.01))
	float LensVignetteAmount = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensMaxAperture = false;
	// 렌즈 최대 개방 F-Stop. 이 값에서 동공이 경통과 같은 크기가 된다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.5, ClampMax=32, Delta=0.1))
	float LensMaxAperture = 1.4f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensBarrelRadius = false;
	// 최대 개방 동공 반지름을 1로 둔 경통 반지름. 1보다 클수록 중앙부가 개방에서도 잘리지 않는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=3, Delta=0.01))
	float LensBarrelRadius = 1.2f;

	// ---------- Depth of Field ----------
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFocalDistance = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float DepthOfFieldFocalDistance = 10.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFocusRange = false;
	// 셰이더에서 분모로 쓰이므로 0이 되지 않게 막는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01))
	float DepthOfFieldFocusRange = 5.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldFstop = false;
	// 렌즈의 F-Stop(N). 조리개 지름 = 초점 거리 / N
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=32, Delta=0.1))
	float DepthOfFieldFstop = 2.8f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldSensorWidth = false;
	// 센서 가로 폭(mm). 36 = 풀프레임
	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	float DepthOfFieldSensorWidth = 36.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_DepthOfFieldBladeCount = false;
	// 조리개 날 수. 보케가 이 수만큼의 다각형이 된다
	UPROPERTY(EditAnywhere, meta=(ClampMin=4, ClampMax=16))
	int32 DepthOfFieldBladeCount = 5;

	// ---------- Lens Flares (이미지 기반) ----------
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareIntensity = false;
	// 고스트를 장면에 더할 때 곱하는 세기. 0이면 꺼진 것과 같다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=16, Delta=0.01))
	float LensFlareIntensity = 0.15f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareThreshold = false;
	// 노출 전 HDR 밝기가 이 값을 넘는 부분만 플레어를 만든다. 씬 라이트가 물리 단위가 아니라 UE 기본값(8)이면 거의 아무것도 안 넘는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=64, Delta=0.1))
	float LensFlareThreshold = 2.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareGhostCount = false;
	// 광원 반대편 직선 위에 줄지어 생기는 고스트 개수
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=16))
	int32 LensFlareGhostCount = 6;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareGhostDispersal = false;
	// 고스트가 퍼지는 정도. 0.25에서 셰이더 배율표 그대로이고, 클수록 고스트가 바깥으로 퍼지며 커진다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float LensFlareGhostDispersal = 0.25f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareHaloWidth = false;
	// 할로 링의 반지름(UV). 광원 위치와 상관없이 링 크기는 이 값으로 고정된다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float LensFlareHaloWidth = 0.05f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareHaloIntensity = false;
	// 할로 세기
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=2, Delta=0.01))
	float LensFlareHaloIntensity = 1.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareBokehSize = false;
	// 고스트 원판 반경(반해상도 px). 광원 점을 조리개 모양(DepthOfFieldBladeCount 각형)으로 번지게 하는 크기
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=64, Delta=0.1))
	float LensFlareBokehSize = 16.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareChromaticDistortion = false;
	// R/G/B를 읽는 위치를 벌리는 정도(UV). 0이면 색수차가 없다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=0.1, Delta=0.001))
	float LensFlareChromaticDistortion = 0.004f;
	UPROPERTY(EditAnywhere)
	bool bOverride_LensFlareDirtIntensity = false;
	// 렌즈 먼지 텍스처가 플레어에 곱해지는 세기. 0이면 먼지 없이 플레어 그대로
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=8, Delta=0.01))
	float LensFlareDirtIntensity = 1.0f;

	// ---------- Camera (물리 노출) ----------
	// 조리개는 DepthOfFieldFstop을 같이 쓴다 (UE도 노출과 DOF가 같은 F-Stop을 본다)
	UPROPERTY(EditAnywhere)
	bool bOverride_CameraShutterSpeed = false;
	// 셔터 속도의 분모(1/s). 125면 1/125초
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, ClampMax=8000))
	float CameraShutterSpeed = 60.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_CameraISO = false;
	UPROPERTY(EditAnywhere, meta=(ClampMin=25, ClampMax=6400))
	float CameraISO = 100.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_bApplyPhysicalCameraExposure = false;
	// 켜면 F-Stop·셔터·ISO로 노출 배율을 정한다. 끄면 배율 1 (UE의 Apply Physical Camera Exposure)
	UPROPERTY(EditAnywhere)
	bool bApplyPhysicalCameraExposure = false;

	// ---------- Auto Exposure (눈 적응) ----------
	// 켜면 카메라 조리개·셔터·ISO 대신 노출계가 잰 장면 밝기를 천천히 따라간다 (UE의 AutoExposureMethod != Manual)
	UPROPERTY(EditAnywhere)
	bool bOverride_bAutoExposure = false;
	// UE처럼 기본으로 켜 두고, 뷰포트는 Eye Adaptation Show Flag로, 카메라는 bOverride_로 끈다
	UPROPERTY(EditAnywhere)
	bool bAutoExposure = true;
	UPROPERTY(EditAnywhere)
	bool bOverride_AutoExposureMinEV100 = false;
	// 이보다 더 밝히지 않는다. 아주 어두운 곳이 회색으로 떠 보이지 않게 막는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=-10, ClampMax=24, Delta=0.1))
	float AutoExposureMinEV100 = 13.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_AutoExposureMaxEV100 = false;
	// 이보다 더 어둡게 하지 않는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=-10, ClampMax=24, Delta=0.1))
	float AutoExposureMaxEV100 = 18.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_AutoExposureSpeedUp = false;
	// 어두운 곳으로 갈 때(화면을 밝혀야 할 때) 따라가는 속도. 클수록 빠르다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.02, ClampMax=20, Delta=0.05))
	float AutoExposureSpeedUp = 3.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_AutoExposureSpeedDown = false;
	// 밝은 곳으로 갈 때(화면을 어둡게 해야 할 때) 따라가는 속도
	UPROPERTY(EditAnywhere, meta=(ClampMin=0.02, ClampMax=20, Delta=0.05))
	float AutoExposureSpeedDown = 1.0f;
	UPROPERTY(EditAnywhere)
	bool bOverride_AutoExposureBias = false;
	// 스톱 단위. +1이면 2배 밝게. 자동 노출과 물리 카메라 노출 둘 다에 얹는다 (UE의 Exposure Compensation)
	UPROPERTY(EditAnywhere, meta=(ClampMin=-8, ClampMax=8, Delta=0.1))
	float AutoExposureBias = 0.5f;

	// 노출계가 돌아야 하는지. 노출 배율이 장면 밝기에 따라 달라지는 모드일 때만 잰다
	bool NeedsExposureMeter() const { return bAutoExposure || bApplyPhysicalCameraExposure; }

	// 조리개·셔터·ISO를 ISO 100 기준 노출값(EV100)으로 바꾼다. 클수록 빛을 적게 받는다
	float GetCameraEV100() const;
	static float ComputeEV100(float Fstop, float ShutterSpeed, float ISO);
	// 노출계: 측정한 평균 휘도(노출 전 HDR)가 중간 회색으로 찍히려면 필요한 EV100.
	// DisplayGamma는 톤맵 패스가 실제로 거는 감마(보정을 껐으면 1)
	static float GetMeteredEV100(float AverageLuminance, float DisplayGamma = 1.0f);
	// 톤매핑 전에 곱할 배율. Sunny 16(f/16, 1/200초, ISO 200)에서 1이 되도록 맞췄다
	float GetPhysicalCameraExposureScale() const;
	// 어떤 EV100이든 톤매핑 배율로 바꾼다. 물리 노출과 자동 노출이 같은 기준점을 쓰게 한다
	static float EV100ToExposureScale(float EV100);
	// 이번 프레임에 쓸 노출 배율. 자동 노출이면 적응된 EV, 아니면 카메라 EV(또는 1)
	float GetExposureScale(float AdaptedEV100) const;

	// Src에서 bOverride_가 켜진 값만 Weight(0~1)만큼 이 값 쪽에서 Src 쪽으로 섞는다.
	void OverrideBlend(const FPostProcessSettings& Src, float Weight);
};
