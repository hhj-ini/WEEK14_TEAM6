#pragma once

#include "UI/UserWidget.h"
#include "ViewfinderWidget.generated.h"

class UFont;
class UCameraModifier_Photo;

// 기계식 바늘. 목표값을 스프링으로 따라가서 살짝 넘쳤다가 멈춘다
struct FViewfinderNeedle
{
	float Position = 0.0f;   // 0~1, 눈금 왼쪽(위)에서 오른쪽(아래)
	float Velocity = 0.0f;
	bool bInitialized = false;

	void Update(float Target, float DeltaTime);
};

UCLASS()
// 필름 카메라 뷰파인더 HUD. 위젯 트리 없이 Paint에서 직접 그린다.
// 위: 셔터 눈금(35RC), 오른쪽: 조리개 눈금(QL17), 가운데: 프레임과 초점 패치, 아래: 거리계(LC-A)
class UViewfinderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UViewfinderWidget();

	virtual void Paint(FUIDrawList& DrawList, float ParentOpacity) const override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(float DeltaTime) override;

private:
	// 매 프레임 PCM에서 찾는다. 모디파이어는 PCM이 지우므로 포인터를 들고 있지 않는다
	UCameraModifier_Photo* FindPhotoModifier() const;

	// 피벗에서 뻗어 나온 바늘이 눈금판(Plate)을 지나가는 부분만 그린다
	void PaintNeedle(FUIDrawList& DrawList, const FVector2& Pivot, const FVector2& Tip,
		float HalfLength, float Thickness, const FLinearColor& Color) const;

	UFont* Font = nullptr;
	FViewfinderNeedle ShutterNeedle;
	FViewfinderNeedle ApertureNeedle;
	FViewfinderNeedle FocusNeedle;
	// 노출계 바늘. 지금 셔터·ISO에서 적정 노출이 되는 조리개를 가리킨다 (QL17 셔터 우선 방식)
	FViewfinderNeedle MeterNeedle;
	bool bMeterValid = false;
	float ExposureOffsetEV = 0.0f;   // +면 그만큼 과다 노출
};
