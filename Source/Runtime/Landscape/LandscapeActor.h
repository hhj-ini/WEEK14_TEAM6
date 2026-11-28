#pragma once

#include "GameFramework/Actor.h"
#include "Terrain/TerrainData.h"
#include "LandscapeActor.generated.h"

class ULandscapeComponent;
class FLandscapeSharedBuffers;

// 씬에 놓는 지형 전체. UE ALandscape 대응.
// HeightmapPath의 PNG를 읽어 63x63칸 ULandscapeComponent들로 나눠 깐다 (UE 규격, 1009 = 16x63+1).
// 액터는 위치만 쓴다 (회전·스케일은 1로 둔다. 크기는 CellSize·HeightScale로 정한다).
UCLASS()
class ALandscapeActor : public AActor
{
	GENERATED_BODY()
public:
	ALandscapeActor() = default;

	// B/C/D용 높이 데이터. Origin은 액터 위치를 따라간다
	const FTerrainData& GetTerrainData() const;
	const TSharedPtr<FLandscapeSharedBuffers>& GetSharedBuffers() const { return SharedBuffers; }
	const TArray<ULandscapeComponent*>& GetLandscapeComponents() const { return LandscapeComponents; }

	// 하이트맵을 읽고 컴포넌트를 깐다.
	// 이미 깔려 있고 조각 수가 같으면 컴포넌트를 지우지 않고 재사용한다 (에디터가 쥔 포인터 보호)
	void BuildLandscape();
	// 컴포넌트와 공유 버퍼를 모두 지운다
	void ReleaseLandscape();

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
	void PostEditChangeProperty(const FProperty& Property) override;
	void RegisterAllComponents() override;

	UPROPERTY(EditAnywhere)
	FString HeightmapPath = "Terrain/Erangel_1009_L16.png";
	// 정점 사이 거리 (cm)
	UPROPERTY(EditAnywhere)
	float CellSize = 100.0f;
	// 하이트맵 값 65535일 때의 높이 (cm)
	UPROPERTY(EditAnywhere)
	float HeightScale = 2560.0f;
		
private:
	// CellSize·HeightScale만 바뀌었을 때: 컴포넌트 트랜스폼만 다시 계산한다
	void ApplyComponentTransforms();
	// 기존 조각을 그대로 쓸 수 있는지 (개수가 같고, 누가 지우지 않았는지)
	bool CanReuseComponents(int32 NumComponents) const;

	mutable FTerrainData TerrainData;                  // 이 액터의 지형 (에셋 데이터 복사본)
	TSharedPtr<FLandscapeSharedBuffers> SharedBuffers;
	TArray<ULandscapeComponent*> LandscapeComponents;
	mutable bool bWarnedTransform = false;             // 회전·스케일 경고를 한 번만 내려고 둔다
	bool bLandscapeBuilt = false;                      // 저장하지 않는다. 복제본은 false로 시작해 다시 만든다
	float AppliedCellSize = 100.0f;                    // 마지막으로 적용한 유효값 (0 이하 입력을 되돌릴 때 쓴다)
	float AppliedHeightScale = 2560.0f;
};
