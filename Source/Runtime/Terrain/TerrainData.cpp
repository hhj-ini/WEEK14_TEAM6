#include "EnginePCH.h"
#include "Terrain/TerrainData.h"
#include <cmath>           
#include <algorithm>       

namespace
{
	// 인덱스를 받아서 그지점의 높이값을 반환
	float VertexZ(const FTerrainData& D, int32 IX, int32 IY) 
	{
		return D.Origin.Z + D.Heights[IY * D.SizeX + IX] * (D.HeightScale / 65535.0f);
	}

	//월드 현재위치로부터 격자의 몇번째칸 어디인지를 반환하는 함수
	bool LocateCell(const FTerrainData& D, float WorldX, float WorldY, int32& CX, int32& CY, float& FX, float& FY)
	{
		if (D.SizeX < 2 || D.SizeY < 2)
		{
			return false;
		}

		// World의 현재위치로부터 내가 격자의 어디에 있는지를 가리키는 값
		const float GX = (WorldX - D.Origin.X) / D.CellSize;  
		const float GY = (WorldY - D.Origin.Y) / D.CellSize;

		if (GX < 0.0f || GY < 0.0f                      // 맵 왼쪽·아래 밖
			|| GX > D.SizeX - 1 || GY > D.SizeY - 1)       // 맵 오른쪽·위 밖 (마지막 정점 1024 초과)
		{
			return false;
		}

		CX = (std::min)(static_cast<int32>(GX), D.SizeX - 2);
		CY = (std::min)(static_cast<int32>(GY), D.SizeY - 2);
		FX = GX - CX;
		FY = GY - CY;
		return true;
	}
	//격자안에 임의의 지점에 높이를 반환하는 함수
	float CellHeight(const FTerrainData& D, int32 CX, int32 CY, float FX, float FY, float& OutDZdFX, float& OutDZdFY)
	{
		

		const float H00 = VertexZ(D, CX, CY);        // 왼쪽 아래 
		const float H10 = VertexZ(D, CX + 1, CY);        // X+1      
		const float H01 = VertexZ(D, CX, CY + 1);    // Y+1       
		const float H11 = VertexZ(D, CX + 1, CY + 1);    // 대각선   
	
		if (FX >= FY)                    // 대각선(FX=FY) 기준 X쪽 반 → 삼각형
		{
			OutDZdFX = H10 - H00;        // 00→10: X만 1 증가할 때 높이 변화
			OutDZdFY = H11 - H10;        // 10→11: Y만 1 증가할 때 높이 변화
		}
		else                           
		{
			OutDZdFX = H11 - H01;      
			OutDZdFY = H01 - H00;    
		}

		return H00 + OutDZdFX * FX + OutDZdFY * FY;   // 500 + 40*0.45 + 40*0.89 
		
	}

}
bool FTerrainData::GetHeight(float WorldX, float WorldY, float& OutZ) const
{
	int32 CX, CY;
	float FX, FY;

	// 맵밖이면
	if (!LocateCell(*this, WorldX, WorldY, CX, CY, FX, FY))
	{
		return false;
	}

	float DX, DY;
	OutZ = CellHeight(*this, CX, CY, FX, FY, DX, DY);
	return true;
}

FVector FTerrainData::GetNormal(float WorldX, float WorldY) const
{
	int32 CX, CY;
	float FX, FY;

	//맵밖
	if (!LocateCell(*this, WorldX, WorldY, CX, CY, FX, FY))
	{
		return FVector::ZeroVector;
	}

	float DX, DY;
	//기울기 추출
	CellHeight(*this, CX, CY, FX, FY, DX, DY);

	const float NX = -DX / CellSize;
	const float NY = -DY / CellSize;
	const float Len = std::sqrt(NX * NX + NY * NY + 1.0f);

	return FVector(NX / Len, NY / Len, 1.0f / Len);
}

void FTerrainData::GetLOD0Geometry(FTerrainGeometry& Out) const
{
	Out.Positions.Empty();
	Out.Indices.Empty();
	if (SizeX < 2 || SizeY < 2) return;

	Out.Positions.SetNum(SizeX * SizeY);
	for (int32 IY = 0; IY < SizeY; ++IY)
	{
		for (int32 IX = 0; IX < SizeX; ++IX)
		{
			Out.Positions[IY * SizeX + IX] = FVector(
				Origin.X + IX * CellSize,
				Origin.Y + IY * CellSize,
				VertexZ(*this, IX, IY));
		}
	}

	Out.Indices.SetNum((SizeX - 1) * (SizeY - 1) * 6);
	int32 K = 0;
	for (int32 IY = 0; IY < SizeY - 1; ++IY)
	{
		for (int32 IX = 0; IX < SizeX - 1; ++IX)
		{
			const uint32 V00 = IY * SizeX + IX;
			const uint32 V10 = V00 + 1;          // X+1
			const uint32 V01 = V00 + SizeX;      // Y+1
			const uint32 V11 = V01 + 1;

			// 위(+Z)에서 내려다볼 때 시계 방향 (DX11 기본 앞면)
			Out.Indices[K++] = V00; Out.Indices[K++] = V10; Out.Indices[K++] = V11;   // 삼각형 A
			Out.Indices[K++] = V00; Out.Indices[K++] = V11; Out.Indices[K++] = V01;   // 삼각형 B
		}
	}
}

