#pragma once

#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

class UCameraModifier_Photo;

UCLASS()
class AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	void Tick(float DeltaTime) override;
	virtual void InputKey(int32 Key, bool bDown) override;
	virtual void InputAxis(EGameInputAxis Axis, float Delta) override;

protected:
	// 사진 모드 중에는 WASD·마우스를 캐릭터가 아니라 자유 카메라로 보낸다
	virtual void ProcessPlayerInput(float DeltaTime) override;

private:
	// 처음 사진 모드를 켤 때 PCM에 붙인다. PCM이 소유하므로 여기서는 지우지 않는다
	UCameraModifier_Photo* GetPhotoModifier(bool bCreate);
	bool IsPhotoModeActive();
	// 사진 모드 중 눌린 키를 처리했으면 true (캐릭터 액션으로 넘기지 않는다)
	bool HandlePhotoKey(int32 Key);
};
