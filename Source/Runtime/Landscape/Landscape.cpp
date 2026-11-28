#include "EnginePCH.h"
#include "Landscape/Landscape.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Asset/AssetManager.h"
#include "Asset/LandscapeHeightmap.h"
#include "UObject/Property.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <algorithm>

const FTerrainData& ALandscape::GetTerrainData() const
{
	// 에디터에서 액터를 옮겨도 질의 결과가 따라가게 한다
	TerrainData.Origin = GetActorLocation();
	return TerrainData;
}

void ALandscape::BuildLandscape()
{
	ReleaseLandscape();
	if (!GetWorld())
		return;

	// 1. 에셋 (AssetManager가 캐시한다)
	ULandscapeHeightmap* Heightmap = UAssetManager::LoadLandscapeHeightmap(HeightmapPath);
	if (!Heightmap)
		return;

	// 2. 이 액터의 FTerrainData
	TerrainData = Heightmap->Data;
	TerrainData.CellSize = CellSize;
	TerrainData.HeightScale = HeightScale;
	TerrainData.Origin = GetActorLocation();

	// 3. 몇 장으로 나눌지
	const int32 Q = LandscapeComponentQuads;
	const int32 NumX = (TerrainData.SizeX - 1) / Q;
	const int32 NumY = (TerrainData.SizeY - 1) / Q;
	if (NumX <= 0 || NumY <= 0)
	{
		KRS_LOG(Warning, "Landscape: heightmap too small ({}x{}). Need at least {}x{}",
			TerrainData.SizeX, TerrainData.SizeY, Q + 1, Q + 1);
		return;
	}
	if ((TerrainData.SizeX - 1) % Q != 0 || (TerrainData.SizeY - 1) % Q != 0)
	{
		KRS_LOG(Warning, "Landscape: {}x{} is not 64n+1. Edge strip will not be drawn",
			TerrainData.SizeX, TerrainData.SizeY);
	}

	// 4. 모든 조각이 같이 쓰는 격자
	SharedBuffers = std::make_shared<FLandscapeSharedBuffers>(Q);

	// 5. 조각 깔기
	for (int32 CY = 0; CY < NumY; ++CY)
	{
		for (int32 CX = 0; CX < NumX; ++CX)
		{
			ULandscapeComponent* Component = Cast<ULandscapeComponent>(
				AddComponent(ULandscapeComponent::StaticClass(), FName("None")));
			if (!Component)
				continue;

			Component->SectionBaseX = CX * Q;
			Component->SectionBaseY = CY * Q;

			// 이 조각 높이 범위 → 경계 상자
			uint16 MinH = 65535;
			uint16 MaxH = 0;
			for (int32 IY = Component->SectionBaseY; IY <= Component->SectionBaseY + Q; ++IY)
			{
				for (int32 IX = Component->SectionBaseX; IX <= Component->SectionBaseX + Q; ++IX)
				{
					const uint16 H = TerrainData.Heights[IY * TerrainData.SizeX + IX];
					MinH = (std::min)(MinH, H);
					MaxH = (std::max)(MaxH, H);
				}
			}
			Component->HeightMin01 = MinH / 65535.0f;
			Component->HeightMax01 = MaxH / 65535.0f;

			// 로컬 (칸 번호, 0~1 높이) → 월드 cm
			Component->SetRelativeLocation(FVector(Component->SectionBaseX * CellSize, Component->SectionBaseY * CellSize, 0.0f));
			Component->SetRelativeScale3D(FVector(CellSize, CellSize, HeightScale));
			Component->MarkRenderStateDirty();

			LandscapeComponents.Add(Component);
		}
	}

	bLandscapeBuilt = true;
	KRS_LOG(Info, "Landscape built: {} ({}x{} vertices, {}x{} components)",
		HeightmapPath, TerrainData.SizeX, TerrainData.SizeY, NumX, NumY);
}

void ALandscape::ReleaseLandscape()
{
	// 내가 만든 것뿐 아니라 복제·옛 씬에서 따라온 조각까지 모두 지운다
	TArray<UActorComponent*> ToRemove;
	for (UActorComponent* Component : Components)
	{
		if (Cast<ULandscapeComponent>(Component))
			ToRemove.Add(Component);
	}
	for (UActorComponent* Component : ToRemove)
		DestroyComponent(Component);

	LandscapeComponents.Reset();
	SharedBuffers.reset();   // 프록시가 아직 쥐고 있으면 마지막 프록시가 사라질 때 풀린다
	TerrainData = FTerrainData();
	bLandscapeBuilt = false;
}

void ALandscape::RegisterAllComponents()
{
	Super::RegisterAllComponents();
	// 에디터에 새로 놓을 때, 복제(PIE 포함)될 때 한 번 깐다
	if (!bLandscapeBuilt && GetWorld())
		BuildLandscape();
}

void ALandscape::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.IsSaving())
	{
		// 조각은 HeightmapPath로 언제든 다시 만들 수 있으므로 씬 파일에 넣지 않는다
		const TArray<UActorComponent*> AllComponents = Components;
		Components.Reset();
		for (UActorComponent* Component : AllComponents)
		{
			if (!Cast<ULandscapeComponent>(Component))
				Components.Add(Component);
		}
		Super::Serialize(Record);
		Components = AllComponents;
		return;
	}

	Super::Serialize(Record);

	// 씬 로드: 기본값으로 한 번 깔린 뒤 저장된 HeightmapPath를 읽었으므로 다시 깐다
	if (Ar.IsLoading() && !Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate) && GetWorld())
		BuildLandscape();
}

void ALandscape::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "HeightmapPath" || Property.Name == "CellSize" || Property.Name == "HeightScale")
		BuildLandscape();
}
