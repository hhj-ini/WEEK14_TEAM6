#pragma once

#include "UObject/Class.h"
#include "../Components/SceneComponent.h"
#include "../Math/EngineMath.h"
#include "../Math/Rotator.h"
#include "Engine/PostProcessSettings.h"
#include "CameraComponent.generated.h"

struct FMinimalViewInfo;

UCLASS()
class UCameraComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCameraComponent() { PrimaryComponentTick.bCanEverTick = true; }   // 이동·회전 입력을 매 프레임 처리한다

	// 매 프레임 카메라 입력/이동 처리
	virtual void TickComponent(float DeltaTime) override;

	// Camera 행렬 계산
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const;

	// Picking
	FRay DeProjection(FVector2 MousePos, float ScreenW, float ScreenH);

	// Get & Set
	// bUseFocalLength면 렌즈 길이와 센서 폭으로 계산한 세로 화각을 돌려준다
	float GetFieldOfView() const;
	void SetFieldOfView(float InFieldOfView) { FieldOfView = InFieldOfView; }

	float GetAspectRatio() const { return AspectRatio; }
	void SetAspectRatio(float InAspectRatio) { AspectRatio = InAspectRatio; }

	float GetNearZ() const { return NearClipPlane; }
	void SetNearZ(float InNearZ) { NearClipPlane = InNearZ; }

	float GetFarZ() const { return FarClipPlane; }
	void SetFarZ(float InFarZ) { FarClipPlane = InFarZ; }

	bool GetIsOrthogonal() const { return bIsOrthogonal; }
	void SetIsOrthogonal(bool bInIsOrthographic) { bIsOrthogonal = bInIsOrthographic; }

	float GetOrthoWidth() const { return OrthoWidth; }
	void SetOrthoWidth(float InOrthoWidth) { OrthoWidth = InOrthoWidth; }

	// 마우스 조작 설정
	float GetMoveSpeed() const { return MoveSpeed; }
	void SetMoveSpeed(float InMoveSpeed) { MoveSpeed = InMoveSpeed; }

	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetMouseSensitivity(float InMouseSensitivity) { MouseSensitivity = InMouseSensitivity; }

	float GetWheelSpeed() const { return WheelSpeed; }
	void SetWheelSpeed(float InWheelSpeed) { WheelSpeed = InWheelSpeed; }
	// 외부 ViewportLayout가 View별 입력을 처리할 때 기본 카메라의 중복 입력 갱신을 끈다.

	void SetExternalInputManaged(bool bValue) { bExternalInputManaged = bValue; }
	bool IsExternalInputManaged() const { return bExternalInputManaged; }

	// 마우스 조작 업데이트
	void UpdateMovement(float DeltaTime); // 마우스 이동
	void UpdateRotation(float DeltaTime); // 마우스 우클릭 회전
	void UpdateZoom(float DeltaTime);     // 마우스 휠 줌

protected:
	// 세로 화각(도). 0°/180° 근처는 투영 행렬이 깨지므로 막는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=5, ClampMax=170, Delta=0.5))
	float FieldOfView = 60.0f;

	// 켜면 FieldOfView 대신 FocalLength로 화각을 정한다 (UE CineCamera처럼 mm로 렌즈를 고른다)
	UPROPERTY(EditAnywhere)
	bool bUseFocalLength = false;

	// 렌즈 길이(mm). 센서 폭은 PostProcessSettings.DepthOfFieldSensorWidth를 같이 쓴다
	UPROPERTY(EditAnywhere, meta=(ClampMin=4, ClampMax=1000, Delta=0.5))
	float FocalLength = 35.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearClipPlane = 0.1f;
	float FarClipPlane = 10000.0f;

	bool bIsOrthogonal = false;
	float OrthoWidth = 16.0f;
	
	float MoveSpeed = 10.0f;
	float MouseSensitivity = 0.05f;
	float WheelSpeed = 0.1f;

	UPROPERTY(EditAnywhere)
	bool bExternalInputManaged = false;

public:
	// UE와 같이 소유 Pawn의 Controller ControlRotation을 뷰 회전으로 쓴다 (부모/스프링 암 회전보다 우선)
	UPROPERTY(EditAnywhere)
	bool bUsePawnControlRotation = false;

	// 이 카메라로 볼 때 쓰는 후처리 값. bOverride_가 켜진 값만 기본값 위에 덮어쓴다.
	UPROPERTY(EditAnywhere)
	FPostProcessSettings PostProcessSettings;

	// PostProcessSettings를 얼마나 섞을지(0이면 쓰지 않음, 1이면 그대로)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float PostProcessBlendWeight = 1.0f;

};

