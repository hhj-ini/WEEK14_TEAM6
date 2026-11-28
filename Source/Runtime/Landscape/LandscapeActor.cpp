#include "EnginePCH.h"
#include "Landscape/LandscapeActor.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Asset/AssetManager.h"
#include "Asset/LandscapeHeightmap.h"
#include "UObject/Property.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <algorithm>
#include <cmath>

const FTerrainData& ALandscapeActor::GetTerrainData() const
{
	// 에디터에서 액터를 옮겨도 질의 결과가 따라가게 한다
	TerrainData.Origin = GetActorLocation();

	// FTerrainData는 위치만 안다. 액터를 돌리거나 늘리면 화면의 지형과 B/C/D가 묻는 높이가 어긋난다.
	// 엔진에 bAbsoluteRotation/Scale이 선언만 있고 쓰이지 않아 막을 수 없으므로 경고만 한 번 낸다
	const FRotator Rotation = GetActorRotation();
	const FVector Scale = GetActorScale3D();
	const bool bIdentity =
		std::abs(Rotation.Pitch) < 0.01f && std::abs(Rotation.Yaw) < 0.01f && std::abs(Rotation.Roll) < 0.01f &&
		std::abs(Scale.X - 1.0f) < 0.001f && std::abs(Scale.Y - 1.0f) < 0.001f && std::abs(Scale.Z - 1.0f) < 0.001f;
	if (!bIdentity && !bWarnedTransform)
	{
		KRS_LOG(Warning, "Landscape: actor rotation/scale is ignored by height queries. "
			"Keep rotation 0 and scale 1, use CellSize/HeightScale instead");
	}
	bWarnedTransform = !bIdentity;   // 다시 0·1로 돌려 놓으면 다음 번에 또 알린다
	return TerrainData;
}

void ALandscapeActor::BuildLandscape()
{
	if (!GetWorld())
		return;

	// 0. 0 이하 값은 쓸 수 없다 (스케일 0 행렬). 마지막 유효값으로 되돌린다
	if (CellSize <= 0.0f)
		CellSize = AppliedCellSize;
	if (HeightScale <= 0.0f)
		HeightScale = AppliedHeightScale;

	// 1. 에셋 (AssetManager가 캐시한다)
	ULandscapeHeightmap* Heightmap = UAssetManager::LoadLandscapeHeightmap(HeightmapPath);
	if (!Heightmap)
	{
		ReleaseLandscape();
		return;
	}

	// 2. 이 액터의 FTerrainData
	TerrainData = Heightmap->Data;
	TerrainData.CellSize = CellSize;
	TerrainData.HeightScale = HeightScale;
	TerrainData.Origin = GetActorLocation();
	AppliedCellSize = CellSize;
	AppliedHeightScale = HeightScale;

	// 3. 몇 장으로 나눌지
	const int32 Q = LandscapeComponentQuads;
	const int32 NumX = (TerrainData.SizeX - 1) / Q;
	const int32 NumY = (TerrainData.SizeY - 1) / Q;
	if (NumX <= 0 || NumY <= 0)
	{
		KRS_LOG(Warning, "Landscape: heightmap too small ({}x{}). Need at least {}x{}",
			TerrainData.SizeX, TerrainData.SizeY, Q + 1, Q + 1);
		ReleaseLandscape();
		return;
	}
	if ((TerrainData.SizeX - 1) % Q != 0 || (TerrainData.SizeY - 1) % Q != 0)
	{
		// 1025를 넣으면 앞 1009(=16x63+1)만 그리고 끝 16줄은 안 그린다. 1009로 다시 줄인 파일을 쓰면 사라진다
		KRS_LOG(Warning, "Landscape: {}x{} is not {}n+1. Only {}x{} vertices are drawn",
			TerrainData.SizeX, TerrainData.SizeY, Q, NumX * Q + 1, NumY * Q + 1);
	}

	// 4. 조각: 개수가 같으면 그대로 쓰고, 다를 때만 지우고 새로 만든다.
	//    컴포넌트를 delete하면 Details 패널·선택·기즈모가 쥔 포인터가 같은 프레임에 깨진다
	const bool bReuse = CanReuseComponents(NumX * NumY);
	if (!bReuse)
	{
		// ReleaseLandscape는 TerrainData까지 비우므로 여기서는 조각만 지운다
		TArray<UActorComponent*> ToRemove;
		for (UActorComponent* Component : Components)
		{
			if (Cast<ULandscapeComponent>(Component))
				ToRemove.Add(Component);
		}
		for (UActorComponent* Component : ToRemove)
			DestroyComponent(Component);
		LandscapeComponents.Reset();

		for (int32 Index = 0; Index < NumX * NumY; ++Index)
		{
			ULandscapeComponent* Component = Cast<ULandscapeComponent>(
				AddComponent(ULandscapeComponent::StaticClass(), FName("None")));
			if (Component)
				LandscapeComponents.Add(Component);
		}
		if (LandscapeComponents.Num() != NumX * NumY)
		{
			KRS_LOG(Warning, "Landscape: failed to create components");
			ReleaseLandscape();
			return;
		}
	}

	// 5. 공유 격자. 높이가 바뀌었을 수 있으므로 매번 새로 만든다.
	//    프록시는 SharedBuffers 포인터가 바뀐 걸 보고 자기 높이 텍스처를 다시 굽는다
	SharedBuffers = std::make_shared<FLandscapeSharedBuffers>(Q);

	// 6. 조각마다 시작점과 높이 범위
	for (int32 CY = 0; CY < NumY; ++CY)
	{
		for (int32 CX = 0; CX < NumX; ++CX)
		{
			ULandscapeComponent* Component = LandscapeComponents[CY * NumX + CX];
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
			Component->MarkRenderStateDirty();   // 높이 텍스처 다시 굽기
		}
	}

	// 7. 로컬 (칸 번호, 0~1 높이) → 월드 cm
	ApplyComponentTransforms();

	bLandscapeBuilt = true;
	KRS_LOG(Info, "Landscape built: {} ({}x{} vertices, {}x{} components, {})",
		HeightmapPath, TerrainData.SizeX, TerrainData.SizeY, NumX, NumY, bReuse ? "reused" : "created");
}

void ALandscapeActor::ApplyComponentTransforms()
{
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (!Component)
			continue;
		Component->SetRelativeLocation(FVector(Component->SectionBaseX * CellSize, Component->SectionBaseY * CellSize, 0.0f));
		Component->SetRelativeScale3D(FVector(CellSize, CellSize, HeightScale));
	}
}

bool ALandscapeActor::CanReuseComponents(int32 NumComponents) const
{
	if (!bLandscapeBuilt || LandscapeComponents.Num() != NumComponents)
		return false;

	// Details 트리의 Delete 등으로 누가 조각을 지웠으면 목록의 포인터가 이미 죽어 있다
	int32 Alive = 0;
	for (UActorComponent* Component : Components)
	{
		if (Cast<ULandscapeComponent>(Component))
		{
			if (LandscapeComponents.Find(static_cast<ULandscapeComponent*>(Component)) == INDEX_NONE)
				return false;   // 목록에 없는 조각이 섞여 있다
			++Alive;
		}
	}
	return Alive == NumComponents;
}

void ALandscapeActor::ReleaseLandscape()
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

void ALandscapeActor::RegisterAllComponents()
{
	Super::RegisterAllComponents();
	// 에디터에 새로 놓을 때, 복제(PIE 포함)될 때 한 번 깐다
	if (!bLandscapeBuilt && GetWorld())
		BuildLandscape();
}

void ALandscapeActor::Serialize(FStructuredArchive::FRecord Record)
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

void ALandscapeActor::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "HeightmapPath")
	{
		BuildLandscape();   // 조각 수가 같으면 컴포넌트는 재사용, 텍스처만 다시 굽는다
	}
	else if (Property.Name == "CellSize" || Property.Name == "HeightScale")
	{
		// 숫자 칸은 드래그·타이핑 중 매 프레임 바뀐다. 컴포넌트는 지우지 않고 트랜스폼과 텍스처만 바꾼다
		if (CellSize <= 0.0f)
			CellSize = AppliedCellSize;
		if (HeightScale <= 0.0f)
			HeightScale = AppliedHeightScale;
		AppliedCellSize = CellSize;
		AppliedHeightScale = HeightScale;

		if (!bLandscapeBuilt)
			return;
		if (!CanReuseComponents(LandscapeComponents.Num()))
		{
			BuildLandscape();   // 누가 조각을 지웠다면 목록이 깨졌으니 다시 깐다
			return;
		}
		TerrainData.CellSize = CellSize;
		TerrainData.HeightScale = HeightScale;
		ApplyComponentTransforms();
		// 텍스처의 법선이 월드 비율로 구워져 있으므로 다시 굽게 한다 (UE BGRA8 방식의 비용).
		// 드래그 중에는 매 프레임 256장을 다시 구우므로 끊길 수 있다
		for (ULandscapeComponent* Component : LandscapeComponents)
			Component->MarkRenderStateDirty();
	}
}
