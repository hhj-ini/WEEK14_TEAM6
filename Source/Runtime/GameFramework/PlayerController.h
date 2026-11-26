#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "PlayerController.generated.h"

class APawn;
class APlayerCameraManager;
class UCurveFloat;

UCLASS()
class APlayerController : public AActor
{
	GENERATED_BODY()
public:
	APlayerController();
	void Tick(float DeltaTime) override;
	// 사진 모드처럼 입력을 폰 대신 다른 곳으로 보낼 때 파생 컨트롤러가 바꾼다
	virtual void ProcessPlayerInput(float DeltaTime);
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	virtual void InputKey(int32 Key, bool bDown);
	bool IsKeyDown(int32 Key) const { return Key >= 0 && Key < 256 && bKeyDown[Key]; }
	virtual void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	FRotator ControlRotation;
	//PCM을 만들고 주인을 알려준다.
	void SpawnPlayerCameraManager();
	//Wolrd Tick으로부터 전달
	void UpdateCameraManager(float DeltaTime);
	APlayerCameraManager* GetCameraManager() const { return PlayerCameraManager; }
protected:
	// 이번 프레임에 쌓인 마우스 이동량. ProcessPlayerInput이 쓰고 비운다
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;

private:

	APlayerCameraManager* PlayerCameraManager = nullptr;
	APawn* PossessedPawn = nullptr;
	bool bKeyDown[256]{};
};
