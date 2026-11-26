#include "EnginePCH.h"
#include "SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"
#include "Clothing/ClothingAsset.h"
#include "UObject/UObjectGlobals.h"

#include <filesystem>

#include "Asset/AssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetUtils.h"

namespace
{
	TArray<FBox> BuildBoneBounds(const FSkeletalMeshLODRenderData& LOD, int32 NumBones)
	{
		TArray<FBox> Bounds;
		Bounds.SetNum(NumBones);
		for (FBox& Box : Bounds)
			Box = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

		for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
		{
			const FVector& Position = LOD.BindVertices[V].Position;
			const FSkinWeight& Weight = LOD.SkinWeights[V];
			for (int32 K = 0; K < MaxBoneInfluences; ++K)
			{
				if (Weight.Weight[K] <= 0.0f) continue;
				FBox& Box = Bounds[Weight.BoneIndex[K]];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					Box.Min[Axis] = std::min(Box.Min[Axis], Position[Axis]);
					Box.Max[Axis] = std::max(Box.Max[Axis], Position[Axis]);
				}
			}
		}
		return Bounds;
	}
}

bool USkeletalMesh::Build(FSkeletalMeshData&& ImportData, FString& OutError, const FString& OwnerMeshPath)
{
	// CreateStaticMesh와 같은 기본값: 슬롯/섹션이 없으면 하나씩 채운다
	if (ImportData.MaterialSlots.IsEmpty())
	{
		FSkeletalMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		ImportData.MaterialSlots.Add(DefaultSlot);
	}
	if (ImportData.Sections.IsEmpty())
	{
		FSkeletalMeshSection DefaultSection;
		DefaultSection.StartIndex = 0;
		DefaultSection.IndexCount = static_cast<uint32>(ImportData.Indices.Num());
		DefaultSection.MaterialSlotIndex = 0;
		ImportData.Sections.Add(DefaultSection);
	}

	if (!ImportData.Validate(OutError))
	{
		return false;
	}

	// 공유 IB. VB는 인스턴스마다 FSkeletalMeshObjectCPUSkin이 동적으로 만든다
	TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(ImportData.Indices.GetData(), static_cast<uint32>(ImportData.Indices.Num()));
	if (!IB)
	{
		OutError = "SkeletalMesh: index buffer creation failed";
		return false;
	}

	TArray<UMaterial*> NewMaterials;
	if (!UAssetManager::BuildMaterialsFromSlots(ImportData.MaterialSlots, NewMaterials, OwnerMeshPath))
	{
		OutError = "SkeletalMesh: material creation failed";
		return false;
	}

	FSkeletalMeshLODRenderData LOD;
	LOD.BindVertices = std::move(ImportData.BindVertices);
	LOD.SkinWeights = std::move(ImportData.SkinWeight);
	LOD.Indices = std::move(ImportData.Indices);
	LOD.Sections = std::move(ImportData.Sections);
	LOD.IndexBuffer = std::move(IB);

	// 임포터가 채운 값에 의존하지 않고 바인드 정점에서 다시 계산
	LOD.BindAABB.Min = LOD.BindAABB.Max = LOD.BindVertices[0].Position;
	for (const FVertex& Vertex : LOD.BindVertices)
	{
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			LOD.BindAABB.Min[Axis] = std::min(LOD.BindAABB.Min[Axis], Vertex.Position[Axis]);
			LOD.BindAABB.Max[Axis] = std::max(LOD.BindAABB.Max[Axis], Vertex.Position[Axis]);
		}
	}

	TArray<FGpuSkinVertex> GpuSkinVertices;
	GpuSkinVertices.SetNum(LOD.BindVertices.Num());
	for (int32 V = 0; V < GpuSkinVertices.Num(); ++V)
	{
		GpuSkinVertices[V].Vertex = LOD.BindVertices[V];
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
		{
			GpuSkinVertices[V].BoneIndex[K] = LOD.SkinWeights[V].BoneIndex[K];
			GpuSkinVertices[V].Weight[K] = LOD.SkinWeights[V].Weight[K];
		}
	}
	LOD.GpuSkinVertexBuffer = RenderCommand::CreateStaticVertexBuffer(GpuSkinVertices.GetData(),
		sizeof(FGpuSkinVertex) * static_cast<uint32>(GpuSkinVertices.Num()), sizeof(FGpuSkinVertex));
	LOD.BoneBounds = BuildBoneBounds(LOD, ImportData.Bones.Num());

	RefSkeleton = std::move(ImportData.Bones);
	MaterialSlots = std::move(ImportData.MaterialSlots);
	Materials = std::move(NewMaterials);
	RenderData.LODRenderData.Empty();
	RenderData.LODRenderData.Add(std::move(LOD));
	SectionClothingBindings.SetNum(RenderData.LODRenderData[0].Sections.Num());
	for (int32& Slot : SectionClothingBindings)
		Slot = -1;
	RenderStates.Reset();
	return true;
}

int32 USkeletalMesh::FindBoneIndex(const FString& Name) const
{
	for (int32 I = 0; I < RefSkeleton.Num(); ++I)
	{
		if (RefSkeleton[I].Name == Name)
			return I;
	}
	return -1;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void USkeletalMesh::SetClothingAsset(UClothingAssetCommon* InAsset)
{
	if (!InAsset)
	{
		for (int32& Slot : SectionClothingBindings)
			Slot = -1;
		RebuildClothingMappingBuffer();
		return;
	}

	int32 AssetIdx = -1;
	for (int32 i = 0; i < MeshClothingAssets.Num(); ++i)
	{
		if (MeshClothingAssets[i] == InAsset)
		{
			AssetIdx = i;
			break;
		}
	}
	if (AssetIdx < 0)
	{
		AssetIdx = MeshClothingAssets.Num();
		MeshClothingAssets.Add(InAsset);
	}
	ApplyClothingAssetToSection(InAsset->GetSourceSectionIndex(), InAsset);
}

UClothingAssetCommon* USkeletalMesh::GetClothingAsset() const
{
	for (int32 AssetIdx : SectionClothingBindings)
	{
		if (AssetIdx >= 0 && AssetIdx < MeshClothingAssets.Num() && MeshClothingAssets[AssetIdx])
			return MeshClothingAssets[AssetIdx];
	}
	return nullptr;
}

UClothingAssetCommon* USkeletalMesh::CreateClothingAssetFromSection(int32 SectionIndex, const FString& Name)
{
	if (RenderData.LODRenderData.IsEmpty())
		return nullptr;

	const auto& Sections = RenderData.LODRenderData[0].Sections;
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
		return nullptr;

	UClothingAssetCommon* Asset = NewObject<UClothingAssetCommon>();
	if (!Asset || !Asset->InitializeForSection(*this, SectionIndex, Name))
		return nullptr;

	MeshClothingAssets.Add(Asset);
	return Asset;
}

bool USkeletalMesh::ApplyClothingAssetToSection(int32 SectionIndex, UClothingAssetCommon* Asset)
{
	if (RenderData.LODRenderData.IsEmpty())
		return false;

	const int32 NumSections = RenderData.LODRenderData[0].Sections.Num();
	if (SectionIndex < 0 || SectionIndex >= NumSections)
		return false;

	if (SectionClothingBindings.Num() != NumSections)
	{
		SectionClothingBindings.SetNum(NumSections);
		for (int32& Slot : SectionClothingBindings)
			Slot = -1;
	}

	int32 AssetIdx = -1;
	if (Asset)
	{
		for (int32 i = 0; i < MeshClothingAssets.Num(); ++i)
		{
			if (MeshClothingAssets[i] == Asset)
			{
				AssetIdx = i;
				break;
			}
		}
		if (AssetIdx < 0)
			return false;
	}

	SectionClothingBindings[SectionIndex] = AssetIdx;
	RebuildClothingMappingBuffer();
	return true;
}

void USkeletalMesh::RemoveClothingAssetFromSection(int32 SectionIndex)
{
	if (SectionIndex < 0 || SectionIndex >= SectionClothingBindings.Num())
		return;
	SectionClothingBindings[SectionIndex] = -1;
	RebuildClothingMappingBuffer();
}

void USkeletalMesh::RemoveClothingAsset(UClothingAssetCommon* Asset)
{
	if (!Asset)
		return;

	int32 RemovedIdx = -1;
	for (int32 i = 0; i < MeshClothingAssets.Num(); ++i)
	{
		if (MeshClothingAssets[i] == Asset)
		{
			RemovedIdx = i;
			break;
		}
	}
	if (RemovedIdx < 0)
		return;

	MeshClothingAssets.RemoveAt(RemovedIdx);
	for (int32& Binding : SectionClothingBindings)
	{
		if (Binding == RemovedIdx)
			Binding = -1;
		else if (Binding > RemovedIdx)
			--Binding;
	}
	RebuildClothingMappingBuffer();
}

UClothingAssetCommon* USkeletalMesh::GetClothingAssetForSection(int32 SectionIndex) const
{
	if (SectionIndex < 0 || SectionIndex >= SectionClothingBindings.Num())
		return nullptr;
	const int32 AssetIdx = SectionClothingBindings[SectionIndex];
	if (AssetIdx < 0 || AssetIdx >= MeshClothingAssets.Num())
		return nullptr;
	return MeshClothingAssets[AssetIdx];
}

void USkeletalMesh::RebuildClothingMappingBuffer()
{
	ClothMappingBuffer.reset();

	UClothingAssetCommon* ActiveAsset = GetClothingAsset();
	if (!ActiveAsset || !ActiveAsset->IsValid())
		return;

	const TArray<FClothVertMapping>& Mapping = ActiveAsset->GetRenderToSimMapping();
	if (Mapping.IsEmpty())
		return;

	ClothMappingBuffer = RenderCommand::CreateDynamicStructuredBuffer(
		static_cast<uint32>(Mapping.Num()), sizeof(FClothVertMapping));
	if (ClothMappingBuffer)
	{
		if (void* Dst = RenderCommand::MapWriteDiscard(ClothMappingBuffer.get()))
		{
			std::memcpy(Dst, Mapping.GetData(), sizeof(FClothVertMapping) * Mapping.Num());
			RenderCommand::Unmap(ClothMappingBuffer.get());
		}
	}
}

bool USkeletalMesh::SaveClothingAssets(const FString& MeshFilePath, FString& OutError) const
{
	const FString ClothPath = ClothingAssetFile::GetClothBinaryPath(MeshFilePath);
	if (ClothPath.empty())
	{
		OutError = "Invalid mesh file path for saving cloth binary.";
		return false;
	}
	return ClothingAssetFile::Write(ClothPath, MeshClothingAssets, SectionClothingBindings, OutError);
}

bool USkeletalMesh::LoadClothingAssets(const FString& MeshFilePath)
{
	const FString ClothPath = ClothingAssetFile::GetClothBinaryPath(MeshFilePath);
	const std::filesystem::path ClothFsPath(std::u8string_view(
		reinterpret_cast<const char8_t*>(ClothPath.data()), ClothPath.size()));
	if (ClothPath.empty() || !std::filesystem::exists(ClothFsPath))
		return false;

	TArray<UClothingAssetCommon*> LoadedAssets;
	TArray<int32> LoadedBindings;
	FString Error;
	if (!ClothingAssetFile::Read(ClothPath, *this, LoadedAssets, LoadedBindings, Error))
	{
		if (!Error.empty())
			KRS_LOG(Warning, "[Cloth] {}", Error);
		return false;
	}

	MeshClothingAssets = std::move(LoadedAssets);
	if (!RenderData.LODRenderData.IsEmpty())
	{
		const int32 NumSections = RenderData.LODRenderData[0].Sections.Num();
		SectionClothingBindings.SetNum(NumSections);
		for (int32 i = 0; i < NumSections; ++i)
		{
			const int32 Val = (i < LoadedBindings.Num()) ? LoadedBindings[i] : -1;
			SectionClothingBindings[i] = (Val >= 0 && Val < MeshClothingAssets.Num()) ? Val : -1;
		}
	}
	RebuildClothingMappingBuffer();
	KRS_LOG(Info, "[Cloth] Loaded {} clothing asset(s) from {}", MeshClothingAssets.Num(), ClothPath);
	return true;
}

size_t USkeletalMesh::FMaterialSlotsHash::operator()(const TArray<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

TSharedPtr<const FMeshRenderState> USkeletalMesh::GetRenderState(const TArray<UMaterial*>& InMaterials)
{
	auto& WeakState = RenderStates[InMaterials];
	if (auto State = WeakState.lock()) return State;

	// LOD는 하나뿐이라 임계값 없이 LOD 0 범위만 채운다
	auto State = MakeShared<FMeshRenderState>();
	if (!RenderData.LODRenderData.IsEmpty())
	{
		for (const FSkeletalMeshSection& Section : RenderData.LODRenderData[0].Sections)
		{
			UMaterial* Material = Section.MaterialSlotIndex < InMaterials.size()
				? InMaterials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
			if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
		}
	}
	WeakState = State;
	return State;
}

UPhysicsAsset* USkeletalMesh::GetPhysicsAsset() const
{
	if (bPhysicsAssetResolved)
		return PhysicsAsset;
	bPhysicsAssetResolved = true;

	// 1) 저장된 .phys가 있으면 읽는다 (UAssetManager가 캐시한다)
	const FString PhysPath = GetDefaultPhysicsAssetPath();
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(PhysPath);
	if (!GetPath().empty() && std::filesystem::exists(FilePath.empty() ? PhysPath : FilePath))
	{
		PhysicsAsset = UAssetManager::LoadPhysicsAsset(PhysPath);
		if (PhysicsAsset)
			return PhysicsAsset;
	}

	// 2) 없으면 자동 생성. 랙돌을 바로 시험할 수 있게 한다.
	UPhysicsAsset* Generated = NewObject<UPhysicsAsset>();
	FString Error;
	if (FPhysicsAssetUtils::CreateFromSkeletalMesh(*Generated, *this, FPhysAssetCreateParams{}, Error))
	{
		Generated->SetAssetPath(PhysPath);   // 에디터에서 저장하면 이 경로로 간다
		PhysicsAsset = Generated;
	}
	else
	{
		KRS_LOG(Warning, "[Physics] No PhysicsAsset for {} ({})", GetPath(), Error);
		delete Generated;
	}
	return PhysicsAsset;
}

FString USkeletalMesh::GetDefaultPhysicsAssetPath() const
{
	std::filesystem::path P(GetPath());
	P.replace_extension(".phys");
	return P.generic_string();
}
