#pragma once

// 터레인 공용 데이터 인터페이스. B(물리), C(네비), D(레벨)가 이걸 쓴다.
// 좌표계는 엔진과 같다 (왼손, X 앞, Y 오른쪽, Z 위).
// 정점 (IX, IY) 위치 = Origin + (IX * CellSize, IY * CellSize, Heights[IY * SizeX + IX] / 65535 * HeightScale)

#include "Containers/Array.h"
#include "Math/Vector.h"

struct FTerrainGeometry
{
	TArray<FVector> Positions;   // 월드 공간
	TArray<uint32> Indices;
};

struct FTerrainData
{
	TArray<uint16> Heights;      // 16bit 하이트맵, SizeX * SizeY
	int32 SizeX = 0;             // 정점 수
	int32 SizeY = 0;

	FVector Origin = FVector(0.0f, 0.0f, 0.0f);
	float CellSize = 100.0f;     // 정점 간 XY 간격
	float HeightScale = 2560.0f; // Heights = 65535일 때의 높이

	// 월드 XY의 지형 높이. 지형 밖이면 false.
	bool GetHeight(float WorldX, float WorldY, float& OutZ) const;
	// 월드 XY의 표면 노멀.
	FVector GetNormal(float WorldX, float WorldY) const;
	// LOD0 메시 (NavMesh 입력 등).
	void GetLOD0Geometry(FTerrainGeometry& Out) const;
};
