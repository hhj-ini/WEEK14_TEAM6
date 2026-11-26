#include "EnginePCH.h"
#include "ClothingAsset.h"
#include "Render/SkeletalMesh.h"
#include "Core/EngineLog.h"
#include "UObject/UObjectGlobals.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace
{
	constexpr uint32 ClothBinaryMagic = 0x544C434Bu; // "KCLT"
	constexpr uint32 ClothBinaryVersion = 2u;

	struct FClothConfigNvV1
	{
		FVector Gravity;
		FVector WindVelocity;
		float DragCoefficient;
		float LiftCoefficient;
		float Damping;
		float StretchStiffness;
		float BendStiffness;
		float ShearStiffness;
		float TetherStiffness;
		float TetherScale;
		float MaxDistanceScale;
		float SolverFrequency;
		float LinearInertiaScale;
		float AngularInertiaScale;
	};

	struct FPosKey
	{
		int64 X = 0;
		int64 Y = 0;
		int64 Z = 0;

		bool operator==(const FPosKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}
	};

	struct FPosKeyHash
	{
		size_t operator()(const FPosKey& K) const noexcept
		{
			size_t H = std::hash<int64>{}(K.X);
			H ^= std::hash<int64>{}(K.Y) + 0x9e3779b9 + (H << 6) + (H >> 2);
			H ^= std::hash<int64>{}(K.Z) + 0x9e3779b9 + (H << 6) + (H >> 2);
			return H;
		}
	};

	// 0.01cm 단위로 양자화해 UV Seam 중복 정점을 같은 키로 묶는다.
	FPosKey MakePosKey(const FVector& P)
	{
		return {
			static_cast<int64>(std::llround(P.X * 100.0f)),
			static_cast<int64>(std::llround(P.Y * 100.0f)),
			static_cast<int64>(std::llround(P.Z * 100.0f))
		};
	}

	template<typename T>
	void WritePod(std::ofstream& Out, const T& Val)
	{
		Out.write(reinterpret_cast<const char*>(&Val), sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::ifstream& In, T& Val)
	{
		return static_cast<bool>(In.read(reinterpret_cast<char*>(&Val), sizeof(T)));
	}

	void WriteString(std::ofstream& Out, const FString& Str)
	{
		const uint32 Len = static_cast<uint32>(Str.size());
		WritePod(Out, Len);
		if (Len > 0)
			Out.write(Str.data(), Len);
	}

	bool ReadString(std::ifstream& In, FString& Str)
	{
		uint32 Len = 0;
		if (!ReadPod(In, Len) || Len > 1024 * 64)
			return false;
		Str.resize(Len);
		if (Len > 0 && !In.read(Str.data(), Len))
			return false;
		return true;
	}
}

bool UClothingAssetCommon::InitializeForSection(const USkeletalMesh& Mesh, int32 InSectionIndex, const FString& InName)
{
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return false;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	if (InSectionIndex < 0 || InSectionIndex >= LOD.Sections.Num())
		return false;

	AssetName = InName.empty() ? "Clothing" : InName;
	SourceSectionIndex = InSectionIndex;

	const int32 NumRenderVerts = LOD.BindVertices.Num();
	PaintedMaxDistances.SetNum(NumRenderVerts);
	for (int32 i = 0; i < NumRenderVerts; ++i)
		PaintedMaxDistances[i] = 0.0f;

	PhysicalMeshData = {};
	RenderToSimMapping.SetNum(NumRenderVerts);
	for (int32 i = 0; i < NumRenderVerts; ++i)
		RenderToSimMapping[i] = {};

	return true;
}

bool UClothingAssetCommon::RebuildFromPaintedWeights(const USkeletalMesh& Mesh)
{
	return BuildFromMaxDistances(Mesh, PaintedMaxDistances, SourceSectionIndex);
}

bool UClothingAssetCommon::BuildFromMaxDistances(
	const USkeletalMesh& Mesh,
	const TArray<float>& PerRenderVertMaxDist,
	int32 RestrictToSection)
{
	PhysicalMeshData = {};
	RenderToSimMapping.Empty();

	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return false;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const int32 NumRenderVerts = LOD.BindVertices.Num();
	const int32 NumRenderIndices = LOD.Indices.Num();
	if (NumRenderVerts == 0 || NumRenderIndices < 3 || PerRenderVertMaxDist.Num() != NumRenderVerts)
		return false;

	if (&PaintedMaxDistances != &PerRenderVertMaxDist)
		PaintedMaxDistances = PerRenderVertMaxDist;

	// 섹션 범위 결정
	int32 StartIdx = 0;
	int32 EndIdx = NumRenderIndices;
	if (RestrictToSection >= 0 && RestrictToSection < LOD.Sections.Num())
	{
		const FSkeletalMeshSection& Sec = LOD.Sections[RestrictToSection];
		StartIdx = static_cast<int32>(Sec.StartIndex);
		EndIdx = FMath::Min(NumRenderIndices, StartIdx + static_cast<int32>(Sec.IndexCount));
	}

	RenderToSimMapping.SetNum(NumRenderVerts);

	std::unordered_map<FPosKey, uint32, FPosKeyHash> WeldMap;
	TArray<int32> RenderToWelded;
	RenderToWelded.SetNum(NumRenderVerts);
	for (int32 i = 0; i < NumRenderVerts; ++i)
		RenderToWelded[i] = -1;

	auto GetOrAddSimVertex = [&](uint32 RenderIdx) -> uint32
	{
		if (RenderToWelded[RenderIdx] >= 0)
			return static_cast<uint32>(RenderToWelded[RenderIdx]);

		const FVector& Pos = LOD.BindVertices[RenderIdx].Position;
		const float MaxDist = FMath::Max(0.0f, PerRenderVertMaxDist[RenderIdx]);
		const FPosKey Key = MakePosKey(Pos);

		auto It = WeldMap.find(Key);
		if (It != WeldMap.end())
		{
			const uint32 SimIdx = It->second;
			// 경계 정점이 찢어지지 않도록 더 작은 MaxDist로 보수적 병합
			PhysicalMeshData.MaxDistances[SimIdx] = FMath::Min(PhysicalMeshData.MaxDistances[SimIdx], MaxDist);
			PhysicalMeshData.InverseMasses[SimIdx] = (PhysicalMeshData.MaxDistances[SimIdx] > 0.0f) ? 1.0f : 0.0f;
			RenderToWelded[RenderIdx] = static_cast<int32>(SimIdx);
			return SimIdx;
		}

		const uint32 NewSimIdx = static_cast<uint32>(PhysicalMeshData.Vertices.Num());
		PhysicalMeshData.Vertices.Add(Pos);
		PhysicalMeshData.MaxDistances.Add(MaxDist);
		PhysicalMeshData.InverseMasses.Add(MaxDist > 0.0f ? 1.0f : 0.0f);
		PhysicalMeshData.SkinWeights.Add(LOD.SkinWeights[RenderIdx]);

		WeldMap.emplace(Key, NewSimIdx);
		RenderToWelded[RenderIdx] = static_cast<int32>(NewSimIdx);
		return NewSimIdx;
	};

	// 1. MaxDist > 0 정점이 하나라도 포함된 삼각형만 수집 + UV Weld
	for (int32 i = StartIdx; i + 2 < EndIdx; i += 3)
	{
		const uint32 R0 = LOD.Indices[i];
		const uint32 R1 = LOD.Indices[i + 1];
		const uint32 R2 = LOD.Indices[i + 2];

		if (PerRenderVertMaxDist[R0] <= 0.0f &&
			PerRenderVertMaxDist[R1] <= 0.0f &&
			PerRenderVertMaxDist[R2] <= 0.0f)
		{
			continue;
		}

		const uint32 S0 = GetOrAddSimVertex(R0);
		const uint32 S1 = GetOrAddSimVertex(R1);
		const uint32 S2 = GetOrAddSimVertex(R2);

		if (S0 == S1 || S1 == S2 || S2 == S0)
			continue;

		// 면적이 0인 퇴화 삼각형은 NvCloth applyWind(normal / doubleArea)에서 NaN을 유발하므로 제외한다.
		const FVector E1 = PhysicalMeshData.Vertices[S1] - PhysicalMeshData.Vertices[S0];
		const FVector E2 = PhysicalMeshData.Vertices[S2] - PhysicalMeshData.Vertices[S0];
		if (FVector::Cross(E1, E2).SizeSquared() < 1e-8f)
			continue;

		PhysicalMeshData.Indices.Add(S0);
		PhysicalMeshData.Indices.Add(S1);
		PhysicalMeshData.Indices.Add(S2);
	}

	// 2. RenderToSimMapping 채우기
	for (int32 v = 0; v < NumRenderVerts; ++v)
	{
		if (RenderToWelded[v] >= 0 && PerRenderVertMaxDist[v] > 0.0f)
		{
			RenderToSimMapping[v].SimIndex = static_cast<uint32>(RenderToWelded[v]);
			RenderToSimMapping[v].BlendWeight = 1.0f;
		}
	}

	KRS_LOG(Info, "[ClothAsset] '{}' built sim mesh: {} verts, {} tris (from {} render verts)",
		AssetName,
		PhysicalMeshData.Vertices.Num(),
		PhysicalMeshData.Indices.Num() / 3,
		NumRenderVerts);

	return IsValid();
}

void UClothingAssetCommon::GenerateMaxDistancesFromBones(
	const USkeletalMesh& Mesh,
	const TArray<int32>& RootBoneIndices,
	float MaxDistValue,
	TArray<float>& OutMaxDistances)
{
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	const int32 NumBones = Bones.Num();
	const int32 NumVerts = LOD.BindVertices.Num();

	OutMaxDistances.SetNum(NumVerts);
	for (int32 v = 0; v < NumVerts; ++v)
		OutMaxDistances[v] = 0.0f;

	TArray<int32> BoneDepth;
	BoneDepth.SetNum(NumBones);
	for (int32 b = 0; b < NumBones; ++b)
		BoneDepth[b] = -1;

	for (int32 RootIdx : RootBoneIndices)
	{
		if (RootIdx >= 0 && RootIdx < NumBones)
			BoneDepth[RootIdx] = 0;
	}

	int32 MaxDepth = 1;
	for (int32 b = 0; b < NumBones; ++b)
	{
		if (BoneDepth[b] >= 0)
			continue;

		const int32 Parent = Bones[b].ParentIndex;
		if (Parent >= 0 && BoneDepth[Parent] >= 0)
		{
			BoneDepth[b] = BoneDepth[Parent] + 1;
			MaxDepth = FMath::Max(MaxDepth, BoneDepth[b]);
		}
	}

	for (int32 v = 0; v < NumVerts; ++v)
	{
		const FSkinWeight& W = LOD.SkinWeights[v];
		float ClothWeight = 0.0f;
		float WeightedDepth = 0.0f;

		for (int32 k = 0; k < MaxBoneInfluences; ++k)
		{
			const float Weight = W.Weight[k];
			const int32 BoneIdx = static_cast<int32>(W.BoneIndex[k]);
			if (Weight > 0.0f && BoneIdx < NumBones && BoneDepth[BoneIdx] >= 0)
			{
				ClothWeight += Weight;
				WeightedDepth += Weight * static_cast<float>(BoneDepth[BoneIdx]);
			}
		}

		if (ClothWeight > 0.25f)
		{
			const float AvgDepth = WeightedDepth / ClothWeight;
			const float Ratio = FMath::Clamp(AvgDepth / static_cast<float>(MaxDepth), 0.0f, 1.0f);
			OutMaxDistances[v] = Ratio * MaxDistValue;
		}
	}
}

namespace ClothingAssetFile
{
	FString GetClothBinaryPath(const FString& MeshFilePath)
	{
		if (MeshFilePath.empty())
			return {};

		// "Foo.fbx.bin" -> "Foo.cloth.bin"
		FString Lower = MeshFilePath;
		for (char& C : Lower)
			if (C >= 'A' && C <= 'Z') C += 'a' - 'A';

		if (Lower.size() >= 8 && Lower.substr(Lower.size() - 8) == ".fbx.bin")
			return MeshFilePath.substr(0, MeshFilePath.size() - 8) + ".cloth.bin";

		std::filesystem::path P(MeshFilePath);
		P.replace_extension(".cloth.bin");
		return P.string();
	}

	bool Write(
		const FString& FilePath,
		const TArray<UClothingAssetCommon*>& Assets,
		const TArray<int32>& SectionBindings,
		FString& OutError)
	{
		OutError.clear();
		const std::filesystem::path FsPath(std::u8string_view(
			reinterpret_cast<const char8_t*>(FilePath.data()), FilePath.size()));
		std::ofstream Out(FsPath, std::ios::binary | std::ios::trunc);
		if (!Out.is_open())
		{
			OutError = "Failed to open cloth binary for writing: " + FilePath;
			return false;
		}

		WritePod(Out, ClothBinaryMagic);
		WritePod(Out, ClothBinaryVersion);

		const uint32 NumAssets = static_cast<uint32>(Assets.Num());
		WritePod(Out, NumAssets);
		for (uint32 i = 0; i < NumAssets; ++i)
		{
			const UClothingAssetCommon* Asset = Assets[i];
			WriteString(Out, Asset ? Asset->GetAssetName() : FString("Clothing"));
			const int32 SectionIdx = Asset ? Asset->GetSourceSectionIndex() : 0;
			WritePod(Out, SectionIdx);
			const FClothConfigNv Cfg = Asset ? Asset->GetConfig() : FClothConfigNv{};
			WritePod(Out, Cfg);

			const TArray<float>& Weights = Asset ? Asset->GetPaintedMaxDistances() : TArray<float>{};
			const uint32 NumWeights = static_cast<uint32>(Weights.Num());
			WritePod(Out, NumWeights);
			if (NumWeights > 0)
				Out.write(reinterpret_cast<const char*>(Weights.GetData()), sizeof(float) * NumWeights);
		}

		const uint32 NumBindings = static_cast<uint32>(SectionBindings.Num());
		WritePod(Out, NumBindings);
		if (NumBindings > 0)
			Out.write(reinterpret_cast<const char*>(SectionBindings.GetData()), sizeof(int32) * NumBindings);

		Out.flush();
		if (!Out)
		{
			OutError = "Failed while writing cloth binary: " + FilePath;
			return false;
		}
		return true;
	}

	bool Read(
		const FString& FilePath,
		const USkeletalMesh& Mesh,
		TArray<UClothingAssetCommon*>& OutAssets,
		TArray<int32>& OutSectionBindings,
		FString& OutError)
	{
		OutError.clear();
		OutAssets.Empty();
		OutSectionBindings.Empty();

		const std::filesystem::path FsPath(std::u8string_view(
			reinterpret_cast<const char8_t*>(FilePath.data()), FilePath.size()));
		std::ifstream In(FsPath, std::ios::binary);
		if (!In.is_open())
			return false;

		uint32 Magic = 0, Ver = 0;
		if (!ReadPod(In, Magic) || Magic != ClothBinaryMagic || !ReadPod(In, Ver) || (Ver != 1u && Ver != ClothBinaryVersion))
		{
			OutError = "Invalid cloth binary header: " + FilePath;
			return false;
		}

		uint32 NumAssets = 0;
		if (!ReadPod(In, NumAssets) || NumAssets > 256)
		{
			OutError = "Invalid cloth asset count: " + FilePath;
			return false;
		}

		const auto& LODs = Mesh.GetRenderData().LODRenderData;
		const uint32 ExpectedVerts = LODs.IsEmpty() ? 0u : static_cast<uint32>(LODs[0].BindVertices.Num());

		for (uint32 i = 0; i < NumAssets; ++i)
		{
			FString Name;
			int32 SectionIdx = 0;
			FClothConfigNv Cfg{};
			uint32 NumWeights = 0;

			if (!ReadString(In, Name) || !ReadPod(In, SectionIdx))
			{
				OutError = "Truncated cloth binary asset entry: " + FilePath;
				return false;
			}

			if (Ver == 1u)
			{
				FClothConfigNvV1 OldCfg{};
				if (!ReadPod(In, OldCfg))
				{
					OutError = "Truncated cloth binary config (v1): " + FilePath;
					return false;
				}
				Cfg.Gravity = OldCfg.Gravity;
				Cfg.WindVelocity = OldCfg.WindVelocity;
				Cfg.DragCoefficient = OldCfg.DragCoefficient;
				Cfg.LiftCoefficient = OldCfg.LiftCoefficient;
				Cfg.Damping = OldCfg.Damping;
				Cfg.StretchStiffness = OldCfg.StretchStiffness;
				Cfg.BendStiffness = OldCfg.BendStiffness;
				Cfg.ShearStiffness = OldCfg.ShearStiffness;
				Cfg.TetherStiffness = OldCfg.TetherStiffness;
				Cfg.TetherScale = OldCfg.TetherScale;
				Cfg.MaxDistanceScale = OldCfg.MaxDistanceScale;
				Cfg.SolverFrequency = OldCfg.SolverFrequency;
				Cfg.LinearInertiaScale = OldCfg.LinearInertiaScale;
				Cfg.AngularInertiaScale = OldCfg.AngularInertiaScale;
			}
			else
			{
				if (!ReadPod(In, Cfg))
				{
					OutError = "Truncated cloth binary config (v2): " + FilePath;
					return false;
				}
			}

			if (!ReadPod(In, NumWeights))
			{
				OutError = "Truncated cloth binary weight count: " + FilePath;
				return false;
			}
			if (NumWeights > 10000000u)
			{
				OutError = "Unreasonable cloth weight count: " + FilePath;
				return false;
			}

			TArray<float> Weights;
			Weights.SetNum(static_cast<int32>(NumWeights));
			if (NumWeights > 0 && !In.read(reinterpret_cast<char*>(Weights.GetData()), sizeof(float) * NumWeights))
			{
				OutError = "Truncated cloth binary weights: " + FilePath;
				return false;
			}

			UClothingAssetCommon* Asset = NewObject<UClothingAssetCommon>();
			Asset->InitializeForSection(Mesh, SectionIdx, Name);
			Asset->GetConfig() = Cfg;
			if (NumWeights == ExpectedVerts)
			{
				Asset->GetPaintedMaxDistances() = std::move(Weights);
				Asset->RebuildFromPaintedWeights(Mesh);
			}
			OutAssets.Add(Asset);
		}

		uint32 NumBindings = 0;
		if (!ReadPod(In, NumBindings) || NumBindings > 4096)
		{
			OutError = "Invalid cloth bindings count: " + FilePath;
			return false;
		}
		OutSectionBindings.SetNum(static_cast<int32>(NumBindings));
		if (NumBindings > 0 && !In.read(reinterpret_cast<char*>(OutSectionBindings.GetData()), sizeof(int32) * NumBindings))
		{
			OutError = "Truncated cloth bindings: " + FilePath;
			return false;
		}

		return true;
	}
}
