#pragma once

#include "UObject/Object.h"
#include "Clothing/ClothingTypes.h"
#include "ClothingAsset.generated.h"

class USkeletalMesh;

struct FClothConfigNv
{
	// 엔진 월드 표준 단위(m) 기준. 시뮬레이션 시 컴포넌트 Scale의 역수(InvScale)로 로컬 공간(cm)에 자동 보정한다.
	FVector Gravity = FVector(0.0f, 0.0f, -9.8f);
	FVector WindVelocity = FVector::ZeroVector;
	float DragCoefficient = 0.25f;
	float LiftCoefficient = 0.35f;
	float Damping = 0.2f;

	float StretchStiffness = 1.0f;
	float BendStiffness = 0.25f;
	float ShearStiffness = 0.8f;

	float TetherStiffness = 1.0f;
	float TetherScale = 1.0f;
	float MaxDistanceScale = 1.0f;
	float SolverFrequency = 60.0f;
	float LinearInertiaScale = 0.5f;
	float AngularInertiaScale = 0.5f;

	// 자기충돌 (Self-Collision): Distance > 0이면 천 파티클끼리 겹침/관통을 방지 (로컬 메시 단위, FBX cm 기준 1~5 추천)
	float SelfCollisionDistance = 0.0f;
	float SelfCollisionStiffness = 0.2f;
};

UCLASS()
class UClothingAssetCommon : public UObject
{
	GENERATED_BODY()
public:
	// 지정한 섹션을 대상으로 빈 클로딩 에셋을 초기화한다 (PaintedMaxDistances는 전부 0으로 시작).
	bool InitializeForSection(const USkeletalMesh& Mesh, int32 InSectionIndex, const FString& InName);

	// 현재 PaintedMaxDistances 배열로 시뮬 서브메시 + UV Weld + 렌더 매핑을 재구축한다.
	bool RebuildFromPaintedWeights(const USkeletalMesh& Mesh);

	// 렌더 정점별 MaxDistance 배열로 시뮬 서브메시 + UV Weld + 렌더 매핑 생성
	bool BuildFromMaxDistances(const USkeletalMesh& Mesh, const TArray<float>& PerRenderVertMaxDist, int32 RestrictToSection = -1);

	// 본 선택 기반 자동 마스크 생성기
	static void GenerateMaxDistancesFromBones(
		const USkeletalMesh& Mesh,
		const TArray<int32>& RootBoneIndices,
		float MaxDistValue,
		TArray<float>& OutMaxDistances);

	const FString& GetAssetName() const { return AssetName; }
	void SetAssetName(const FString& InName) { AssetName = InName; }
	int32 GetSourceSectionIndex() const { return SourceSectionIndex; }
	void SetSourceSectionIndex(int32 InSectionIndex) { SourceSectionIndex = InSectionIndex; }

	TArray<float>& GetPaintedMaxDistances() { return PaintedMaxDistances; }
	const TArray<float>& GetPaintedMaxDistances() const { return PaintedMaxDistances; }

	const FClothPhysicalMeshData& GetPhysicalMeshData() const { return PhysicalMeshData; }
	const TArray<FClothVertMapping>& GetRenderToSimMapping() const { return RenderToSimMapping; }
	FClothConfigNv& GetConfig() { return ClothConfig; }
	const FClothConfigNv& GetConfig() const { return ClothConfig; }
	bool IsValid() const { return !PhysicalMeshData.Vertices.IsEmpty() && !PhysicalMeshData.Indices.IsEmpty(); }

private:
	FString AssetName = "Clothing";
	int32 SourceSectionIndex = 0;
	TArray<float> PaintedMaxDistances;

	FClothPhysicalMeshData PhysicalMeshData;
	TArray<FClothVertMapping> RenderToSimMapping;
	FClothConfigNv ClothConfig;
};

namespace ClothingAssetFile
{
	// "<Mesh>.fbx.bin" -> "<Mesh>.cloth.bin"
	FString GetClothBinaryPath(const FString& MeshFilePath);

	bool Write(
		const FString& FilePath,
		const TArray<UClothingAssetCommon*>& Assets,
		const TArray<int32>& SectionBindings,
		FString& OutError);

	bool Read(
		const FString& FilePath,
		const USkeletalMesh& Mesh,
		TArray<UClothingAssetCommon*>& OutAssets,
		TArray<int32>& OutSectionBindings,
		FString& OutError);
}
