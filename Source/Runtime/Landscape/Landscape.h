#pragma once

#include "GameFramework/Actor.h"
#include "Terrain/TerrainData.h"
#include "Landscape.generated.h"

class ULandscapeComponent;
class FLandscapeSharedBuffers;

// 씬에 놓는 지형 전체. UE ALandscape 대응.
// HeightmapPath의 PNG를 읽어 64x64칸 ULandscapeComponent들로 나눠 깐다.
// 액터는 위치만 쓴다 (회전·스케일은 1로 둔다. 크기는 CellSize·HeightScale로 정한다).
UCLASS()
class ALandscape : public AActor
{
	GENERATED_BODY()
public:
	ALandscape() = default;

	// B/C/D용 높이 데이터. Origin은 액터 위치를 따라간다
	const FTerrainData& GetTerrainData() const;
	const TSharedPtr<FLandscapeSharedBuffers>& GetSharedBuffers() const { return SharedBuffers; }
	const TArray<ULandscapeComponent*>& GetLandscapeComponents() const { return LandscapeComponents; }

	// 하이트맵을 읽고 컴포넌트를 새로 깐다. 이미 깔려 있으면 지우고 다시 만든다
	void BuildLandscape();
	// 컴포넌트와 공유 버퍼를 모두 지운다
	void ReleaseLandscape();

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
	void PostEditChangeProperty(const FProperty& Property) override;
	void RegisterAllComponents() override;

	UPROPERTY(EditAnywhere)
	FString HeightmapPath = "Terrain/Erangel_1025_L16.png";
	// 정점 사이 거리 (cm)
	UPROPERTY(EditAnywhere)
	float CellSize = 100.0f;
	// 하이트맵 값 65535일 때의 높이 (cm)
	UPROPERTY(EditAnywhere)
	float HeightScale = 2560.0f;

private:
	mutable FTerrainData TerrainData;                  // 이 액터의 지형 (에셋 데이터 복사본)
	TSharedPtr<FLandscapeSharedBuffers> SharedBuffers;
	TArray<ULandscapeComponent*> LandscapeComponents;
	bool bLandscapeBuilt = false;                      // 저장하지 않는다. 복제본은 false로 시작해 다시 만든다
};
