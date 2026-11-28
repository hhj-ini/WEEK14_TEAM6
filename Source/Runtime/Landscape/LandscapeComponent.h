#pragma once

#include "Components/PrimitiveComponent.h"
#include "LandscapeComponent.generated.h"

class ALandscapeActor;

// 지형 한 조각 (64x64칸). 컬링·드로우콜의 단위. UE ULandscapeComponent 대응.
// 위치·크기는 트랜스폼에 담는다: 상대 위치 = 조각 시작점, 상대 스케일 = (CellSize, CellSize, HeightScale).
// 그래서 로컬 공간은 X·Y = 칸 번호(0~64), Z = 높이 0~1 이다.
UCLASS()
class ULandscapeComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	// 이 조각의 시작 정점 (FTerrainData 격자 기준). -1이면 아직 배치 전
	UPROPERTY()
	int32 SectionBaseX = -1;
	UPROPERTY()
	int32 SectionBaseY = -1;

	// 이 조각 높이의 최소·최대 (0~1 = 하이트맵 값 / 65535). 경계 상자(컬링)에 쓴다
	UPROPERTY()
	float HeightMin01 = 0.0f;
	UPROPERTY()
	float HeightMax01 = 1.0f;

	ALandscapeActor* GetLandscapeActor() const;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

	UMaterial* GetLandscapeMaterial() const;
};
