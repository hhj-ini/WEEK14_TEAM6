#include "EnginePCH.h"
#include "SkeletalMeshObject.h"
#include "RenderCommand.h"
#include "Tasks/Tasks.h"
#include "Tasks/TaskScheduler.h"
#include "Render/SkinningStats.h"
#include "Clothing/ClothingAsset.h"

// GpuSkinCommon.hlsli가 이 배치를 가정한다
static_assert(sizeof(FMatrix) == 64);

namespace
{
	// 비균등 스케일에서도 면에 수직이 되도록 3x3 부분의 여인수 행렬로 노멀을 옮긴다.
	// 미러(행렬식 음수)면 여인수 결과가 뒤집히므로 부호를 되돌린다.
	FVector TransformNormal(const FMatrix& M, const FVector& N)
	{
		const FVector R0(M.M[0][0], M.M[0][1], M.M[0][2]);
		const FVector R1(M.M[1][0], M.M[1][1], M.M[1][2]);
		const FVector R2(M.M[2][0], M.M[2][1], M.M[2][2]);
		const FVector C0 = FVector::Cross(R1, R2);
		const FVector Result = C0 * N.X + FVector::Cross(R2, R0) * N.Y + FVector::Cross(R0, R1) * N.Z;
		return (R0.Dot(C0) < 0.0f ? Result * -1.0f : Result).Normalized();
	}
}

FSkeletalMeshObject::FSkeletalMeshObject(USkeletalMesh* InMesh)
	:SkeletalMesh(InMesh)
{
	RefToLocal.SetNum(InMesh->GetNumBones());
	for (FMatrix& Matrix : RefToLocal)
		Matrix = FMatrix::Identity;
	Bounds = SkinnedBounds = InMesh->GetRenderData().LODRenderData[0].BindAABB;
}

void FSkeletalMeshObject::SkinVerticesCPU(TArray<FVertex>& OutVertices, FBox& OutBounds) const
{
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];
	const uint32 Count = static_cast<uint32>(LOD.BindVertices.Num());
	const uint32 NumThreads = Tasks::FTaskScheduler::Get().GetNumWorkers() + 1;
	const uint32 ChunkCount = std::clamp(Count / 2048u, 1u, NumThreads * 2);

	TArray<FBox> ChunkBounds;
	ChunkBounds.SetNum(ChunkCount);

	//Liner Blend Skinning LBS
	Tasks::ParallelForChunks(Count, ChunkCount, [&](uint32 Begin, uint32 End, uint32 Chunk)
		{
			FBox ChunkBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX,-FLT_MAX,-FLT_MAX) };
			for (uint32 v = Begin; v < End; ++v)
			{
				const FVertex& Bind = LOD.BindVertices[v];
				const FSkinWeight& W = LOD.SkinWeights[v];

				FMatrix M = RefToLocal[W.BoneIndex[0]] * W.Weight[0];
				for (int32 k = 1; k < MaxBoneInfluences; ++k)
				{
					if (W.Weight[k] > 0.0f)
						M += RefToLocal[W.BoneIndex[k]] * W.Weight[k];
				}

				FVertex& Out = OutVertices[v];
				Out = Bind;
				Out.Position = M.TransformPosition(Bind.Position);
				Out.Normal = TransformNormal(M, Bind.Normal);
				const FVector Tangent = M.TransformVector(FVector(Bind.Tangent.X, Bind.Tangent.Y, Bind.Tangent.Z)).Normalized();
				Out.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Bind.Tangent.W);

				if (DebugWeightBone >= 0)
				{
					float Weight = 0.0f;
					for (int32 k = 0; k < MaxBoneInfluences; ++k)
					{
						if (W.BoneIndex[k] == DebugWeightBone)
							Weight += W.Weight[k];
					}
					// 픽셀 셰이더에서 언리얼 히트맵 그라디언트로 보간되도록 스칼라 가중치를 정점에 실어 전달한다.
					const float ClampedWeight = std::clamp(Weight, 0.0f, 1.0f);
					Out.Color = FVector4(ClampedWeight, ClampedWeight, ClampedWeight, 1.0f);
				}

				for (int Axis = 0; Axis < 3; ++Axis)
				{
					ChunkBox.Min[Axis] = FMath::Min(ChunkBox.Min[Axis], Out.Position[Axis]);
					ChunkBox.Max[Axis] = FMath::Max(ChunkBox.Max[Axis], Out.Position[Axis]);
				}
			}
			ChunkBounds[Chunk] = ChunkBox;
		});
	OutBounds = ChunkBounds[0];
	for (uint32 i = 1; i < ChunkCount; ++i)
		OutBounds.Expand(ChunkBounds[i]);
}



bool FSkeletalMeshObject::UpdateBounds()
{
	bool bInside = true;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (SkinnedBounds.Min[Axis] < Bounds.Min[Axis] || SkinnedBounds.Max[Axis] > Bounds.Max[Axis])
			bInside = false;
	}
	if (bInside) return false;

	const FVector Margin = (SkinnedBounds.Max - SkinnedBounds.Min) * 0.25f;
	Bounds.Min = SkinnedBounds.Min - Margin;
	Bounds.Max = SkinnedBounds.Max + Margin;
	return true;
}

void FSkeletalMeshObject::UpdateRefToLocalMatrices(const TArray<FMatrix>& ComponentSpaceTransforms)
{
	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetRefSkeleton();
	const int32 Count = std::min(Bones.Num(), ComponentSpaceTransforms.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		RefToLocal[i] = Bones[i].InverseBindPose * ComponentSpaceTransforms[i];
	}
}

FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectCPUSkin(USkeletalMesh* InMesh)
	:FSkeletalMeshObject(InMesh)
{
	const FSkeletalMeshLODRenderData& LOD = InMesh->GetRenderData().LODRenderData[0];
	SkinnedVertices = LOD.BindVertices;

	const uint32 Size = sizeof(FVertex) * static_cast<uint32>(SkinnedVertices.Num());
	FinalVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(Size, sizeof(FVertex));
	VertexFactory.Init(FinalVertexBuffer.get(), GetIndexBuffer(0));

	UploadVertices();
}

void FSkeletalMeshObjectCPUSkin::Update(const TArray<FMatrix>& ComponentSpaceTransforms)
{
	UpdateRefToLocalMatrices(ComponentSpaceTransforms);
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CpuSkinning);
		SkinVerticesCPU(SkinnedVertices, SkinnedBounds);
	}
	if (!ClothPaintOverlay.IsEmpty())
	{
		const int32 Count = FMath::Min(SkinnedVertices.Num(), ClothPaintOverlay.Num());
		for (int32 v = 0; v < Count; ++v)
		{
			if (ClothPaintOverlay[v].SimIndex > 0u)
			{
				const float Val = ClothPaintOverlay[v].BlendWeight;
				SkinnedVertices[v].Color = FVector4(Val, Val, Val, 1.0f);
			}
			else
			{
				SkinnedVertices[v].Color.W = 0.0f;
			}
		}
	}
	UploadVertices();
}

void FSkeletalMeshObjectCPUSkin::SetClothPaintOverlay(const TArray<FClothVertMapping>* PaintOverlay)
{
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];
	const int32 Count = FMath::Min(SkinnedVertices.Num(), LOD.BindVertices.Num());
	if (!PaintOverlay || PaintOverlay->IsEmpty())
	{
		ClothPaintOverlay.Empty();
		for (int32 v = 0; v < Count; ++v)
			SkinnedVertices[v].Color = LOD.BindVertices[v].Color;
		UploadVertices();
		return;
	}

	ClothPaintOverlay = *PaintOverlay;
	const int32 PaintCount = FMath::Min(Count, ClothPaintOverlay.Num());
	for (int32 v = 0; v < PaintCount; ++v)
	{
		if (ClothPaintOverlay[v].SimIndex > 0u)
		{
			const float Val = ClothPaintOverlay[v].BlendWeight;
			SkinnedVertices[v].Color = FVector4(Val, Val, Val, 1.0f);
		}
		else
		{
			SkinnedVertices[v].Color = LOD.BindVertices[v].Color;
			SkinnedVertices[v].Color.W = 0.0f;
		}
	}
	UploadVertices();
}

void FSkeletalMeshObjectCPUSkin::UploadVertices()
{
	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_SkinUpload);
	if (!FinalVertexBuffer) return;
	void* Dst = RenderCommand::MapWriteDiscard(FinalVertexBuffer.get());
	if (!Dst)return;
	std::memcpy(Dst, SkinnedVertices.GetData(), sizeof(FVertex) * SkinnedVertices.Num());
	RenderCommand::Unmap(FinalVertexBuffer.get());
}

void FSkeletalMeshObjectCPUSkin::UpdateClothData(const FClothingSimulationData* SimData)
{
	if (!SkeletalMesh)
		return;

	if (!SimData || SimData->Positions.IsEmpty())
	{
		// 클로스 일시정지/해제 시 마지막 시뮬 정점에 머물지 않도록 스키닝 포즈로 즉시 복구한다.
		SkinVerticesCPU(SkinnedVertices, SkinnedBounds);
		if (!ClothPaintOverlay.IsEmpty())
		{
			const int32 Count = FMath::Min(SkinnedVertices.Num(), ClothPaintOverlay.Num());
			for (int32 v = 0; v < Count; ++v)
			{
				if (ClothPaintOverlay[v].SimIndex > 0u)
				{
					const float Val = ClothPaintOverlay[v].BlendWeight;
					SkinnedVertices[v].Color = FVector4(Val, Val, Val, 1.0f);
				}
				else
				{
					SkinnedVertices[v].Color.W = 0.0f;
				}
			}
		}
		UploadVertices();
		return;
	}

	const UClothingAssetCommon* ClothAsset = SkeletalMesh->GetClothingAsset();
	if (!ClothAsset)
		return;

	const TArray<FClothVertMapping>& Mapping = ClothAsset->GetRenderToSimMapping();
	const int32 NumVerts = FMath::Min(SkinnedVertices.Num(), Mapping.Num());
	const int32 NumSimVerts = SimData->Positions.Num();
	const bool bHasNormals = (SimData->Normals.Num() == NumSimVerts);

	for (int32 v = 0; v < NumVerts; ++v)
	{
		const FClothVertMapping& Map = Mapping[v];
		if (Map.BlendWeight <= 0.0f || static_cast<int32>(Map.SimIndex) >= NumSimVerts)
			continue;

		FVertex& Out = SkinnedVertices[v];
		const FVector& SimPos = SimData->Positions[Map.SimIndex];
		Out.Position = Out.Position + (SimPos - Out.Position) * Map.BlendWeight;
		if (bHasNormals)
		{
			const FVector& SimN = SimData->Normals[Map.SimIndex];
			Out.Normal = (Out.Normal + (SimN - Out.Normal) * Map.BlendWeight).GetSafeNormal(SMALL_NUMBER, Out.Normal);
			const FVector T(Out.Tangent.X, Out.Tangent.Y, Out.Tangent.Z);
			const FVector OrthoT = (T - Out.Normal * T.Dot(Out.Normal)).GetSafeNormal(SMALL_NUMBER, T);
			Out.Tangent = FVector4(OrthoT.X, OrthoT.Y, OrthoT.Z, Out.Tangent.W);
		}
	}

	UploadVertices();
}

FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectGPUSkin(USkeletalMesh* InMesh)
	:FSkeletalMeshObject(InMesh)
{
	CPUVertices = InMesh->GetRenderData().LODRenderData[0].BindVertices;
	BoneMatrixBuffer = RenderCommand::CreateDynamicStructuredBuffer(static_cast<uint32>(InMesh->GetNumBones()), sizeof(FMatrix));
	VertexFactory.Init(InMesh->GetRenderData().LODRenderData[0].GpuSkinVertexBuffer.get(), GetIndexBuffer(0), BoneMatrixBuffer.get());

	// 첫 Update 전에도 바인드 포즈로 그려지도록 단위행렬을 올려 둔다
	for (FMatrix& Matrix : RefToLocal)
		Matrix = FMatrix::Identity;
	if (BoneMatrixBuffer && BoneMatrixBuffer->GetSRV())
		UploadBoneMatrices();
}

bool FSkeletalMeshObjectGPUSkin::IsValid() const
{
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];
	return BoneMatrixBuffer && BoneMatrixBuffer->GetSRV()
		&& LOD.GpuSkinVertexBuffer && LOD.GpuSkinVertexBuffer->GetBuffer();
}

void FSkeletalMeshObjectGPUSkin::Update(const TArray<FMatrix>& ComponentSpaceTransforms)
{
	UpdateRefToLocalMatrices(ComponentSpaceTransforms);
	UpdateSkinnedBoundsFromBones();
	UploadBoneMatrices();
	bCPUVerticesDirty = true;
}

const TArray<FVertex>* FSkeletalMeshObjectGPUSkin::GetCPUSkinnedVertices() const
{
	if (bCPUVerticesDirty)
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CpuSkinningPicking); 
		FBox Unused;
		SkinVerticesCPU(CPUVertices, Unused);
		bCPUVerticesDirty = false;
	}
	return &CPUVertices;
}

void FSkeletalMeshObjectGPUSkin::UpdateSkinnedBoundsFromBones()
{
	// LBS 결과는 영향 본들이 옮긴 위치의 볼록 결합이라, 본별 바운드의 합집합 안에 항상 들어간다
	const TArray<FBox>& BoneBounds = SkeletalMesh->GetRenderData().LODRenderData[0].BoneBounds;
	bool bFirst = true;
	for (int32 i = 0; i < BoneBounds.Num(); ++i)
	{
		if (BoneBounds[i].Min.X > BoneBounds[i].Max.X) continue;
		const FBox Box = BoneBounds[i].GetWorldAABB(RefToLocal[i]);
		if (bFirst) SkinnedBounds = Box;
		else SkinnedBounds.Expand(Box);
		bFirst = false;
	}
}

void FSkeletalMeshObjectGPUSkin::UploadBoneMatrices()
{
	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_BoneUpload);
	void* Dst = RenderCommand::MapWriteDiscard(BoneMatrixBuffer.get());
	if (!Dst) return;
	std::memcpy(Dst, RefToLocal.GetData(), sizeof(FMatrix) * RefToLocal.Num());
	RenderCommand::Unmap(BoneMatrixBuffer.get());
}

void FSkeletalMeshObjectGPUSkin::UpdateClothData(const FClothingSimulationData* SimData)
{
	if (bClothPaintOverlayActive)
		return;

	const FStructuredBuffer* MappingBuffer = SkeletalMesh ? SkeletalMesh->GetClothMappingBuffer() : nullptr;
	if (!SimData || !MappingBuffer || SimData->Positions.IsEmpty())
	{
		VertexFactory.SetClothBuffers(nullptr, nullptr);
		return;
	}

	const uint32 NumSimVerts = static_cast<uint32>(SimData->Positions.Num());
	if (!ClothSimulBuffer || ClothSimulBuffer->GetCapacity() < NumSimVerts)
	{
		ClothSimulBuffer = RenderCommand::CreateDynamicStructuredBuffer(NumSimVerts, sizeof(FClothSimulVertex));
	}
	if (!ClothSimulBuffer || !ClothSimulBuffer->GetSRV())
	{
		VertexFactory.SetClothBuffers(nullptr, nullptr);
		return;
	}

	ClothSimulStaging.SetNum(NumSimVerts);
	const bool bHasNormals = (SimData->Normals.Num() == SimData->Positions.Num());
	for (uint32 i = 0; i < NumSimVerts; ++i)
	{
		ClothSimulStaging[i].Position = SimData->Positions[i];
		ClothSimulStaging[i].Pad0 = 1.0f; // 유효 시뮬 버퍼 표시 (GpuSkinCommon.hlsli)
		ClothSimulStaging[i].Normal = bHasNormals ? SimData->Normals[i] : FVector(0.0f, 0.0f, 1.0f);
		ClothSimulStaging[i].Pad1 = 0.0f;
	}

	if (void* Dst = RenderCommand::MapWriteDiscard(ClothSimulBuffer.get()))
	{
		std::memcpy(Dst, ClothSimulStaging.GetData(), sizeof(FClothSimulVertex) * NumSimVerts);
		RenderCommand::Unmap(ClothSimulBuffer.get());
	}

	VertexFactory.SetClothBuffers(MappingBuffer, ClothSimulBuffer.get());
}

void FSkeletalMeshObjectGPUSkin::SetClothPaintOverlay(const TArray<FClothVertMapping>* PaintOverlay)
{
	if (!PaintOverlay || PaintOverlay->IsEmpty())
	{
		bClothPaintOverlayActive = false;
		VertexFactory.SetClothBuffers(nullptr, nullptr);
		return;
	}

	const uint32 Count = static_cast<uint32>(PaintOverlay->Num());
	if (!ClothPaintOverlayBuffer || ClothPaintOverlayBuffer->GetCapacity() < Count)
	{
		ClothPaintOverlayBuffer = RenderCommand::CreateDynamicStructuredBuffer(Count, sizeof(FClothVertMapping));
	}
	if (!ClothPaintOverlayBuffer || !ClothPaintOverlayBuffer->GetSRV())
	{
		bClothPaintOverlayActive = false;
		return;
	}

	if (void* Dst = RenderCommand::MapWriteDiscard(ClothPaintOverlayBuffer.get()))
	{
		std::memcpy(Dst, PaintOverlay->GetData(), sizeof(FClothVertMapping) * Count);
		RenderCommand::Unmap(ClothPaintOverlayBuffer.get());
	}

	bClothPaintOverlayActive = true;
	// t12에 PaintOverlay, t13은 nullptr(시뮬 변위 비활성)로 바인딩
	VertexFactory.SetClothBuffers(ClothPaintOverlayBuffer.get(), nullptr);
}


