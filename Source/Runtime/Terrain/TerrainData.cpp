#include "EnginePCH.h"
#include "Terrain/TerrainData.h"

// TODO(A): 구현

bool FTerrainData::GetHeight(float WorldX, float WorldY, float& OutZ) const
{
	return false;
}

FVector FTerrainData::GetNormal(float WorldX, float WorldY) const
{
	return FVector(0.0f, 0.0f, 1.0f);
}

void FTerrainData::GetLOD0Geometry(FTerrainGeometry& Out) const
{
	Out.Positions.SetNum(0);
	Out.Indices.SetNum(0);
}
