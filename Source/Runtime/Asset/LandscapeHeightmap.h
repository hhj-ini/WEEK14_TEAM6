#pragma once

#include "Asset/RenderAsset.h"
#include "Terrain/TerrainData.h"

#include "LandscapeHeightmap.generated.h"

// 16비트 PNG 하이트맵 한 장을 읽어 만든 에셋. UAssetManager::LoadLandscapeHeightmap으로 얻는다.
// 높이 질의(B/C/D)와 렌더(ALandscape)는 모두 Data를 본다.
UCLASS()
class ULandscapeHeightmap : public URenderAsset
{
	GENERATED_BODY()
public:
	FTerrainData Data;
};
